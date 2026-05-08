import re, sys
sys.stdout.reconfigure(encoding='utf-8')

with open('App/ui/font/chars.txt', 'r', encoding='utf-8') as f:
    chars_txt = f.read().strip()

with open('App/ui/ui_lang.c', 'r', encoding='utf-8') as f:
    lang_content = f.read()

# Extract hex-escaped CJK strings like "\xe5\x9f\xba\xe6\x9c\xac"
hex_strings = re.findall(r'"((?:\\x[0-9a-fA-F]{2})+)"', lang_content)

all_cjk = set()
for hs in hex_strings:
    # Decode the \x sequences
    decoded = bytes(hs, 'ascii').decode('unicode-escape').encode('latin-1').decode('utf-8')
    for ch in decoded:
        if '一' <= ch <= '鿿':
            all_cjk.add(ch)

missing = []
for ch in sorted(all_cjk):
    if ch not in chars_txt:
        missing.append(ch)

if missing:
    print('Missing in UI strings:')
    for ch in missing:
        for hs in hex_strings:
            decoded = bytes(hs, 'ascii').decode('unicode-escape').encode('latin-1').decode('utf-8')
            if ch in decoded:
                print(f'  {ch} (U+{ord(ch):04X}) in "{decoded}"')
                break
    print(f'Total missing: {len(missing)}')
else:
    print('All UI CJK chars present in chars.txt')
print(f'Unique CJK chars in UI: {len(all_cjk)}')
