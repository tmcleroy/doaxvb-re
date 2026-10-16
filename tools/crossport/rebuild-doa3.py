"""Rebuild every DOA3 character asset for DOAXBV from the harvested dumps.
Names were identified from renders and hair textures (some are guesses)."""
import glob, os, sys
sys.path.insert(0, os.path.expanduser('~') + '/code/recomp/doaxvb-re/private/claude-work/xport')
ONLY = set(sys.argv[1:]); sys.argv = ['x']
import build_doa3_asset as b, asset_thumbs
H = os.path.expanduser('~') + '/code/recomp/doa3-re/private/wt-meshdump/private/'
OUT = os.path.expanduser('~') + '/code/recomp/doaxvb-re/private/claude-work/doa3-costumes/doa3-%s.d3c'
P1, P2 = 0x4785d0, 0x478b90
# Optional 4th field: a calm frame window for hair (dense fight frames catch it swinging).
JOBS = [('meshdump-run6', P1, 'leifang'), ('helena-dense', P2, 'helena'),
        ('harvest/scene-5800', P1, 'christie'), ('harvest/scene-5800', P2, 'genfu'),
        ('hitomi-dense', P1, 'hitomi', 'harvest/scene-6700'), ('harvest/scene-6700', P2, 'bass2'),
        ('harvest/scene-7300', P1, 'tina'), ('harvest/scene-7300', P2, 'hitomi2'),
        ('harvest/scene-7900', P2, 'brad'), ('harvest/scene-8700', P1, 'bayman2'),
        ('harvest/scene-8700', P2, 'jannlee'), ('harvest/scene-10000', P2, 'ayane'),
        ('harvest/scene-10600', P1, 'hayate'), ('harvest/scene-10600', P2, 'christie2'),
        ('harvest/scene-11380', P2, 'hitomi3'), ('harvest/scene-12330', P2, 'brad2'),
        ('harvest/scene-13500', P1, 'kasumi'), ('harvest/scene-13500', P2, 'ayane2'),
        ('harvest/scene-15350', P1, 'bayman'), ('harvest/scene-15350', P2, 'zack'),
        ('harvest/scene-16900', P1, 'christie3'), ('harvest/scene-16900', P2, 'helena2'),
        ('harvest/scene-19900', P2, 'bass')]
only = ONLY
def frames(d):
    return sorted([p for p in glob.glob(H + d + '/present-*') if os.path.exists(p + '/ram.bin')],
                  key=lambda p: int(p.rsplit('-', 1)[1]))
for job in JOBS:
    d, base, name = job[:3]
    if only and name not in only: continue
    b.build(frames(d), OUT % name, base, frames(job[3]) if len(job) > 3 else None)
    asset_thumbs.thumbs(OUT % name)
