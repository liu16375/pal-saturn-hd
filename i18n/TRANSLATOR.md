# Translating the dialogue of 仙劍奇俠傳 (Sega Saturn text) into Japanese and English

You translate ONE chunk file `src_NN.tsv` and write `tr_NN.tsv` in this folder (E:\GAME\REA_GAME\sword_1998\i18n).

## Read first
- `glossary_core.md` — fixed names/places and naming rules. Binding.
- `names_tr.tsv` — speaker labels (source, ja, en). Binding for how each speaker is called.
- `words_tr.tsv` — items, spells, stats, menu words (index, source, ja, en). Binding: when a line mentions an item or spell (得到止血草, 學會了御劍術 …) use exactly this rendering.
- your chunk `src_NN.tsv`.

## Source format (TAB separated)
`id  type  limit  source`
- Lines starting with `#` are speaker labels: context only (who says the following units). Do NOT output them.
- `source`: the original lines of one dialogue box, separated by ` / ` (the original broke lines every 13 characters; the breaks carry no meaning unless the text is verse or a list).
- `type = text`: free length. Write ONE flowing string; the game re-wraps it. Use the two characters `\n` only for a forced line break (verse, couplets, lists, sign-board layouts).
- `type = line`: one fixed screen line. Its display width must not exceed `limit` columns (a full-width character such as kanji/kana/full-width punctuation = 2 columns, an ASCII character = 1). Be concise; never exceed the limit.
- Control codes — copy them exactly, at the same position (start or end): `$NN` (speed) and `~NN` (timed pause), e.g. `$02哇哇！…~40` → `$02Waaah! …~40`. Leading spaces used for indentation may be dropped.
- A trailing `(` or `)` in a source line is an emotion-icon code: keep exactly one such character at the very END of your translation of that unit.
- ASCII double quotes `"…"` in the source mark narration / system text (shown in yellow). Keep ASCII `"` at the start and end of the same narration in the translation. Do not use ASCII `"` for anything else: quoted speech uses 「」 in Japanese and “ ” in English.
- Do not use these ASCII characters anywhere else in a translation, they are control codes in this engine: `@ $ ~ ( )` and backslash. (Apostrophes and hyphens are fine in English. For Japanese use full-width punctuation: 、。！？…「」（）．)
- Stat lines such as `防禦+13　身法-10` keep the same form with the stat names from words_tr.tsv (`Defense+13 Speed-10`, `防御+13　身法-10`).

## Output format — `tr_NN.tsv`, UTF-8, LF, no BOM
One line per non-`#` source line, same order: `id<TAB>ja<TAB>en`. No header, no blank lines, no commentary. Neither translation may contain a TAB or a real line break.

## Style
This is a beloved 1995 wuxia/xianxia RPG: comic in the early chapters, tragic later. Keep the original flavour and every piece of plot information; do not summarise, soften or embellish.
- 日本語: natural game-script Japanese with character voices — 李逍遥: cheeky young man (俺、〜だぜ／〜だろ); 趙霊児: gentle, polite, innocent (わたし、〜です／〜ですわ; calls him 逍遥お兄ちゃん); 林月如: proud tomboyish young lady (あたし、〜よ／〜じゃない); 阿奴: lively child-like Miao girl (あたし、〜だよ); elders and immortals speak in an older register (わし、〜じゃ); villains cold and formal. Chinese idioms and four-character phrases: use the equivalent Japanese idiom where one exists, otherwise translate the meaning. Use shinjitai kanji. Do not use the character ・ (not in the game font); use 、 or ＝ or nothing instead. Use ．． or … for the original ．． pauses. Honorifics: 姑娘 → お嬢さん／〜さん, 公子 → 若様／〜殿, 大俠 → 大侠／〜殿, 前輩 → 先輩／〜殿 as fits.
- English: natural, readable game-script English, wuxia terms kept where idiomatic (Master, Elder, Immortal, sect, qi). ASCII only (no é, ü, …; use ... for pauses, and plain ' and -). Names in pinyin per the glossary. Chinese idioms: use an English idiom of the same meaning, or translate the sense plainly. Forms of address: 姑娘 → Miss, 公子 → young master / sir, 大俠 → hero / sir, 前輩 → Master / Elder, 逍遙哥哥 → Brother Xiaoyao.
- The same source sentence should be translated the same way when it repeats (shop greetings, "得到X" lines: Japanese `Xを手に入れた`, English `Got X`; money: `500文を手に入れた` / `Got 500 coins`).
- Speaker identity comes from the `#` label before a unit. A unit without a preceding label continues the previous speaker or is narration.

## Before you finish
Write and run a Python check on your file: every id of the chunk present exactly once and in order; 3 fields per line; no empty field; `line` units within their width limit in BOTH languages (count columns as defined above, ignoring `$NN`, `~NN`, `"`, and a trailing `(` / `)`); control codes preserved; English is ASCII only; no ・; trailing `(` / `)` preserved; ASCII `"` count equal to the source's. Fix every violation. Then reply with: the file name, the check result, and up to 10 units you were unsure about (id + reason). Keep the reply short.
