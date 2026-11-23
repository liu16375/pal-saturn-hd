"""Saturn CD-DA tracks (raw 16-bit LE stereo 44.1k, NN.WAV) -> SDLPAL ogg/100NN.ogg (CD track NN)"""
import os, sys, glob, numpy as np, soundfile as sf
DISC, OUT = sys.argv[1], sys.argv[2]
os.makedirs(os.path.join(OUT, 'ogg'), exist_ok=True)
for f in sorted(glob.glob(os.path.join(DISC, '[0-9][0-9].WAV'))):
    n = int(os.path.basename(f)[:2])
    pcm = np.fromfile(f, dtype='<i2').reshape(-1, 2)
    dst = os.path.join(OUT, 'ogg', '%d.ogg' % (10000 + n))
    with sf.SoundFile(dst, 'w', 44100, 2, format='OGG', subtype='VORBIS') as o:
        for i in range(0, len(pcm), 32768): o.write(pcm[i:i + 32768])
    print(dst, len(pcm) / 44100, 's')

