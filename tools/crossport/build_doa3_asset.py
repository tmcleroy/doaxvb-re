"""Build a DOAXBV costume-injection asset (magic D3C1) from DOA3 mesh dumps.

usage: build_doa3_asset.py <present> [more presents...] <out.d3c> --bank 0xADDR

All presents must show the same fighter in the bone bank at --bank (23 bones).
DOA3 writes its joint and cloth skins on the CPU each frame, so those draws
are baked at the source pose; they are re-rigged here: every vertex is bound
to the two bone segments nearest to it (source-pose relative transforms, so
the source pose is reproduced exactly) with weights from their distances.
Output is extracted game data: keep it under private/.
"""
import os, struct, sys
import numpy as np
from common import draws, local_positions
from retarget_preview import bank
import mesh_dump_export as mde

BONES = 23
# DOA3 hierarchy (recovered from motion; same numbering as DOAXBV's first 15).
PARENT = {0: 2, 1: 0, 8: 0, 14: 0, 4: 8, 10: 14, 5: 4, 11: 10, 6: 2, 12: 2, 7: 6, 13: 12,
          3: 7, 9: 13, 15: 1, 16: 1, 19: 4, 20: 10}
HIERARCHY = [(0, 2), (1, 0), (15, 1), (16, 1), (8, 0), (14, 0), (4, 8), (10, 14), (5, 4), (11, 10),
             (19, 4), (20, 10), (6, 2), (12, 2), (7, 6), (13, 12), (3, 7), (9, 13)]
RELATIVE = {15: 1, 16: 1, 19: 4, 20: 10}   # DOA3-only bones: carried by source offset
SEGMENT_END = {2: 0, 0: 1, 1: None, 8: 4, 14: 10, 4: 5, 10: 11, 5: None, 11: None,
               6: 7, 12: 13, 7: 3, 13: 9, 3: None, 9: None}

def valid(B, i): return abs(np.linalg.det(B[i][:3, :3])) > 0.5

def segment_distance(p, a, b):
    if b is None: return np.linalg.norm(p - a, axis=1)
    ab = b - a; t = np.clip(((p - a) @ ab) / max(ab @ ab, 1e-9), 0, 1)
    return np.linalg.norm(p - (a + t[:, None] * ab), axis=1)

CPU_TRACKS = {}   # (bone, verts, index hash) -> [(world positions, bank)] across presents

def fit_weights(world0, B0, track, chosen, prior):
    """Per-vertex blend weights that best reproduce every tracked frame.
    Each vertex blends only the bones its distance prior uses (its two
    nearest), so a piece spanning both hands cannot borrow the other arm; the
    two-bone case is solved exactly with the weight bounded to [0, 1]."""
    n = len(world0); K = len(chosen)
    local = [np.c_[world0, np.ones(n)] @ np.linalg.inv(B0[k]) for k in chosen]
    pred = [np.stack([(local[k] @ B_t[chosen[k]])[:, :3] for _, B_t in track]) for k in range(K)]  # K x T x n x 3
    target = np.stack([pos_t for pos_t, _ in track])                                              # T x n x 3
    w = prior.copy()
    for v in range(n):
        use = [k for k in range(K) if prior[v, k] > 0]
        if len(use) == 1:
            continue
        if len(use) != 2:
            use = list(np.argsort(-prior[v])[:2])
        a, b = use
        pa, pb, y = pred[a][:, v], pred[b][:, v], target[:, v]
        d = (pa - pb).ravel()                      # y = pb + t (pa - pb)
        t = float(np.clip(d @ (y - pb).ravel() / max(d @ d, 1e-12), 0.0, 1.0))
        w[v] = 0.0; w[v, a] = t; w[v, b] = 1.0 - t
    return w

