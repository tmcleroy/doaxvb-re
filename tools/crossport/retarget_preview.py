"""Re-attach a DOAXBV girl's dumped parts to a DOA3 fighter's bones and render
both from the DOA3 camera. Bone index mapping: identity for 0-14, plus extras."""
import os
import sys, json, numpy as np
from PIL import Image, ImageDraw
from common import *
DX_TO_D3={i:i for i in range(15)}
DX_TO_D3.update({16:0,20:0,19:1,23:2,17:15,21:16,18:3,22:9})
def bank(ram, base, n): return np.array([np.frombuffer(ram[base+i*64:base+i*64+64],np.float32).reshape(4,4) for i in range(n)],dtype=float)
def bone_of(W, B, n):
    errs=[np.abs(W[:3,:3]-B[i,:3,:3]).max() for i in range(n)]
    order=np.argsort(errs)
    for i in order:
        if errs[i]>0.02: break
        if abs(np.linalg.det(B[i,:3,:3]))>0.5: return int(i)
    return None
def doax_parts(P, actor=0):
    ram=open(P+'/ram.bin','rb').read(); B=bank(ram,0x4d3650+actor*0x800,32); dr=draws(P); parts=[]
    for x in dr.values():
        s=x.get('source')
        if x['offscreen'] or not x['worlds'] or x.get('program'): continue
        if s and s['archive']=='datahdd.afs' and s['entry']<67: continue
        if x.get('stencil') and not x.get('depth_write',True) and not x.get('texture'): continue
        if not __import__('os').path.exists(P+'/d%05d.idx'%x['ordinal']): continue
        pos=local_positions(P,x['ordinal'],x['stride'])
        if np.ptp(pos[np.isfinite(pos).all(1)],axis=0).max()>2.0: continue  # scenery sharing the root rotation
        idx=np.frombuffer(open(P+'/d%05d.idx'%x['ordinal'],'rb').read(),np.uint16).astype(int)
        import mesh_dump_export as m
        tris=m.triangles(x['primitive'],idx); tris=tris[(tris<len(pos)).all(1)]
        Ws=[np.array(w).reshape(4,4) for w in x['worlds']]
        binds=[]
        for W in Ws:
            b=bone_of(W,B,32)
            if b is None: binds=None; break
            binds.append((b, W@np.linalg.inv(B[b])))   # W = O @ B_b
        if binds is None: continue
        nw=x['blend_weights']; stride=x['stride']
        raw=open(P+'/d%05d.vtx'%x['ordinal'],'rb').read(); n=len(raw)//stride
        rows=np.frombuffer(raw[:n*stride],np.uint8).reshape(n,stride)
        wts=rows[:,12:12+4*nw].copy().view(np.float32).reshape(n,nw).astype(float) if nw else np.zeros((n,0))
        parts.append(dict(pos=pos.astype(float),tris=tris,binds=binds,weights=wts,ordinal=x['ordinal']))
    return parts
# DOAXBV bone-local -> DOA3 bone-local rotation (row vectors: v3 = vX @ C).
# Torso/head share conventions; limbs from knee/elbow hinge axes, hands/feet
# from principal axes of the parts.
_THIGH=[[0,-1,0],[0,0,-1],[1,0,0]]; _SHIN=[[0,-1,0],[0,0,1],[-1,0,0]]
_HAND=[[1,0,0],[0,1,0],[0,0,1]]; _FOOT=[[1,0,0],[0,1,0],[0,0,1]]
CORR={6:_THIGH,12:[[0,1,0],[0,0,-1],[-1,0,0]],7:_SHIN,13:[[0,1,0],[0,0,1],[1,0,0]],3:_FOOT,9:_FOOT,5:_HAND,11:_HAND,
      8:[[0,0,1],[0,1,0],[-1,0,0]],14:[[0,0,-1],[0,1,0],[1,0,0]],
      4:[[0,0,-1],[0,1,0],[1,0,0]],10:[[0,0,1],[0,1,0],[-1,0,0]]}
def corr4(b):
    m=np.eye(4)
    if b in CORR: m[:3,:3]=np.array(CORR[b],float)
    return m
RELATIVE={15:2,23:2,16:0,20:0,19:0,17:1,21:1,18:3,22:9}
HIER=[(23,2),(0,23),(19,0),(1,19),(17,1),(21,1),(16,0),(20,0),(8,16),(14,20),(4,8),(10,14),(5,4),(11,10),
      (6,2),(12,2),(7,6),(13,12),(3,7),(9,13),(18,3),(22,9)]
