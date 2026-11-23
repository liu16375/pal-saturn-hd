import sys;sys.path.insert(0,sys.argv[1])
from satlib import *
D='E:/GAME/REA_GAME/sword_1998/disc/'
cs=smkf(open(D+'FIREN.AV','rb').read())
k=int(sys.argv[3]); c=cs[k]; pal=pal555(c)
body=c[512:]; n=struct.unpack('>I',body[:4])[0]//4
offs=struct.unpack('>%dI'%n,body[:n*4])
H=256; strip=[]
can=bytearray(512*H)
for i,o in enumerate(offs):
    flag=o>>31; o&=0x7fffffff
    end=vdp_decode(body[o:],can)
    print(i,hex(flag),o,'len',end)
    strip.append(bytes(can))
# render first 6 frames stacked
N=min(6,len(strip)); img=b''.join(strip[:N])
png(sys.argv[2]+f'/fire{k}.png',512,H*N,img,pal)
