"""Sega Saturn SEQ (Sound Library) parser -> list of events (abs_step, kind, ch, a, b, dur)"""
import struct

EXT_DUR = {0x88: 0x200, 0x89: 0x800, 0x8a: 0x1000, 0x8b: 0x2000}
EXT_DEL = {0x8c: 0x100, 0x8d: 0x200, 0x8e: 0x800, 0x8f: 0x1000}

def parse_song(bank, songno=0, dur_bit=0x40, del_bit=0x20, maxsteps=1 << 22):
    nsongs = struct.unpack('>H', bank[:2])[0]
    so = struct.unpack('>I', bank[2 + 4 * songno:6 + 4 * songno])[0]
    s = bank[so:]
    res, ntempo, dofs, tloop = struct.unpack('>4H', s[:8])
    tempos = [struct.unpack('>II', s[8 + 8 * i:16 + 8 * i]) for i in range(ntempo)]
    data = s[dofs:]
    ev = []; t = 0; p = 0; ret = None; refleft = 0
    xdur = xdel = 0; unknown = {}
    loop_t = None
    while p < len(data) and t < maxsteps:
        st = data[p]; p += 1
        if st < 0x80:
            ch = st & 0x0f
            key, vel, dur, dl = data[p], data[p + 1], data[p + 2], data[p + 3]; p += 4
            if st & dur_bit: dur += 0x100
            if st & del_bit: dl += 0x100
            ev.append((t, 'note', ch, key, vel, dur + xdur)); t += dl + xdel; xdur = xdel = 0
        elif st == 0x81:
            off, cnt = struct.unpack('>HB', data[p:p + 3]); p += 3
            ret = p; refleft = cnt; p = off
            continue
        elif st == 0x82:
            loop_t = t if loop_t is None else loop_t
            p += 1 if False else 0
        elif st == 0x83:
            break
        elif st in EXT_DUR: xdur += EXT_DUR[st]
        elif st in EXT_DEL: xdel += EXT_DEL[st]
        elif 0xb0 <= st <= 0xbf:
            c, v, dl = data[p], data[p + 1], data[p + 2]; p += 3
            ev.append((t, 'cc', st & 15, c, v, 0)); t += dl + xdel; xdel = 0
        elif 0xc0 <= st <= 0xcf:
            v, dl = data[p], data[p + 1]; p += 2
            ev.append((t, 'prog', st & 15, v, 0, 0)); t += dl + xdel; xdel = 0
        elif 0xd0 <= st <= 0xdf:
            v, dl = data[p], data[p + 1]; p += 2
            ev.append((t, 'at', st & 15, v, 0, 0)); t += dl + xdel; xdel = 0
        elif 0xe0 <= st <= 0xef:
            v, dl = data[p], data[p + 1]; p += 2
            ev.append((t, 'bend', st & 15, v, 0, 0)); t += dl + xdel; xdel = 0
        else:
            unknown[st] = unknown.get(st, 0) + 1
        if ret is not None:
            refleft -= 1
            if refleft <= 0: p = ret; ret = None
    return dict(res=res, tempos=tempos, events=ev, end=t, loop=loop_t, unknown=unknown, tloop=tloop)