def rerig(x, b, O, B, vtx, snap=None, chosen=None, rows_sub=None):
    """Auto-rig a baked part bound to bone b onto up to four bones: the part's
    most-used bones among each vertex's two nearest bone segments, with
    inverse-square distance weights (source pose reproduced exactly)."""
    lay = mde.fvf_layout(x['fvf']); stride = x['stride']; n = len(vtx) // stride
    rows = np.frombuffer(vtx[:n * stride], np.uint8).reshape(n, stride)
    pos = rows[:, :12].copy().view(np.float32).reshape(n, 3).astype(float)
    world = (np.c_[pos, np.ones(n)] @ (O @ B[b]))[:, :3]
    cands = [k for k in range(15) if valid(B, k)]
    trk = CPU_TRACKS.get(x.get('_track_key'))
    if trk and rows_sub is not None:
        trk = [(pos_t[rows_sub], B_t) for pos_t, B_t in trk]
    d = bone_distance(world, B, cands, trk)
    if chosen is None:
        top2 = np.argsort(d, axis=1)[:, :2]
        use = np.bincount(top2.ravel(), minlength=len(cands))
        chosen = [cands[i] for i in np.argsort(use)[::-1][:4] if use[i] > 0]
        if b in cands and b not in chosen:
            chosen = [b] + chosen[:3]
    if len(chosen) < 2:
        return None
    ci = [cands.index(k) for k in chosen]
    dc = d[:, ci]
    w = 1.0 / np.maximum(dc, 1e-3) ** 2
    keep = np.argsort(dc, axis=1)[:, :2]                     # each vertex: its two nearest of the chosen
    mask = np.zeros_like(w); np.put_along_axis(mask, keep, 1.0, axis=1)
    w = w * mask; w /= w.sum(1, keepdims=True)
    # Along a bone's segment, hand weight to the child bone as the joint nears
    # (half at the joint). Without this the neck, which lies on the chest's
    # segment up to the head joint, follows the chest alone.
    for j, k in enumerate(chosen):
        c = SEGMENT_END.get(k)
        if c is None or c not in chosen: continue
        a0, a1 = B[k][3, :3], B[c][3, :3]; ab = a1 - a0
        tt = np.clip(((world - a0) @ ab) / max(ab @ ab, 1e-9), 0, 1)
        ramp = np.clip((tt - 0.5) / 0.5, 0, 1); ramp = ramp * ramp * (3 - 2 * ramp)
        # An end bone (head, hand, foot) has no segment of its own to take the
        # other half at the joint, so it takes all of it there.
        if SEGMENT_END.get(c) is not None: ramp *= 0.5
        move = w[:, j] * ramp
        w[:, j] -= move; w[:, chosen.index(c)] += move
    track = CPU_TRACKS.get(x.get('_track_key'))
    if track and rows_sub is not None:
        track = [(pos_t[rows_sub], B_t) for pos_t, B_t in track]
    if track and len(track) >= 3:
        rest = int(np.argmin([np.abs(t[0] - world).max() for t in track]))
        others = [t for i, t in enumerate(track) if i != rest]
        fitted = fit_weights(world, B, others, chosen, w)
        # Keep the fit only where blend skinning explains DOA3's motion. Cloth
        # that DOA3 simulates (coat tails) fits nothing; fitted weights there
        # explode, so those vertices keep the smooth distance weights.
        local = [np.c_[world, np.ones(n)] @ np.linalg.inv(B[k]) for k in chosen]
        resid = np.zeros(n)
        for pos_t, B_t in others:
            rec = sum(fitted[:, j:j+1] * (local[j] @ B_t[k])[:, :3] for j, k in enumerate(chosen))
            resid = np.maximum(resid, np.linalg.norm(rec - pos_t, axis=1))
        good = resid < 0.02
        w = np.where(good[:, None], fitted, w)
        x = dict(x, _fit_kept=float(good.mean()))
        print('  rerig bones %s verts %d fit kept %.0f%%' % (chosen, n, 100 * good.mean()))
    if snap is not None and len(snap[0]):
        # Border vertices that coincide with a rigid part's vertex follow that
        # part's bone exactly, so the joint skin meets the segments seamlessly.
        rp, rb = snap
        for start in range(0, n, 512):
            chunk = world[start:start + 512]
            d = np.linalg.norm(chunk[:, None, :] - rp[None, :, :], axis=2)
            near = d.argmin(1)
            for j, (k, dist) in enumerate(zip(near, d[np.arange(len(chunk)), near])):
                if dist < 0.002 and int(rb[k]) in chosen:
                    w[start + j] = 0.0; w[start + j, chosen.index(int(rb[k]))] = 1.0
    nb = len(chosen); weights = np.ascontiguousarray(w[:, :nb - 1], dtype=np.float32)   # last bone takes the remainder
    binds = [(k, O @ B[b] @ np.linalg.inv(B[k])) for k in chosen]
    nrm = rows[:, lay['normal']:lay['normal'] + 12] if lay['normal'] is not None else np.zeros((n, 12), np.uint8)
    rest = rows[:, (lay['normal'] + 12) if lay['normal'] is not None else 12:]
    out = np.concatenate([rows[:, :12], weights.view(np.uint8).reshape(n, 4 * (nb - 1)), nrm, rest], axis=1)
    fvf = (x['fvf'] & ~0x0E) | {2: 0x06, 3: 0x08, 4: 0x0A}[nb]   # XYZB1/B2/B3
    return dict(x, fvf=fvf, stride=out.shape[1]), binds, out.tobytes()

