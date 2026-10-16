import numpy as np, itertools, json
from retarget_preview import bank
from common import draws, local_positions
def rotations():
    out=[]
    for perm in itertools.permutations(range(3)):
        for signs in itertools.product((1,-1),repeat=3):
            R=np.zeros((3,3))
            for i,(p,s) in enumerate(zip(perm,signs)): R[i,p]=s
            if np.linalg.det(R)>0: out.append(R)
    return out
def part_points(P, B, n, actor_filter=None):
    dr=draws(P); pts={}
    for x in dr.values():
        if x['offscreen'] or not x['worlds'] or x.get('program') or len(x['worlds'])!=1: continue
        W=np.array(x['worlds'][0]).reshape(4,4)
        errs=[np.abs(W-B[i]).max() for i in range(n)]
        b=int(np.argmin(errs))
        if errs[b]>1e-3: continue
        p=local_positions(P,x['ordinal'],x['stride']).astype(float)
        p=p[np.isfinite(p).all(1)&(np.abs(p)<2).all(1)]
        pts.setdefault(b,[]).append(p)
    return {b:np.concatenate(v) for b,v in pts.items()}
def chamfer(a,b):
    a=a[::max(1,len(a)//600)]; b=b[::max(1,len(b)//600)]
    d=np.linalg.norm(a[:,None]-b[None],axis=2); return d.min(1).mean()+d.min(0).mean()
