"""Render a character's bones (bank translations) numbered, over its parts, from the game camera."""
import os
import sys, struct, json, numpy as np
from PIL import Image, ImageDraw
from common import *
def bank_mats(ram, base, n):
    return [np.frombuffer(ram[base+i*64:base+i*64+64],np.float32).reshape(4,4).astype(float) for i in range(n)]
def best_bone(W, mats):
    W=np.array(W).reshape(4,4); errs=[np.abs(W[:3,:3]-M[:3,:3]).max()+0.01*np.linalg.norm(W[3,:3]-M[3,:3]) for M in mats]
    i=int(np.argmin(errs)); return i, errs[i]
def render(P, base, n, filt, out, stride_default=32):
    ram=open(P+'/ram.bin','rb').read(); mats=bank_mats(ram,base,n); dr=draws(P)
    first=next(iter(dr.values())); V=np.array(first['view']).reshape(4,4); Pj=np.array(first['projection']).reshape(4,4)
    W_,H_=900,900
    def proj(p):
        h=np.c_[p,np.ones(len(p))]@V@Pj; return np.c_[(h[:,0]/h[:,3]*0.5+0.5)*W_,(0.5-h[:,1]/h[:,3]*0.5)*H_], h[:,3]
    img=Image.new('RGB',(W_,H_),(25,25,30)); d=ImageDraw.Draw(img)
    pts_by_bone={}
    for x in dr.values():
        s=x.get('source')
        if not s or not filt(s) or not x['worlds']: continue
        b,e=best_bone(x['worlds'][0],mats)
        if e>0.05: continue
        p=local_positions(P,x['ordinal'],x['stride'])[:4000]
        Wm=np.array(x['worlds'][0]).reshape(4,4); wp=(np.c_[p,np.ones(len(p))]@Wm)[:,:3]
        pts_by_bone.setdefault(b,[]).append(wp)
    import colorsys
    for b,lst in pts_by_bone.items():
        wp=np.concatenate(lst); sp,z=proj(wp); r,g,bb=colorsys.hsv_to_rgb((b*0.137)%1,0.6,0.9)
        for q in sp[::3]: d.point((q[0],q[1]),fill=(int(r*255),int(g*255),int(bb*255)))
    for i,M in enumerate(mats):
        sp,z=proj(M[3:4,:3])
        if z[0]<=0: continue
        d.ellipse([sp[0,0]-4,sp[0,1]-4,sp[0,0]+4,sp[0,1]+4],fill=(255,255,255)); d.text((sp[0,0]+6,sp[0,1]-6),str(i),fill=(255,255,0))
    img.save(out); print(out, sorted(pts_by_bone))
if __name__=='__main__':
    which=sys.argv[1]
    if which=='doa3':
        P=os.path.expanduser('~') + '/code/recomp/doa3-re/private/wt-meshdump/private/meshdump-run3/present-4300'
        render(P,0x478bd0,24,lambda s:s['entry'] in (407,412),'skel-doa3-B.png')
        render(P,0x4785d0,24,lambda s:s['entry'] in (536,541),'skel-doa3-A.png')
    else:
        P=DXRUN.replace('run2','run3')+'/present-1650'
        render(P,0x4d3650,32,lambda s:s['archive']=='datadvd.afs' or s['entry']>=67,'skel-doax-a0.png')
