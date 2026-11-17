# Linguistic data

`phrases.tsv` is derived from McBopomofo BPMFMappings.txt and phrase.occ at
2f50c20521db780729013a59bfbcbdc3e98a387e (MIT).
https://github.com/openvanilla/McBopomofo
The combined dictionary contains readings of 2–8 characters, sorted by frequency with at most
64 alternatives per reading.

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
frequency ordering and length limits.
The generation script takes the McBopomofo and Terra Pinyin checkouts.
