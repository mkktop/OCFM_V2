#!/usr/bin/env python3
"""Merge 3 new glyphs (U+4E32/U+5830/U+8BBE) into noto_sans_sc_16.c font file."""

import re

FILEPATH = r'd:\UGit\OCFM_V2\App\ui\font\noto_sans_sc_16.c'
RANGE_START = 0x4E00


def calc_bitmap_size(box_w, box_h, bpp=4):
    return ((box_w * bpp + 7) // 8) * box_h


def main():
    with open(FILEPATH, 'r', encoding='utf-8') as f:
        content = f.read()

    # --- New glyph data from user's generated file ---
    new_glyphs = [
        {
            'cp': 0x4E32,  # 串
            'bitmap': bytes([
                0x0, 0x0, 0x1, 0x71, 0x0, 0x0, 0x0, 0x0,
                0x0, 0x1, 0xc0, 0x0, 0x0, 0x0, 0x29, 0x77,
                0x78, 0xd7, 0x77, 0x8c, 0x10, 0x3b, 0x0, 0x1,
                0xb0, 0x0, 0x2b, 0x0, 0x3b, 0x0, 0x1, 0xb0,
                0x0, 0x2a, 0x0, 0x3d, 0x66, 0x68, 0xd6, 0x66,
                0x8a, 0x0, 0x39, 0x0, 0x1, 0xb0, 0x0, 0x27,
                0x0, 0x10, 0x0, 0x1, 0xb0, 0x0, 0x1, 0x10,
                0xe7, 0x77, 0x78, 0xd7, 0x77, 0x7b, 0xb0, 0xd0,
                0x0, 0x1, 0xb0, 0x0, 0x6, 0x70, 0xd0, 0x0,
                0x1, 0xb0, 0x0, 0x6, 0x70, 0xe7, 0x77, 0x78,
                0xd7, 0x77, 0x7a, 0x70, 0xa0, 0x0, 0x1, 0xb0,
                0x0, 0x4, 0x30, 0x0, 0x0, 0x1, 0xb0, 0x0,
                0x0, 0x0, 0x0, 0x0, 0x1, 0xa0, 0x0, 0x0,
                0x0,
            ]),
            'adv_w': 256, 'box_w': 14, 'box_h': 15, 'ofs_x': 2, 'ofs_y': -1,
        },
        {
            'cp': 0x5830,  # 堰
            'bitmap': bytes([
                0x0, 0x1a, 0x0, 0x40, 0x0, 0x0, 0x3, 0x70,
                0x0, 0x2a, 0x0, 0xe7, 0x77, 0x77, 0x77, 0x71,
                0x0, 0x2a, 0x0, 0xd0, 0xa7, 0x77, 0x7d, 0x30,
                0x0, 0x2a, 0x51, 0xd0, 0xc0, 0x0, 0xc, 0x0,
                0x7, 0x8d, 0x96, 0xd0, 0xc7, 0x77, 0x7e, 0x0,
                0x0, 0x2a, 0x0, 0xd0, 0xc7, 0x77, 0x7e, 0x0,
                0x0, 0x2a, 0x0, 0xd0, 0xa0, 0x50, 0x9, 0x0,
                0x0, 0x2a, 0x0, 0xd4, 0x7a, 0xc7, 0x79, 0xd0,
                0x0, 0x2a, 0x0, 0xd0, 0x1c, 0x0, 0xc2, 0x0,
                0x0, 0x3d, 0x83, 0xd0, 0x79, 0x37, 0x80, 0x0,
                0xc, 0xc4, 0x0, 0xd0, 0x2, 0xde, 0xa2, 0x0,
                0x3, 0x0, 0x0, 0xd3, 0x67, 0x20, 0x5b, 0x10,
                0x0, 0x0, 0x2, 0xe7, 0x77, 0x77, 0x77, 0xf4,
                0x0, 0x0, 0x0, 0x40, 0x0, 0x0, 0x0, 0x0,
            ]),
            'adv_w': 256, 'box_w': 16, 'box_h': 14, 'ofs_x': 0, 'ofs_y': -1,
        },
        {
            'cp': 0x8BBE,  # 设
            'bitmap': bytes([
                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                0x0, 0x94, 0x0, 0x8, 0x87, 0x77, 0xc1, 0x0,
                0x0, 0xd, 0x40, 0xb, 0x20, 0x0, 0xe0, 0x0,
                0x0, 0x4, 0x60, 0xc, 0x10, 0x0, 0xd0, 0x0,
                0x0, 0x4, 0x0, 0xd, 0x0, 0x0, 0xd0, 0x0,
                0x7, 0x7e, 0x10, 0x67, 0x0, 0x0, 0xd0, 0x0,
                0x0, 0xd, 0x3, 0x80, 0x0, 0x0, 0x9b, 0xb6,
                0x0, 0xd, 0x4, 0x77, 0x77, 0x77, 0x7c, 0x20,
                0x0, 0xd, 0x0, 0x5, 0x0, 0x0, 0x6a, 0x0,
                0x0, 0xd, 0x0, 0x7, 0x10, 0x1, 0xd1, 0x0,
                0x0, 0xd, 0x0, 0x11, 0xa0, 0xb, 0x50, 0x0,
                0x0, 0xd, 0x37, 0x0, 0x4c, 0xb5, 0x0, 0x0,
                0x0, 0x1f, 0x90, 0x0, 0x4d, 0xd7, 0x0, 0x0,
                0x0, 0x49, 0x0, 0x29, 0x70, 0x19, 0xe9, 0x40,
                0x0, 0x0, 0x46, 0x51, 0x0, 0x0, 0x27, 0xc8,
                0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
            ]),
            'adv_w': 256, 'box_w': 16, 'box_h': 16, 'ofs_x': 0, 'ofs_y': -2,
        },
    ]

    # --- Parse existing arrays ---
    print("Parsing existing font file...")

    bm_match = re.search(
        r'(static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap\[\] = \{)(.*?)(\};)',
        content, re.DOTALL)
    assert bm_match, "Could not find glyph_bitmap"
    existing_bitmap = bytearray(
        int(h, 16) for h in re.findall(r'0x([0-9a-fA-F]+)', bm_match.group(2)))
    print(f"  Existing bitmap: {len(existing_bitmap)} bytes")

    dsc_match = re.search(
        r'(static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc\[\] = \{)(.*?)(\};)',
        content, re.DOTALL)
    assert dsc_match, "Could not find glyph_dsc"
    dsc_re = re.compile(
        r'\{\.bitmap_index\s*=\s*(\d+),\s*\.adv_w\s*=\s*(\d+),\s*'
        r'\.box_w\s*=\s*(\d+),\s*\.box_h\s*=\s*(\d+),\s*'
        r'\.ofs_x\s*=\s*(-?\d+),\s*\.ofs_y\s*=\s*(-?\d+)\}')
    dsc_list = [(int(m[0]),int(m[1]),int(m[2]),int(m[3]),int(m[4]),int(m[5]))
                for m in dsc_re.findall(dsc_match.group(2))]
    print(f"  Existing glyphs: {len(dsc_list) - 1}")

    ul_match = re.search(
        r'(static const uint16_t unicode_list_0\[\] = \{)(.*?)(\};)',
        content, re.DOTALL)
    assert ul_match, "Could not find unicode_list_0"
    ul_values = [int(h, 16) for h in re.findall(r'0x([0-9a-fA-F]+)', ul_match.group(2))]
    print(f"  Existing unicode entries: {len(ul_values)}")

    # --- Rebuild glyph list ---
    glyphs = []
    for i in range(len(ul_values)):
        bi, aw, bw, bh, ox, oy = dsc_list[i + 1]
        bsz = calc_bitmap_size(bw, bh)
        glyphs.append({
            'offset': ul_values[i],
            'bitmap': bytes(existing_bitmap[bi:bi+bsz]),
            'adv_w': aw, 'box_w': bw, 'box_h': bh, 'ofs_x': ox, 'ofs_y': oy,
            'is_new': False,
        })

    for ng in new_glyphs:
        offset = ng['cp'] - RANGE_START
        assert offset not in [g['offset'] for g in glyphs], \
            f"U+{ng['cp']:04X} already exists!"
        glyphs.append({
            'offset': offset,
            'bitmap': ng['bitmap'],
            'adv_w': ng['adv_w'], 'box_w': ng['box_w'], 'box_h': ng['box_h'],
            'ofs_x': ng['ofs_x'], 'ofs_y': ng['ofs_y'],
            'is_new': True, 'cp': ng['cp'],
        })

    glyphs.sort(key=lambda g: g['offset'])

    # --- Rebuild arrays ---
    new_bitmap = bytearray()
    new_dsc = [(0, 0, 0, 0, 0, 0)]
    new_ul = []

    for g in glyphs:
        bi = len(new_bitmap)
        new_bitmap.extend(g['bitmap'])
        new_dsc.append((bi, g['adv_w'], g['box_w'], g['box_h'], g['ofs_x'], g['ofs_y']))
        new_ul.append(g['offset'])

    print(f"  New total: {len(new_ul)} glyphs, {len(new_bitmap)} bitmap bytes")

    # --- Format C code ---
    # Bitmap: 8 bytes per line
    bm_lines = []
    for i in range(0, len(new_bitmap), 8):
        row = new_bitmap[i:i+8]
        bm_lines.append('    ' + ', '.join(f'0x{b:x}' for b in row))
    bm_text = ',\n'.join(bm_lines)
    new_bm_section = bm_match.group(1) + '\n' + bm_text + '\n' + bm_match.group(3)

    # Glyph descriptions: 1 per line
    dsc_lines = [
        '    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,'
    ]
    for i, (bi, aw, bw, bh, ox, oy) in enumerate(new_dsc[1:]):
        comment = ''
        if glyphs[i].get('is_new'):
            comment = f' /* U+{glyphs[i]["cp"]:04X} */'
        dsc_lines.append(
            f'    {{.bitmap_index = {bi}, .adv_w = {aw}, .box_w = {bw}, '
            f'.box_h = {bh}, .ofs_x = {ox}, .ofs_y = {oy}}}{comment},')
    dsc_lines[-1] = dsc_lines[-1].rstrip(',')
    dsc_text = '\n'.join(dsc_lines)
    new_dsc_section = dsc_match.group(1) + '\n' + dsc_text + '\n' + dsc_match.group(3)

    # Unicode list: 8 values per line
    ul_lines = []
    for i in range(0, len(new_ul), 8):
        row = new_ul[i:i+8]
        ul_lines.append('    ' + ', '.join(f'0x{v:x}' for v in row))
    ul_text = ',\n'.join(ul_lines)
    new_ul_section = ul_match.group(1) + '\n' + ul_text + '\n' + ul_match.group(3)

    # --- Replace sections (bottom to top to preserve positions) ---
    content = content[:ul_match.start()] + new_ul_section + content[ul_match.end():]
    content = content[:dsc_match.start()] + new_dsc_section + content[dsc_match.end():]
    content = content[:bm_match.start()] + new_bm_section + content[bm_match.end():]

    # Update list_length
    content = content.replace('.list_length = 103,', '.list_length = 106,')

    # --- Write result ---
    with open(FILEPATH, 'w', encoding='utf-8') as f:
        f.write(content)

    print("Done! Merged 3 new glyphs successfully.")

    # --- Verify ---
    for ng in new_glyphs:
        offset = ng['cp'] - RANGE_START
        assert offset in new_ul, f"FAIL: {chr(ng['cp'])} not in unicode list"
        idx = new_ul.index(offset)
        bi = new_dsc[idx + 1][0]
        bsz = calc_bitmap_size(ng['box_w'], ng['box_h'])
        assert new_bitmap[bi:bi+bsz] == ng['bitmap'], \
            f"FAIL: bitmap mismatch for {chr(ng['cp'])}"
        print(f"  Verified U+{ng['cp']:04X} '{chr(ng['cp'])}' at glyph_id {idx+1}")


if __name__ == '__main__':
    main()
