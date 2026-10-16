"""Rig accuracy of a D3C1 asset's joint parts against DOA3's own CPU-written
joints: pose each part on the dump's DOA3 skeleton and compare with the
vertices DOA3 actually drew that frame.

usage: rig_error.py <asset.d3c> <bank hex> <present dir>...
"""
import sys, json, os, numpy as np
import mesh_dump_export as mde
from asset_preview import load, pose
from retarget_preview import bank
from common import local_positions

def compact(idx, n):
    used = np.unique(idx[idx < n]); remap = np.zeros(n, np.int64); remap[used] = np.arange(len(used))
    return used, remap[np.minimum(idx, n - 1)]

def truth(P):
    out = {}
    for l in open(P + '/draws.jsonl'):
        x = json.loads(l)
        if not x['worlds'] or x.get('offscreen') or not os.path.exists(P + '/d%05d.idx' % x['ordinal']): continue
        idx = np.frombuffer(open(P + '/d%05d.idx' % x['ordinal'], 'rb').read(), np.uint16).astype(int)
        pos = local_positions(P, x['ordinal'], x['stride']).astype(float)
        used, cidx = compact(idx, len(pos))
        W = np.array(x['worlds'][0]).reshape(4, 4)
        out.setdefault(cidx.tobytes(), []).append((np.c_[pos[used], np.ones(len(used))] @ W)[:, :3])
    return out

if __name__ == '__main__':
    asset, base, presents = sys.argv[1], int(sys.argv[2], 16), sys.argv[3:]
    magic, rel, hier, parts = load(asset)
    errs = {}
    for P in presents:
        B = bank(open(P + '/ram.bin', 'rb').read(), base, 23); T = truth(P)
        for i, p in enumerate(parts):
            if len(p['binds']) < 2: continue
            key = p['idx'].astype(np.int64).tobytes()
            hip = B[2][3, :3]
            cands = [t for t in T.get(key, []) if len(t) == p['count'] and np.linalg.norm(t.mean(0) - hip) < 1.0]
            if not cands: continue
            v = pose(p, B)
            d = min((np.linalg.norm(v - t, axis=1) for t in cands), key=lambda d: d.mean())
            errs.setdefault(i, []).append((d.mean(), d.max()))
    for i, e in sorted(errs.items(), key=lambda kv: -max(m for _, m in kv[1])):
        e = np.array(e)
        print('part %3d bones %-16s frames %2d mean err %.4f max err %.4f' % (i, [b for b, _ in parts[i]['binds']], len(e), e[:, 0].mean(), e[:, 1].max()))
