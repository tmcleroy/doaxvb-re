"""Turn an F9 capture (mesh-dump capture-NN folder) into a costume asset.

usage: capture_to_asset.py <capture dir> --game doaxbv|doa3 [--name NAME] [--actor N|--all]

doaxbv captures become DOA3 assets: doa3-re/private/wt-meshdump/private/doax-<name>.dxc
doa3 captures become DOAXBV assets: doaxvb-re/private/claude-work/doa3-<name>.d3c
Without --actor the character with the most parts on screen is converted;
--all converts every character seen. A preview PNG is written beside each.
"""
import glob, os, struct, subprocess, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import draws
from retarget_preview import bank

DOAX_DISC = os.path.expanduser('~') + '/code/recomp/doaxvb-re/private/setup-8569d80610c6/imported-disc/disc'
DOA3_AFS = os.path.expanduser('~') + '/code/recomp/doa3-re/private/wt-meshdump/private/play-disc/loadfile.afs'
DOA3_OUT = os.path.expanduser('~') + '/code/recomp/doa3-re/private/wt-meshdump/private'
DOAX_OUT = os.path.expanduser('~') + '/code/recomp/doaxvb-re/private/claude-work/doa3-costumes'
DOA3_BANKS = (0x4785d0, 0x478b90)

def export(presents, afs):
    args = [sys.executable, os.path.join(HERE, 'mesh_dump_export.py'), *presents]
    for a in afs: args += ['--afs', a]
    env = dict(os.environ, MESH_DUMP_INDEX_DIR=os.path.expanduser('~') + '/code/recomp/doaxvb-re/private/claude-work/afs-index')
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)

def count_parts(P, B, n, rotation_only):
    hits = 0
    for x in draws(P).values():
        if x['offscreen'] or not x['worlds']: continue
        W = np.array(x['worlds'][0]).reshape(4, 4)
        for i in range(n):
            if abs(np.linalg.det(B[i][:3, :3])) < .5: continue
            err = np.abs(W[:3, :3] - B[i][:3, :3]).max() if rotation_only else np.abs(W - B[i]).max()
            if err < 1e-4 and np.linalg.norm(W[3, :3] - B[i][3, :3]) < 0.6:
                hits += 1; break
    return hits

def keep(out):
    """Drop an asset with too few parts (character off screen): an empty one
    would hide the replaced character entirely."""
    with open(out, 'rb') as f:
        f.read(4); parts = struct.unpack('<4I', f.read(16))[3]
    if parts < 20:
        os.remove(out); print('dropped', out, '(only %d parts)' % parts); return False
    return True

def main():
    argv = sys.argv[1:]
    def opt(name, default=None):
        if name in argv:
            i = argv.index(name); v = argv[i + 1]; del argv[i:i + 2]; return v
        return default
    game = opt('--game'); name = opt('--name'); actor = opt('--actor')
    every = '--all' in argv
    if every: argv.remove('--all')
    capture = argv[0].rstrip('/')
    presents = sorted(glob.glob(capture + '/present-*'), key=lambda p: int(p.rsplit('-', 1)[1]))
    if not presents: raise SystemExit('no present-* folders in ' + capture)
    name = name or os.path.basename(capture)
    if game == 'doaxbv':
        export(presents, [DOAX_DISC + '/datahdd.afs', DOAX_DISC + '/datadvd.afs'])
        import build_costume_asset as bca
        counts = {}
        for a in range(4):
            best = max(presents, key=lambda P: count_parts(P, bank(open(P + '/ram.bin', 'rb').read(), 0x4d3650 + a * 0x800, 32), 32, True))
            counts[a] = (count_parts(best, bank(open(best + '/ram.bin', 'rb').read(), 0x4d3650 + a * 0x800, 32), 32, True), best)
        print('parts on screen per actor:', {a: c for a, (c, _) in counts.items()})
        actors = [int(actor)] if actor is not None else ([a for a, (c, _) in counts.items() if c >= 20] if every
                                                          else [max(counts, key=lambda a: counts[a][0])])
        for a in actors:
            primary = counts[a][1]
            out = '%s/doax-%s%s.dxc' % (DOA3_OUT, name, '' if len(actors) == 1 else '-a%d' % a)
            bca.SOFT_DIR = capture
            bca.build([primary] + [p for p in presents if p != primary], out, a)
            if keep(out):
                import asset_thumbs; print('thumbnail', asset_thumbs.thumbs(out))
    elif game == 'doa3':
        export(presents, [DOA3_AFS])
        import build_doa3_asset as bda
        counts = {b: max(count_parts(P, bank(open(P + '/ram.bin', 'rb').read(), b, 23), 23, False) for P in presents)
                  for b in DOA3_BANKS}
        print('parts on screen per fighter:', {hex(b): c for b, c in counts.items()})
        banks = [DOA3_BANKS[int(actor)]] if actor is not None else (list(DOA3_BANKS) if every
                                                                    else [max(counts, key=counts.get)])
        for b in banks:
            out = '%s/doa3-%s%s.d3c' % (DOAX_OUT, name, '' if len(banks) == 1 else '-p%d' % (DOA3_BANKS.index(b) + 1))
            bda.build(presents, out, b)
            if keep(out):
                import asset_thumbs; print('thumbnail', asset_thumbs.thumbs(out))
            subprocess.run([sys.executable, os.path.join(HERE, 'asset_preview.py'), out,
                            os.path.expanduser('~') + '/code/recomp/doaxvb-re/private/claude-work/meshdump-run5/present-2410', '0x4d3650',
                            out.replace('.d3c', '-preview.png')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        raise SystemExit('--game doaxbv or --game doa3')

if __name__ == '__main__':
    main()
