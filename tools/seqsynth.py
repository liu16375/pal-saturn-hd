"""
Render Sega Saturn music (MID.JOE: per song a 0x33000-byte SCSP sound-RAM image =
TON tone bank (0x30000) + SEQ bank (0x3000)) to OGG for SDLPAL (Music=OGG -> ogg/NN.ogg).

Simplified SCSP model: sample playback with loop, ADSR (MAME rate tables), TL, DISDL/DIPAN,
velocity, CC7/CC10/CC11, pitch bend. No LFO/FM/DSP effects.
usage: python -I seqsynth.py <disc_dir> <out_dir> [song numbers...]
"""
import os, sys, struct
import numpy as np
import soundfile as sf

SR = 44100
SLOT = 102 * 2048

AR_MS = [1e5, 1e5, 8100.0, 6900.0, 6000.0, 4800.0, 4000.0, 3400.0, 3000.0, 2400.0, 2000.0, 1700.0, 1500.0,
         1200.0, 1000.0, 860.0, 760.0, 600.0, 500.0, 430.0, 380.0, 300.0, 250.0, 220.0, 190.0, 150.0, 130.0, 110.0,
         95.0, 76.0, 63.0, 55.0, 47.0, 38.0, 31.0, 27.0, 24.0, 19.0, 15.0, 13.0, 12.0, 9.4, 7.9, 6.8, 6.0, 4.7, 3.8,
         3.4, 3.0, 2.4, 2.0, 1.8, 1.6, 1.3, 1.1, 0.93, 0.85, 0.65, 0.53, 0.44, 0.40, 0.35, 0.0, 0.0]
DR_MS = [1e5, 1e5, 118200.0, 101300.0, 88600.0, 70900.0, 59100.0, 50700.0, 44300.0, 35500.0, 29600.0, 25300.0,
         22200.0, 17700.0, 14800.0, 12700.0, 11100.0, 8900.0, 7400.0, 6300.0, 5500.0, 4400.0, 3700.0, 3200.0,
         2800.0, 2200.0, 1800.0, 1600.0, 1400.0, 1100.0, 920.0, 790.0, 690.0, 550.0, 460.0, 390.0, 340.0, 270.0,
         230.0, 200.0, 170.0, 140.0, 110.0, 98.0, 85.0, 68.0, 57.0, 49.0, 43.0, 34.0, 28.0, 25.0, 22.0, 18.0, 14.0,
         12.0, 11.0, 8.5, 7.1, 6.1, 5.4, 4.3, 3.6, 3.1]

EXT_GATE = {0x88: 0x200, 0x89: 0x800, 0x8a: 0x1000, 0x8b: 0x2000}
EXT_STEP = {0x8c: 0x100, 0x8d: 0x200, 0x8e: 0x800, 0x8f: 0x1000}


# ------------------------------------------------------------------ SEQ
def parse_seq(bank):
    so = struct.unpack('>I', bank[2:6])[0]
    s = bank[so:]
    res, ntempo, dofs, tloop = struct.unpack('>4H', s[:8])
    tempos = [struct.unpack('>II', s[8 + 8 * i:16 + 8 * i]) for i in range(ntempo)]
    d = s[dofs:]
    ev = []; t = 0; p = 0; ret = None; left = 0; xg = xs = 0; n = 0
    while p < len(d) and n < 400000:
        n += 1
        st = d[p]; p += 1; counted = True
        if st < 0x80:
            key, vel, gate, dl = d[p:p + 4]; p += 4
            if st & 0x40: gate += 0x100
            if st & 0x20: dl += 0x100
            ev.append((t, 0, st & 15, key, vel, gate + xg)); t += dl + xs; xg = xs = 0
        elif st == 0x81:
            off, cnt = struct.unpack('>HB', d[p:p + 3]); p += 3
            ret = p; left = cnt; p = off; continue
        elif st == 0x82:
            t += d[p] + xs; xs = 0; p += 1
        elif st == 0x83:
            break
        elif st in EXT_GATE:
            xg += EXT_GATE[st]; counted = False
        elif st in EXT_STEP:
            xs += EXT_STEP[st]; counted = False
        elif 0xb0 <= st <= 0xbf:
            c, v, dl = d[p:p + 3]; p += 3
            ev.append((t, 1, st & 15, c, v, 0)); t += dl + xs; xs = 0
        elif 0xc0 <= st <= 0xcf:
            v, dl = d[p:p + 2]; p += 2
            ev.append((t, 2, st & 15, v, 0, 0)); t += dl + xs; xs = 0
        elif 0xd0 <= st <= 0xdf:
            v, dl = d[p:p + 2]; p += 2; t += dl + xs; xs = 0
        elif 0xe0 <= st <= 0xef:
            v, dl = d[p:p + 2]; p += 2
            ev.append((t, 3, st & 15, v, 0, 0)); t += dl + xs; xs = 0
        if ret is not None and counted:
            left -= 1
            if left <= 0:
                p = ret; ret = None
    return res, tempos, tloop, ev, t