def bone_distance(world, B, cands, track=None):
    """How well each candidate bone explains each vertex. With motion tracks,
    the worst-frame error of following that bone rigidly from the rest pose
    (touching limbs move apart); without, the distance to the bone segment."""
    geo = np.stack([segment_distance(world, B[k][3, :3],
                                     B[SEGMENT_END[k]][3, :3] if SEGMENT_END.get(k) is not None else None)
                    for k in cands], axis=1)
    if track and len(track) >= 3:
        rest = int(np.argmin([np.abs(t[0] - world).max() for t in track]))
        homo = np.c_[world, np.ones(len(world))]
        cols = []
        for k in cands:
            local = homo @ np.linalg.inv(B[k])
            cols.append(np.max([np.linalg.norm((local @ B_t[k])[:, :3] - pos_t, axis=1)
                                for i, (pos_t, B_t) in enumerate(track) if i != rest], axis=0))
        # A far bone can match by coincidence in a short capture (the upper arm
        # for the neck); only bones near the vertex may drive it.
        return np.stack(cols, axis=1) + np.where(geo > geo.min(1, keepdims=True) + 0.10, 10.0, 0.0)
    return geo

def split_by_bones(x, b, O, B, vtx):
    """Groups of triangles whose vertices' two nearest bones fit the runtime's
    four bones per part. A CPU piece can span both hands or both legs; one
    shared four-bone set would bind some vertices to the wrong limb."""
    stride = x['stride']; n = len(vtx) // stride
    pos = np.frombuffer(vtx[:n * stride], np.uint8).reshape(n, stride)[:, :12].copy().view(np.float32).reshape(n, 3).astype(float)
    world = (np.c_[pos, np.ones(n)] @ (O @ B[b]))[:, :3]
    cands = [k for k in range(15) if valid(B, k)]
    d = bone_distance(world, B, cands, CPU_TRACKS.get(x.get('_track_key')))
    near = [set(cands[i] for i in r) for r in np.argsort(d, axis=1)[:, :2]]
    tris = mde.triangles(x['primitive'], np.frombuffer(x['_idx'], np.uint16).astype(int))
    tris = tris[(tris < n).all(1)]
    tris = tris[(tris[:, 0] != tris[:, 1]) & (tris[:, 1] != tris[:, 2]) & (tris[:, 0] != tris[:, 2])]
    groups = []                                     # [bone set, triangle list]
    order = np.argsort([min(near[t[0]]) for t in tris], kind='stable')
    for t in tris[order]:
        need = near[t[0]] | near[t[1]] | near[t[2]]
        if len(need) > 4:                           # keep the four that fit the triangle best
            need = set(sorted(need, key=lambda k: d[t, cands.index(k)].sum())[:4])
        for g in groups:
            if len(g[0] | need) <= 4:
                g[0] |= need; g[1].append(t); break
        else:
            groups.append([set(need), [t]])
    return groups

