import os
import struct, json, collections
D3AFS=os.path.expanduser('~') + '/code/recomp/doa3-re/private/wt-meshdump/private/play-disc/loadfile.afs'
DXHDD=os.path.expanduser('~') + '/code/recomp/doaxvb-re/private/setup-8569d80610c6/imported-disc/disc/datahdd.afs'
DXDVD=os.path.expanduser('~') + '/code/recomp/doaxvb-re/private/setup-8569d80610c6/imported-disc/disc/datadvd.afs'
D3RUN=os.path.expanduser('~') + '/code/recomp/doa3-re/private/wt-meshdump/private/meshdump-run2'
DXRUN=os.path.expanduser('~') + '/code/recomp/doaxvb-re/private/claude-work/meshdump-run2'
def entry(path,i):
    f=open(path,'rb'); f.seek(8+8*i); o,s=struct.unpack('<II',f.read(8)); f.seek(o); return f.read(s)
def objs(d):
    n=struct.unpack('<I',d[0x18:0x1c])[0]
    return list(struct.unpack('<%dI'%n,d[0x28:0x28+4*n]))
def draws(present):
    out={}
    for l in open(present+'/draws.jsonl'):
        x=json.loads(l); out[x['ordinal']]=x
    import os
    ex=json.load(open(present+'/export.json')) if os.path.exists(present+'/export.json') else []
    for e in ex: out[e['ordinal']]['source']=e['source']
    return out
import numpy as np
def local_positions(present, ordinal, stride):
    raw=open(present+'/d%05d.vtx'%ordinal,'rb').read(); n=len(raw)//stride
    rows=np.frombuffer(raw[:n*stride],np.uint8).reshape(n,stride)
    return rows[:,:12].copy().view(np.float32).reshape(n,3)
def world_centroid(x, pos):
    W=np.array(x['worlds'][0]).reshape(4,4)
    return (np.c_[pos,np.ones(len(pos))]@W)[:,:3].mean(0)
def obj_table(path, ent, doa3):
    d=entry(path,ent); offs=objs(d); recs=[]
    for k,o in enumerate(offs):
        if doa3:
            f=struct.unpack('<8I',d[o:o+32]); vc=f[4]&0xffff if f[4]>0xffff else f[4]; vb=f[5]; stride=32
        else:
            vc=struct.unpack('<I',d[o+12:o+16])[0]; vb=struct.unpack('<I',d[o+0x24:o+0x28])[0]; stride=64
        recs.append((k,vb,vc,o))
    return d,recs
def match_obj(recs, off, stride=32):
    for k,vb,vc,o in recs:
        if vb<=off<vb+max(vc,1)*stride: return k
    return None
