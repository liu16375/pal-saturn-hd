import json, re
U = json.load(open('units.json', encoding='utf-8'))
LIM = {'U': 26, 'L': 26, 'C': 32, 'W': 30}
def width(s):
    s = re.sub(r'\$\d\d|~\d\d', '', s); w = 0
    for ch in s:
        if ch in '\'@"()': continue
        w += 1 if ord(ch) < 0x100 else 2
    return w
todo = [u for u in U if u['t'] != 'name']
N = 10; per = (len(todo) + N - 1) // N
# keep name units in the stream as context lines (not to be translated)
idx = {u['id']: k for k, u in enumerate(todo)}
chunks = [[] for _ in range(N)]
cur = 0; cnt = 0
for u in U:
    if u['t'] == 'name':
        chunks[cur].append('#\t\t\t' + u['src'][0])
        continue
    lim = max(LIM[u['k']], max(width(s) for s in u['src'])) if u['t'] == 'line' else 0
    chunks[cur].append('%d\t%s\t%d\t%s' % (u['id'], u['t'], lim, ' / '.join(u['src'])))
    cnt += 1
    if cnt >= per and cur < N - 1: cur += 1; cnt = 0
for k, c in enumerate(chunks):
    open('src_%02d.tsv' % k, 'w', encoding='utf-8', newline='\n').write('\n'.join(c) + '\n')
    print(k, sum(1 for l in c if not l.startswith('#')), sum(len(l) for l in c))
