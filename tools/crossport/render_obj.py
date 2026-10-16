"""Flat-shaded painter's render of a mesh-dump scene.obj, coloured by object.
usage: render_obj.py <scene.obj> <out.png> [--view draws.jsonl] [--only substr] [--fit]
--view uses the first draw's game camera; otherwise a front view fitted to the bounds."""
import json, sys, colorsys, hashlib
import numpy as np
from PIL import Image, ImageDraw

args = sys.argv[1:]
src, out = args[0], args[1]
only = args[args.index('--only') + 1] if '--only' in args else None
view_file = args[args.index('--view') + 1] if '--view' in args else None
V = []; F = []; names = []; cur = None
for line in open(src):
    if line.startswith('v '):
        V.append([float(x) for x in line.split()[1:4]])
    elif line.startswith('o '):
        cur = line.split()[1]
    elif line.startswith('f '):
        if only and (cur is None or only not in cur):
            continue
        F.append([int(t.split('/')[0]) - 1 for t in line.split()[1:4]]); names.append(cur)
V = np.array(V); F = np.array(F)
W, H = 960, 960
if view_file:
    d = json.loads(open(view_file).readline())
    view = np.array(d['view']).reshape(4, 4); proj = np.array(d['projection']).reshape(4, 4)
    h = np.c_[V, np.ones(len(V))] @ view @ proj
    ndc = h[:, :3] / h[:, 3:4]
    sx = (ndc[:, 0] * 0.5 + 0.5) * W; sy = (0.5 - ndc[:, 1] * 0.5) * H; depth = h[:, 3]
    W2 = W
else:
    used = V[np.unique(F)]
    lo, hi = used.min(0), used.max(0); c = (lo + hi) / 2; s = 0.9 * W / max(hi[0] - lo[0], hi[1] - lo[1])
    # +Z forward, +X left in this game's world: look down +Z.
    sx = W / 2 - (V[:, 0] - c[0]) * s; sy = H / 2 - (V[:, 1] - c[1]) * s; depth = V[:, 2]
img = Image.new('RGB', (W, H), (30, 30, 36)); dr = ImageDraw.Draw(img)
a, b, c3 = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
n = np.cross(b - a, c3 - a); n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
light = np.abs(n @ np.array([0.3, 0.5, -0.8]) / np.linalg.norm([0.3, 0.5, -0.8]))
order = np.argsort(-depth[F].mean(1))
for i in order:
    zs = depth[F[i]]
    if view_file and (zs <= 0.01).any():
        continue
    hue = int(hashlib.md5(names[i].encode()).hexdigest()[:4], 16) / 65535
    r, g, bb = colorsys.hsv_to_rgb(hue, 0.45, 0.35 + 0.65 * light[i])
    pts = [(sx[k], sy[k]) for k in F[i]]
    dr.polygon(pts, fill=(int(r * 255), int(g * 255), int(bb * 255)))
img.save(out)
print(out, len(F), 'faces')
