import sys,struct,capstone
BASE=0x06020000
B=open('E:/GAME/REA_GAME/sword_1998/disc/PROG.BIN','rb').read()
md=capstone.Cs(capstone.CS_ARCH_SH, capstone.CS_MODE_BIG_ENDIAN|capstone.CS_MODE_SH2)
def u32(a): o=a-BASE; return struct.unpack('>I',B[o:o+4])[0]
def dis(a,n):
    out=[]
    o=a-BASE
    for i in range(n):
        code=B[o+2*i:o+2*i+2]
        ins=list(md.disasm(code,a+2*i))
        pc=a+2*i
        if not ins: out.append(f'{pc:08x}: .word {code.hex()}'); continue
        s=f'{pc:08x}: {code.hex()}  {ins[0].mnemonic} {ins[0].op_str}'
        w=struct.unpack('>H',code)[0]
        if w>>12==0xD:  # mov.l @(disp,pc),Rn
            la=((pc+4)&~3)+(w&0xff)*4; s+=f'   ; ={u32(la):#010x}'
        elif w>>12==0x9:
            la=pc+4+(w&0xff)*2; s+=f'   ; ={struct.unpack(">H",B[la-BASE:la-BASE+2])[0]:#06x}'
        out.append(s)
    return out
def find_lit(v):
    res=[]
    tgt=struct.pack('>I',v)
    i=B.find(tgt)
    while i>=0:
        if i%4==0: res.append(BASE+i)
        i=B.find(tgt,i+1)
    return res
def lit_users(v):
    # code locations whose mov.l @(disp,pc) loads literal v
    users=[]
    for la in find_lit(v):
        for d in range(256):
            pc_lo=la-d*4-4
            for pc in (pc_lo,pc_lo+2):
                o=pc-BASE
                if o<0 or o+2>len(B): continue
                w=struct.unpack('>H',B[o:o+2])[0]
                if w>>12==0xD and (w&0xff)==d and ((pc+4)&~3)+d*4==la: users.append(pc)
    return users
if __name__=='__main__':
    cmd=sys.argv[1]
    if cmd=='dis': print('\n'.join(dis(int(sys.argv[2],16),int(sys.argv[3]))))
    if cmd=='lit': v=int(sys.argv[2],16); print('pool',[hex(x) for x in find_lit(v)],'users',[hex(x) for x in lit_users(v)])
