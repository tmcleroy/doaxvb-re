"""Which asset parts cover a screen point, posed on a dump's skeleton (static
pose only: no springs or gravity hang). usage: pick_part.py <asset> <present> <bank hex> <x> <y> [w h]
x, y in screenshot pixels (default size 1707x960)."""
import sys, json, numpy as np
from asset_preview import load, pose, doa3_bank_on
from retarget_preview import bank
import mesh_dump_export as mde
asset, P, base = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
X, Y = float(sys.argv[4]), float(sys.argv[5])
Wd, Ht = (float(sys.argv[6]), float(sys.argv[7])) if len(sys.argv) > 7 else (1707.0, 960.0)
m, rel, hier, parts = load(asset)
ram = open(P + '/ram.bin', 'rb').read()
if m == b'D3C1':
    bk = doa3_bank_on(bank(ram, base, 32), rel, hier)
else:
    import validate_asset
    bk = validate_asset.target_bank(m, rel, hier, ram, base)
first = next(json.loads(l) for l in open(P + '/draws.jsonl') if json.loads(l).get('projection') and json.loads(l)['projection'][11] != 0)
V = np.array(first['view']).reshape(4, 4); Pj = np.array(first['projection']).reshape(4, 4)
t = np.array([X / Wd * 2 - 1, 1 - Y / Ht * 2]); hits = []
for i, p in enumerate(parts):
    tris = mde.triangles(p['primitive'], p['idx']); tris = tris[(tris < p['count']).all(1)]
    if not len(tris): continue
    h = np.c_[pose(p, bk), np.ones(p['count'])] @ V @ Pj; s = h[:, :2] / h[:, 3:4]
    a, b, c = s[tris[:, 0]], s[tris[:, 1]], s[tris[:, 2]]
    e = lambda u, v: (v[:, 0] - u[:, 0]) * (t[1] - u[:, 1]) - (v[:, 1] - u[:, 1]) * (t[0] - u[:, 0])
    d1, d2, d3 = e(a, b), e(b, c), e(c, a)
    inside = ((d1 >= 0) & (d2 >= 0) & (d3 >= 0)) | ((d1 <= 0) & (d2 <= 0) & (d3 <= 0))
    if inside.any():
        hits.append((round(float(h[tris[inside], 3].mean(1).min()), 3), i, [k for k, _ in p['binds']], p['count']))
for z, i, bones, n in sorted(hits)[:6]: print('depth %.3f part %3d bones %s verts %d' % (z, i, bones, n))
