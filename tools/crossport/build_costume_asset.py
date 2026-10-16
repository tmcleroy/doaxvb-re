"""Build a DOA3 costume-injection asset from a DOAXBV mesh dump (with ram.bin).

usage: build_costume_asset.py <primary present> [more presents...] <out.dxc> [--actor N]

Selects the actor's character draws (every world matrix rotation-matches a
bone of that actor's 32-bone bank), stores raw vertex/index bytes, each
world's bone and attachment offset O (W = O @ bank[bone]), the stage-0
texture, and the source-frame offsets of DOAXBV-only bones relative to a
shared parent. Output is extracted game data: keep it under private/.
"""
import json, os, struct, sys
import numpy as np
from common import draws, local_positions
from retarget_preview import bank, RELATIVE

# Topological order, parents first. 2 hip is the root carried from DOA3.
HIERARCHY = [(23, 2), (0, 23), (19, 0), (1, 19), (17, 1), (21, 1), (16, 0), (20, 0),
             (8, 16), (14, 20), (4, 8), (10, 14), (5, 4), (11, 10),
             (6, 2), (12, 2), (7, 6), (13, 12), (3, 7), (9, 13), (18, 3), (22, 9)]

# DOAXBV writes its elbow skins on the CPU each frame, so a dump holds them
# baked at the source elbow angle. Re-rig them as forearm/upper-arm blends:
# each vertex keeps its forearm-local position, the second world is the
# upper-arm helper with the source-frame relative transform (so the source
# pose is reproduced exactly), and weights ramp across the elbow along the
# forearm axis.
# bone -> (parent it blends into, sign of the bone axis, ramp start, ramp length)
ELBOWS = {4: (30, 1.0, -0.03, 0.08), 10: (31, -1.0, -0.03, 0.08),
          5: (4, 1.0, -0.02, 0.05), 11: (10, -1.0, -0.02, 0.05)}

def rerig_elbow(x, binds, vtx, BX):
    import mesh_dump_export as mde
    bone, O = binds[0]
    parent, axis_sign, start, length = ELBOWS[bone]
    lay = mde.fvf_layout(x['fvf']); stride = x['stride']; n = len(vtx) // stride
    rows = np.frombuffer(vtx[:n * stride], np.uint8).reshape(n, stride)
    pos = rows[:, :12].copy().view(np.float32).reshape(n, 3)
    local = np.c_[pos, np.ones(n)] @ O                      # forearm-bone local
    t = local[:, 0] * axis_sign                             # distance past the elbow
    w = np.clip((t - start) / length, 0.0, 1.0).astype(np.float32)
    nrm = rows[:, lay['normal']:lay['normal'] + 12] if lay['normal'] is not None else np.zeros((n, 12), np.uint8)
    rest = rows[:, (lay['normal'] + 12) if lay['normal'] is not None else 12:]
    out = np.concatenate([rows[:, :12], w.view(np.uint8).reshape(n, 4), nrm, rest], axis=1)
    fvf = (x['fvf'] & ~0x0E) | 0x06                         # XYZB1
    x = dict(x, fvf=fvf, stride=out.shape[1])
    O2 = O @ BX[bone] @ np.linalg.inv(BX[parent])
    return x, [(bone, O), (parent, O2)], out.tobytes()

# Physics: CPU-written parts (hair, chest) move every frame. From a dense run
# of frames, take each vertex's mean bone-local position as the rest shape and
# how far it travels as a 0-1 jiggle mask; the injector drives a spring per part.
SOFT = {1: (55.0, 5.0, 0.15, 0.07), 0: (140.0, 9.0, 0.17, 0.03), 23: (140.0, 9.0, 0.17, 0.03)}

SOFT_DIR = None

def soft_samples(dense_dir, actor):
    import glob, collections
    out = collections.defaultdict(list)
    for P in sorted(glob.glob(dense_dir + '/present-*')):
      ram = open(P + '/ram.bin', 'rb').read()
      for a in range(4):   # girls change actor slots between scenes; part keys tell them apart
        B = bank(ram, 0x4d3650 + a * 0x800, 32)
        for x in draws(P).values():
            if x['offscreen'] or not x['worlds'] or len(x['worlds']) > 1: continue
            W = np.array(x['worlds'][0]).reshape(4, 4)
            e = [np.abs(W[:3, :3] - B[i][:3, :3]).max() for i in range(32)]; b = int(np.argmin(e))
            if min(e) > 1e-4 or np.linalg.norm(W[3, :3] - B[b][3, :3]) > 0.6: continue
            pos = local_positions(P, x['ordinal'], x['stride']).astype(float)
            loc = (np.c_[pos, np.ones(len(pos))] @ (W @ np.linalg.inv(B[b])))[:, :3]
            out[(b, len(pos))].append((loc, B[b][:3, :3].copy()))
    return out

