# Build game/lang_sc.txt, lang_ja.txt, lang_en.txt from the translation tables; report every layout problem.
#   python build.py            build what is available, print a summary
#   python build.py -v         also list every problem
import json, re, sys, os, glob, collections
import opencc
import budoux
JP = budoux.load_default_japanese_parser()

os.chdir(os.path.dirname(os.path.abspath(__file__)))
VERBOSE = '-v' in sys.argv
cc = opencc.OpenCC('t2s')
FW = json.load(open('font_width.json'))
U = json.load(open('units.json', encoding='utf-8'))
MSGS = json.load(open('msgs.json', encoding='utf-8'))
WORDS = json.load(open('words.json', encoding='utf-8'))
LIM = {'U': 26, 'L': 26, 'C': 32, 'W': 30}
ZERO = '"\'-@()'          # colour / icon control characters of the dialogue renderer (zero width)
problems = collections.defaultdict(list)


def cw(ch):
    o = ord(ch)
    if o >= 0xF900: o -= 0xF900 - 0xD800
    if o < 0x100: return 1
    v = FW[o] if o < len(FW) else 0
    return 2 if v == 32 else 1 if v == 16 else 0


def width(s, dialog=True):
    s = re.sub(r'\$\d\d|~\d\d', '', s)
    w = 0
    for ch in s:
        if dialog and ch in ZERO: continue
        w += cw(ch)
    return w


REPL = {
    'en': [('Lin Family Fort', 'Lin Fortress'), ('Shuiyue Palace', 'Water Moon Palace'),
           ('tournament for her hand', 'marriage tournament')],
    'ja': [('婿選びの試合', '婿選びの武術大会')],
}


def norm(s, lang, where):
    """engine-safe form of a translated string"""
    for x, y in REPL.get(lang, []): s = s.replace(x, y)
    s = s.replace('　', '　')
    if lang == 'en':
        s = s.replace("'", '’')
        s = re.sub(r'-(?!\d)', '‐', s)          # a hyphen before a digit stays (stat lines: Speed-10)
        s = re.sub(r'(?<=[A-Za-z])-(?=\d)', '‐', s)
        s = s.replace('...', '…') if False else s
    if lang == 'ja':
        s = s.replace('・', '、')
        s = s.replace('(', '（').replace(')', '）') if False else s
    for ch in s:
        if ch in '\n': continue
        if ord(ch) >= 0x100 and cw(ch) == 0:
            problems[lang].append('%s: character %r (U+%04X) is not in the font' % (where, ch, ord(ch)))
    return s


NOSTART = set('、。，．！？：；）」』】〉》…‥ー～ぁぃぅぇぉっゃゅょァィゥェォッャュョ,.!?:;)]’”')
NOEND = set('（「『【〈《([“')


def wrap(text, W, lang):
    """-> list of lines, each at most W columns"""
    out = []
    for para in text.split('\\n'):
        if lang == 'en':
            words = para.split(' ')
            line = ''
            for wd in words:
                if wd == '' and line == '': continue
                cand = wd if line == '' else line + ' ' + wd
                if width(cand) <= W:
                    line = cand
                else:
                    if line: out.append(line)
                    while width(wd) > W:          # a single word wider than the line
                        k = len(wd)
                        while width(wd[:k]) > W: k -= 1
                        out.append(wd[:k]); wd = wd[k:]
                    line = wd
            out.append(line)
        else:
            line = ''
            for ph in JP.parse(para) if para else ['']:
                if width(line + ph) <= W:
                    line += ph; continue
                if line.strip(' 　'):
                    out.append(line.rstrip(' 　')); line = ''
                ph = ph.lstrip(' 　') if not line else ph
                while width(ph) > W:          # one phrase wider than the line: cut by character (kinsoku)
                    k = 0
                    while k < len(ph) and width(ph[:k + 1]) <= W: k += 1
                    c = k
                    while c > 1 and (ph[c] in NOSTART or ph[c - 1] in NOEND): c -= 1
                    if c <= 1: c = k
                    out.append(ph[:c]); ph = ph[c:]
                line = ph
            out.append(line.rstrip(' 　'))
    return [l for l in out] or ['']


