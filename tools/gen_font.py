"""Convert the original Saturn font CM.FNT (records of 2-byte Big5 code + 16x16 1bpp glyph, row 15 always
empty) into SDLPAL's DOS font pair wor16.asc (Big5 codes) / wor16.fon (glyphs, 30 bytes each from 0x682).
usage: python tools/gen_font.py disc game"""
import sys, os


def main():
    disc, game = sys.argv[1:3]
    d = open(os.path.join(disc, 'CM.FNT'), 'rb').read()
    n = len(d) // 34
    codes, glyphs = bytearray(), bytearray(b'\0' * 0x682)
    for i in range(n):
        r = d[i * 34:i * 34 + 34]
        codes += r[:2]
        glyphs += r[2:32]
    open(os.path.join(game, 'wor16.asc'), 'wb').write(codes)
    open(os.path.join(game, 'wor16.fon'), 'wb').write(glyphs)
    print('glyphs', n)


if __name__ == '__main__':
    main()
