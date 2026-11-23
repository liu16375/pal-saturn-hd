import re, glob, json, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sdlpal'))
out = []
BS = chr(92)


def dec(s):
    s = re.sub(r'\\x([0-9A-Fa-f]{4})', lambda m: chr(int(m.group(1), 16)), s)
    return s.replace(BS + '"', '"').replace(BS + BS, BS)


for fn in sorted(glob.glob('*.c') + glob.glob('*.h') + glob.glob('win32/*.cpp')):
    for ln, l in enumerate(open(fn, encoding='utf-8', errors='replace'), 1):
        for m in re.finditer(r'L"((?:[^"\\]|\\.)*)"', l):
            t = dec(m.group(1))
            if any(ord(c) > 0x2000 for c in t):
                out.append((fn, ln, t))
        for m in re.finditer(r'WCHAR\s+\w+\[\d*\]\s*=\s*\{([^}]*)\}', l):
            if '0x' in m.group(1):
                t = ''.join(chr(int(x, 16)) for x in re.findall(r'0x([0-9A-Fa-f]+)', m.group(1)) if int(x, 16))
                out.append((fn, ln, '[arr] ' + t))
sn = [o for o in out if o[0] == 'scenenames.h']
ot = [o for o in out if o[0] != 'scenenames.h']
print(len(sn), len(ot))
for o in ot:
    print('%s:%d\t%s' % o)
names = sorted(set(o[2] for o in sn))
json.dump(names, open('../i18n/scenenames.json', 'w', encoding='utf-8'), ensure_ascii=False)
print(len(names))
print(' | '.join(names))
