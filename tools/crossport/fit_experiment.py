"""Compare skinning models against DOA3's CPU joints across frames.
Model A (LBS): v = sum_k w_k (p @ B_k^-1(rest) @ B_k(t)), one rest point.
Model B (per-bone locals): v = sum_k (q_k @ R_k(t) + w_k T_k(t)), q_k = w_k p_k.
Fit on half the frames, report error on the rest."""
import sys, glob, numpy as np
from asset_preview import load
from retarget_preview import bank
import rig_error as re_

asset, base, presents = sys.argv[1], int(sys.argv[2], 16), sys.argv[3:]
magic, rel, hier, parts = load(asset)
frames = []
for P in presents:
    B = bank(open(P + '/ram.bin', 'rb').read(), base, 23); T = re_.truth(P)
    frames.append((B, T))
bones_all = [b for b in range(23) if all(abs(np.linalg.det(f[0][b][:3, :3])) > 0.5 for f in frames)]
for pi in map(int, sys.argv[0:0] or []): pass
def tracks(p):
    key = p['idx'].astype(np.int64).tobytes(); out = []
    for B, T in frames:
        hip = B[2][3, :3]
        c = [t for t in T.get(key, []) if len(t) == p['count'] and np.linalg.norm(t.mean(0) - hip) < 1.0]
        if c: out.append((B, c[0]))
    return out
def solve(tr, cand, model):
    n = len(tr[0][1]); K = len(cand); err = []
    train, test = tr[::2], tr[1::2]
    W = np.zeros((n, K)); Q = np.zeros((n, K, 3))
    B0, V0 = train[0]
    for v in range(n):
        A, y = [], []
        for B, V in train:
            if model == 'A':
                cols = [(np.r_[V0[v], 1] @ np.linalg.inv(B0[k]) @ B[k])[:3] for k in cand]
                A.append(np.stack(cols, 1)); y.append(V[v])
            else:
                blk = np.zeros((3, 4 * K))
                for j, k in enumerate(cand):
                    blk[:, 4*j:4*j+3] = B[k][:3, :3].T; blk[:, 4*j+3] = B[k][3, :3]
                A.append(blk); y.append(V[v])
        A = np.concatenate(A); y = np.concatenate(y)
        if model == 'A':
            A = np.vstack([A, np.full((1, K), 10.0)]); y = np.r_[y, 10.0]
            s = np.linalg.lstsq(A, y, rcond=None)[0]; W[v] = s
        else:
            row = np.zeros(4 * K); row[3::4] = 10.0
            A = np.vstack([A, row, 1e-3 * np.eye(4 * K)]); y = np.r_[y, 10.0, np.zeros(4 * K)]
            s = np.linalg.lstsq(A, y, rcond=None)[0]
            for j in range(K): Q[v, j] = s[4*j:4*j+3]; W[v, j] = s[4*j+3]
    for B, V in test:
        if model == 'A':
            P = np.stack([sum(W[v, j] * (np.r_[V0[v], 1] @ np.linalg.inv(B0[k]) @ B[k])[:3] for j, k in enumerate(cand)) for v in range(n)])
        else:
            P = np.stack([sum(Q[v, j] @ B[k][:3, :3] + W[v, j] * B[k][3, :3] for j, k in enumerate(cand)) for v in range(n)])
        err.append(np.linalg.norm(P - V, axis=1))
    e = np.concatenate(err); return e.mean(), e.max()
for i, p in enumerate(parts):
    if len(p['binds']) < 2: continue
    if sys.argv[-1:] and False: pass
    tr = tracks(p)
    if len(tr) < 6: continue
    mine = [b for b, _ in p['binds']]
    ra = solve(tr, mine, 'A'); rb = solve(tr, mine, 'B')
    near = sorted(bones_all, key=lambda b: np.linalg.norm(tr[0][1].mean(0) - tr[0][0][b][3, :3]))[:6]
    rc = solve(tr, near, 'B')
    print('part %3d frames %2d bones %-14s LBS %.4f/%.4f  per-bone %.4f/%.4f  6-nearest per-bone %s %.4f/%.4f'
          % (i, len(tr), mine, *ra, *rb, near, *rc))
