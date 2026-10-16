import numpy as np
def swing(a,b):
    a=np.asarray(a,float); b=np.asarray(b,float); m=np.eye(4); c=a@b; ax=np.cross(a,b)
    if 1+c<=1e-6 or (c<0 and ax@ax<=1e-12):
        least=int(np.argmin(np.abs(a))); basis=np.zeros(3); basis[least]=1
        ax=np.cross(a,basis); ax/=np.linalg.norm(ax)
        m[:3,:3]=2*np.outer(ax,ax)-np.eye(3); return m
    R=np.eye(3)*c+np.outer(ax,ax)/(1+c)
    for i in range(3):
        u,v=(i+1)%3,(i+2)%3; R[u,v]+=ax[i]; R[v,u]-=ax[i]
    m[:3,:3]=R; return m
def blend(a,b,w):
    rel=b@np.linalg.inv(np.eye(4)) if False else None
    at=np.eye(4); at[:3,:3]=a[:3,:3].T
    rel=b@at
    ax=np.array([rel[1,2]-rel[2,1], rel[2,0]-rel[0,2], rel[0,1]-rel[1,0]])
    L=np.linalg.norm(ax); ax=ax/L if L>0 else ax
    c=(rel[2,2]+rel[1,1]+rel[0,0]-1)*0.5
    ang=np.arctan2(L*0.5,c)*w; s,cc=np.sin(ang),np.cos(ang)
    R=np.eye(4); R[:3,:3]=np.eye(3)*cc+(1-cc)*np.outer(ax,ax)
    for i in range(3):
        u,v=(i+1)%3,(i+2)%3; R[u,v]+=s*ax[i]; R[v,u]-=s*ax[i]
    out=R@a; out[3,:3]=(1-w)*a[3,:3]+w*b[3,:3]; out[3,3]=1; return out
def derive(B, extra=False):
    B=B.copy()
    for out,p,c in ((26,16,8),(27,20,14),(24,5,4),(25,11,10)):
        B[out]=B[p]@swing(B[p][0,:3],B[c][0,:3]); B[out][3]=B[c][3]
    B[30]=blend(B[8],B[26],.5); B[31]=blend(B[14],B[27],.5)
    B[4]=blend(B[4],B[24],.5); B[10]=blend(B[10],B[25],.5)
    B[24]=blend(B[4],B[24],.5) if extra else B[4].copy(); B[25]=blend(B[10],B[25],.5) if extra else B[10].copy()
    B[28]=B[24].copy(); B[29]=B[25].copy(); B[28][3]=B[5][3]; B[29][3]=B[11][3]
    return B
