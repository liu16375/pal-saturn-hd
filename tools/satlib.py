import struct,zlib
def smkf(b):
    idx=[]
    for i in range(1024):
        v=struct.unpack('>H',b[i*2:i*2+2])[0]
        if (i and v==0) or (idx and v<idx[-1]): break
        idx.append(v)
    return [b[idx[k]*2048:idx[k+1]*2048] for k in range(len(idx)-1)]
def pal555(b,off=0):
    p=[]
    for i in range(256):
        v=struct.unpack('>H',b[off+i*2:off+i*2+2])[0]
        p.append(((v&31)<<3,((v>>5)&31)<<3,((v>>10)&31)<<3))
    return p
def png(fn,w,h,pix,pal):
    raw=b''.join(b'\0'+bytes(c for i in pix[y*w:(y+1)*w] for c in pal[i]) for y in range(h))
    def ch(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    open(fn,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b''))
def sprites(c):
    n=struct.unpack('>H',c[:2])[0]
    dims=[struct.unpack('>HH',c[2+4*i:6+4*i]) for i in range(n)]
    return n,dims
def vdp_decode(src, canvas, W=512, x=0, y=0):
    """Saturn delta codec (PROG.BIN 0x06024220, flag=0). canvas: bytearray W*H"""
    p=0; row=y; dst=row*W+x
    while True:
        op=src[p]; hi=op&0xf0; n=op&15
        if op==0: return p+1
        if hi==0x10: row+=1; dst=row*W+x
        elif hi==0x20: dst+=n
        elif hi==0x30: p+=1; dst+=(n<<8)|src[p]
        elif hi in (0x40,0x50):
            if hi==0x50: p+=1; n=(n<<8)|src[p]
            canvas[dst:dst+n]=bytes(n); dst+=n
        elif hi in (0x60,0x70):
            if hi==0x70: p+=1; n=(n<<8)|src[p]
            canvas[dst:dst+n]=src[p+1:p+1+n]; p+=n; dst+=n
        elif hi in (0x80,0x90):
            if hi==0x90: p+=1; n=(n<<8)|src[p]
            p+=1; canvas[dst:dst+n]=bytes([src[p]])*n; dst+=n
        else: raise ValueError('bad op %02x at %d'%(op,p))
        p+=1
def rle_decode(d, w, h, trans=0):
    """DOS PAL RLE body -> raw pixels (trans fill)."""
    out=bytearray([trans])*(w*h); i=0; p=0; n=w*h
    while i<n and p<len(d):
        T=d[p]; p+=1
        if (T&0x80) and T<=0x80+w: i+=T-0x80
        else:
            out[i:i+T]=d[p:p+T]; p+=T; i+=T
    return bytes(out),p
