import numpy as np, sys
def bank(path, base, n):
    ram=open(path,'rb').read()
    return np.array([np.frombuffer(ram[base+i*64:base+i*64+64],np.float32).reshape(4,4) for i in range(n)],dtype=float)
def tree(paths, base, n):
    B=np.array([bank(p,base,n) for p in paths])  # T x n x 4 x 4
    T=len(paths); R=B[:,:,:3,:3]; t=B[:,:,3,:3]
    # local offset of j's origin in i's frame: (t_j - t_i) @ inv(R_i)
    off=np.einsum('tjk,tikl->tijl', t, np.zeros(1)) if False else None
    out=np.zeros((T,n,n,3))
    for k in range(T):
        Rinv=np.linalg.pinv(R[k])
        for i in range(n):
            out[k,i]=(t[k]-t[k,i])@Rinv[i]
    var=out.std(0).max(-1)              # n x n
    dist=np.linalg.norm(out.mean(0),axis=-1)
    moving=t.std(0).max(-1)             # does bone move at all
    return var, dist, out.mean(0), moving, B
if __name__=='__main__':
    pass
