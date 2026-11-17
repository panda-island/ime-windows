"""Generate phrase data from pinned sources.

Usage: python tools/generate_linguistic_data.py /path/to/McBopomofo /path/to/rime-terra-pinyin
Sources and licenses are recorded in frontend/data/SOURCES.md.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
mcb, terra = map(Path, sys.argv[1:])
frequencies = {}
for line in (mcb / 'Source/Data/phrase.occ').read_text(encoding='utf-8').splitlines():
    parts = line.split()
    if len(parts) == 2:
        try: frequencies[parts[0]] = float(parts[1])
        except ValueError: pass

def normalize(reading):
    # Our composition appends the light tone and represents first tone as space.
    return reading.lstrip('˙') + ('˙' if reading.startswith('˙') else '')

entries = {}
for line in (mcb / 'Source/Data/BPMFMappings.txt').read_text(encoding='utf-8').splitlines():
    parts = line.split()
    if not parts: continue
    word, *readings = parts
    if not 2 <= len(word) <= 8 or len(word) != len(readings): continue
    key = '|'.join(map(normalize, readings))
    entries.setdefault(key, set()).add(word)
# Terra Pinyin / CC-CEDICT adds vocabulary outside the primary Zhuyin source.
pinyin_to_zhuyin = {}
for line in (mcb / 'Source/Data/BPMFBase.txt').read_text(encoding='utf-8').splitlines():
    parts = line.split()
    if len(parts) < 3: continue
    reading, pinyin = parts[1:3]
    pinyin = pinyin.replace('ü', 'v').replace('u:', 'v')
    if not pinyin[-1:].isdigit(): pinyin += '1'
    pinyin_to_zhuyin.setdefault(pinyin, normalize(reading))
terra_added = 0
for line in (terra / 'terra_pinyin.dict.yaml').read_text(encoding='utf-8').splitlines():
    parts = line.split('\t')
    if len(parts) < 2: continue
    word, pinyin = parts[:2]
    codes = pinyin.replace('ü', 'v').replace('u:', 'v').split()
    if not 2 <= len(word) <= 8 or len(word) != len(codes): continue
    if any(code not in pinyin_to_zhuyin for code in codes): continue
    key = '|'.join(pinyin_to_zhuyin[code] for code in codes)
    bucket = entries.setdefault(key, set())
    if word not in bucket: terra_added += 1
    bucket.add(word)
print(f'Terra Pinyin added {terra_added} additional word readings')
with (root / 'frontend/data/phrases.tsv').open('w', encoding='utf-8', newline='\n') as out:
    for key, words in sorted(entries.items()):
        ordered = sorted(words, key=lambda word: (-frequencies.get(word, 0), word))[:64]
        out.write(key + '\t' + '\t'.join(ordered) + '\n')
(root / 'licenses/linguistic/McBopomofo-MIT.txt').write_text((mcb / 'LICENSE.txt').read_text(encoding='utf-8'), encoding='utf-8')
print(f'{len(entries)} phrase readings')

(root / 'licenses/linguistic/Terra-Pinyin-LGPL.txt').write_text((terra / 'LICENSE').read_text(encoding='utf-8'), encoding='utf-8')
(root / 'licenses/linguistic/Terra-Pinyin-AUTHORS.txt').write_text((terra / 'AUTHORS').read_text(encoding='utf-8'), encoding='utf-8')
