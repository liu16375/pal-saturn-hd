# Extract translatable units from the game data -> units.json
import struct, json, collections, re, sys
G = '../game/'
f = open(G + 'sss.mkf', 'rb').read()
n = struct.unpack('<I', f[:4])[0] // 4
offs = struct.unpack('<%dI' % n, f[:4 * n])
chunk = lambda i: f[offs[i]:offs[i + 1]]
c3 = chunk(3); mo = struct.unpack('<%dI' % (len(c3) // 4), c3)
d = open(G + 'm.msg', 'rb').read()
msgs = [d[mo[i]:mo[i + 1]].decode('cp950', 'replace') for i in range(len(mo) - 1)]
sc = chunk(4); ne = len(sc) // 8
ent = [struct.unpack('<4H', sc[i * 8:i * 8 + 8]) for i in range(ne)]

def width(s):
    s = re.sub(r'\$\d\d|~\d\d', '', s)
    w = 0
    for ch in s:
        if ch in '-\'@"()': continue
        w += 1 if ord(ch) < 0x100 else 2
    return w

units = []; seen = collections.defaultdict(list)
kind = 'U'; i = 0
KIND = {0x3B: 'C', 0x3C: 'U', 0x3D: 'L', 0x3E: 'W'}
while i < ne:
    op = ent[i][0]
    if op in KIND: kind = KIND[op]
    if op == 0xFFFF:
        j = i
        while j < ne and ent[j][0] == 0xFFFF: j += 1
        run = [ent[k][1] for k in range(i, j)]
        # split the run: name lines and timed lines stand alone
        cur = []
        def flush():
            global cur
            if cur: units.append({'e': None, 'k': kind, 't': 'text', 'm': cur}); cur = []
        for m in run:
            if m >= len(msgs): continue
            s = msgs[m]
            if kind != 'C' and kind != 'W' and not cur and s.endswith('：') and width(s) <= 14:
                flush(); units.append({'k': kind, 't': 'name', 'm': [m]})
            elif '~' in s or '$' in s or kind == 'W':
                flush(); units.append({'k': kind, 't': 'line', 'm': [m]})
            else:
                cur.append(m)
        flush()
        for u in units[-(j - i) - 1:]:
            pass
        i = j
    else:
        i += 1
for u in units:
    u.pop('e', None)
# de-duplicate units with the same message tuple; detect conflicts
uniq = {}; order = []
for u in units:
    key = tuple(u['m'])
    if key not in uniq: uniq[key] = u; order.append(key)
owner = collections.defaultdict(set)
for key in order:
    for m in key: owner[m].add(key)
conf = {m for m, ks in owner.items() if len(ks) > 1}
final = []; done = set()
for key in order:
    u = uniq[key]
    if any(m in conf for m in key):
        # conflicting grouping: fall back to one unit per line
        for m in key:
            if m not in done:
                done.add(m); final.append({'k': u['k'], 't': 'name' if u['t'] == 'name' else 'line', 'm': [m]})
    else:
        for m in key: done.add(m)
        final.append(u)
unref = [m for m in range(len(msgs)) if m not in done and msgs[m].strip()]
for m in unref: final.append({'k': 'U', 't': 'line', 'm': [m], 'unref': 1})
for idx, u in enumerate(final):
    u['id'] = idx
    u['src'] = [msgs[m] for m in u['m']]
json.dump(final, open('units.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
json.dump(msgs, open('msgs.json', 'w', encoding='utf-8'), ensure_ascii=False)
c = collections.Counter((u['k'], u['t']) for u in final)
print(len(msgs), 'msgs', len(final), 'units', len(conf), 'conflict msgs', len(unref), 'unref')
print(c)
for k in 'ULCW':
    ws = [width(s) for u in final if u['k'] == k for s in u['src']]
    if ws: print(k, 'max width', max(ws), collections.Counter(ws).most_common(6), 'over26:', sum(1 for w in ws if w > 26))
print(collections.Counter(len(u['m']) for u in final if u['t'] == 'text').most_common(12))
names = collections.Counter(u['src'][0] for u in final if u['t'] == 'name')
print(len(names)); print(' '.join('%s%d' % kv for kv in names.most_common(400)))