def collect(P, BX, parts, textures, tex_index, seen, entries):
    """Add this present's character draws not already collected. Culling hides
    different parts in different frames, so assets are built from several."""
    for x in draws(P).values():
        s = x.get('source')
        if x['offscreen'] or not x['worlds'] or x.get('program'):
            continue
        if s and s['archive'] == 'datahdd.afs' and s['entry'] < 67:
            continue
        if entries is not None and (not s or (s['archive'], s['entry']) not in entries):
            continue
        if x.get('stencil') and not x.get('depth_write', True) and not x.get('texture'):
            continue
        vtx = P + '/d%05d.vtx' % x['ordinal']; idx = P + '/d%05d.idx' % x['ordinal']
        if not (os.path.exists(vtx) and os.path.exists(idx)):
            continue
        idx_bytes = open(idx, 'rb').read()
        key = (s['archive'], s['entry'], s['offset'], hash(idx_bytes)) if s else None
        if key is not None and key in seen:
            continue
        pos = local_positions(P, x['ordinal'], x['stride'])
        if np.ptp(pos[np.isfinite(pos).all(1)], axis=0).max() > 2.0:
            continue
        binds = []
        for w in x['worlds']:
            W = np.array(w, float).reshape(4, 4)
            errs = [np.abs(W[:3, :3] - BX[i][:3, :3]).max() if abs(np.linalg.det(BX[i][:3, :3])) > .5 else 9
                    for i in range(32)]
            b = int(np.argmin(errs))
            if errs[b] > 0.02 or np.linalg.norm(W[3, :3] - BX[b][3, :3]) > 0.6:
                binds = None; break
            binds.append((b, W @ np.linalg.inv(BX[b])))
        if binds is None:
            continue
        if key is not None:
            seen.add(key)
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
        vtx_bytes = open(vtx, 'rb').read()
        # A part hung on a physics matrix (ponytail, ribbon) is bound to the head
        # or chest with a rotated offset: keep its pivot and the source head
        # orientation so it can swing back toward gravity.
        if len(binds) == 1 and binds[0][0] in (0, 1):
            Rrel = binds[0][1][:3, :3]
            angle = np.degrees(np.arccos(np.clip((np.trace(Rrel) - 1) / 2, -1, 1)))
            if angle > 10.0:
                x = dict(x, _hang=(BX[binds[0][0]][:3, :3].copy(), binds[0][1][3, :3].copy()))
        if not s and len(binds) == 1 and binds[0][0] in ELBOWS:
            x, binds, vtx_bytes = rerig_elbow(x, binds, vtx_bytes, BX)
        parts.append((x, binds, t, flags, vtx_bytes, idx_bytes))

def main():
    global SOFT_DIR
    argv = sys.argv[1:]
    if '--soft' in argv:
        i = argv.index('--soft'); SOFT_DIR = argv[i + 1]; del argv[i:i + 2]
    actor = 0
    if '--actor' in argv:
        i = argv.index('--actor'); actor = int(argv[i + 1]); del argv[i:i + 2]
    *presents, out = argv
    build(presents, out, actor)

def snap_elbows(parts, tol=0.002):
    """Re-rigged elbow/wrist vertices that coincide with a rigid neighbour's
    vertex follow that neighbour's bone exactly, so the joint skin meets the
    segments without a seam. Compared in each bone's local frame, so parts
    collected from different presents line up."""
    rigid = {}
    for x, binds, t, flags, vtx, idx in parts:
        if len(binds) != 1: continue
        n = len(vtx) // x['stride']
        pos = np.frombuffer(vtx[:n * x['stride']], np.uint8).reshape(n, x['stride'])[:, :12].copy().view(np.float32).reshape(n, 3)
        rigid.setdefault(binds[0][0], []).append((np.c_[pos, np.ones(n)] @ binds[0][1])[:, :3])
    rigid = {b: np.concatenate(v) for b, v in rigid.items()}
    for i, (x, binds, t, flags, vtx, idx) in enumerate(parts):
        if len(binds) != 2 or binds[0][0] not in ELBOWS or binds[1][0] != ELBOWS[binds[0][0]][0]: continue
        n = len(vtx) // x['stride']
        rows = np.frombuffer(vtx[:n * x['stride']], np.uint8).reshape(n, x['stride']).copy()
        pos = rows[:, :12].copy().view(np.float32).reshape(n, 3)
        w = rows[:, 12:16].copy().view(np.float32).reshape(n)
        snapped = 0
        for k, (b, O) in enumerate(binds):          # world 0 weight 1 = forearm/hand, 0 = its parent
            if b not in rigid: continue
            local = (np.c_[pos, np.ones(n)] @ O)[:, :3]
            d = np.sqrt(((local[:, None, :] - rigid[b][None]) ** 2).sum(-1)).min(1)
            hit = d < tol
            w[hit] = 1.0 if k == 0 else 0.0; snapped += int(hit.sum())
        rows[:, 12:16] = w.astype(np.float32).view(np.uint8).reshape(n, 4)
        parts[i] = (x, binds, t, flags, rows.tobytes(), idx)
        print('elbow snap: bone', binds[0][0], 'verts', n, 'snapped', snapped)

