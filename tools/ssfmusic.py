"""Render all songs with the ORIGINAL Saturn sound driver + data via .work/ssfrender.exe
(Highly Theoretical SCSP/68K emulation). Length of one pass comes from the SEQ tempo track.
usage: python tools/ssfmusic.py disc game [songs...]   (music number n = MID.JOE slot n-1)"""
import os, sys, subprocess, numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(__file__))
from seqsynth import parse_seq, step_to_sec, SLOT

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, '..', '.work', 'ssfrender.exe')
TMP = os.path.join(HERE, '..', '.work', 'ssf_tmp.wav')

def song_info(blob):
    res, tempos, tloop, ev, end = parse_seq(blob[0x30000:0x33000])
    if not ev: return None
    t2s = step_to_sec(res, tempos)
    total = max(end, sum(d for d, _ in tempos))
    # SEQ header 'tempo loop' = byte offset of the tempo event the driver loops back to
    # (header is 8 bytes, events 8 bytes): 16 -> event 1 (after the 479-step lead-in)
    k = max(0, tloop // 8 - 1)
    return t2s(total), t2s(sum(d for d, _ in tempos[:k]))

def main():
    disc, out = sys.argv[1], sys.argv[2]
    data = open(os.path.join(disc, 'MID.JOE'), 'rb').read()
    songs = [int(x) for x in sys.argv[3:]] or range(1, len(data) // SLOT + 1)
    os.makedirs(os.path.join(out, 'ogg'), exist_ok=True)
    for n in songs:
        blob = data[(n - 1) * SLOT:(n - 1) * SLOT + 0x33000]
        info = song_info(blob)
        if info is None: print('song', n, 'empty'); continue
        L, S = info
        B = L - S                                    # loop body length (s)
        subprocess.run([EXE, os.path.join(disc, 'ALLDAT.NEW'), os.path.join(disc, 'MID.JOE'),
                        str(n - 1), '%.3f' % (L + 2 * B + 6), TMP], check=True, stderr=subprocess.DEVNULL)
        pcm, sr = sf.read(TMP, dtype='float32')
        tail = pcm[int(L * sr) + sr:int(L * sr) + 4 * sr]
        looping = np.sqrt((tail ** 2).mean()) > 0.01
        loopf = os.path.join(out, 'ogg', '%02d.loop' % n)
        if looping:
            # The driver itself loops: keep lead-in + pass 1 + pass 2 and restart at pass 2, whose start
            # already carries the release tails of pass 1's end. Pass boundaries are located by
            # cross-correlation (emulated timer jitter ~ms), so the kept body [P2, P3) is exactly one
            # real driver period.
            m = pcm.mean(1)
            def align(ref, guess):
                a = m[ref:ref + 2 * sr]
                best, bb = None, guess
                for d in range(-800, 801):
                    c = float(np.dot(a, m[guess + d:guess + d + len(a)]))
                    if best is None or c > best: best, bb = c, guess + d
                return bb
            s0, b0 = int(round(S * sr)), int(round(B * sr))
            o = sr // 2                                  # compare 0.5 s into each pass
            p2 = align(s0 + o, s0 + o + b0) - o
            p3 = align(p2 + o, p2 + o + b0) - o
            loop = p2
            pcm = pcm[:p3].copy()
            # Emulated passes are not sample-identical (timer/LFO phase), so the jump p3 -> p2 would click.
            # Crossfade the last W samples into the W samples that precede p2: playback then reaches exactly
            # what it would have played right before p2, and continues seamlessly after the rewind.
            W = min(2048, p2, p3 - p2)
            f = np.linspace(0.0, 1.0, W, dtype=np.float32)[:, None]
            pcm[p3 - W:p3] = pcm[p3 - W:p3] * (1 - f) + pcm[p2 - W:p2] * f
            open(loopf, 'w').write('%d\n' % loop)
        else:                                        # jingle: keep release tail, trim silence
            nz = np.nonzero(np.abs(pcm).max(1) > 1e-3)[0]
            pcm = pcm[:(nz[-1] + 1 if len(nz) else 1)]
            if os.path.exists(loopf): os.remove(loopf)
        path = os.path.join(out, 'ogg', '%02d.ogg' % n)
        with sf.SoundFile(path, 'w', sr, 2, format='OGG', subtype='VORBIS') as f:
            for i in range(0, len(pcm), 32768): f.write(pcm[i:i + 32768])
        print('song %d %.1fs %s loop@%.3f' % (n, len(pcm) / sr, 'loop' if looping else 'once',
                                               (loop / sr) if looping else 0))

if __name__ == '__main__':
    main()