def collect(P, B, parts, textures, tex_index, seen):
    pending, rigid_points = [], []
    solid_hair = set()        # trimmed index lists of head-bound CPU draws that write depth
    for x in draws(P).values():
        if x.get('source') or x['offscreen'] or not x['worlds'] or not x.get('depth_write', True): continue
        W = np.array(x['worlds'][0], float).reshape(4, 4)
        if not valid(B, 1) or np.abs(W[:3, :3] - B[1][:3, :3]).max() > 1e-4: continue
        ip = P + '/d%05d.idx' % x['ordinal']; vp = P + '/d%05d.vtx' % x['ordinal']
        if os.path.exists(ip) and os.path.exists(vp):
            solid_hair.add(own_vertices(open(ip, 'rb').read(), os.path.getsize(vp) // x['stride'])[1])
    for x in draws(P).values():
        if x['offscreen'] or not x['worlds'] or x.get('program'):
            continue
        if x.get('stencil') and not x.get('depth_write', True) and not x.get('texture'):
            continue
        vtx = P + '/d%05d.vtx' % x['ordinal']; idx = P + '/d%05d.idx' % x['ordinal']
        if not (os.path.exists(vtx) and os.path.exists(idx)):
            continue
        W = np.array(x['worlds'][0], float).reshape(4, 4)
        errs = [np.abs(W[:3, :3] - B[i][:3, :3]).max() if valid(B, i) else 9 for i in range(BONES)]
        b = int(np.argmin(errs))
        if errs[b] > 1e-4 or np.linalg.norm(W[3, :3] - B[b][3, :3]) > 0.6:
            continue
        pos = local_positions(P, x['ordinal'], x['stride'])
        if np.ptp(pos[np.isfinite(pos).all(1)], axis=0).max() > 2.0:
            continue
        s = x.get('source'); idx_bytes = open(idx, 'rb').read(); vtx_bytes = open(vtx, 'rb').read()
        if not s and b == 1 and x.get('alpha_blend') and not x.get('depth_write', True) \
                and own_vertices(open(idx, 'rb').read(), len(pos))[1] in solid_hair:
            continue    # hair's translucent first pass; its second pass draws it solid with depth
        track_key = (b, len(pos), hash(idx_bytes))
        if not s:
            # CPU skins share one buffer whose length varies by frame: trim to
            # this piece's own vertices first, so the same piece seen in two
            # frames is recognised as one.
            used, idx_bytes = own_vertices(idx_bytes, len(pos))
            st = x['stride']
            vtx_bytes = np.frombuffer(vtx_bytes[:len(pos) * st], np.uint8).reshape(len(pos), st)[used].tobytes()
            pos = pos[used]
        key = (s['entry'], s['offset'], hash(idx_bytes)) if s else ('cpu', b, hash(idx_bytes), len(pos))
        if key in seen:
            continue
        seen.add(key)
        O = W @ np.linalg.inv(B[b])
        binds = [(b, O)]
        if not s:
            x = dict(x, _track_key=track_key, _hair_key=hair_key(idx_bytes, len(pos)))
        defer = not s and b != 1      # head parts stay single-bone: hair is handled as a soft part
        t = -1
        tx = x.get('texture')
        if tx and os.path.exists(P + '/tex-%08x.bin' % tx['data']):
            data = open(P + '/tex-%08x.bin' % tx['data'], 'rb').read()
            tkey = (tx['format'], tx['width'], tx['height'], hash(data))
            if tkey not in tex_index:
                pal = P + '/pal-%08x.bin' % tx['data']
                tex_index[tkey] = len(textures)
                textures.append((tx, data, open(pal, 'rb').read() if os.path.exists(pal) else b''))
            t = tex_index[tkey]
        flags = (1 if x['alpha_blend'] else 0) | (2 if x.get('depth_write', True) else 0)
        if defer:
            pending.append((len(parts), x, b, O, vtx_bytes))
        elif s:
            rigid_points.append(((np.c_[pos.astype(float), np.ones(len(pos))] @ W)[:, :3], b))
        parts.append((x, binds, t, flags, vtx_bytes, idx_bytes))
    snap = (np.concatenate([p for p, _ in rigid_points]) if rigid_points else np.zeros((0, 3)),
            np.concatenate([np.full(len(p), b) for p, b in rigid_points]) if rigid_points else np.zeros(0))
    for i, x, b, O, vtx_bytes in pending:
        old = parts[i]
        groups = split_by_bones(dict(x, _idx=old[5]), b, O, B, vtx_bytes)
        if len(groups) <= 1:
            chosen = sorted(groups[0][0], key=lambda k: k != b) if groups else None
            r = rerig(x, b, O, B, vtx_bytes, snap, chosen=chosen if chosen and len(chosen) >= 2 else None)
            if r:
                nx, binds, nv = r
                parts[i] = (nx, binds, old[2], old[3], nv, old[5])
            continue
        st = x['stride']; n = len(vtx_bytes) // st
        rows = np.frombuffer(vtx_bytes[:n * st], np.uint8).reshape(n, st)
        first = True
        for bones, tris in groups:
            tris = np.array(tris)
            used = np.unique(tris); remap = np.zeros(n, np.int64); remap[used] = np.arange(len(used))
            sub_vtx = rows[used].tobytes(); sub_idx = remap[tris].astype(np.uint16).ravel().tobytes()
            sx = dict(x, primitive=mde.PT_LIST)
            chosen = sorted(bones, key=lambda k: k != b)
            r = rerig(sx, b, O, B, sub_vtx, snap, chosen=chosen if len(chosen) >= 2 else None, rows_sub=used) if len(chosen) >= 2 else None
            entry = (r[0], r[1], old[2], old[3], r[2], sub_idx) if r else (sx, [(b, O)], old[2], old[3], sub_vtx, sub_idx)
            if first: parts[i] = entry; first = False
            else: parts.append(entry)
        print('  split CPU part on bone %d into %d groups: %s' % (b, len(groups), [sorted(g[0]) for g in groups]))

def main():
    argv = sys.argv[1:]
    i = argv.index('--bank'); base = int(argv[i + 1], 0); del argv[i:i + 2]
    *presents, out = argv
    build(presents, out, base)

def own_vertices(idx_bytes, n):
    """Vertices a draw's triangles use, and its indices remapped onto them.
    DOA3 draws CPU skins from one shared buffer, so each draw's vertex range
    also holds every earlier piece."""
    ix = np.frombuffer(idx_bytes, np.uint16).astype(np.int64)
    used = np.unique(ix[ix < n])
    remap = np.zeros(n, np.int64); remap[used] = np.arange(len(used))
    return used, remap[np.minimum(ix, n - 1)].astype(np.uint16).tobytes()

def hair_key(idx_bytes, n):
    return (n, hash(idx_bytes))

def gather_tracks(presents, base):
    CPU_TRACKS.clear()
    for P in presents:
        B = bank(open(P + '/ram.bin', 'rb').read(), base, BONES)
        for x in draws(P).values():
            if x.get('source') or x['offscreen'] or not x['worlds'] or x.get('program'): continue
            W = np.array(x['worlds'][0], float).reshape(4, 4)
            errs = [np.abs(W[:3, :3] - B[i][:3, :3]).max() if valid(B, i) else 9 for i in range(BONES)]
            b = int(np.argmin(errs))
            if errs[b] > 1e-4 or np.linalg.norm(W[3, :3] - B[b][3, :3]) > 0.6: continue
            idx = P + '/d%05d.idx' % x['ordinal']
            if not os.path.exists(idx): continue
            pos = local_positions(P, x['ordinal'], x['stride']).astype(float)
            ib = open(idx, 'rb').read()
            key = (b, len(pos), hash(ib))
            used, _ = own_vertices(ib, len(pos))
            CPU_TRACKS.setdefault(key, []).append(((np.c_[pos[used], np.ones(len(used))] @ W)[:, :3], B))

def same_fighter(presents, base):
    """Presents that show the same character in this bank. Dumps spanning a
    scene change hold the next fighter in the same slot; their textures differ."""
    sets = []
    for P in presents:
        B = bank(open(P + '/ram.bin', 'rb').read(), base, BONES); tex = set()
        for x in draws(P).values():
            if not x['worlds'] or x['offscreen'] or not x.get('texture'): continue
            W = np.array(x['worlds'][0], float).reshape(4, 4)
            if min(np.abs(W[:3, :3] - B[i][:3, :3]).max() if valid(B, i) else 9 for i in range(BONES)) > 1e-4: continue
            f = P + '/tex-%08x.bin' % x['texture']['data']
            if os.path.exists(f): tex.add(hash(open(f, 'rb').read()))
        sets.append(tex)
    def sim(a, b): return len(a & b) / max(1, min(len(a), len(b)))
    ref = max(range(len(sets)), key=lambda i: sum(sim(sets[i], t) for t in sets))
    keep = [P for P, t in zip(presents, sets) if t and sim(t, sets[ref]) >= 0.5]
    if len(keep) < len(presents):
        print('  kept %d of %d presents (others show a different fighter)' % (len(keep), len(presents)))
    return keep

def build(presents, out, base, hair_presents=None):
    presents = same_fighter(presents, base)
    hair_presents = same_fighter(hair_presents, base) if hair_presents else presents
    gather_tracks(presents, base)
    textures, tex_index, parts, seen = [], {}, [], set()
    B0 = None
    for P in presents:
        B = bank(open(P + '/ram.bin', 'rb').read(), base, BONES)
        if B0 is None: B0 = B
        before = len(parts)
        collect(P, B, parts, textures, tex_index, seen)
        print(P.split('/')[-1], '+', len(parts) - before)
    # Soft parts: head-bound parts whose vertices move between the presents
    # (hair rewritten in place). Rest shape = mean, mask from travel distance.
    import collections
    samples = collections.defaultdict(list)
    for P in hair_presents:     # a calm, tight window: hair swings during a fight
        B = bank(open(P + '/ram.bin', 'rb').read(), base, BONES)
        for x in draws(P).values():
            if x['offscreen'] or not x['worlds'] or len(x['worlds']) > 1: continue
            W = np.array(x['worlds'][0], float).reshape(4, 4)
            if np.abs(W[:3, :3] - B[1][:3, :3]).max() > 1e-4 or np.linalg.norm(W[3, :3] - B[1][3, :3]) > 0.6: continue
            pos = local_positions(P, x['ordinal'], x['stride']).astype(float)
            ip = P + '/d%05d.idx' % x['ordinal']
            if not os.path.exists(ip): continue
            used, cidx = own_vertices(open(ip, 'rb').read(), len(pos))
            loc = (np.c_[pos[used], np.ones(len(used))] @ (W @ np.linalg.inv(B[1])))[:, :3]
            samples[hair_key(cidx, len(used))].append((loc, B[1][:3, :3].copy()))
    soft = {}
    for i, (x, binds, t, flags, vtx, idx) in enumerate(parts):
        if len(binds) != 1 or binds[0][0] != 1: continue
        b, O = binds[0]; n = len(vtx) // x['stride']
        got = samples.get(x.get('_hair_key'))
        if not got: continue
        arr = np.stack([g[0] for g in got])
        # Rest shape: the average over the calm hair window (frames a couple of
        # presents apart), with its mean head rotation. Every head part takes
        # it, moving or not: the collected geometry may be mid-swing.
        mean = arr.mean(0)
        u, _, vt = np.linalg.svd(np.mean([g[1] for g in got], axis=0)); Rsrc = u @ vt
        raw = (np.c_[mean, np.ones(n)] @ np.linalg.inv(O))[:, :3].astype(np.float32)
        rows = np.frombuffer(vtx[:n * x['stride']], np.uint8).reshape(n, x['stride']).copy()
        rows[:, :12] = raw.view(np.uint8).reshape(n, 12)
        parts[i] = (x, binds, t, flags, rows.tobytes(), idx)
        amp = np.linalg.norm(arr - mean, axis=2).max(0) if len(got) >= 4 else np.zeros(n)
        mask = np.clip((amp - 0.005) / 0.04, 0, 1)
        mask = mask.astype(np.float32)
        if mask.max() <= 0: continue
        # DOA3 hair parts share one dynamic buffer: only the vertices this
        # part's triangles use count when finding its still root.
        ix = np.frombuffer(idx, np.uint16).astype(int); own = np.zeros(n, bool); own[ix[ix < n]] = True
        root = mean[(mask < 0.05) & own]
        parts[i] = (x, binds, t, flags | 4, rows.tobytes(), idx)
        soft[i] = ((55.0, 5.0, 0.15, 0.07), mask, Rsrc, root.mean(0) if len(root) else np.zeros(3))
    print('soft parts:', len(soft))
    # Hair below the head joint (the nape) rests against the neck in DOA3's
    # simulation. Rigid to the head it swings in front of the throat when the
    # head turns, so blend it toward the chest the lower it hangs.
    Bc = bank(open(hair_presents[0] + '/ram.bin', 'rb').read(), base, BONES)
    for i, (x, binds, t, flags, vtx, idx) in enumerate(parts):
        if x.get('source') or len(binds) != 1 or binds[0][0] != 1: continue
        O = binds[0][1]; st = x['stride']; n = len(vtx) // st
        rows = np.frombuffer(vtx[:n * st], np.uint8).reshape(n, st)
        local = (np.c_[rows[:, :12].copy().view(np.float32).reshape(n, 3), np.ones(n)] @ O)[:, :3]
        # Hair wraps the head; the mouth interior also dips below the joint but is narrow.
        if local[:, 1].min() >= 0.0 or np.ptp(local[:, 0]) < 0.12: continue
        chest = np.clip((0.0 - local[:, 1]) / 0.10, 0, 0.7)
        lay = mde.fvf_layout(x['fvf'])
        nrm = rows[:, lay['normal']:lay['normal'] + 12] if lay['normal'] is not None else np.zeros((n, 12), np.uint8)
        rest = rows[:, (lay['normal'] + 12) if lay['normal'] is not None else 12:]
        wh = (1.0 - chest).astype(np.float32)                    # first bone (head); chest takes the rest
        blended = np.concatenate([rows[:, :12], wh.view(np.uint8).reshape(n, 4), nrm, rest], axis=1)
        nx = dict(x, fvf=(x['fvf'] & ~0x0E) | 0x06, stride=blended.shape[1])
        nb = [(1, O), (0, O @ Bc[1] @ np.linalg.inv(Bc[0]))]
        parts[i] = (nx, nb, t, flags & ~4, blended.tobytes(), idx)
        soft.pop(i, None)
        print('  nape blend: part %d verts %d max chest weight %.2f' % (i, n, chest.max()))
    with open(out, 'wb') as f:
        f.write(b'D3C1'); f.write(struct.pack('<4I', 6, len(RELATIVE), len(textures), len(parts)))
        for b, parent in RELATIVE.items():
            rel = B0[b] @ np.linalg.inv(B0[parent])
            f.write(struct.pack('<2I16f', b, parent, *rel.astype(np.float32).ravel()))
        f.write(struct.pack('<I', len(HIERARCHY)))
        for b, parent in HIERARCHY:
            off = (B0[b][3, :3] - B0[parent][3, :3]) @ np.linalg.inv(B0[parent][:3, :3])
            f.write(struct.pack('<2I3f', b, parent, *off.astype(np.float32)))
        for tx, data, pal in textures:
            f.write(struct.pack('<9I', tx['format'], tx['width'], tx['height'], tx['mips'], tx['bpp'],
                                1 if tx['linear'] else 0, tx['pitch'], len(data), len(pal)))
            f.write(data); f.write(pal)
        for pi, (x, binds, t, flags, vtx, idx) in enumerate(parts):
            # Keep only the vertices this part's triangles use (DOA3 draws hair
            # from a shared buffer, so captured parts carry its whole prefix).
            n0 = len(vtx) // x['stride']
            ix = np.frombuffer(idx, np.uint16).astype(int)
            used = np.unique(ix[ix < n0])
            remap = np.full(n0, 0, np.int64); remap[used] = np.arange(len(used))
            rows = np.frombuffer(vtx[:n0 * x['stride']], np.uint8).reshape(n0, x['stride'])
            vtx = rows[used].tobytes()
            idx = remap[np.minimum(ix, n0 - 1)].astype(np.uint16).tobytes()
            if flags & 4:
                prm, mask, Rsrc, anchor = soft[pi]
                soft[pi] = (prm, mask[used], Rsrc, anchor)
            count = len(vtx) // x['stride']
            f.write(struct.pack('<5IiII', x['stride'], x['fvf'], x['primitive'], count, len(idx) // 2,
                                t, flags, len(binds)))
            for b, O in binds:
                f.write(struct.pack('<I16f', b, *O.astype(np.float32).ravel()))
            m = x.get('raw_material')
            f.write(struct.pack('<I17f', 1 if m else 0, *((m or [0.0] * 17)[:17])))
            st = x.get('material') or {}
            f.write(struct.pack('<3I', 1 if st.get('alpha_test') else 0, st.get('alpha_func', 0), st.get('alpha_ref', 0)))
            if flags & 4:
                (k, c, g, limit), mask, Rsrc, anchor = soft[pi]
                f.write(struct.pack('<4f', k, c, g, limit))
                f.write(struct.pack('<9f3f', *Rsrc.astype(np.float32).ravel(), *np.asarray(anchor, np.float32)))
                f.write(mask.tobytes())
            f.write(vtx[:count * x['stride']]); f.write(idx)
    print(out, len(parts), 'parts', len(textures), 'textures', os.path.getsize(out), 'bytes')

if __name__ == '__main__':
    main()
