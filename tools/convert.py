"""
Convert the Sega Saturn release of PAL (T-37401H) into a data directory usable by (patched) SDLPAL.
usage: python -I convert.py <disc_dir> <out_dir>
See NOTES.md for the reverse-engineered formats.
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from satlib import smkf, vdp_decode, rle_decode

DISC, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
BASE = 0x06020000

def rd(name): return open(os.path.join(DISC, name), 'rb').read()
PROG = rd('PROG.BIN')
def pa(addr): return addr - BASE
def bew(addr, n): o = pa(addr); return list(struct.unpack('>%dH' % n, PROG[o:o + 2 * n]))
def besw(addr, n): o = pa(addr); return list(struct.unpack('>%dh' % n, PROG[o:o + 2 * n]))

# ---------------------------------------------------------------- containers
def raw(data): return b'RAW!' + struct.pack('<I', len(data)) + data

def write_mkf(name, chunks):
    n = len(chunks)
    offs = []; pos = 4 * (n + 1)
    for c in chunks: offs.append(pos); pos += len(c)
    offs.append(pos)
    with open(os.path.join(OUT, name), 'wb') as f:
        f.write(struct.pack('<%dI' % (n + 1), *offs))
        for c in chunks: f.write(c)
    print('wrote', name, n, 'chunks', pos, 'bytes')

def le_words(ws): return struct.pack('<%dH' % len(ws), *[w & 0xffff for w in ws])

# ---------------------------------------------------------------- RLE
def rle_encode(px, w, h, trans=0):
    """raw 8bpp (trans = transparent index) -> DOS PAL RLE frame (LE w,h header)."""
    out = bytearray(struct.pack('<HH', w, h))
    for y in range(h):
        row = px[y * w:(y + 1) * w]; x = 0
        while x < w:
            if row[x] == trans:
                n = 0
                while x + n < w and row[x + n] == trans and n < 0x7f: n += 1
                out.append(0x80 + n); x += n
            else:
                n = 0
                while x + n < w and row[x + n] != trans and n < 0x7f: n += 1
                out.append(n); out += bytes(row[x:x + n]); x += n
    return bytes(out)

def bank_hdr(bank):
    """Port frame prefix 02 00 <bank> 00: the frame's pixels are raw indices of ALLDAT palette <bank>
    (Saturn CRAM bank); sdlpal resolves the colour per pixel (video.c SAT_*). bank 0 = scene palette."""
    return bytes([2, 0, bank, 0]) if bank else b''

def sprite_group(frames, bank=0):
    """list of RLE frames -> DOS sprite group (WORD offset table, n+1 entries, offsets in words)."""
    n = len(frames)
    if n == 0: return b''
    hdr = 2 * (n + 1)
    offs = []; body = bytearray(); pos = hdr
    for f in frames:
        if len(f) & 1: f = f + b'\0'
        f = bank_hdr(bank) + f
        offs.append(pos // 2); body += f; pos += len(f)
    offs.append(pos // 2)
    body += bytes(4)   # SDLPAL treats the end entry as frame n -> give it an empty 0x0 frame
    return struct.pack('<%dH' % (n + 1), *[o & 0xffff for o in offs]) + bytes(body)

def sat_sprite_frames(c):
    """Saturn raw sprite chunk -> list of RLE frames"""
    if len(c) < 2: return []
    n = struct.unpack('>H', c[:2])[0]
    dims = [struct.unpack('>HH', c[2 + 4 * i:6 + 4 * i]) for i in range(n)]
    off = 2 + 4 * n; frames = []
    for w, h in dims:
        frames.append(rle_encode(c[off:off + w * h], w, h)); off += w * h
    return frames

RAWBANK = True   # keep raw palette indices + bank prefix instead of nearest-colour remapping

def sat_sheet(addr, palidx=0):
    """Saturn RLE sheet (BE byte offset table + BE w,h + DOS RLE) -> list of DOS RLE frames"""
    base = pa(addr)
    n = struct.unpack('>H', PROG[base:base + 2])[0] // 2
    offs = struct.unpack('>%dH' % n, PROG[base:base + 2 * n])
    frames = []
    for o in offs:
        w, h = struct.unpack('>HH', PROG[base + o:base + o + 4])
        if w == 0 or w > 320 or h > 200: continue
        px, used = rle_decode(PROG[base + o + 4:], w, h, 0xff)
        if palidx and not RAWBANK:
            tab = bytearray(remap_table(sat_pal_to_dos(rd('ALLDAT.NEW'), palidx * 512), main_pal()))
            for i in range(255):
                if tab[i] == 0xff: tab[i] = 0xfe     # keep 0xff free for transparency
            tab[0xff] = 0xff
            px = px.translate(bytes(tab))
        frames.append(rle_encode(px, w, h, 0xff))
    return frames

def swap_rle_header(c):
    """BE w,h + DOS RLE -> LE w,h + DOS RLE"""
    w, h = struct.unpack('>HH', c[:4])
    _, used = rle_decode(c[4:], w, h, 0)
    return struct.pack('<HH', w, h) + c[4:4 + used]

# ---------------------------------------------------------------- palettes
def sat_pal_to_dos(b, off=0):
    out = bytearray()
    for i in range(256):
        v = struct.unpack('>H', b[off + 2 * i:off + 2 * i + 2])[0]
        for c in (v & 31, (v >> 5) & 31, (v >> 10) & 31):
            out.append((c << 1) | (c >> 4))
    return bytes(out)

def pal_rgb(dospal): return [tuple(dospal[3 * i:3 * i + 3]) for i in range(256)]

# SDLPAL palette-cycles 0xF9..0xFE while a dialog waits for a key (blinking icon);
# never let converted art land on those entries.
# (sdlpal text.c cycling is disabled in our build, so all entries are usable)
NONCYC0 = list(range(256))
NONCYC = [i for i in NONCYC0 if i]

def cyc_fix_table(keep0=True):
    """identity table except 0xF9..0xFE -> nearest non-cycling colour of the main palette"""
    P = pal_rgb(main_pal()); tab = list(range(256))
    for i in [j for j in range(0xf9, 0xff) if j not in NONCYC0]:
        c = P[i]
        tab[i] = min(NONCYC if keep0 else NONCYC0, key=lambda j: sum((P[j][k] - c[k]) ** 2 for k in range(3)))
    return bytes(tab)

def remap_table(src, dst, keep0=False):
    """index map: each src colour -> nearest colour in dst (both DOS 6-bit palettes).
    keep0: index 0 is transparency -> 0 stays 0 and no opaque colour maps to 0"""
    S, D = pal_rgb(src), pal_rgb(dst); tab = []
    for i, c in enumerate(S):
        if keep0 and i == 0: tab.append(0); continue
        tab.append(min(NONCYC if keep0 else NONCYC0, key=lambda j: (D[j][0] - c[0]) ** 2 * 3 + (D[j][1] - c[1]) ** 2 * 4 + (D[j][2] - c[2]) ** 2 * 2))
    return bytes(tab)

def remap_rle_frame(w, h, body, tab):
    """re-encode an RLE frame with pixel remap; transparency kept as-is"""
    out = bytearray(struct.pack('<HH', w, h)); p = 0; i = 0; n = w * h
    while i < n and p < len(body):
        T = body[p]; p += 1
        if (T & 0x80) and T <= 0x80 + w:
            out.append(T); i += T - 0x80
        else:
            out.append(T); out += body[p:p + T].translate(tab); p += T; i += T
    return bytes(out)

MAINPAL = None
def main_pal():
    global MAINPAL
    if MAINPAL is None: MAINPAL = sat_pal_to_dos(rd('ALLDAT.NEW'), 0)
    return MAINPAL

def conv_pat():
    a = rd('ALLDAT.NEW')
    write_mkf('pat.mkf', [sat_pal_to_dos(a, 512 * i) for i in range(8)])

# ---------------------------------------------------------------- sprites
def conv_sprite_file(src, dst, palidx=0, first=0):
    """palidx: ALLDAT palette the Saturn sprites are drawn with (remapped to the main palette)
    first: sprite number of the file's chunk 0 (MGO.J: the Saturn loads chunk N-1 for sprite N -- verified in
    an emulator by matching VDP1 VRAM: event sprites 21/55/628 -> chunks 20/54/627, party Li (2) -> chunk 1)"""
    tab = remap_table(sat_pal_to_dos(rd('ALLDAT.NEW'), palidx * 512), main_pal(), True) if palidx else cyc_fix_table()
    bank = 0
    if palidx and RAWBANK: tab, bank = None, palidx
    chunks = [b''] * first
    for c in smkf(rd(src)):
        if not c: chunks.append(b''); continue
        if tab:
            hdr = 2 + 4 * struct.unpack('>H', c[:2])[0]
            c = c[:hdr] + c[hdr:].translate(tab)
        chunks.append(raw(sprite_group(sat_sprite_frames(c), bank)))
    write_mkf(dst, chunks)

def conv_fbp():
    b = rd('FBP.PX')
    chunks, pals = [], []
    for i in range(len(b) // 65536):
        c = b[i * 65536:(i + 1) * 65536]
        pals.append(sat_pal_to_dos(c, 64000))       # each FBP carries its own palette (shown with it on the Saturn)
        if RAWBANK:
            chunks.append(raw(c[:64000]))           # raw indices; sdlpal resolves them with fbppal.mkf #n
        else:
            chunks.append(raw(c[:64000].translate(remap_table(pals[-1], main_pal()))))
    # title picture (FBP #2): the Saturn shows it with ALLDAT palette 5 (emulator CRAM bank 5 == ALLDAT 5), whose
    # entries 147..159 / 240..253 differ from the palette stored after the picture
    if len(pals) > 2: pals[2] = sat_pal_to_dos(rd('ALLDAT.NEW'), 5 * 512)
    write_mkf('fbp.mkf', chunks)
    write_mkf('fbppal.mkf', pals)

def conv_ball():
    b = rd('BALL.NEW')
    n = struct.unpack('>I', b[:4])[0] // 4
    offs = struct.unpack('>%dI' % n, b[:4 * n])
    tab = bytes(range(256)) if RAWBANK else remap_table(sat_pal_to_dos(rd('ALLDAT.NEW'), 3 * 512), main_pal())   # items use palette 3
    chunks = []
    for i in range(n - 1):
        c = b[offs[i]:offs[i + 1]]; w, h = struct.unpack('>HH', c[:4])
        _, used = rle_decode(c[4:], w, h, 0)
        chunks.append(bank_hdr(3 if RAWBANK else 0) + remap_rle_frame(w, h, c[4:4 + used], tab))
    write_mkf('ball.mkf', chunks)

def conv_rgm():
    b = rd('RGM.JOE'); slot = 5 * 2048
    tab = bytes(range(256)) if RAWBANK else remap_table(sat_pal_to_dos(rd('ALLDAT.NEW'), 3 * 512), main_pal())
    chunks = []
    for k in range(len(b) // slot):
        c = b[k * slot:(k + 1) * slot]
        if not any(c[:4]): chunks.append(b''); continue
        w, h = struct.unpack('>HH', c[:4])
        _, used = rle_decode(c[4:], w, h, 0)
        chunks.append(bank_hdr(3 if RAWBANK else 0) + remap_rle_frame(w, h, c[4:4 + used], tab))   # portraits use ALLDAT palette 3
    write_mkf('rgm.mkf', chunks)

def conv_fire():
    chunks = []
    for c in smkf(rd('FIREN.AV')):
        if len(c) < 520: chunks.append(b''); continue
        body = c[512:]
        tab = remap_table(sat_pal_to_dos(c, 0), main_pal(), True)   # each effect has its own palette
        n = (struct.unpack('>I', body[:4])[0] & 0xffff) // 4
        offs = struct.unpack('>%dI' % n, body[:4 * n])
        W, H = 512, 256
        can = bytearray(W * H); imgs = []
        for o in offs:
            vdp_decode(body[o & 0xffffff:], can, W)
            imgs.append(bytes(can))
        # common bounding box over all frames
        x0, y0, x1, y1 = W, H, 0, 0
        for im in imgs:
            for y in range(H):
                row = im[y * W:(y + 1) * W]
                if any(row):
                    y0 = min(y0, y); y1 = max(y1, y + 1)
                    nz = [i for i, v in enumerate(row) if v]
                    x0 = min(x0, nz[0]); x1 = max(x1, nz[-1] + 1)
        if x1 <= x0: chunks.append(b''); continue
        w, h = x1 - x0, y1 - y0
        frames = []
        # emulator (wind 0x0603f108 trace): an n-entry table plays frames 0..n-2; the last entry is never shown
        for im in (imgs[:-1] if len(imgs) > 1 else imgs):
            px = b''.join(im[(y0 + y) * W + x0:(y0 + y) * W + x1] for y in range(h)).translate(tab)
            frames.append(rle_encode(px, w, h))
        g = sprite_group(frames)   # >128KB is fine: patched PAL_SpriteGetFrame handles 16-bit offset wrap
        chunks.append(raw(g))
    write_mkf('fire.mkf', chunks)

# ---------------------------------------------------------------- RNG movies
def rng_encode(cur, prev):
    """SDLPAL RNG frame: diff in 2-pixel units. 03 n / 04 w = skip (n+1) pairs, 0b n / 0c w = copy (n+1) pairs, 00 end"""
    import numpy as np
    c = np.frombuffer(cur, np.uint16)
    changed = np.ones(len(c), bool) if prev is None else (c != np.frombuffer(prev, np.uint16))
    out = bytearray(); i = 0; n = len(c)
    # run boundaries
    edges = np.flatnonzero(np.diff(changed.astype(np.int8))) + 1
    starts = np.concatenate(([0], edges)); ends = np.concatenate((edges, [n]))
    for a, b in zip(starts, ends):
        cnt = int(b - a)
        if not changed[a]:
            if b == n: break
            while cnt:
                k = min(cnt, 0x10000)
                out += bytes([0x03, k - 1]) if k <= 0x100 else bytes([0x04]) + struct.pack('<H', k - 1)
                cnt -= k
        else:
            pos = int(a)
            while cnt:
                k = min(cnt, 0x10000)
                out += bytes([0x0b, k - 1]) if k <= 0x100 else bytes([0x0c]) + struct.pack('<H', k - 1)
                out += cur[2 * pos:2 * (pos + k)]
                pos += k; cnt -= k
    out.append(0)
    return bytes(out)

def conv_rng():
    chunks = []; pals = bytearray()
    P = rd('RNG.PAL')
    for k, x in enumerate(smkf(rd('RNG.AV'))):
        pals += sat_pal_to_dos(P, k * 512)
        if len(x) < 8: chunks.append(b''); continue
        n = struct.unpack('>I', x[:4])[0]
        offs = struct.unpack('>%dI' % n, x[4:4 + 4 * n])
        can = bytearray(320 * 256); prev = None; frames = []
        # emulator (o_d194, RNG 1): the movie ends on frame n-2; the last table entry is never shown (as FIREN.AV)
        for o in (offs[:-1] if n > 1 else offs):
            if o & 0x80000000:   # raw 320x200 keyframe
                o &= 0x7fffffff; can[:64000] = x[o:o + 64000]
            else:
                vdp_decode(x[o:], can, 320)
            cur = bytes(can[:64000])
            frames.append(raw(rng_encode(cur, prev))); prev = cur
        hdr = 4 * (len(frames) + 1); offl = []; pos = hdr
        for f in frames: offl.append(pos); pos += len(f)
        offl.append(pos)
        chunks.append(struct.pack('<%dI' % len(offl), *offl) + b''.join(frames))
    write_mkf('rng.mkf', chunks)
    open(os.path.join(OUT, 'rngpal.dat'), 'wb').write(bytes(pals))

# ---------------------------------------------------------------- maps
def conv_maps():
    maps, gops = [], []
    CYC = cyc_fix_table()
    for c in smkf(rd('MGOP.JOE')):
        if not c: maps.append(b''); gops.append(b''); continue
        cells = struct.unpack('>32768H', c[:65536])
        out = []
        for i in range(0, 32768, 2):
            lo, hi = cells[i], cells[i + 1]
            def dosw(v, obst):
                idx = v & 0x1ff; height = (v >> 12) & 0xf
                r = (idx & 0xff) | (height << 8) | ((idx >> 8) << 12)
                if obst: r |= 0x2000
                return r
            out.append(dosw(lo, lo & 0x200)); out.append(dosw(hi, 0))
        maps.append(raw(le_words(out)))
        tiles = c[65536:]
        tiles = tiles.translate(CYC)
        frames = [rle_encode(tiles[t * 480:(t + 1) * 480], 32, 15) for t in range(len(tiles) // 480)]
        gops.append(sprite_group(frames))
    write_mkf('map.mkf', maps)
    write_mkf('gop.mkf', gops)

# ---------------------------------------------------------------- text
MSG_BOUNDS = [0x853a, 0xb4f3, 0xd543, 0xe161, 0x125dd, 0x15d38, 0x21438, 0x255cd, 0x2bc65, 0x2e239,
              0x2f2ef, 0x3066d, 0x30f56, 0x3197c]

def msg_offsets():
    a = 0x06077754; out = []; hi = 0; prev = -1
    while True:
        x = struct.unpack('>H', PROG[pa(a):pa(a) + 2])[0]
        if x < prev: hi += 0x10000
        v = hi + x
        if out and (v < out[-1] or v - out[-1] > 2000): break
        out.append(v); prev = x; a += 2
    return out

def conv_text():
    open(os.path.join(OUT, 'ascfont.15'), 'wb').write(rd('ASCFONT.15'))   # Saturn half-width font (sdlpal font.c)
    offs = msg_offsets()
    total = offs[-1]
    cm = rd('CM.MSG'); bounds = [0] + MSG_BOUNDS + [total]
    msg = bytearray(); pos = 0
    for i in range(len(bounds) - 1):
        ln = bounds[i + 1] - bounds[i]
        msg += cm[pos:pos + ln]
        pos += (ln + 2047) // 2048 * 2048
    assert len(msg) == total
    open(os.path.join(OUT, 'm.msg'), 'wb').write(bytes(msg))
    # word.dat
    w = pa(0x0608005a); words = []
    while w + 10 <= len(PROG):
        e = PROG[w:w + 10]
        if b'\0' in e: break
        words.append(e); w += 10
    words[0] = b' ' * 10
    open(os.path.join(OUT, 'word.dat'), 'wb').write(b''.join(words))
    print('m.msg', total, 'bytes', len(offs) - 1, 'msgs; word.dat', len(words), 'words')
    return offs

# ---------------------------------------------------------------- sss.mkf
def conv_sss(msgoffs):
    # 0 event objects
    idx = rd('CM.IDX'); ev = bytearray()
    for i in range(len(idx) // 22):
        r = idx[i * 22:(i + 1) * 22]
        x, y, layer, trig, auto, spr = struct.unpack('>HHhHHH', r[:12])
        state, tmode, nfr, dirn, cur = r[13], r[14], r[15], r[16], r[17]
        ev += struct.pack('<hHHhHHhHHHHHHHHH', 0, x, y, layer, trig, auto, state, tmode, spr, nfr, dirn, cur,
                          0, 0, 0, 0)
    # 1 scenes (DOS = Saturn[1..300])
    sc = bew(0x06076dec, 301 * 4)[4:]
    # 2 objects (Win95 layout, 14 bytes), pad to 600
    ob = bew(0x0607e112, 572 * 7) + [0] * (28 * 7)
    # 3 message offsets (u32)
    mo = struct.pack('<%dI' % len(msgoffs), *msgoffs)
    # 4 script entries
    se = rd('CM.SEC') + rd('CM.NOR')
    se = le_words(struct.unpack('>%dH' % (len(se) // 2), se))
    write_mkf('sss.mkf', [bytes(ev), le_words(sc), le_words(ob), mo, se])

# ---------------------------------------------------------------- data.mkf
def conv_data():
    ch = [b''] * 16
    # 0 stores: Saturn [9 slots][21 stores] -> DOS [21][9]
    st = bew(0x0606a8d2, 9 * 21)
    ch[0] = le_words([st[slot * 21 + s] for s in range(21) for slot in range(9)])
    # 1 enemies
    ch[1] = le_words(bew(0x06066d8e, 154 * 35))
    # 2 enemy teams
    ch[2] = le_words(bew(0x060697aa, 381 * 5))
    # 3 player roles: Saturn [6][75] -> DOS [75][6]
    pr = bew(0x060663f8, 6 * 75)
    ch[3] = le_words([pr[p * 75 + f] for f in range(75) for p in range(6)])
    # 4 magic (packed on Saturn, provisional mapping — see NOTES)
    ch[4] = conv_magic()
    # 5 battlefields
    bf = []
    for i in range(64):
        a = pa(0x0606a67e + 6 * i)
        wave = struct.unpack('b', PROG[a:a + 1])[0]
        eff = [struct.unpack('b', PROG[pa(0x0606a684 + 6 * i + e):pa(0x0606a684 + 6 * i + e) + 1])[0] for e in range(1, 6)]
        bf += [wave & 0xffff] + [x & 0xffff for x in eff]
    ch[5] = le_words(bf)
    # 6 level-up magic: Saturn per-player (level, magic) lists, 0-terminated -> DOS rows of 5 x (level, magic)
    ch[6] = conv_levelup_magic()
    # 9 UI sprites, 10 battle effect sprites, 12 dialog icons
    ch[9] = sprite_group(sat_sheet(0x0606eff8, 3), 3 if RAWBANK else 0)
    ch[10] = sprite_group(sat_sheet(0x0606abdc, 3), 3 if RAWBANK else 0)
    ch[12] = sprite_group(sat_sheet(0x060753b4, 3), 3 if RAWBANK else 0)   # dialog icons are drawn with the UI palette (ALLDAT pal 3)
    # 11 battle effect index [10][2]: Saturn init 0x06026900 fills byte table 0x06089e90[sprite][2];
    # [1]*3 = slash frame base (0x060374f2)
    ch[11] = le_words([0,0, 1,1, 2,2, 3,3, 4,4, 5,3, 2,2, 4,0, 6,4, 3,3])
    # 13 enemy positions: ORIGINAL Saturn table, filled by immediates at PROG 0x060269c4 into RAM
    # 0x06089bb0[(count-1)*20 + i*4] = (x,y); read at 0x0602e284 (y += enemy wYPosOffset, same as SDLPAL)
    pos = []
    layouts = {1: [(100, 110)], 2: [(70, 130), (140, 106)], 3: [(70, 140), (100, 110), (160, 100)],
               4: [(70, 140), (100, 110), (160, 100), (50, 100)],
               5: [(70, 140), (100, 110), (160, 100), (30, 110), (90, 80)]}
    for i in range(5):            # SDLPAL: EnemyPos.pos[enemy index][enemy count - 1]
        for n in range(1, 6):
            x, y = layouts[n][i] if i < n else (0, 0)
            pos += [x, y]
    ch[13] = le_words(pos)
    # 14 level-up exp
    ch[14] = le_words(bew(0x0606a80a, 100))
    write_mkf('data.mkf', ch)

def conv_levelup_magic():
    # Saturn: 5 players x 20 (level, magic) pairs, fixed 40-word slots, zero padded
    w = bew(0x0606aa4c, 200)
    lists = [[(w[p * 40 + 2 * i], w[p * 40 + 2 * i + 1]) for i in range(20) if w[p * 40 + 2 * i]] for p in range(5)]
    n = max(len(l) for l in lists)
    rows = []
    for r in range(n):
        for p in range(5):
            rows += list(lists[p][r]) if r < len(lists[p]) else [0, 0]
    print('levelup magic lists', [len(l) for l in lists])
    return le_words(rows)

def conv_magic():
    """Saturn MAGIC (PROG 0x0606677c, 111 x 14 bytes, magic k uses FIREN effect k):
    w0 MP cost, w1 base damage, w2 sound, b6/b7 effect anchor (x,y in frame), b8/b9 s8 extra offset,
    b10 bits0-1 repeat count, bits2-3 ?, bits5-6 ==2 shake all frames; b11 = type<<4 | element, b12 summon/keep, b13 hold+shake frames"""
    import json
    bbox = {int(k): v for k, v in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fire_bbox.json'))).items()}
    TYPE = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 7: 5, 8: 8, 9: 3}   # summons -> field effect (god art is inside FIREN)
    out = []
    a = pa(0x0606677c)
    for k in range(111):
        r = PROG[a + 14 * k:a + 14 * k + 14]
        cost, dmg, snd = struct.unpack('>HHH', r[:6])
        ax, ay = r[6], r[7]
        ox, oy = struct.unpack('bb', r[8:10])
        st = r[11] >> 4; elem = r[11] & 15
        # effect position switch (player 0x0603bfa8 / enemy 0x06039fa0): type 3 = (b8,b9) except magic 91 -> (-80,-40)
        # and enemy-cast magic 43 -> (120,24); types 6/7/9 (summons) are drawn at (0,0), b8/b9 ignored
        if st in (6, 7, 9): ox = oy = 0
        if k == 91: ox, oy = -80, -40
        if k == 43: ox, oy = 120, 24
        if elem > 5: elem = 0
        mtype = TYPE.get(st, 0)
        x0, y0, x1, y1 = bbox.get(k, (0, 0, 0, 0))
        w, h = x1 - x0, y1 - y0
        # SDLPAL draws the frame with its bottom-centre at base+offset; Saturn draws the canvas at base-anchor+offset
        if mtype == 3:
            bx, by = 160, 200
            xo, yo = x0 + ox + w // 2 - bx, y0 + oy + h - by
        elif mtype == 2 and ax == 0 and ay == 0:
            xo, yo = x0 + ox + w // 2 - 120, y0 + oy + h - 100
        else:
            xo, yo = x0 - ax + ox + w // 2, y0 - ay + oy + h
        # effect loop 0x0603f108: plays (b10&3)+1 times (SDLPAL plays n + n*wEffectTimes), shakes the screen every
        # frame when (b10&0x60)==0x40 (-> wUnknown bit0, patched fight.c), then holds the last frame b13 frames
        # with shaking (= SDLPAL wShake). b10&0x0C is added to the battlefield wave 0x0607699c and written as VDP2
        # line-scroll table by 0x0602475c (0x0603f1b0), restored after (0x0603f45e) = SDLPAL wWave.
        times = r[10] & 3; shakeall = 1 if (r[10] & 0x60) == 0x40 else 0
        # b12 = pre-cast frames 0..b12-1, every pass restarts at b12 (0x0603a19c/0x0603f21a) = SDLPAL wFireDelay
        out += [k, mtype, xo & 0xffff, yo & 0xffff, 0, 0, 0, r[12], times, r[13], r[10] & 12, shakeall, cost, dmg, elem, snd]
    return le_words(out)

if __name__ == '__main__':
    steps = sys.argv[3:] or ['pat', 'sprites', 'maps', 'text', 'data']
    if 'pat' in steps: conv_pat()
    if 'sprites' in steps:
        # MGO (map characters/objects): PROG 0x060266e2 gives every non-tile texture entry colno 0x100
        # = CRAM bank 1 = ALLDAT palette 1 (tiles use bank 0). Palette 0 gave blond hair / red faces.
        conv_sprite_file('MGO.J', 'mgo.mkf', 1, 1)
        conv_sprite_file('ABC.J', 'abc.mkf', 2, 1)   # ABC.J also N-1 (emulator VDP1: enemy 97 -> chunk 96)
        conv_sprite_file('FFF.J', 'f.mkf', 2)        # FFF.J is not shifted (Li battle sprite 0 -> chunk 0)
        conv_fbp(); conv_ball(); conv_rgm(); conv_fire()
    if 'mgo' in steps: conv_sprite_file('MGO.J', 'mgo.mkf', 1, 1)
    if 'fbp' in steps: conv_fbp()
    if 'rgm' in steps: conv_rgm()
    if 'fire' in steps: conv_fire()
    if 'maps' in steps: conv_maps()
    if 'text' in steps or 'data' in steps:
        offs = conv_text(); conv_sss(offs)
    if 'data' in steps: conv_data()
    if 'rng' in steps or len(sys.argv) == 3: conv_rng()
