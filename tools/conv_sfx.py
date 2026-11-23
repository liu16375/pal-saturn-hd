"""AIF.NEW (sector-MKF; chunk = BE u32 byte length + 8 zero bytes + 16-bit BE mono PCM @22050 Hz,
i.e. AIFF data with the header stripped, cf. AIV.NEW) -> SDLPAL sounds.mkf (one WAV per chunk).
Sound number n = chunk n (PROG 0x06076234 = same sector index)."""
import os, sys, struct, io, wave
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from satlib import smkf
DISC, OUT = sys.argv[1], sys.argv[2]
chunks = []
for c in smkf(open(os.path.join(DISC, 'AIF.NEW'), 'rb').read()):
    n = struct.unpack('>I', c[:4])[0] if len(c) >= 12 else 0
    if n == 0: chunks.append(b''); continue
    pcm = c[12:12 + n]; pcm = pcm[:len(pcm) // 2 * 2]
    le = bytearray(pcm); le[0::2], le[1::2] = pcm[1::2], pcm[0::2]
    bio = io.BytesIO()
    with wave.open(bio, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050); w.writeframes(bytes(le))
    chunks.append(bio.getvalue())
offs = []; pos = 4 * (len(chunks) + 1)
for c in chunks: offs.append(pos); pos += len(c)
offs.append(pos)
with open(os.path.join(OUT, 'sounds.mkf'), 'wb') as f:
    f.write(struct.pack('<%dI' % len(offs), *offs))
    for c in chunks: f.write(c)
print('sounds.mkf', len(chunks), 'chunks', sum(1 for c in chunks if c), 'non-empty', pos, 'bytes')
