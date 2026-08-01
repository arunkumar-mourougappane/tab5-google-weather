/*******************************************************************************
 * Size: 12 px
 * Bpp: 4
 * Opts: --font /var/folders/bq/kb5g0c6s7hq0gck8hf7gq_yw0000gn/T/dashboard_fonts_ttf_cache.BnnRrM4YUx/docs_mockups_assets_fonts_plexmono-400_woff2.ttf --size 12 --bpp 4 --format lvgl --symbols 0123456789° --no-compress --lv-font-name font_plexmono_dayrange_12 -o src/dashboard_fonts/font_plexmono_dayrange_12.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef FONT_PLEXMONO_DAYRANGE_12
#define FONT_PLEXMONO_DAYRANGE_12 1
#endif

#if FONT_PLEXMONO_DAYRANGE_12

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0030 "0" */
    0x2, 0xcd, 0xd3, 0x0, 0xc4, 0x2, 0xe0, 0x2d,
    0x0, 0xa, 0x54, 0xb1, 0xf4, 0x87, 0x4b, 0x3,
    0x8, 0x72, 0xd0, 0x0, 0x95, 0xd, 0x40, 0x2e,
    0x10, 0x2c, 0xdd, 0x40,

    /* U+0031 "1" */
    0x0, 0xae, 0x60, 0x1, 0xb7, 0x96, 0x0, 0x14,
    0x9, 0x60, 0x0, 0x0, 0x96, 0x0, 0x0, 0x9,
    0x60, 0x0, 0x0, 0x96, 0x0, 0x0, 0x9, 0x60,
    0x0, 0xdd, 0xee, 0xd8,

    /* U+0032 "2" */
    0x4, 0xcd, 0xc3, 0x0, 0xe2, 0x3, 0xe0, 0x0,
    0x0, 0xf, 0x0, 0x0, 0x5, 0xc0, 0x0, 0x3,
    0xd2, 0x0, 0x5, 0xc1, 0x0, 0x8, 0xa0, 0x0,
    0x2, 0xfd, 0xdd, 0xd5,

    /* U+0033 "3" */
    0x5, 0xdd, 0xd5, 0x0, 0x71, 0x3, 0xf0, 0x0,
    0x0, 0x4c, 0x0, 0x8, 0xdd, 0x20, 0x0, 0x0,
    0x4d, 0x0, 0x0, 0x0, 0xe2, 0x2a, 0x0, 0x3e,
    0x0, 0x7d, 0xdc, 0x30,

    /* U+0034 "4" */
    0x0, 0x4, 0xf8, 0x0, 0x0, 0xc9, 0x80, 0x0,
    0x77, 0x78, 0x0, 0x1c, 0x7, 0x80, 0xa, 0x40,
    0x78, 0x4, 0xa0, 0x7, 0x80, 0x8d, 0xcc, 0xee,
    0x90, 0x0, 0x7, 0x80,

    /* U+0035 "5" */
    0x9, 0xdd, 0xdd, 0x0, 0xa3, 0x0, 0x0, 0xb,
    0x20, 0x0, 0x0, 0xc7, 0xcc, 0x50, 0x6, 0x30,
    0x2e, 0x20, 0x0, 0x0, 0xa5, 0x9, 0x10, 0x1e,
    0x20, 0x4c, 0xdc, 0x50,

    /* U+0036 "6" */
    0x0, 0x1b, 0x70, 0x0, 0xc, 0x50, 0x0, 0x8,
    0x80, 0x0, 0x0, 0xe7, 0xcc, 0x50, 0x3f, 0x30,
    0x1e, 0x32, 0xd0, 0x0, 0xa6, 0xd, 0x20, 0x1d,
    0x20, 0x3b, 0xcc, 0x50,

    /* U+0037 "7" */
    0x3f, 0xdd, 0xdf, 0x53, 0xb0, 0x0, 0xe1, 0x13,
    0x0, 0x69, 0x0, 0x0, 0xd, 0x20, 0x0, 0x5,
    0xb0, 0x0, 0x0, 0xc4, 0x0, 0x0, 0x3d, 0x0,
    0x0, 0xb, 0x60, 0x0,

    /* U+0038 "8" */
    0x5, 0xdc, 0xd7, 0x0, 0xf1, 0x0, 0xd3, 0xd,
    0x20, 0x1d, 0x10, 0x3e, 0xce, 0x50, 0x1d, 0x20,
    0x1c, 0x34, 0xb0, 0x0, 0x88, 0x2e, 0x10, 0xc,
    0x40, 0x5c, 0xcc, 0x70,

    /* U+0039 "9" */
    0x3, 0xcc, 0xc4, 0x0, 0xe2, 0x1, 0xd1, 0x3d,
    0x0, 0xa, 0x50, 0xe3, 0x1, 0xd6, 0x4, 0xcc,
    0x8d, 0x30, 0x0, 0x5, 0xb0, 0x0, 0x2, 0xd2,
    0x0, 0x4, 0xc2, 0x0,

    /* U+00B0 "°" */
    0x8, 0xb9, 0x5, 0x90, 0x58, 0x59, 0x6, 0x80,
    0x8b, 0x90
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 28, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 56, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 112, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 140, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 168, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 196, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 224, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 252, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 280, .adv_w = 115, .box_w = 5, .box_h = 4, .ofs_x = 1, .ofs_y = 4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 48, .range_length = 10, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 176, .range_length = 1, .glyph_id_start = 11,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 2,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t font_plexmono_dayrange_12 = {
#else
lv_font_t font_plexmono_dayrange_12 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 8,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if FONT_PLEXMONO_DAYRANGE_12*/

