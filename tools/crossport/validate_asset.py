"""Pose a converted asset on the target game's skeleton from a RAM dump and
report parts that deform badly: triangles stretched or crushed relative to
the captured rest shape, vertices far from every bone, or binds to bones the
runtime never fills.

usage: validate_asset.py <asset> <present dir> <bank hex> [--top N]
A D3C1 asset is posed on a DOAXBV bank (0x4D3650 + actor*0x800); a DXC1 on a
DOA3 fighter bank (0x4785D0 / 0x478B90).
"""
import sys, numpy as np
import mesh_dump_export as mde
from asset_preview import load, pose, doa3_bank_on
from retarget_preview import bank

def target_bank(magic, rel, hier, ram, base):
    if magic == b'D3C1':
        return doa3_bank_on(bank(ram, base, 32), rel, hier)
    from pose_dxc import doax_bank
    return doax_bank(rel, hier, bank(ram, base, 23))

def check(asset, present, base, top=12):
    magic, rel, hier, parts = load(asset)
    ram = open(present + '/ram.bin', 'rb').read()
    bk = target_bank(magic, rel, hier, ram, base)
    filled = [b for b in range(len(bk)) if abs(np.linalg.det(bk[b][:3, :3])) > 0.5]
    joints = np.array([bk[b][3, :3] for b in filled])
    report = []
    for i, p in enumerate(parts):
        bones = [b for b, _ in p['binds']]
        dead = [b for b in bones if b not in filled]
        tris = mde.triangles(p['primitive'], p['idx']); tris = tris[(tris < p['count']).all(1)]
        if not len(tris): continue
        n = p['count']
        rows = np.frombuffer(p['vtx'], np.uint8).reshape(n, p['stride'])
        pos = rows[:, :12].copy().view(np.float32).reshape(n, 3).astype(float)
        rest = (np.c_[pos, np.ones(n)] @ p['binds'][0][1])[:, :3]     # capture shape, bone-0 frame
        posed = pose(p, bk)
        def edges(v): return np.linalg.norm(v[tris[:, [1, 2, 0]]] - v[tris], axis=2)
        er, ep = edges(rest), edges(posed)
        ok = er > 1e-4
        ratio = np.where(ok, ep / np.maximum(er, 1e-6), 1.0)
        bad = ((ratio > 1.8) | (ratio < 0.4)) & ok & (np.abs(ep - er) > 0.01)
        far = np.linalg.norm(posed[:, None, :] - joints[None], axis=2).min(1)
        score = bad.any(1).mean()
        if dead or score > 0.02 or far.max() > 0.45:
            report.append((score, i, bones, dead, round(float(ratio[ok].max()), 2), round(float(far.max()), 3), n))
    report.sort(key=lambda r: (-len(r[3]), -r[0], -r[5]))
    for score, i, bones, dead, rmax, fmax, n in report[:top]:
        print('  part %3d bones %-18s verts %5d stretched-tris %5.1f%% max-edge-ratio %6.2f max-dist-to-joint %.3f%s'
              % (i, bones, n, 100 * score, rmax, fmax, ('  UNFILLED %s' % dead) if dead else ''))
    return report

if __name__ == '__main__':
    asset, present, base = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 12
    print(asset.rsplit('/', 1)[-1], present.rsplit('/', 1)[-1])
    check(asset, present, base, top)
