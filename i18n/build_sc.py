# Simplified Chinese overlay: character conversion of the original text (same widths, same line structure)
import json, opencc
cc = opencc.OpenCC('t2s')
msgs = json.load(open('msgs.json', encoding='utf-8'))
words = json.load(open('words.json', encoding='utf-8'))
out = []
for i, m in enumerate(msgs):
    t = cc.convert(m)
    assert len(t) == len(m), (i, m, t)
    if t != m: out.append('M%d\t%s' % (i, t))
for i, w in enumerate(words):
    if i == 0 or not w.strip(): continue
    if w.endswith('1') and len(w) > 1: w = w[:-1]
    t = cc.convert(w)
    if t != w: out.append('W%d\t%s' % (i, t))
try:
    for l in open('strings_sc.tsv', encoding='utf-8'):
        l = l.rstrip('\n')
        if l: out.append('S\t' + l)
except FileNotFoundError:
    pass
open('../game/lang_sc.txt', 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
print(len(out))
