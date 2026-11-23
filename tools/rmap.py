import sys;sys.path.insert(0,sys.argv[1])
from satlib import *
D='E:/GAME/REA_GAME/sword_1998/disc/'
pal=pal555(open(D+'ALLDAT.NEW','rb').read())
cs=smkf(open(D+'MGOP.JOE','rb').read())
m=int(sys.argv[3]); c=cs[m]
cells=struct.unpack('>32768H',c[:65536])
tiles=c[65536:]; nt=len(tiles)//480
W,H=64*32+16,128*16+8
can=[0]*(W*H); mx=0
def blit(t,px,py):
    if t>=nt: return
    for yy in range(15):
        for xx in range(32):
            v=tiles[t*480+yy*32+xx]
            if v and 0<=px+xx<W and 0<=py+yy<H: can[(py+yy)*W+px+xx]=v
for y in range(128):
    for x in range(64):
        for h in range(2):
            i=((y*64+x)*2+h)*2
            lo,hi=cells[i],cells[i+1]
            b=lo&0x1ff; t=(hi&0x1ff)-1
            mx=max(mx,b,t)
            px,py=x*32+h*16-16,y*16+h*8-8
            blit(b,px,py)
            if t>=0: blit(t,px,py)
print('map',m,'tiles',nt,'max idx',mx)
png(sys.argv[2]+f'/map{m}.png',W,H,can,pal)