def doax_bank_on(B3, BX):
    """DOAXBV-layout 32-bone bank posed by a DOA3 skeleton: shared bones 0-14
    copied, DOAXBV-only bones carried by their source-frame offset from a
    shared parent, then the blend helpers derived as DOAXBV derives them."""
    from derive import derive
    B=np.zeros((32,4,4))
    for b in range(15): B[b]=corr4(b)@B3[b]
    for b,parent in HIER:
        if b in RELATIVE: B[b]=BX[b]@np.linalg.inv(BX[parent])@B[parent]; continue
        off=(BX[b][3,:3]-BX[parent][3,:3])@np.linalg.inv(BX[parent][:3,:3])
        B[b][3,:3]=off@B[parent][:3,:3]+B[parent][3,:3]
    B[15]=BX[15]@np.linalg.inv(BX[2])@B[2]
    return derive(B)
def pose(part, bank32):
    homo=np.c_[part['pos'],np.ones(len(part['pos']))]
    mats=[O@bank32[b] for b,O in part['binds']]
    w=part['weights']
    if w.shape[1]==0 or len(mats)<2: return (homo@mats[0])[:,:3]
    rem=1-w.sum(1); out=(homo@mats[0])*w[:,:1]
    for k in range(1,len(mats)):
        ww=rem if k==len(mats)-1 else w[:,k]; out+= (homo@mats[k])*ww[:,None]
    return out[:,:3]
def render(tri_sets, view, proj, out, W=900,H=900):
    img=Image.new('RGB',(W,H),(25,25,30)); d=ImageDraw.Draw(img); faces=[]
    for verts,tris,color in tri_sets:
        h=np.c_[verts,np.ones(len(verts))]@view@proj
        ok=h[:,3]>0.01; sx=(h[:,0]/h[:,3]*0.5+0.5)*W; sy=(0.5-h[:,1]/h[:,3]*0.5)*H
        a,b,c=verts[tris[:,0]],verts[tris[:,1]],verts[tris[:,2]]
        nrm=np.cross(b-a,c-a); nrm/=np.linalg.norm(nrm,axis=1,keepdims=True)+1e-12
        lit=np.abs(nrm@np.array([0.3,0.6,-0.7]))
        for t,l in zip(tris,lit):
            if not ok[t].all(): continue
            faces.append((h[t,3].mean(),[(sx[k],sy[k]) for k in t],tuple(int(cc*(0.35+0.65*l)) for cc in color)))
    for z,p,col in sorted(faces,key=lambda f:-f[0]): d.polygon(p,fill=col)
    img.save(out)
if __name__=='__main__':
    sys.path.insert(0,os.path.expanduser('~') + '/code/recomp/doaxvb-re/private/wt-meshdump/tools')
    PX=sys.argv[1]; P3=sys.argv[2]; base3=int(sys.argv[3],16); out=sys.argv[4]
    parts=doax_parts(PX)
    ram3=open(P3+'/ram.bin','rb').read(); B3=bank(ram3,base3,24)
    BX=bank(open(PX+'/ram.bin','rb').read(),0x4d3650,32)
    B32=doax_bank_on(B3,BX)
    first=json.loads(open(P3+'/draws.jsonl').readline())
    V=np.array(first['view']).reshape(4,4); Pj=np.array(first['projection']).reshape(4,4)
    sets=[]
    only=[int(v) for v in __import__('os').environ.get('ONLY','').split(',') if v]
    for p in parts:
        if only and p['ordinal'] not in only: continue
        v=pose(p,B32); col=(240,170,150) if len(p['binds'])==1 else (120,230,120)
        sets.append((v,p['tris'],col))
    # shift the retargeted girl sideways so she stands beside the fighter
    if len(sys.argv)>5:
        for x in json.load(open(P3+'/export.json')):
            pass
        dr3=draws(P3); ent=[int(e) for e in sys.argv[5].split(',')]
        for x in dr3.values():
            src=x.get('source')
            if not src or src['entry'] not in ent or not x['worlds']: continue
            pos=local_positions(P3,x['ordinal'],x['stride']).astype(float)
            import mesh_dump_export as m
            idx=np.frombuffer(open(P3+'/d%05d.idx'%x['ordinal'],'rb').read(),np.uint16).astype(int)
            tris=m.triangles(x['primitive'],idx); tris=tris[(tris<len(pos)).all(1)]
            W=np.array(x['worlds'][0]).reshape(4,4)
            sets.append(((np.c_[pos,np.ones(len(pos))]@W)[:,:3],tris,(120,170,240)))
    render(sets,V,Pj,out)
    print(out,len(parts),'parts')
