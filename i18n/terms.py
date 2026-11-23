import json, glob, re, sys, collections
U = {u['id']: u for u in json.load(open('units.json', encoding='utf-8'))}
TR = {}
for fn in sorted(glob.glob('tr_[0-9][0-9].tsv')):
    for l in open(fn, encoding='utf-8'):
        p = l.rstrip('\n').split('\t')
        if len(p) == 3: TR[int(p[0])] = (p[1], p[2], fn[3:5])
import os
if os.path.exists('tr_fix.tsv'):
    for l in open('tr_fix.tsv', encoding='utf-8'):
        p = l.rstrip('\n').split('\t')
        if len(p) == 3 and p[0].isdigit():
            o = TR[int(p[0])]; TR[int(p[0])] = (p[1] or o[0], p[2] or o[1], o[2])
def show(term, pats_en, pats_ja, limit=0):
    ce = collections.Counter(); cj = collections.Counter(); miss = []
    for i, (ja, en, ch) in TR.items():
        src = ''.join(U[i]['src'])
        if term in src:
            fe = [p for p in pats_en if re.search(p, en, re.I)]
            fj = [p for p in pats_ja if re.search(p, ja)]
            ce[fe[0] if fe else '?'] += 1; cj[fj[0] if fj else '?'] += 1
            if (not fe or not fj) and len(miss) < limit: miss.append((i, ch, src, ja, en))
    print(term, dict(ce), dict(cj))
    for m in miss: print('   ', m)
if __name__ == '__main__':
    lim = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    show('林家堡', ['Lin Manor', 'Lin Family Fort', 'Lin Fortress', 'Lin Castle', 'Lin Fort'], ['林家堡'], lim)
    show('水月宮', ['Water Moon Palace', 'Shuiyue Palace'], ['水月宮'], lim)
    show('比武招親', ['marriage tournament', 'tournament for her hand', 'tournament'], ['婿選びの武術大会', '婿選びの試合', '婿選び'], lim)
    show('李大哥', ['Brother Li'], ['李兄さん', '李のお兄さん'], lim)
    show('逍遙哥哥', ['Brother Xiaoyao'], ['逍遥お兄ちゃん', '逍遥兄さん', '逍遥兄ちゃん'], lim)
    show('桃源', ['Peach Blossom', 'Peach Spring', 'Taoyuan'], ['桃源'], lim)
    show('山神廟', ['Mountain God Shrine', 'Mountain God Temple'], ['山神廟'], lim)
    show('公主', ['Your Highness', 'Princess', 'Her Highness'], ['姫様', '公主', '姫'], lim)
    show('少主', ['Young Mistress', 'Little Mistress', 'Young Master'], ['少主'], lim)
    show('殭屍', ['zombie', 'jiangshi', 'hopping'], ['キョンシー', '殭屍', 'ゾンビ'], lim)
    show('教主', ['Cult Leader', 'Leader', 'Master'], ['教主'], lim)
    show('聖姑', ['Holy Lady'], ['聖姑'], lim)
    show('鎖妖塔', ['Demon-Sealing Tower'], ['鎖妖塔'], lim)
    show('試煉窟', ['Trial Cave'], ['試練の洞窟'], lim)
    show('聖靈珠', ['Holy Orb'], ['聖霊珠'], lim)
    show('玉佛寺', ['Jade Buddha Temple'], ['玉仏寺'], lim)
    show('白河村', ['Baihe Village'], ['白河村'], lim)
    show('黑水鎮', ['Blackwater Town'], ['黒水鎮'], lim)
    show('仙靈島', ['Xianling Isle'], ['仙霊島'], lim)
    show('蜀山', ['Mount Shu', 'Shu Sect', 'Shushan'], ['蜀山'], lim)
    show('酒劍仙', ['Wine Sword Immortal'], ['酒剣仙'], lim)
    show('劍聖', ['Sword Saint'], ['剣聖'], lim)
    show('拜月', ['Moon'], ['拝月'], lim)
    show('女媧', ['Nuwa'], ['女媧'], lim)
    show('靈兒', ["Ling'er"], ['霊児'], lim)
    show('月如', ['Yueru'], ['月如'], lim)
    show('逍遙', ['Xiaoyao'], ['逍遥'], lim)
    show('阿奴', ['Anu'], ['阿奴'], lim)
    show('林天南', ['Lin Tiannan'], ['林天南'], lim)
    show('劉晉元', ['Liu Jinyuan', 'Jinyuan'], ['劉晋元', '晋元'], lim)
    show('彩依', ['Caiyi'], ['彩依'], lim)
    show('石長老', ['Elder Shi'], ['石長老'], lim)
    show('姬三娘', ['Ji Sanniang'], ['姫三娘'], lim)
    show('蓋羅嬌', ['Gai Luojiao'], ['蓋羅嬌'], lim)
    show('南詔', ['Nanzhao'], ['南詔'], lim)
    show('蘇州', ['Suzhou'], ['蘇州'], lim)
    show('揚州', ['Yangzhou'], ['揚州'], lim)
    show('余杭', ['Yuhang'], ['余杭'], lim)
    show('餘杭', ['Yuhang'], ['余杭'], lim)
    show('隱龍窟', ['Hidden Dragon Cave'], ['隠龍窟'], lim)
    show('將軍塚', ["General's Tomb"], ['将軍塚'], lim)
    show('水魔獸', ['Water Demon Beast'], ['水魔獣'], lim)
    show('巫后', ['Witch Queen'], ['巫后'], lim)
    show('巫王', ['Witch King'], ['巫王'], lim)
    show('苗', ['Miao'], ['苗'], lim)