def load_tr():
    tr = {}
    for fn in sorted(glob.glob('tr_[0-9][0-9].tsv')):
        for ln, l in enumerate(open(fn, encoding='utf-8'), 1):
            l = l.rstrip('\n').rstrip('\r')
            if not l: continue
            p = l.split('\t')
            if len(p) != 3 or not p[0].isdigit():
                problems['all'].append('%s:%d: bad line' % (fn, ln)); continue
            tr[int(p[0])] = {'ja': p[1], 'en': p[2]}
    fix = 'tr_fix.tsv'                       # manual corrections made after the review win
    if os.path.exists(fix):
        for l in open(fix, encoding='utf-8'):
            p = l.rstrip('\n').split('\t')
            if len(p) == 3 and p[0].isdigit():
                tr.setdefault(int(p[0]), {})
                if p[1]: tr[int(p[0])]['ja'] = p[1]
                if p[2]: tr[int(p[0])]['en'] = p[2]
    return tr


def table(fn, ncol):
    rows = []
    for l in open(fn, encoding='utf-8'):
        l = l.rstrip('\n').rstrip('\r')
        if l:
            p = l.split('\t')
            assert len(p) == ncol, (fn, l)
            rows.append(p)
    return rows


TR = load_tr()
NAMES = {r[0]: {'ja': r[1], 'en': r[2]} for r in table('names_tr.tsv', 3)}
WTR = {int(r[0]): {'src': r[1], 'ja': r[2], 'en': r[3]} for r in table('words_tr.tsv', 4)}
STR = table('strings_tr.tsv', 3)
SCN = table('scenes_tr.tsv', 3)
stats = {}

