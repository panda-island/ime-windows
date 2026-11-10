# Linguistic data

`shared/traditional_characters.hpp` is generated from OpenCC STCharacters.txt and TWVariants.txt at
fae2a0a911c30764e669a7d8b0591cd8e852934a (Apache-2.0).
https://github.com/BYVoid/OpenCC
Characters also present among traditional alternatives are retained, including
shared characters such as 后, 里 and 干 when the source permits them.
Taiwan standard forms from TWVariants are also treated as traditional, including
群, 峰, 床, 灶, 痴, 粽 and 么, which STCharacters alone otherwise misclassifies.
The classification ranks traditional candidates ahead of simplified-only
candidates without changing their original spelling. The same preference is
applied to frontend candidates, backend wire responses and selection memory.

`phrases.tsv` is derived from McBopomofo BPMFMappings.txt and phrase.occ at
2f50c20521db780729013a59bfbcbdc3e98a387e (MIT).
https://github.com/openvanilla/McBopomofo
The combined dictionary contains readings of 2–8 characters, sorted by frequency with at most
64 alternatives per reading. Traditional words are listed first; simplified
words are retained for manual selection.

The upstream licenses are included in licenses/linguistic and installed
with the application. Regenerate using tools/generate_linguistic_data.py
with checkouts of the revisions above. No network is used at runtime.

Additional word readings come from Terra Pinyin 723e51bc266cf9464530c1ddedb856aa18e3da34,
https://github.com/rime/rime-terra-pinyin . The dictionary identifies CC-CEDICT,
published by MDBG and community contributors, under CC BY-SA 3.0.
https://creativecommons.org/licenses/by-sa/3.0/
The repository also supplies LGPL-3.0 notices and its AUTHORS file, included
under licenses/linguistic. `phrases.tsv` is an adapted combined database
released under CC BY-SA 3.0, with the original MIT and LGPL notices retained.
Changes include pinyin-to-Zhuyin conversion, tone normalization, merging,
traditional-first frequency ordering and length limits.
The generation script takes the OpenCC, McBopomofo and Terra Pinyin checkouts.