def step_to_sec(res, tempos):
    """returns function step -> seconds (piecewise tempo)"""
    bounds = []; acc_t = 0; acc_s = 0.0
    for dur, usb in tempos:
        bounds.append((acc_t, acc_s, usb / 1e6 / res))
        acc_t += dur; acc_s += dur * usb / 1e6 / res
    def f(t):
        for i in range(len(bounds) - 1, -1, -1):
            if t >= bounds[i][0] or i == 0:
                t0, s0, k = bounds[i]
                return s0 + (t - t0) * k
    return f


# ------------------------------------------------------------------ TON
class Layer:
    pass


def parse_ton(t):
    hdr0 = struct.unpack('>H', t[:2])[0]
    offs = [struct.unpack('>H', t[i:i + 2])[0] for i in range(0, hdr0, 2)]
    voices = []
    for vo in offs[4:]:
        bend = t[vo]; nl = struct.unpack('b', t[vo + 2:vo + 3])[0] + 1
        layers = []
        for i in range(max(nl, 0)):
            L = t[vo + 4 + 32 * i:vo + 4 + 32 * (i + 1)]
            if len(L) < 32: break
            ly = Layer()
            ly.lo, ly.hi = L[0], L[1]
            r0, sa, lsa, lea, eg1, eg2, tl = struct.unpack('>7H', L[2:16])
            ly.lpctl = (r0 >> 5) & 3; ly.pcm8 = (r0 >> 4) & 1
            ly.sa = ((r0 & 0xf) << 16) | sa; ly.lsa = lsa; ly.lea = lea
            ly.ar = eg1 & 31; ly.d1r = (eg1 >> 6) & 31; ly.d2r = (eg1 >> 11) & 31
            ly.rr = eg2 & 31; ly.dl = (eg2 >> 5) & 31; ly.krs = (eg2 >> 10) & 15
            ly.tl = tl & 0xff
            ly.disdl = (L[0x18] >> 5) & 7; ly.dipan = L[0x18] & 31
            ly.base = L[0x19]; ly.fine = struct.unpack('b', L[0x1a:0x1b])[0]
            ly.pcm = load_pcm(t, ly)
            layers.append(ly)
        voices.append((bend or 2, layers))
    return voices


def load_pcm(t, ly):
    n = ly.lea + 1
    if ly.pcm8:
        raw = np.frombuffer(t[ly.sa:ly.sa + n], dtype=np.int8).astype(np.float32) / 128.0
    else:
        raw = np.frombuffer(t[ly.sa:ly.sa + 2 * n], dtype='>i2').astype(np.float32) / 32768.0
    if len(raw) < n: raw = np.pad(raw, (0, n - len(raw)))
    return raw


# ------------------------------------------------------------------ synth
def eg_rate(r, key, krs):
    if r == 0: return 0
    if krs != 0xf:
        oct_ = key // 12 - 5
        rate = 2 * r + krs * 2 + max(oct_, -7)  # rough key-rate scaling
    else:
        rate = 2 * r
    return max(0, min(63, rate))


def envelope(ly, key, nsamp_on, nsamp_total):
    """amplitude envelope (linear) for nsamp_total samples, key-off at nsamp_on"""
    env = np.zeros(nsamp_total, np.float32)
    # attack
    a_ms = AR_MS[eg_rate(ly.ar, key, ly.krs)] if ly.ar else 1e5
    na = int(a_ms * SR / 1000)
    # decay dB/sample: 96 dB over DR time
    def dbps(r):
        rr = eg_rate(r, key, ly.krs)
        return 0.0 if r == 0 else 96.0 / (DR_MS[rr] * SR / 1000)
    d1, d2, rr = dbps(ly.d1r), dbps(ly.d2r), dbps(ly.rr)
    dl_db = ly.dl * 3.0
    att = np.empty(nsamp_total, np.float32)
    i = np.arange(nsamp_total, dtype=np.float32)
    # attack portion (linear rise)
    on = min(nsamp_on, nsamp_total)
    na_eff = min(na, on)
    lvl = np.ones(nsamp_total, np.float32)
    if na > 0:
        lvl[:na_eff] = (i[:na_eff] + 1) / na
    # decay after attack
    db = np.zeros(nsamp_total, np.float32)
    if on > na_eff:
        k = i[na_eff:on] - na_eff
        if d1 > 0:
            t1 = dl_db / d1
            db1 = np.where(k < t1, k * d1, dl_db + (k - t1) * d2)
        else:
            db1 = k * 0
        db[na_eff:on] = db1
    # release
    start_db = db[on - 1] if on > 0 else 0.0
    start_lvl = lvl[on - 1] if on > 0 else 0.0
    if nsamp_total > on:
        k = i[on:] - on + 1
        db[on:] = start_db + k * (rr if rr > 0 else 96.0 / (SR * 0.01))
        lvl[on:] = start_lvl
    env = lvl * np.power(10.0, -np.minimum(db, 120.0) / 20.0)
    return env


