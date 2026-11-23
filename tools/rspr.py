import sys;sys.path.insert(0,sys.argv[1])
from satlib import *
D='E:/GAME/REA_GAME/sword_1998/disc/'
pal=pal555(open(D+'ALLDAT.NEW','rb').read())
out=sys.argv[2]
for fn,ci in (('MGO.J',2),('ABC.J',0),('FFF.J',0)):
    c=smkf(open(D+fn,'rb').read())[ci]
    n,dims=sprites(c)
    off=2+4*n
    W=sum(w for w,h in dims); H=max(h for w,h in dims)
    canvas=[0]*(W*H); x0=0
    for w,h in dims:
        for y in range(h):
            canvas[y*W+x0:y*W+x0+w]=c[off+y*w:off+y*w+w]
        off+=w*h; x0+=w
    print(fn,ci,n,dims[:4],'used',off,'of',len(c))
    png(f'{out}/{fn}_{ci}.png',W,H,canvas,pal)
