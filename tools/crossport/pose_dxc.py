"""Pose a DXC1 asset on a DOA3 bank exactly as costume_inject.c doax_bank does; render by bone."""
import sys, json, numpy as np
from asset_preview import load, pose
from retarget_preview import bank, render, corr4
from derive import derive
def doax_bank(rel, hier, B3):
    B=np.zeros((32,4,4))
    for i in range(15): B[i]=corr4(i)@B3[i]
    rels={}
    for r in rel:   # first entry per bone; a second (neck to head) is the runtime's halfway blend, not modelled here
        rels.setdefault(r[0],(r[1],np.array(r[2:]).reshape(4,4)))
    for bone,parent,*off in hier:
        if bone in rels: B[bone]=rels[bone][1]@B[parent]; continue
        B[bone][3,:3]=np.array(off)@B[parent][:3,:3]+B[parent][3,:3]
    for b,(p,R) in rels.items():
        if not any(h[0]==b for h in hier): B[b]=R@B[p]
    return derive(B)
if __name__ == '__main__':
    asset,P,base,out=sys.argv[1],sys.argv[2],int(sys.argv[3],16),sys.argv[4]
    magic,rel,hier,parts=load(asset)
    B3=bank(open(P+'/ram.bin','rb').read(),base,23)
    bk=doax_bank(rel,hier,B3)
    import mesh_dump_export as mde
    first=json.loads(open(P+'/draws.jsonl').readline())
    V=np.array(first['view']).reshape(4,4); Pj=np.array(first['projection']).reshape(4,4)
    # frame camera on fighter
    sets=[]; legs={7,13,3,9,18,22}
    for p in parts:
        tris=mde.triangles(p['primitive'],p['idx']); tris=tris[(tris<p['count']).all(1)]
        if not len(tris): continue
        bones={b for b,_ in p['binds']}
        col=(230,60,60) if bones&legs else ((120,230,120) if len(bones)>1 else (200,170,150))
        v=pose(p,bk)
        if not np.isfinite(v).all(): print('nonfinite part', bones)
        sets.append((v,tris,col))
    c=B3[2][3,:3]
    eye=c+np.array([0,0.6,-3.5]); f=(c-eye)/np.linalg.norm(c-eye); r=np.cross([0,1,0],f); r/=np.linalg.norm(r); u=np.cross(f,r)
    Vw=np.eye(4); Vw[:3,0]=r; Vw[:3,1]=u; Vw[:3,2]=f; Vw[3,:3]=[-r@eye,-u@eye,-f@eye]
    render(sets,Vw,Pj,out); print(out)
    print('bones 6/7/3:',[np.round(bk[b][3,:3],2) for b in (6,7,3,12,13,9)])
    hip=B3[2][3,:3]
    for k,p in enumerate(parts):
        bones=[b for b,_ in p['binds']]
        if not set(bones)&legs: continue
        v=pose(p,bk); 
        print(k,bones,p['count'],'centroid-hip',np.round(v.mean(0)-hip,2),'extent',np.round(np.ptp(v,0),2))
