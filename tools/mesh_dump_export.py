"""Rebuild a RECOMP_MESH_DUMP present as world-space meshes.

Reads <present>/draws.jsonl with its d*.vtx, d*.idx and tex-*.bin files and
writes scene.obj, scene.mtl and textures as PNG beside them. Vertices are
skinned on the CPU with the dumped WORLD0..WORLD3 matrices exactly as the
runtime's fixed-function blend does, so the OBJ is the posed scene in world
space. With --afs, each draw's vertex bytes are searched for in the given
archives and the matching entry is reported, which names the file a mesh came
from. Output stays under private/: it is extracted game data.

Needs numpy, and Pillow for textures.
"""

from __future__ import annotations

import argparse
import json
import mmap
import struct
import sys
from pathlib import Path

import numpy as np

FVF_POSITION_MASK = 0x0E
FVF_XYZ = 0x02
FVF_XYZRHW = 0x04
FVF_NORMAL = 0x10
FVF_DIFFUSE = 0x40
FVF_SPECULAR = 0x80

PT_LIST, PT_STRIP, PT_FAN = 5, 6, 7


def fvf_layout(fvf: int) -> dict | None:
    """Mirror of recomp_d3d_fvf_layout."""
    position = fvf & FVF_POSITION_MASK
    if position == 0:
        return None
    pretransformed = position == FVF_XYZRHW
    weights = 0 if pretransformed or position == FVF_XYZ else (position - 4) // 2
    offset = 16 if pretransformed else 12 + weights * 4
    layout = {"pretransformed": pretransformed, "weights": weights,
              "normal": None, "diffuse": None, "texcoord": None}
    if fvf & FVF_NORMAL:
        layout["normal"] = offset
        offset += 12
    if fvf & FVF_DIFFUSE:
        layout["diffuse"] = offset
        offset += 4
    if fvf & FVF_SPECULAR:
        offset += 4
    texcount = (fvf >> 8) & 0xF
    if texcount:
        layout["texcoord"] = offset
    layout["stride"] = offset + texcount * 8
    return layout


def triangles(primitive: int, indices: np.ndarray) -> np.ndarray:
    n = len(indices)
    if primitive == PT_LIST:
        tris = indices[: n - n % 3].reshape(-1, 3)
    elif primitive == PT_STRIP and n >= 3:
        k = np.arange(n - 2)
        a, b, c = indices[k], indices[k + 1], indices[k + 2]
        odd = (k & 1) == 1
        tris = np.stack([a, np.where(odd, c, b), np.where(odd, b, c)], axis=1)
    elif primitive == PT_FAN and n >= 3:
        k = np.arange(1, n - 1)
        tris = np.stack([np.full(len(k), indices[0]), indices[k], indices[k + 1]], axis=1)
    else:
        return np.zeros((0, 3), dtype=np.int64)
    keep = (tris[:, 0] != tris[:, 1]) & (tris[:, 1] != tris[:, 2]) & (tris[:, 0] != tris[:, 2])
    return tris[keep]


def skin(positions: np.ndarray, weights: np.ndarray, worlds: list) -> np.ndarray:
    """Fixed-function blend as the runtime composes it: weights[0] selects
    WORLD0, weights[k] WORLDk, and the last world takes the remainder."""
    homo = np.concatenate([positions, np.ones((len(positions), 1), np.float32)], axis=1)
    mats = [np.array(w, dtype=np.float64).reshape(4, 4) for w in worlds]
    count = weights.shape[1]
    if count == 0 or len(mats) < count + 1:
        return (homo @ mats[0])[:, :3]
    remainder = 1.0 - weights.sum(axis=1)
    out = (homo @ mats[0]) * weights[:, :1]
    for k in range(1, count + 1):
        w = remainder if k == count else weights[:, k]
        out += (homo @ mats[k]) * w[:, None]
    return out[:, :3]


# --- textures -------------------------------------------------------------

def unswizzle(data: np.ndarray, width: int, height: int) -> np.ndarray:
    """Xbox swizzled (Morton) texel order to linear, for power-of-two sizes."""
    xs = np.arange(width)
    ys = np.arange(height)
    sx = np.zeros(width, np.int64)
    sy = np.zeros(height, np.int64)
    bit = 0
    mx, my = width - 1, height - 1
    shift = 0
    while mx or my:
        if mx:
            sx |= ((xs >> bit) & 1) << shift
            shift += 1
        if my:
            sy |= ((ys >> bit) & 1) << shift
            shift += 1
        mx >>= 1
        my >>= 1
        bit += 1
    index = sy[:, None] | sx[None, :]
    return data[index.ravel()].reshape(height, width, *data.shape[1:])