for lang in ('sc', 'ja', 'en'):
    out = []
    if lang == 'sc':
        for i, m in enumerate(MSGS):
            t = cc.convert(m)
            assert len(t) == len(m)
            if t != m: out.append('M%d\t%s' % (i, t))
        for i, w in enumerate(WORDS):
            if i == 0 or not w.strip(): continue
            if w.endswith('1') and len(w) > 1: w = w[:-1]
            t = cc.convert(w)
            if t != w: out.append('W%d\t%s' % (i, t))
        for r in STR + SCN:
            t = cc.convert(r[0])
            if t != r[0]: out.append('S\t%s\t%s' % (r[0], t))
    else:
        missing = 0; nlines = 0; grown = 0; pages = 0
        DESC = json.load(open('desc.json'))
        DUNITS = set(DESC['units'])
        MAGIC_U = set()
        m2u = {m: u['id'] for u in U for m in u['m']}
        for o, ms in DESC['objs'].items():
            if int(o) >= 296: MAGIC_U.update(m2u[m] for m in ms if m in m2u)
        for u in U:
            uid = u['id']; W = LIM[u['k']]
            where = 'unit %d (msg %d)' % (uid, u['m'][0])
            if u['t'] == 'name':
                t = NAMES[u['src'][0]][lang]
                t = norm(t, lang, where)
                if width(t, False) > 26: problems[lang].append('%s: speaker label too wide: %s' % (where, t))
                out.append('M%d\t%s' % (u['m'][0], t))
                continue
            if uid not in TR or not TR[uid].get(lang):
                missing += 1
                continue
            t = norm(TR[uid][lang], lang, where)
            src = ''.join(u['src'])
            # control codes must survive
            for code in re.findall(r'\$\d\d|~\d\d', src):
                if code not in t: problems[lang].append('%s: control code %s lost: %s' % (where, code, t))
            if re.search(r'[@\\]', re.sub(r'\\n', '', t)) or re.search(r'\$(?!\d\d)|~(?!\d\d)', t):
                problems[lang].append('%s: stray control character: %s' % (where, t))
            if t.count('"') != src.count('"'):
                problems[lang].append('%s: quote marks differ from the source (%d vs %d): %s' % (where, t.count('"'), src.count('"'), t))
            icon = ''
            m = re.search(r'[()]$', re.sub(r'\$\d\d$|~\d\d$', '', src.rstrip()))
            body = t
            if m:
                if body.endswith(('(', ')')): icon = body[-1]; body = body[:-1].rstrip()
                else: icon = m.group(0)
            if lang == 'en': body = body.replace('(', '[').replace(')', ']')
            else: body = body.replace('(', '（').replace(')', '）')
            if u['t'] == 'line':
                lim = max(W, max(width(s) for s in u['src']))
                if width(body) > lim:
                    problems[lang].append('%s: fixed line %d columns wide, limit %d: %s' % (where, width(body), lim, body))
                if u['k'] in 'UL' and re.sub(r'\$\d\d$|~\d\d$', '', body).rstrip().endswith((':', '：')): body += ' '
                out.append('M%d\t%s' % (u['m'][0], body + icon))
                nlines += 1
                continue
            if uid in DUNITS:
                # item / spell description: drawn at x = 72 (31 columns to the screen edge), one line per 17 px
                W = 30
                lines = wrap(body, W, lang)
                cap = max(len(u['m']), 3 if uid in MAGIC_U else 5)
                if len(lines) > cap:
                    problems[lang].append('%s: description %d lines, room for %d: %s' % (where, len(lines), cap, body))
            else:
                lines = wrap(body, W, lang)
            for l in lines:
                if width(l) > W: problems[lang].append('%s: wrapped line still %d columns: %s' % (where, width(l), l))
            if u['k'] in 'UL' and lines[0].rstrip().endswith((':', '：')): lines[0] += ' '
            lines[-1] += icon
            if uid not in DUNITS and u['k'] in 'ULC' and len(lines) > 4:
                # balance the pages (4 lines each): no page with a single orphan line
                # choose the page starts: every page at most 4 lines, pages about equal, and where possible a
                # page ends with the end of a sentence
                L = len(lines); npg = (L + 3) // 4
                END = ('.', '!', '?', '。', '！', '？', '…', '．', '"', '」')
                def sent_end(k): return re.sub(r'[()]$', '', lines[k]).rstrip().endswith(END)
                best = None
                def search(start, left, cuts):
                    global _best
                    if left == 1:
                        if 1 <= L - start <= 4: cand.append(list(cuts))
                        return
                    for size in (4, 3, 2):
                        nx = start + size
                        if nx < L and (L - nx) <= 4 * (left - 1) and (L - nx) >= (left - 1) * 1:
                            search(nx, left - 1, cuts + [nx])
                cand = []
                search(0, npg, [])
                def score(cuts):
                    sizes = [b_ - a_ for a_, b_ in zip([0] + cuts, cuts + [L])]
                    sc = sum(3 for c in cuts if sent_end(c - 1))
                    sc -= (max(sizes) - min(sizes))
                    sc -= 4 * sum(1 for z in sizes if z == 1)
                    return sc
                if cand:
                    for k in max(cand, key=score): lines[k] = '' + lines[k]
            n = len(u['m'])
            nlines += len(lines)
            if len(lines) > n: grown += 1
            if len(lines) > 4: pages += 1
            for k, m_ in enumerate(u['m']):
                if k < n - 1:
                    out.append('M%d\t%s' % (m_, lines[k] if k < len(lines) else '\\x01'))
                else:
                    rest = lines[k:]
                    out.append('M%d\t%s' % (m_, '\\n'.join(rest) if rest else '\\x01'))
        # words
        for i, w in sorted(WTR.items()):
            t = norm(w[lang], lang, 'word %d' % i)
            out.append('W%d\t%s' % (i, t))
        for r in STR + SCN:
            t = r[1 if lang == 'ja' else 2]
            t = norm(t, lang, 'string ' + r[0])
            out.append('S\t%s\t%s' % (r[0], t))
        stats[lang] = (missing, nlines, grown, pages)
    open('../game/lang_%s.txt' % lang, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
    print(lang, len(out), 'entries', stats.get(lang, ''), len(problems[lang]), 'problems')
for k, v in problems.items():
    if VERBOSE:
        for p in v: print(k, p)
    else:
        for p in v[:8]: print(k, p)
