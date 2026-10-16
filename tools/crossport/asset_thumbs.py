"""Write <asset>-hair.png: the two head/hair textures the asset uses most, to
tell characters apart (actor slots change between runs). DXC1 or D3C1."""
import struct, sys
import numpy as np
from PIL import Image
import mesh_dump_export as m

def thumbs(path, out=None):
    f = open(path, 'rb'); f.read(4)
    ver, nrel, ntex, npart = struct.unpack('<4I', f.read(16))
    f.read(nrel * 72); nh = struct.unpack('<I', f.read(4))[0]; f.read(nh * 20)
    texs = []
    for _ in range(ntex):
        t = struct.unpack('<9I', f.read(36)); data = f.read(t[7]); pal = f.read(t[8])
        texs.append((dict(format=t[0], width=t[1], height=t[2]), data, pal or None))
    heads = {}
    for _ in range(npart):
        h = struct.unpack('<5IiII', f.read(32)); bones = []
        for _ in range(h[7]): bones.append(struct.unpack('<I', f.read(4))[0]); f.read(64)
        if ver >= 3: f.read(72)
        if ver >= 5: f.read(12)
        if ver >= 4 and h[6] & 4: f.read(16 + (48 if ver >= 6 else 0) + 4 * h[3])
        f.read(h[0] * h[3] + h[4] * 2)
        if bones[0] == 1 and h[5] >= 0: heads[h[5]] = heads.get(h[5], 0) + h[3]
    sheet = Image.new('RGB', (256, 128), (30, 30, 30))
    for j, t in enumerate(sorted(heads, key=lambda t: -heads[t])[:2]):
        desc, data, pal = texs[t]
        try: sheet.paste(Image.fromarray(m.decode_texture(desc, data, pal)).convert('RGB').resize((128, 128)), (j * 128, 0))
        except Exception: pass
    out = out or path.rsplit('.', 1)[0] + '-hair.png'
    sheet.save(out); return out

if __name__ == '__main__':
    for p in sys.argv[1:]: print(thumbs(p))
