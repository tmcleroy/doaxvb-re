"""Load a DXC1/D3C1 costume asset and pose it on the other game's skeleton offline.

usage: asset_preview.py <asset> <target present dir> <target bank hex> <out.png> [overlay entries]
A D3C1 (DOA3 source) asset is posed on a DOAXBV 32-bone bank; a DXC1 on DOA3's.
"""
import struct, sys, json, numpy as np
import mesh_dump_export as mde
from retarget_preview import bank, render, corr4, doax_bank_on
from common import draws, local_positions

def load(path):
    f = open(path, 'rb'); magic = f.read(4)
    ver, nrel, ntex, npart = struct.unpack('<4I', f.read(16))
    rel = [struct.unpack('<2I16f', f.read(72)) for _ in range(nrel)]
    nh = struct.unpack('<I', f.read(4))[0]
    hier = [struct.unpack('<2I3f', f.read(20)) for _ in range(nh)]
    for _ in range(ntex):
        t = struct.unpack('<9I', f.read(36)); f.read(t[7] + t[8])
    parts = []
    for _ in range(npart):
        h = struct.unpack('<5IiII', f.read(32)); binds = []
        for _ in range(h[7]):
            b = struct.unpack('<I', f.read(4))[0]; O = np.array(struct.unpack('<16f', f.read(64))).reshape(4, 4)
            binds.append((b, O))
        if ver >= 3: f.read(4 + 68)
        if ver >= 5: f.read(12)
        if ver >= 4 and h[6] & 4: f.read(16 + (48 if ver >= 6 else 0) + 4 * h[3])
        vtx = f.read(h[0] * h[3]); idx = np.frombuffer(f.read(h[4] * 2), np.uint16).astype(int)
        parts.append(dict(stride=h[0], fvf=h[1], primitive=h[2], count=h[3], binds=binds, vtx=vtx, idx=idx))
    return magic, rel, hier, parts

def doa3_bank_on(BX, rel, hier):
    """DOA3-convention 23-bone bank posed by a DOAXBV skeleton (inverse of doax_bank_on)."""
    B = np.zeros((23, 4, 4))
    for b in range(15):
        B[b] = corr4(b).T @ BX[b] if b in range(15) else BX[b]
    rels = {r[0]: (r[1], np.array(r[2:]).reshape(4, 4)) for r in rel}
    for b, parent, *off in hier:
        if b in rels:
            B[b] = rels[b][1] @ B[parent]; continue
        B[b][3, :3] = np.array(off) @ B[parent][:3, :3] + B[parent][3, :3]
    return B

def pose(p, bk):
    lay = mde.fvf_layout(p['fvf']); n = p['count']
    rows = np.frombuffer(p['vtx'], np.uint8).reshape(n, p['stride'])
    pos = rows[:, :12].copy().view(np.float32).reshape(n, 3).astype(float)
    homo = np.c_[pos, np.ones(n)]
    mats = [O @ bk[b] for b, O in p['binds']]
    nw = min(lay['weights'], len(mats) - 1)
    if nw == 0: return (homo @ mats[0])[:, :3]
    w = rows[:, 12:12 + 4 * nw].copy().view(np.float32).reshape(n, nw).astype(float)
    rem = 1 - w.sum(1); out = (homo @ mats[0]) * w[:, :1]
    for k in range(1, len(mats)):
        ww = rem if k == len(mats) - 1 else w[:, k]; out += (homo @ mats[k]) * ww[:, None]
    return out[:, :3]

if __name__ == '__main__':
    asset, P, base, out = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), sys.argv[4]
    magic, rel, hier, parts = load(asset)
    ram = open(P + '/ram.bin', 'rb').read()
    if magic == b'D3C1':
        bk = doa3_bank_on(bank(ram, base, 32), rel, hier)
    else:
        raise SystemExit('DXC1 preview: use retarget_preview.py')
    first = json.loads(open(P + '/draws.jsonl').readline())
    V = np.array(first['view']).reshape(4, 4); Pj = np.array(first['projection']).reshape(4, 4)
    sets = []
    for p in parts:
        tris = mde.triangles(p['primitive'], p['idx']); tris = tris[(tris < p['count']).all(1)]
        if len(tris): sets.append((pose(p, bk), tris, (240, 170, 150) if len(p['binds']) == 1 else (120, 230, 120)))
    if len(sys.argv) > 5:
        ent = [int(e) for e in sys.argv[5].split(',')]
        for x in draws(P).values():
            s = x.get('source')
            if not s or s['entry'] not in ent or not x['worlds']: continue
            pos = local_positions(P, x['ordinal'], x['stride']).astype(float)
            idx = np.frombuffer(open(P + '/d%05d.idx' % x['ordinal'], 'rb').read(), np.uint16).astype(int)
            tris = mde.triangles(x['primitive'], idx); tris = tris[(tris < len(pos)).all(1)]
            W = np.array(x['worlds'][0]).reshape(4, 4)
            sets.append(((np.c_[pos, np.ones(len(pos))] @ W)[:, :3], tris, (120, 170, 240)))
    render(sets, V, Pj, out); print(out, len(parts), 'parts')
