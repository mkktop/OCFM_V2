/**
 * @file    lv_font_sup3_16.c
 * @brief   Montserrat 16 + superscript 3 (U+00B3) fallback font
 * @details Wrapper font containing only the ³ glyph.
 *          All other characters fall through to built-in lv_font_montserrat_16.
 */

#include "lvgl.h"

/*-----------------
 *    BITMAPS
 *----------------*/

static const uint8_t glyph_bitmap[] = {
    /* U+00B3 "³" */
    0x5f, 0xff, 0xfb, 0x0, 0x0, 0x3e, 0x20, 0x0,
    0x2e, 0x40, 0x0, 0x5, 0xce, 0x60, 0x0, 0x0,
    0x2f, 0x3, 0x61, 0x6, 0xe0, 0x3b, 0xef, 0xc3,
    0x0
};

/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}, /* id=0 reserved */
    {.bitmap_index = 0, .adv_w = 110, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 5}  /* ³ */
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const lv_font_fmt_txt_cmap_t cmaps[] = {
    {
        .range_start = 179, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL,
        .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
};

/*-----------------
 *  PUBLIC FONT
 *----------------*/

const lv_font_t my_font_montserrat_16 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 18,       /* match lv_font_montserrat_16 */
    .base_line = 3,          /* match lv_font_montserrat_16 */
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &font_dsc,
    .fallback = &lv_font_montserrat_16
};