def render_note(ly, key, vel, nsamp_on, bend_semi):
    """mono float32 note including release tail"""
    semis = key - ly.base + ly.fine / 256.0 + bend_semi
    step = 2.0 ** (semis / 12.0)
    # release length: until -60 dB
    rel = 96.0 / max(1e-9, 96.0 / (DR_MS[eg_rate(ly.rr, key, ly.krs)] * SR / 1000)) if ly.rr else SR * 0.01
    nrel = int(min(rel * 60 / 96, SR * 4))
    ntot = nsamp_on + nrel
    pos = np.arange(ntot, dtype=np.float64) * step
    lsa, lea = ly.lsa, ly.lea
    if ly.lpctl and lea > lsa:
        L = lea - lsa
        over = pos >= lea
        pos[over] = lsa + np.mod(pos[over] - lsa, L)
    else:
        end = pos >= lea
        if end.any():
            cut = int(np.argmax(end))
            pos = pos[:cut]; ntot = cut
            if ntot == 0: return np.zeros(0, np.float32)
            nsamp_on = min(nsamp_on, ntot)
    i0 = pos.astype(np.int64); fr = (pos - i0).astype(np.float32)
    pcm = ly.pcm
    i1 = i0 + 1
    if ly.lpctl and lea > lsa:
        i1 = np.where(i1 > lea, lsa, i1)
    i1 = np.minimum(i1, len(pcm) - 1); i0 = np.minimum(i0, len(pcm) - 1)
    s = pcm[i0] * (1 - fr) + pcm[i1] * fr
    env = envelope(ly, key, nsamp_on, ntot)
    gain = 10 ** (-ly.tl * 0.375 / 20) * (vel / 127.0) ** 2
    return (s * env * gain).astype(np.float32)


def pan_gains(dipan, ccpan):
    # SCSP DIPAN: bit4 side (0=right? 1=left), low 4 bits = 3dB steps attenuation on the other side
    att = (dipan & 15) * 3.0
    l = r = 1.0
    if dipan & 16: r = 10 ** (-att / 20)
    else: l = 10 ** (-att / 20)
    p = (ccpan - 64) / 64.0
    l *= min(1.0, 1 - p); r *= min(1.0, 1 + p)
    return l, r


def render_song(blob):
    ton, seq = blob[:0x30000], blob[0x30000:0x33000]
    voices = parse_ton(ton)
    res, tempos, tloop, ev, end = parse_seq(seq)
    if not ev: return None
    t2s = step_to_sec(res, tempos)
    total = max(end, sum(d for d, _ in tempos))
    nout = int(t2s(total) * SR) + SR * 3
    out = np.zeros((nout, 2), np.float32)
    prog = [0] * 16; vol = [127] * 16; expr = [127] * 16; pan = [64] * 16; bend = [64] * 16
    for (t, kind, ch, a, b, gate) in ev:
        if kind == 1:
            if a == 7: vol[ch] = b
            elif a == 10: pan[ch] = b
            elif a == 11: expr[ch] = b
        elif kind == 2: prog[ch] = a
        elif kind == 3: bend[ch] = a
        else:
            if prog[ch] >= len(voices): continue
            br, layers = voices[prog[ch]]
            st = int(t2s(t) * SR)
            non = max(1, int((t2s(t + gate) - t2s(t)) * SR))
            bsemi = (bend[ch] - 64) / 64.0 * br
            for ly in layers:
                if not (ly.lo <= a <= ly.hi) or ly.disdl == 0: continue
                s = render_note(ly, a, b, non, bsemi)
                if len(s) == 0: continue
                g = 10 ** (-(7 - ly.disdl) * 6 / 20) * (vol[ch] / 127.0) ** 2 * (expr[ch] / 127.0) ** 2
                gl, gr = pan_gains(ly.dipan, pan[ch])
                e = min(nout, st + len(s)); s = s[:e - st]
                out[st:e, 0] += s * g * gl; out[st:e, 1] += s * g * gr
    # trim trailing silence, normalise softly
    nz = np.nonzero(np.abs(out).max(axis=1) > 1e-4)[0]
    if len(nz) == 0: return None
    out = out[:nz[-1] + 1]
    peak = np.abs(out).max()
    out *= min(0.6 / peak, 4.0)
    return out


def write_ogg(path, pcm):
    with sf.SoundFile(path, 'w', SR, 2, format='OGG', subtype='VORBIS') as o:
        for i in range(0, len(pcm), 32768): o.write(pcm[i:i + 32768])


if __name__ == '__main__':
    DISC, OUT = sys.argv[1], sys.argv[2]
    data = open(os.path.join(DISC, 'MID.JOE'), 'rb').read()
    nsongs = len(data) // SLOT
    songs = [int(x) for x in sys.argv[3:]] or range(1, nsongs + 1)
    os.makedirs(os.path.join(OUT, 'ogg'), exist_ok=True)
    for n in songs:  # music number n = slot n-1 (PROG.BIN 0x060329c0)
        blob = data[(n - 1) * SLOT:(n - 1) * SLOT + 0x33000]
        try:
            pcm = render_song(blob)
        except Exception as e:
            print('song', n, 'failed', e); continue
        if pcm is None: print('song', n, 'empty'); continue
        write_ogg(os.path.join(OUT, 'ogg', '%02d.ogg' % n), pcm)
        print('song', n, '%.1fs' % (len(pcm) / SR))