def decode_dxt(data: bytes, width: int, height: int, kind: int) -> np.ndarray:
    block = 8 if kind == 1 else 16
    bw, bh = max(1, (width + 3) // 4), max(1, (height + 3) // 4)
    raw = np.frombuffer(data[: bw * bh * block], np.uint8).reshape(bh * bw, block)
    color = raw[:, block - 8:]
    c0 = color[:, 0].astype(np.uint16) | (color[:, 1].astype(np.uint16) << 8)
    c1 = color[:, 2].astype(np.uint16) | (color[:, 3].astype(np.uint16) << 8)

    def rgb(c):
        return np.stack([((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63,
                         (c & 31) * 255 // 31], axis=1).astype(np.int32)
    p0, p1 = rgb(c0), rgb(c1)
    four = (c0 > c1) | (kind != 1)
    p2 = np.where(four[:, None], (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(four[:, None], (p0 + 2 * p1) // 3, 0)
    palette = np.stack([p0, p1, p2, p3], axis=1)  # blocks x 4 x 3
    bits = color[:, 4:8].astype(np.uint32)
    codes = bits[:, 0] | (bits[:, 1] << 8) | (bits[:, 2] << 16) | (bits[:, 3] << 24)
    sel = (codes[:, None] >> (2 * np.arange(16))) & 3
    rgbpx = np.take_along_axis(palette, sel[:, :, None].astype(np.int64).repeat(3, 2), 1)
    alpha = np.full((len(raw), 16), 255, np.int32)
    if kind == 1:
        alpha = np.where((~four)[:, None] & (sel == 3), 0, 255)
    elif kind == 3:
        a = raw[:, :8].astype(np.uint32)
        nib = np.stack([(a >> 0) & 15, (a >> 4) & 15], axis=2).reshape(-1, 16)
        alpha = (nib * 17).astype(np.int32)
    else:
        a0 = raw[:, 0].astype(np.int32)
        a1 = raw[:, 1].astype(np.int32)
        abits = np.zeros(len(raw), np.uint64)
        for i in range(6):
            abits |= raw[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
        asel = ((abits[:, None] >> (np.uint64(3) * np.arange(16, dtype=np.uint64))) & np.uint64(7)).astype(np.int32)
        table = np.zeros((len(raw), 8), np.int32)
        table[:, 0], table[:, 1] = a0, a1
        big = a0 > a1
        for i in range(2, 8):
            table[:, i] = np.where(big, ((8 - i) * a0 + (i - 1) * a1) // 7, 0)
        for i in range(2, 6):
            table[:, i] = np.where(big, table[:, i], ((6 - i) * a0 + (i - 1) * a1) // 5)
        table[:, 6] = np.where(big, table[:, 6], 0)
        table[:, 7] = np.where(big, table[:, 7], 255)
        alpha = np.take_along_axis(table, asel, 1)
    px = np.concatenate([rgbpx, alpha[:, :, None]], axis=2).reshape(bh, bw, 4, 4, 4)
    image = px.transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return image[:height, :width].astype(np.uint8)


def decode_texture(desc: dict, data: bytes, palette: bytes | None) -> np.ndarray | None:
    fmt, w, h = desc["format"], desc["width"], desc["height"]
    if fmt in (0x0C, 0x0E, 0x0F):
        return decode_dxt(data, w, h, {0x0C: 1, 0x0E: 3, 0x0F: 5}[fmt])
    count = w * h
    if fmt in (0x06, 0x12):  # A8R8G8B8 swizzled / linear
        bgra = np.frombuffer(data[: count * 4], np.uint8).reshape(count, 4)
        image = bgra if fmt == 0x12 else unswizzle(bgra, w, h).reshape(count, 4)
        return image.reshape(h, w, 4)[:, :, [2, 1, 0, 3]]
    if fmt == 0x0B and palette is not None:  # P8 swizzled
        idx = np.frombuffer(data[:count], np.uint8)
        lut = np.frombuffer(palette, np.uint8).reshape(-1, 4)[:, [2, 1, 0, 3]]
        return unswizzle(lut[idx], w, h)
    if fmt == 0x19:  # A8 swizzled
        a = unswizzle(np.frombuffer(data[:count], np.uint8), w, h)
        out = np.full((h, w, 4), 255, np.uint8)
        out[:, :, 3] = a
        return out
    return None


# --- AFS provenance --------------------------------------------------------

class Archive:
    def __init__(self, path: Path):
        self.path = path
        self.file = open(path, "rb")
        self.data = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
        count = struct.unpack_from("<I", self.data, 4)[0]
        self.entries = [struct.unpack_from("<II", self.data, 8 + 8 * i) for i in range(count)]

    def locate(self, needle: bytes):
        at = self.data.find(needle)
        if at < 0:
            return None
        for i, (offset, size) in enumerate(self.entries):
            if offset <= at < offset + size:
                return i, at - offset
        return None


# --- export ---------------------------------------------------------------

def export(folder: Path, archives: list[Archive], include_screen: bool) -> dict:
    draws = [json.loads(line) for line in (folder / "draws.jsonl").read_text().splitlines() if line]
    obj = [f"mtllib scene.mtl\n"]
    mtl = []
    written_textures: dict[int, str | None] = {}
    provenance: dict[int, object] = {}
    report = []
    base = 1
    for d in draws:
        layout = fvf_layout(d["fvf"])
        ordinal = d["ordinal"]
        vtx_path = folder / f"d{ordinal:05d}.vtx"
        idx_path = folder / f"d{ordinal:05d}.idx"
        if layout is None or not vtx_path.exists() or not idx_path.exists():
            continue
        if d["offscreen"] or d["program"] or (layout["pretransformed"] and not include_screen):
            continue
        if d.get("color_mask", 15) == 0:
            continue  # depth-only passes
        if d.get("stencil") and not d.get("depth_write", True) and not d.get("texture"):
            continue  # two-pass stencil shadow volumes: untextured, no depth write
        stride = d["stride"]
        raw = vtx_path.read_bytes()
        count = len(raw) // stride
        if count == 0:
            continue
        rows = np.frombuffer(raw[: count * stride], np.uint8).reshape(count, stride)
        positions = rows[:, :12].copy().view(np.float32).reshape(count, 3)
        weights = rows[:, 12:12 + 4 * layout["weights"]].copy().view(np.float32).reshape(count, layout["weights"])
        indices = np.frombuffer(idx_path.read_bytes(), np.uint16).astype(np.int64)
        tris = triangles(d["primitive"], indices)
        tris = tris[(tris < count).all(axis=1)]
        if len(tris) == 0:
            continue
        used = np.unique(tris)
        remap = np.full(count, -1, np.int64)
        remap[used] = np.arange(len(used))
        world = skin(positions[used].astype(np.float64), weights[used].astype(np.float64), d["worlds"]) \
            if d["worlds"] else positions[used]

        texture = d.get("texture")
        material = None
        if texture is not None:
            key = texture["data"]
            if key not in written_textures:
                written_textures[key] = None
                tex_path = folder / f"tex-{key:08x}.bin"
                pal_path = folder / f"pal-{key:08x}.bin"
                if tex_path.exists():
                    try:
                        image = decode_texture(texture, tex_path.read_bytes(),
                                               pal_path.read_bytes() if pal_path.exists() else None)
                    except Exception:  # noqa: BLE001 - one bad texture must not stop the export
                        image = None
                    if image is not None:
                        from PIL import Image
                        name = f"tex-{key:08x}.png"
                        Image.fromarray(image).save(folder / name)
                        written_textures[key] = name
            if written_textures[key]:
                material = f"m{key:08x}"
        source = None
        if archives:
            vdata = d["vertex_data"]
            if vdata not in provenance:
                needle = raw[: min(len(raw), 64)]
                provenance[vdata] = None
                for archive in archives:
                    hit = archive.locate(needle)
                    if hit is not None:
                        provenance[vdata] = {"archive": archive.path.name, "entry": hit[0], "offset": hit[1]}
                        break
            source = provenance[vdata]
        report.append({"ordinal": ordinal, "vertices": int(len(used)), "triangles": int(len(tris)),
                       "fvf": d["fvf"], "blend_weights": d["blend_weights"],
                       "texture": written_textures.get(texture["data"]) if texture else None,
                       "source": source})

        obj.append(f"o d{ordinal:05d}" + (f"_{source['archive']}_{source['entry']}" if source else "") + "\n")
        for p in world:
            obj.append(f"v {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}\n")
        has_uv = layout["texcoord"] is not None
        if has_uv:
            t = layout["texcoord"]
            uv = rows[used, t:t + 8].copy().view(np.float32).reshape(len(used), 2)
            for u, v in uv:
                obj.append(f"vt {u:.6f} {1.0 - v:.6f}\n")
        if material:
            obj.append(f"usemtl {material}\n")
        for a, b, c in remap[tris] + base:
            obj.append(f"f {a}/{a} {b}/{b} {c}/{c}\n" if has_uv else f"f {a} {b} {c}\n")
        base += len(used)
    for key, name in written_textures.items():
        if name:
            mtl.append(f"newmtl m{key:08x}\nKd 1 1 1\nmap_Kd {name}\n\n")
    (folder / "scene.obj").write_text("".join(obj))
    (folder / "scene.mtl").write_text("".join(mtl))
    (folder / "export.json").write_text(json.dumps(report, indent=1))
    return {"draws": len(draws), "exported": len(report),
            "textures": sum(1 for v in written_textures.values() if v),
            "sourced": sum(1 for r in report if r["source"])}


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("presents", nargs="+", type=Path, help="present-<n> folders")
    parser.add_argument("--afs", action="append", type=Path, default=[],
                        help="AFS archive to search for each draw's vertex bytes")
    parser.add_argument("--screen", action="store_true", help="also export pretransformed draws")
    args = parser.parse_args(argv)
    archives = [Archive(p) for p in args.afs]
    for folder in args.presents:
        print(folder, export(folder, archives, args.screen))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
