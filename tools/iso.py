import sys,struct
f=open(sys.argv[1],'rb')
def sec(n):
    f.seek(n*2352+16); return f.read(2048)
pvd=sec(16)
print(pvd[1:6], 'vol', pvd[40:72].strip(), 'blocks', struct.unpack('<I',pvd[80:84])[0])
root=pvd[156:190]
def walk(lba,size,path,out):
    data=b''.join(sec(lba+i) for i in range((size+2047)//2048))
    i=0
    while i<len(data):
        l=data[i]
        if l==0: i=(i//2048+1)*2048; continue
        r=data[i:i+l]
        elba=struct.unpack('<I',r[2:6])[0]; esz=struct.unpack('<I',r[10:14])[0]
        fl=r[25]; nl=r[32]; name=r[33:33+nl]
        if name not in (b'\x00',b'\x01'):
            n=name.decode('latin1').split(';')[0]
            if fl&2: walk(elba,esz,path+n+'/',out)
            else: out.append((path+n,elba,esz))
        i+=l
out=[]
walk(struct.unpack('<I',root[2:6])[0],struct.unpack('<I',root[10:14])[0],'/',out)
for o in out: print(o)
print(len(out),'files; max end lba',max(l+(s+2047)//2048 for _,l,s in out))
import os
if len(sys.argv)>2:
    d=sys.argv[2]; os.makedirs(d,exist_ok=True)
    for p,l,s in out:
        with open(os.path.join(d,p.strip('/')),'wb') as g:
            n=(s+2047)//2048; buf=b''.join(sec(l+i) for i in range(n)); g.write(buf[:s])
    print('extracted to',d)