def build(presents, out, actor):
    P = presents[0]
    BX = bank(open(P + '/ram.bin', 'rb').read(), 0x4d3650 + actor * 0x800, 32)
    textures, tex_index, parts, seen = [], {}, [], set()
    collect(P, BX, parts, textures, tex_index, seen, None)
    entries = {(x.get('source') or {}).get('archive'): None for x, *_ in parts}
    entries = {(x['source']['archive'], x['source']['entry']) for x, *_ in parts if x.get('source')}
    print('primary', len(parts), 'parts from entries', sorted(entries))
    from collections import Counter
    dominant = Counter((x['source']['archive'], x['source']['entry']) for x, *_ in parts
                       if x.get('source')).most_common(1)[0][0]
    for extra in presents[1:]:
        ram = open(extra + '/ram.bin', 'rb').read()
        for a in range(4):
            B = bank(ram, 0x4d3650 + a * 0x800, 32)
            # Only an actor wearing the same body: count its draws from the dominant bundle.
            probe = []
            collect(extra, B, probe, [], {}, set(), {dominant})
            if len(probe) < 10:
                continue
            before = len(parts)
            collect(extra, B, parts, textures, tex_index, seen, entries)
            if len(parts) > before: print(' ', extra.split('/')[-1], 'actor', a, '+', len(parts) - before)
    snap_elbows(parts)
    soft = {}
    if True:
      if SOFT_DIR:
        samples = soft_samples(SOFT_DIR, actor)
        for i, (x, binds, t, flags, vtx, idx) in enumerate(parts):
            if len(binds) != 1 or binds[0][0] not in SOFT: continue   # file-backed hair is rewritten in place too
            b, O = binds[0]; n = len(vtx) // x['stride']
            got = samples.get((b, n))
            if not got or len(got) < 4: continue
            arr = np.stack([g[0] for g in got])
            # Rest shape: the captured frame nearest the average (an average of
            # swinging hair is a blur).
            mean = arr[int(np.argmin(np.linalg.norm(arr - arr.mean(0), axis=2).mean(1)))]
            # Source head orientation for gravity-aware hanging: mean, re-orthonormalised.
            u, _, vt = np.linalg.svd(np.mean([g[1] for g in got], axis=0)); Rsrc = u @ vt
            amp = np.linalg.norm(arr - mean, axis=2).max(0)
            mask = np.clip((amp - 0.005) / 0.04, 0, 1).astype(np.float32)
            if mask.max() <= 0: continue
            raw = (np.c_[mean, np.ones(n)] @ np.linalg.inv(O))[:, :3].astype(np.float32)
            rows = np.frombuffer(vtx[:n * x['stride']], np.uint8).reshape(n, x['stride']).copy()
            rows[:, :12] = raw.view(np.uint8).reshape(n, 12)
            root = mean[mask < 0.05]
            anchor = root.mean(0) if len(root) else np.zeros(3)
            parts[i] = (x, binds, t, flags | 4, rows.tobytes(), idx)
            soft[i] = (SOFT[b], mask, Rsrc, anchor)
        print('soft parts:', len(soft))
    for i, (x, binds, t, flags, vtx, idx) in enumerate(parts):
        if i in soft or not x.get('_hang'): continue
        n = len(vtx) // x['stride']
        Rsrc, anchor = x['_hang']
        parts[i] = (x, binds, t, flags | 4, vtx, idx)
        soft[i] = (SOFT[1], np.ones(n, np.float32), Rsrc, anchor)
        print('pendulum part on bone', binds[0][0], 'verts', n)
    with open(out, 'wb') as f:
        f.write(b'DXC1'); f.write(struct.pack('<4I', 6, len(RELATIVE) + 1, len(textures), len(parts)))
        for b, parent in RELATIVE.items():
            rel = BX[b] @ np.linalg.inv(BX[parent])
            f.write(struct.pack('<2I16f', b, parent, *rel.astype(np.float32).ravel()))
        # The neck (19) again relative to the head: DOA3 has no neck bone, so the
        # injector blends the chest-carried and head-carried necks halfway.
        rel = BX[19] @ np.linalg.inv(BX[1])
        f.write(struct.pack('<2I16f', 19, 1, *rel.astype(np.float32).ravel()))
        # DOAXBV body hierarchy (recovered from motion) with each bone's origin in
        # its parent's frame, so a retarget keeps the source proportions.
        f.write(struct.pack('<I', len(HIERARCHY)))
        for b, parent in HIERARCHY:
            off = (BX[b][3, :3] - BX[parent][3, :3]) @ np.linalg.inv(BX[parent][:3, :3])
            f.write(struct.pack('<2I3f', b, parent, *off.astype(np.float32)))
        for tx, data, pal in textures:
            f.write(struct.pack('<9I', tx['format'], tx['width'], tx['height'], tx['mips'], tx['bpp'],
                                1 if tx['linear'] else 0, tx['pitch'], len(data), len(pal)))
            f.write(data); f.write(pal)
        for pi, (x, binds, t, flags, vtx, idx) in enumerate(parts):
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
