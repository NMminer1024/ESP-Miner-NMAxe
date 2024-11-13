/*******************************************************************************
 * Size: 16 px
 * Bpp: 4
 * Opts: --bpp 4 --size 16 --no-compress --font DS_DIGIB.TTF --range 32-127 --format lvgl -o ds_digib_font_16.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef DS_DIGIB_FONT_16
#define DS_DIGIB_FONT_16 1
#endif

#if DS_DIGIB_FONT_16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x4, 0x3, 0xf0, 0x7f, 0x7, 0xf0, 0x2b, 0x0,
    0x30, 0x5f, 0x7, 0xf0, 0x7f, 0x0, 0xb0, 0x1,
    0x7, 0xf0, 0x14, 0x0,

    /* U+0022 "\"" */
    0x5f, 0x7f, 0x44, 0xf7, 0xf4, 0x7, 0x8, 0x0,

    /* U+0023 "#" */
    0x0, 0x3, 0x0, 0x20, 0x0, 0x4, 0xf2, 0x7e,
    0x0, 0x1, 0x4f, 0x37, 0xe1, 0x0, 0xde, 0x3e,
    0xa5, 0xf8, 0x3, 0x5d, 0x57, 0xc4, 0x10, 0x14,
    0xf3, 0x7e, 0x20, 0xd, 0xe3, 0xea, 0x5f, 0x90,
    0x35, 0xd5, 0x7c, 0x41, 0x0, 0x4f, 0x28, 0xf0,
    0x0, 0x0, 0x30, 0x3, 0x0,

    /* U+0024 "$" */
    0x0, 0x2, 0x0, 0x0, 0x0, 0xc, 0x90, 0x0,
    0x0, 0x6, 0x50, 0x0, 0x0, 0xcf, 0xff, 0xe1,
    0xb, 0x78, 0x88, 0x60, 0x2f, 0x50, 0x0, 0x0,
    0x2f, 0x42, 0x21, 0x0, 0x3, 0xff, 0xff, 0x30,
    0x0, 0x35, 0x55, 0xc3, 0x0, 0x0, 0x3, 0xf4,
    0x0, 0x0, 0x3, 0xf4, 0x5, 0x77, 0x76, 0xb1,
    0xd, 0xff, 0xfe, 0x0, 0x0, 0x6, 0x50, 0x0,
    0x0, 0xd, 0x90, 0x0, 0x0, 0x2, 0x10, 0x0,

    /* U+0025 "%" */
    0x0, 0x0, 0x0, 0x0, 0x55, 0x0, 0xc, 0xd0,
    0x0, 0xaf, 0x20, 0xb, 0x26, 0xb1, 0x3f, 0x80,
    0x2, 0xf5, 0x4f, 0x4c, 0xe0, 0x0, 0xa, 0x27,
    0xa3, 0xf6, 0x0, 0x0, 0xc, 0xd0, 0x8, 0x0,
    0x0, 0x0, 0x0, 0x5a, 0x4, 0x60, 0x0, 0x0,
    0x2f, 0xa3, 0xbe, 0x40, 0x0, 0xb, 0xe0, 0xf6,
    0x2f, 0x40, 0x4, 0xf6, 0xb, 0x7, 0xb2, 0x0,
    0x8c, 0x0, 0xb, 0xe1, 0x0, 0x4, 0x30, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,

    /* U+0026 "&" */
    0x0, 0xcf, 0xfd, 0x0, 0x0, 0xb7, 0x88, 0x50,
    0x0, 0x2f, 0x50, 0x0, 0x70, 0x2, 0xf4, 0x22,
    0x3f, 0x40, 0x3, 0xff, 0xff, 0x5b, 0x41, 0xd5,
    0x55, 0x5c, 0x40, 0x2f, 0x50, 0x3, 0xf4, 0x2,
    0xf5, 0x0, 0x3f, 0x40, 0xb, 0x67, 0x76, 0xb1,
    0x0, 0xc, 0xff, 0xe0, 0x0,

    /* U+0027 "'" */
    0x5f, 0x34, 0xf3, 0x7, 0x0,

    /* U+0028 "(" */
    0x1, 0xef, 0x31, 0xb7, 0x30, 0x5f, 0x30, 0x5,
    0xf2, 0x0, 0x16, 0x0, 0x1, 0x60, 0x0, 0x4f,
    0x20, 0x4, 0xf3, 0x0, 0x1b, 0x43, 0x0, 0x1e,
    0xf3,

    /* U+0029 ")" */
    0x3f, 0xe1, 0x0, 0x37, 0xb2, 0x0, 0x2f, 0x50,
    0x2, 0xf5, 0x0, 0x6, 0x10, 0x0, 0x61, 0x0,
    0x2f, 0x50, 0x2, 0xf5, 0x3, 0x7b, 0x23, 0xee,
    0x10,

    /* U+002A "*" */
    0x0, 0x8, 0x50, 0x0, 0x4, 0xd, 0xa1, 0x40,
    0x2f, 0xda, 0x8f, 0xe0, 0x3, 0x74, 0x57, 0x20,
    0xb, 0xf8, 0x5f, 0x80, 0x2c, 0x6c, 0x98, 0xb0,
    0x0, 0xd, 0x90, 0x0, 0x0, 0x2, 0x0, 0x0,

    /* U+002B "+" */
    0x0, 0x2, 0x0, 0x0, 0x0, 0xf7, 0x0, 0x1,
    0x1e, 0x72, 0x1, 0xff, 0x5b, 0xf9, 0x3, 0x4c,
    0x75, 0x10, 0x0, 0xf8, 0x0, 0x0, 0x4, 0x10,
    0x0,

    /* U+002C "," */
    0x5f, 0x34, 0xf3, 0x7, 0x0,

    /* U+002D "-" */
    0x1, 0x22, 0x22, 0x1, 0xff, 0xff, 0xf9, 0x3,
    0x55, 0x55, 0x10,

    /* U+002E "." */
    0x27, 0x15, 0xf3,

    /* U+002F "/" */
    0x0, 0x0, 0x0, 0x42, 0x0, 0x0, 0xd, 0xd0,
    0x0, 0x0, 0x7f, 0x50, 0x0, 0x1, 0xfb, 0x0,
    0x0, 0x6, 0xf2, 0x0, 0x0, 0x1, 0x40, 0x0,
    0x0, 0xaa, 0x0, 0x0, 0x6, 0xf5, 0x0, 0x0,
    0x1e, 0xc0, 0x0, 0x0, 0x8f, 0x20, 0x0, 0x0,
    0x69, 0x0, 0x0, 0x0, 0x11, 0x0, 0x0, 0x0,

    /* U+0030 "0" */
    0x18, 0xff, 0xff, 0x92, 0x2e, 0x78, 0x87, 0xd4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x7, 0x0, 0x0, 0x61, 0x7, 0x0, 0x0, 0x61,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2e, 0x77, 0x76, 0xd4, 0x18, 0xff, 0xff, 0x92,

    /* U+0031 "1" */
    0x3, 0x3, 0xf0, 0x7f, 0x7, 0xf0, 0x7, 0x0,
    0x70, 0x7f, 0x7, 0xf0, 0x3f, 0x0, 0x30,

    /* U+0032 "2" */
    0x5, 0xff, 0xff, 0x92, 0x0, 0x48, 0x87, 0xd4,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x12, 0x23, 0xf4,
    0x1, 0xff, 0xff, 0x40, 0x1d, 0x55, 0x54, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x2e, 0x67, 0x75, 0x0, 0x18, 0xff, 0xff, 0x80,

    /* U+0033 "3" */
    0x5f, 0xff, 0xf9, 0x20, 0x48, 0x87, 0xd4, 0x0,
    0x0, 0x3f, 0x40, 0x12, 0x23, 0xf4, 0x1f, 0xff,
    0xf5, 0x0, 0x35, 0x55, 0xc3, 0x0, 0x0, 0x3f,
    0x40, 0x0, 0x3, 0xf4, 0x4, 0x77, 0x6d, 0x45,
    0xff, 0xff, 0x92,

    /* U+0034 "4" */
    0x0, 0x0, 0x0, 0x0, 0x27, 0x0, 0x0, 0x54,
    0x2f, 0x40, 0x2, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x42, 0x23, 0xe3, 0x2, 0xff, 0xff, 0x40,
    0x0, 0x35, 0x55, 0xd3, 0x0, 0x0, 0x3, 0xf4,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x0, 0x0, 0x84,
    0x0, 0x0, 0x0, 0x0,

    /* U+0035 "5" */
    0x18, 0xff, 0xff, 0x80, 0x2e, 0x78, 0x86, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x42, 0x21, 0x0,
    0x3, 0xff, 0xff, 0x30, 0x0, 0x35, 0x55, 0xc3,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x0, 0x3, 0xf4,
    0x0, 0x47, 0x76, 0xd4, 0x5, 0xff, 0xff, 0x92,

    /* U+0036 "6" */
    0x18, 0xff, 0xff, 0x80, 0x2e, 0x78, 0x86, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x42, 0x21, 0x0,
    0x3, 0xff, 0xff, 0x30, 0x1d, 0x55, 0x55, 0xc3,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2e, 0x67, 0x76, 0xd4, 0x18, 0xff, 0xff, 0x92,

    /* U+0037 "7" */
    0x5f, 0xff, 0xf9, 0x20, 0x48, 0x87, 0xd4, 0x0,
    0x0, 0x3f, 0x40, 0x0, 0x3, 0xf4, 0x0, 0x0,
    0x6, 0x10, 0x0, 0x0, 0x61, 0x0, 0x0, 0x3f,
    0x40, 0x0, 0x3, 0xf4, 0x0, 0x0, 0x1d, 0x40,
    0x0, 0x0, 0x12,

    /* U+0038 "8" */
    0x18, 0xff, 0xff, 0x92, 0x2e, 0x78, 0x87, 0xd4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x42, 0x23, 0xf4,
    0x3, 0xff, 0xff, 0x50, 0x1d, 0x55, 0x55, 0xc3,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2e, 0x67, 0x76, 0xd4, 0x18, 0xff, 0xff, 0x92,

    /* U+0039 "9" */
    0x18, 0xff, 0xff, 0x92, 0x2e, 0x78, 0x87, 0xd4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x42, 0x23, 0xf4,
    0x3, 0xff, 0xff, 0x50, 0x0, 0x35, 0x55, 0xc3,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x0, 0x3, 0xf4,
    0x0, 0x47, 0x76, 0xd4, 0x5, 0xff, 0xff, 0x92,

    /* U+003A ":" */
    0x21, 0xf8, 0x52, 0x0, 0x0, 0x0, 0x0, 0x74,
    0xf8,

    /* U+003B ";" */
    0x21, 0xf8, 0x52, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xa5, 0xf8, 0xb5, 0x0,

    /* U+003C "<" */
    0x0, 0x4, 0xb0, 0x0, 0x4f, 0xc0, 0x5, 0xfc,
    0x0, 0x19, 0x90, 0x0, 0xa, 0xf7, 0x0, 0x0,
    0x9f, 0x80, 0x0, 0x9, 0xf0,

    /* U+003D "=" */
    0x1e, 0xff, 0xff, 0x80, 0x68, 0x88, 0x82, 0x6,
    0x88, 0x88, 0x21, 0xef, 0xff, 0xf8,

    /* U+003E ">" */
    0x3a, 0x10, 0x0, 0x2f, 0xd1, 0x0, 0x3, 0xee,
    0x20, 0x0, 0x39, 0x70, 0x0, 0xbf, 0x60, 0xc,
    0xf5, 0x0, 0x5f, 0x50, 0x0,

    /* U+003F "?" */
    0xa, 0xff, 0xfd, 0x0, 0x0, 0x68, 0x87, 0xb1,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x12, 0x23, 0xf4,
    0x1, 0xff, 0xff, 0x40, 0x1d, 0x55, 0x54, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x2e, 0x20, 0x0, 0x0, 0x13, 0x0, 0x0, 0x0,
    0x19, 0x30, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,

    /* U+0040 "@" */
    0x0, 0xcf, 0xff, 0xff, 0x80, 0xb, 0x78, 0x88,
    0x87, 0xf0, 0x2f, 0x50, 0x38, 0x87, 0xb0, 0x2f,
    0x43, 0xaf, 0xfc, 0x30, 0x5, 0xc, 0x90, 0x6,
    0xf0, 0x9, 0xb, 0xb0, 0x7, 0xf0, 0x2f, 0x51,
    0x8f, 0xfb, 0xb0, 0x2f, 0x50, 0x17, 0x88, 0x30,
    0xb, 0x67, 0x77, 0x77, 0x50, 0x0, 0xcf, 0xff,
    0xff, 0xe1,

    /* U+0041 "A" */
    0x18, 0xff, 0xff, 0x92, 0x2e, 0x78, 0x87, 0xd4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x42, 0x23, 0xf4,
    0x3, 0xff, 0xff, 0x50, 0x1d, 0x55, 0x55, 0xd3,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2d, 0x10, 0x0, 0xc4, 0x11, 0x0, 0x0, 0x1,

    /* U+0042 "B" */
    0x18, 0xff, 0xfd, 0x0, 0x2e, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x42, 0x23, 0xf4,
    0x3, 0xff, 0xff, 0x50, 0x1d, 0x55, 0x55, 0xc3,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2e, 0x67, 0x76, 0xb1, 0x18, 0xff, 0xfe, 0x0,

    /* U+0043 "C" */
    0x0, 0xcf, 0xff, 0xb0, 0xb, 0x78, 0x87, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x7, 0x0, 0x0, 0x0, 0x7, 0x0, 0x0, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0xb, 0x27, 0x77, 0x0, 0x0, 0xcf, 0xff, 0xb0,

    /* U+0044 "D" */
    0x18, 0xff, 0xfd, 0x0, 0x2e, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x7, 0x0, 0x0, 0x61, 0x7, 0x0, 0x0, 0x61,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2e, 0x77, 0x76, 0xb1, 0x18, 0xff, 0xfe, 0x0,

    /* U+0045 "E" */
    0x18, 0xff, 0xff, 0xb0, 0x2e, 0x78, 0x87, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x42, 0x21, 0x0,
    0x3, 0xff, 0xff, 0x20, 0x1d, 0x55, 0x54, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x2e, 0x67, 0x77, 0x0, 0x18, 0xff, 0xff, 0xb0,

    /* U+0046 "F" */
    0x18, 0xff, 0xff, 0xb0, 0x2e, 0x78, 0x87, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x42, 0x21, 0x0,
    0x3, 0xff, 0xff, 0x20, 0x1d, 0x55, 0x54, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x2a, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,

    /* U+0047 "G" */
    0x0, 0xcf, 0xff, 0xb0, 0xb, 0x78, 0x87, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x40, 0x0, 0x0,
    0x3, 0x7, 0xff, 0x20, 0x1b, 0x12, 0x87, 0xa2,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0xb, 0x27, 0x74, 0xb1, 0x0, 0xcf, 0xfe, 0x10,

    /* U+0048 "H" */
    0x0, 0x0, 0x0, 0x1, 0x2b, 0x0, 0x0, 0xa4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x42, 0x23, 0xe3, 0x3, 0xff, 0xff, 0x40,
    0x1d, 0x55, 0x55, 0xd3, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x50, 0x3, 0xf4, 0x2d, 0x10, 0x0, 0xc4,
    0x11, 0x0, 0x0, 0x1,

    /* U+0049 "I" */
    0x7, 0x5, 0xf0, 0x7f, 0x7, 0xf0, 0x7, 0x0,
    0x70, 0x7f, 0x7, 0xf0, 0x5f, 0x0, 0x70,

    /* U+004A "J" */
    0x0, 0x0, 0x0, 0x43, 0x0, 0x0, 0x2, 0xf4,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x0, 0x3, 0xf4,
    0x0, 0x0, 0x0, 0x61, 0x0, 0x0, 0x0, 0x61,
    0x1c, 0x10, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0xb, 0x27, 0x76, 0xb1, 0x0, 0xcf, 0xfe, 0x0,

    /* U+004B "K" */
    0x26, 0x0, 0x2d, 0xa0, 0x2f, 0x43, 0xee, 0x20,
    0x2f, 0x8f, 0xd1, 0x0, 0x2f, 0x69, 0x31, 0x0,
    0x3, 0xff, 0xff, 0x30, 0x1d, 0x55, 0x55, 0xd3,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2d, 0x10, 0x0, 0xc4, 0x11, 0x0, 0x0, 0x1,

    /* U+004C "L" */
    0x25, 0x0, 0x0, 0x0, 0x2f, 0x30, 0x0, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x7, 0x0, 0x0, 0x0, 0x7, 0x0, 0x0, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x2e, 0x77, 0x77, 0x0, 0x18, 0xff, 0xff, 0xb0,

    /* U+004D "M" */
    0x18, 0xff, 0xff, 0x92, 0x2e, 0x78, 0x87, 0xd4,
    0x2f, 0x57, 0xb3, 0xf4, 0x2f, 0x5a, 0xe3, 0xf4,
    0x7, 0xa, 0xe0, 0x61, 0x7, 0x8, 0xc0, 0x61,
    0x2f, 0x50, 0x13, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x30, 0x2, 0xf4, 0x25, 0x0, 0x0, 0x43,

    /* U+004E "N" */
    0x2a, 0xfd, 0x10, 0x43, 0x2f, 0x8f, 0xd3, 0xf4,
    0x2f, 0x53, 0xfa, 0xf4, 0x2f, 0x40, 0x2, 0xf4,
    0x5, 0x0, 0x0, 0x50, 0x9, 0x0, 0x0, 0x81,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x30, 0x1, 0xe4, 0x14, 0x0, 0x0, 0x23,

    /* U+004F "O" */
    0x0, 0xcf, 0xfd, 0x0, 0xb, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x7, 0x0, 0x0, 0x61, 0x7, 0x0, 0x0, 0x61,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0xb, 0x27, 0x76, 0xb1, 0x0, 0xcf, 0xfe, 0x0,

    /* U+0050 "P" */
    0x18, 0xff, 0xfd, 0x0, 0x2e, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x42, 0x23, 0xf4,
    0x3, 0xff, 0xff, 0x40, 0x1d, 0x55, 0x54, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x2d, 0x10, 0x0, 0x0, 0x11, 0x0, 0x0, 0x0,

    /* U+0051 "Q" */
    0x0, 0xcf, 0xfd, 0x0, 0xb, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x40, 0x2, 0xf4,
    0x4, 0x0, 0x0, 0x30, 0x1a, 0x0, 0x0, 0x92,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x8b, 0x70,
    0xb, 0x27, 0x7f, 0xc0, 0x0, 0xcf, 0x95, 0xf4,

    /* U+0052 "R" */
    0x18, 0xff, 0xfd, 0x0, 0x2e, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x42, 0x23, 0xf4,
    0x3, 0xff, 0xff, 0x40, 0x1d, 0x59, 0x64, 0x0,
    0x2f, 0x5f, 0xc0, 0x0, 0x2f, 0x55, 0xfc, 0x0,
    0x2f, 0x30, 0x6f, 0xb0, 0x25, 0x0, 0x8, 0xf0,

    /* U+0053 "S" */
    0x0, 0xcf, 0xff, 0xe1, 0xb, 0x78, 0x88, 0x60,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x42, 0x21, 0x0,
    0x3, 0xff, 0xff, 0x30, 0x0, 0x35, 0x55, 0xc3,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x0, 0x3, 0xf4,
    0x5, 0x77, 0x76, 0xb1, 0xd, 0xff, 0xfe, 0x0,

    /* U+0054 "T" */
    0x3f, 0xff, 0xff, 0xf5, 0x7, 0x88, 0x88, 0x71,
    0x0, 0x7, 0x90, 0x0, 0x0, 0xa, 0xd0, 0x0,
    0x0, 0x1, 0x60, 0x0, 0x0, 0x1, 0x60, 0x0,
    0x0, 0xa, 0xd0, 0x0, 0x0, 0xb, 0xd0, 0x0,
    0x0, 0xb, 0xd0, 0x0, 0x0, 0x7, 0x90, 0x0,
    0x0, 0x0, 0x0, 0x0,

    /* U+0055 "U" */
    0x25, 0x0, 0x0, 0x43, 0x2f, 0x30, 0x2, 0xf4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x7, 0x0, 0x0, 0x61, 0x7, 0x0, 0x0, 0x61,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0xb, 0x27, 0x76, 0xb1, 0x0, 0xcf, 0xfe, 0x0,

    /* U+0056 "V" */
    0x0, 0x0, 0x0, 0x0, 0x29, 0x0, 0x0, 0x74,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x30, 0x1, 0xe3, 0x1, 0x0, 0x0, 0x20,
    0x1d, 0x20, 0xc, 0xa0, 0x2f, 0x50, 0xbf, 0x40,
    0x2f, 0x6b, 0xf5, 0x0, 0x2f, 0x8f, 0x60, 0x0,
    0x8, 0x10, 0x0, 0x0,

    /* U+0057 "W" */
    0x25, 0x0, 0x0, 0x43, 0x2f, 0x30, 0x2, 0xf4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x7, 0x8, 0xc0, 0x61, 0x7, 0xa, 0xe0, 0x61,
    0x2f, 0x5a, 0xe3, 0xf4, 0x2f, 0x57, 0xb3, 0xf4,
    0x2e, 0x77, 0x86, 0xd4, 0x18, 0xff, 0xff, 0x92,

    /* U+0058 "X" */
    0x2f, 0x10, 0x0, 0xd4, 0x1f, 0xb0, 0x9, 0xf3,
    0x7, 0xf6, 0x4f, 0x90, 0x0, 0xcc, 0xae, 0x0,
    0x0, 0x29, 0x74, 0x0, 0x0, 0x29, 0x74, 0x0,
    0x0, 0xcc, 0xae, 0x0, 0x7, 0xf6, 0x4f, 0x90,
    0x1f, 0xb0, 0x9, 0xf2, 0x2f, 0x20, 0x0, 0xd4,

    /* U+0059 "Y" */
    0x0, 0x0, 0x0, 0x1, 0x2b, 0x0, 0x0, 0x94,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x12, 0x23, 0xe4, 0x0, 0xff, 0xff, 0x40,
    0x0, 0x35, 0x55, 0xc3, 0x0, 0x0, 0x3, 0xf4,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x77, 0x76, 0xb1,
    0x4, 0xff, 0xfe, 0x0,

    /* U+005A "Z" */
    0xa, 0xff, 0xff, 0xe1, 0x0, 0x68, 0x89, 0x70,
    0x0, 0x0, 0x1e, 0x80, 0x0, 0x0, 0xbf, 0x20,
    0x0, 0x0, 0xc6, 0x0, 0x0, 0x4c, 0x0, 0x0,
    0x1, 0xed, 0x0, 0x0, 0x6, 0xf3, 0x0, 0x0,
    0x6, 0x97, 0x76, 0x0, 0xd, 0xff, 0xff, 0x90,

    /* U+005B "[" */
    0x29, 0xff, 0x35, 0xd7, 0x30, 0x5f, 0x30, 0x5,
    0xf2, 0x0, 0x16, 0x0, 0x1, 0x60, 0x0, 0x5f,
    0x20, 0x5, 0xf3, 0x0, 0x5d, 0x73, 0x2, 0x9f,
    0xf3,

    /* U+005C "\\" */
    0x33, 0x0, 0x0, 0x0, 0x1f, 0xb0, 0x0, 0x0,
    0x7, 0xf4, 0x0, 0x0, 0x0, 0xdd, 0x0, 0x0,
    0x0, 0x4f, 0x30, 0x0, 0x0, 0x5, 0x0, 0x0,
    0x0, 0x0, 0xb8, 0x0, 0x0, 0x0, 0x7f, 0x40,
    0x0, 0x0, 0xd, 0xd0, 0x0, 0x0, 0x4, 0xf5,
    0x0, 0x0, 0x0, 0xb3, 0x0, 0x0, 0x0, 0x20,

    /* U+005D "]" */
    0x3f, 0xfa, 0x30, 0x37, 0xd5, 0x0, 0x2f, 0x50,
    0x2, 0xf5, 0x0, 0x6, 0x10, 0x0, 0x61, 0x0,
    0x2f, 0x50, 0x2, 0xf5, 0x3, 0x7d, 0x53, 0xef,
    0x93,

    /* U+005E "^" */
    0x0, 0x6, 0x40, 0x0, 0xc, 0x9f, 0x40, 0x1c,
    0xf4, 0xcf, 0x45, 0xf4, 0x1, 0xcb,

    /* U+005F "_" */
    0x2, 0x22, 0x22, 0x20, 0xbf, 0xff, 0xff, 0xf4,
    0x25, 0x55, 0x55, 0x40,

    /* U+0060 "`" */
    0x1, 0x0, 0x1f, 0x70, 0xb, 0xf1, 0x1, 0x90,

    /* U+0061 "a" */
    0x18, 0xff, 0xff, 0x92, 0x2e, 0x78, 0x87, 0xd4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x42, 0x23, 0xf4,
    0x3, 0xff, 0xff, 0x50, 0x1d, 0x55, 0x55, 0xd3,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2d, 0x10, 0x0, 0xc4, 0x11, 0x0, 0x0, 0x1,

    /* U+0062 "b" */
    0x18, 0xff, 0xfd, 0x0, 0x2e, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x42, 0x23, 0xf4,
    0x3, 0xff, 0xff, 0x50, 0x1d, 0x55, 0x55, 0xc3,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2e, 0x67, 0x76, 0xb1, 0x18, 0xff, 0xfe, 0x0,

    /* U+0063 "c" */
    0x0, 0xcf, 0xff, 0xb0, 0xb, 0x78, 0x87, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x7, 0x0, 0x0, 0x0, 0x7, 0x0, 0x0, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0xb, 0x27, 0x77, 0x0, 0x0, 0xcf, 0xff, 0xb0,

    /* U+0064 "d" */
    0x18, 0xff, 0xfd, 0x0, 0x2e, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x7, 0x0, 0x0, 0x61, 0x7, 0x0, 0x0, 0x61,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2e, 0x77, 0x76, 0xb1, 0x18, 0xff, 0xfe, 0x0,

    /* U+0065 "e" */
    0x18, 0xff, 0xff, 0xb0, 0x2e, 0x78, 0x87, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x42, 0x21, 0x0,
    0x3, 0xff, 0xff, 0x20, 0x1d, 0x55, 0x54, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x2e, 0x67, 0x77, 0x0, 0x18, 0xff, 0xff, 0xb0,

    /* U+0066 "f" */
    0x18, 0xff, 0xff, 0xb0, 0x2e, 0x78, 0x87, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x42, 0x21, 0x0,
    0x3, 0xff, 0xff, 0x20, 0x1d, 0x55, 0x54, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x2a, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,

    /* U+0067 "g" */
    0x0, 0xcf, 0xff, 0xb0, 0xb, 0x78, 0x87, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x40, 0x0, 0x0,
    0x3, 0x7, 0xff, 0x20, 0x1b, 0x12, 0x87, 0xa2,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0xb, 0x27, 0x74, 0xb1, 0x0, 0xcf, 0xfe, 0x10,

    /* U+0068 "h" */
    0x0, 0x0, 0x0, 0x1, 0x2b, 0x0, 0x0, 0xa4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x42, 0x23, 0xe3, 0x3, 0xff, 0xff, 0x40,
    0x1d, 0x55, 0x55, 0xd3, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x50, 0x3, 0xf4, 0x2d, 0x10, 0x0, 0xc4,
    0x11, 0x0, 0x0, 0x1,

    /* U+0069 "i" */
    0x7, 0x5, 0xf0, 0x7f, 0x7, 0xf0, 0x7, 0x0,
    0x70, 0x7f, 0x7, 0xf0, 0x5f, 0x0, 0x70,

    /* U+006A "j" */
    0x0, 0x0, 0x0, 0x43, 0x0, 0x0, 0x2, 0xf4,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x0, 0x3, 0xf4,
    0x0, 0x0, 0x0, 0x61, 0x0, 0x0, 0x0, 0x61,
    0x1c, 0x10, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0xb, 0x27, 0x76, 0xb1, 0x0, 0xcf, 0xfe, 0x0,

    /* U+006B "k" */
    0x26, 0x0, 0x2d, 0xa0, 0x2f, 0x43, 0xee, 0x20,
    0x2f, 0x8f, 0xd1, 0x0, 0x2f, 0x69, 0x31, 0x0,
    0x3, 0xff, 0xff, 0x30, 0x1d, 0x55, 0x55, 0xd3,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2d, 0x10, 0x0, 0xc4, 0x11, 0x0, 0x0, 0x1,

    /* U+006C "l" */
    0x25, 0x0, 0x0, 0x0, 0x2f, 0x30, 0x0, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x7, 0x0, 0x0, 0x0, 0x7, 0x0, 0x0, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x2e, 0x77, 0x77, 0x0, 0x18, 0xff, 0xff, 0xb0,

    /* U+006D "m" */
    0x18, 0xff, 0xff, 0x92, 0x2e, 0x78, 0x87, 0xd4,
    0x2f, 0x57, 0xb3, 0xf4, 0x2f, 0x5a, 0xe3, 0xf4,
    0x7, 0xa, 0xe0, 0x61, 0x7, 0x8, 0xc0, 0x61,
    0x2f, 0x50, 0x13, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x30, 0x2, 0xf4, 0x25, 0x0, 0x0, 0x43,

    /* U+006E "n" */
    0x2a, 0xfd, 0x10, 0x43, 0x2f, 0x8f, 0xd3, 0xf4,
    0x2f, 0x53, 0xfa, 0xf4, 0x2f, 0x40, 0x2, 0xf4,
    0x5, 0x0, 0x0, 0x50, 0x9, 0x0, 0x0, 0x81,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x30, 0x1, 0xe4, 0x14, 0x0, 0x0, 0x23,

    /* U+006F "o" */
    0x0, 0xcf, 0xfd, 0x0, 0xb, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x7, 0x0, 0x0, 0x61, 0x7, 0x0, 0x0, 0x61,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0xb, 0x27, 0x76, 0xb1, 0x0, 0xcf, 0xfe, 0x0,

    /* U+0070 "p" */
    0x18, 0xff, 0xfd, 0x0, 0x2e, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x42, 0x23, 0xf4,
    0x3, 0xff, 0xff, 0x40, 0x1d, 0x55, 0x54, 0x0,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x50, 0x0, 0x0,
    0x2d, 0x10, 0x0, 0x0, 0x11, 0x0, 0x0, 0x0,

    /* U+0071 "q" */
    0x0, 0xcf, 0xfd, 0x0, 0xb, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x40, 0x2, 0xf4,
    0x4, 0x0, 0x0, 0x30, 0x1a, 0x0, 0x0, 0x92,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x8b, 0x70,
    0xb, 0x27, 0x7f, 0xc0, 0x0, 0xcf, 0x95, 0xf4,

    /* U+0072 "r" */
    0x18, 0xff, 0xfd, 0x0, 0x2e, 0x78, 0x87, 0xb1,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x42, 0x23, 0xf4,
    0x3, 0xff, 0xff, 0x40, 0x1d, 0x59, 0x64, 0x0,
    0x2f, 0x5f, 0xc0, 0x0, 0x2f, 0x55, 0xfc, 0x0,
    0x2f, 0x30, 0x6f, 0xb0, 0x25, 0x0, 0x8, 0xf0,

    /* U+0073 "s" */
    0x0, 0xcf, 0xff, 0xe1, 0xb, 0x78, 0x88, 0x60,
    0x2f, 0x50, 0x0, 0x0, 0x2f, 0x42, 0x21, 0x0,
    0x3, 0xff, 0xff, 0x30, 0x0, 0x35, 0x55, 0xc3,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x0, 0x3, 0xf4,
    0x5, 0x77, 0x76, 0xb1, 0xd, 0xff, 0xfe, 0x0,

    /* U+0074 "t" */
    0x3f, 0xff, 0xff, 0xf5, 0x7, 0x88, 0x88, 0x71,
    0x0, 0x7, 0x90, 0x0, 0x0, 0xa, 0xd0, 0x0,
    0x0, 0x1, 0x60, 0x0, 0x0, 0x1, 0x60, 0x0,
    0x0, 0xa, 0xd0, 0x0, 0x0, 0xb, 0xd0, 0x0,
    0x0, 0xb, 0xd0, 0x0, 0x0, 0x7, 0x90, 0x0,
    0x0, 0x0, 0x0, 0x0,

    /* U+0075 "u" */
    0x25, 0x0, 0x0, 0x43, 0x2f, 0x30, 0x2, 0xf4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x7, 0x0, 0x0, 0x61, 0x7, 0x0, 0x0, 0x61,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0xb, 0x27, 0x76, 0xb1, 0x0, 0xcf, 0xfe, 0x0,

    /* U+0076 "v" */
    0x0, 0x0, 0x0, 0x0, 0x29, 0x0, 0x0, 0x74,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x30, 0x1, 0xe3, 0x1, 0x0, 0x0, 0x20,
    0x1d, 0x20, 0xc, 0xa0, 0x2f, 0x50, 0xbf, 0x40,
    0x2f, 0x6b, 0xf5, 0x0, 0x2f, 0x8f, 0x60, 0x0,
    0x8, 0x10, 0x0, 0x0,

    /* U+0077 "w" */
    0x25, 0x0, 0x0, 0x43, 0x2f, 0x30, 0x2, 0xf4,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x7, 0x8, 0xc0, 0x61, 0x7, 0xa, 0xe0, 0x61,
    0x2f, 0x5a, 0xe3, 0xf4, 0x2f, 0x57, 0xb3, 0xf4,
    0x2e, 0x77, 0x86, 0xd4, 0x18, 0xff, 0xff, 0x92,

    /* U+0078 "x" */
    0x2f, 0x10, 0x0, 0xd4, 0x1f, 0xb0, 0x9, 0xf3,
    0x7, 0xf6, 0x4f, 0x90, 0x0, 0xcc, 0xae, 0x0,
    0x0, 0x29, 0x74, 0x0, 0x0, 0x29, 0x74, 0x0,
    0x0, 0xcc, 0xae, 0x0, 0x7, 0xf6, 0x4f, 0x90,
    0x1f, 0xb0, 0x9, 0xf2, 0x2f, 0x20, 0x0, 0xd4,

    /* U+0079 "y" */
    0x0, 0x0, 0x0, 0x1, 0x2b, 0x0, 0x0, 0x94,
    0x2f, 0x50, 0x3, 0xf4, 0x2f, 0x50, 0x3, 0xf4,
    0x2f, 0x12, 0x23, 0xe4, 0x0, 0xff, 0xff, 0x40,
    0x0, 0x35, 0x55, 0xc3, 0x0, 0x0, 0x3, 0xf4,
    0x0, 0x0, 0x3, 0xf4, 0x0, 0x77, 0x76, 0xb1,
    0x4, 0xff, 0xfe, 0x0,

    /* U+007A "z" */
    0xa, 0xff, 0xff, 0xe1, 0x0, 0x68, 0x89, 0x70,
    0x0, 0x0, 0x1e, 0x80, 0x0, 0x0, 0xbf, 0x20,
    0x0, 0x0, 0xc6, 0x0, 0x0, 0x4c, 0x0, 0x0,
    0x1, 0xed, 0x0, 0x0, 0x6, 0xf3, 0x0, 0x0,
    0x6, 0x97, 0x76, 0x0, 0xd, 0xff, 0xff, 0x90,

    /* U+007B "{" */
    0x0, 0xb, 0xf7, 0x0, 0xb7, 0x50, 0x0, 0xf7,
    0x0, 0x2, 0xd7, 0x0, 0x6f, 0x70, 0x0, 0x5,
    0xb5, 0x0, 0x0, 0xf7, 0x0, 0x0, 0xf7, 0x0,
    0x0, 0xa7, 0x50, 0x0, 0xb, 0xf7,

    /* U+007C "|" */
    0x3f, 0x43, 0xf4, 0x3f, 0x43, 0xf4, 0x3f, 0x43,
    0xf4, 0x3f, 0x43, 0xf4, 0x3f, 0x43, 0xf4, 0x3f,
    0x43, 0xf4, 0x2a, 0x20,

    /* U+007D "}" */
    0x3f, 0xe1, 0x0, 0x3, 0x7b, 0x20, 0x0, 0x2f,
    0x50, 0x0, 0x2f, 0x40, 0x0, 0x3, 0xfa, 0x0,
    0x1d, 0x51, 0x0, 0x2f, 0x50, 0x0, 0x2f, 0x50,
    0x3, 0x6b, 0x20, 0x3e, 0xe1, 0x0,

    /* U+007E "~" */
    0x0, 0x0, 0x0, 0x1, 0xcd, 0x60, 0x81, 0x48,
    0x5e, 0xfc, 0x0, 0x0, 0x2, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 61, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 73, .box_w = 3, .box_h = 13, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 20, .adv_w = 79, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 28, .adv_w = 155, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 73, .adv_w = 130, .box_w = 8, .box_h = 16, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 137, .adv_w = 179, .box_w = 11, .box_h = 13, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 209, .adv_w = 139, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 254, .adv_w = 46, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 259, .adv_w = 80, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 284, .adv_w = 80, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 309, .adv_w = 125, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 341, .adv_w = 120, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 366, .adv_w = 46, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 371, .adv_w = 120, .box_w = 7, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 382, .adv_w = 46, .box_w = 3, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 385, .adv_w = 125, .box_w = 8, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 433, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 473, .adv_w = 73, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 488, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 528, .adv_w = 130, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 563, .adv_w = 130, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 607, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 647, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 687, .adv_w = 130, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 722, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 762, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 802, .adv_w = 57, .box_w = 2, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 811, .adv_w = 57, .box_w = 2, .box_h = 12, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 823, .adv_w = 91, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 844, .adv_w = 120, .box_w = 7, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 858, .adv_w = 91, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 879, .adv_w = 130, .box_w = 8, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 927, .adv_w = 158, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 977, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1017, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1057, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1097, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1137, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1177, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1217, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1257, .adv_w = 130, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1301, .adv_w = 73, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1316, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1356, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1396, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1436, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1476, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1516, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1556, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1596, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1636, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1676, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1716, .adv_w = 130, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 1760, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1800, .adv_w = 125, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1844, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1884, .adv_w = 125, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1924, .adv_w = 130, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1968, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2008, .adv_w = 80, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2033, .adv_w = 126, .box_w = 8, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 2081, .adv_w = 80, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2106, .adv_w = 119, .box_w = 7, .box_h = 4, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 2120, .adv_w = 121, .box_w = 8, .box_h = 3, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 2132, .adv_w = 62, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 2140, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2180, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2220, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2260, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2300, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2340, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2380, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2420, .adv_w = 130, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2464, .adv_w = 73, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2479, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2519, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2559, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2599, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2639, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2679, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2719, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2759, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2799, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2839, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2879, .adv_w = 130, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 2923, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 2963, .adv_w = 125, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 3007, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 3047, .adv_w = 125, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 3087, .adv_w = 130, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 3131, .adv_w = 130, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 3171, .adv_w = 101, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 3201, .adv_w = 81, .box_w = 3, .box_h = 13, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 3221, .adv_w = 101, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 3251, .adv_w = 108, .box_w = 7, .box_h = 4, .ofs_x = 0, .ofs_y = 3}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
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
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};

extern const lv_font_t ds_digib_font_16;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ds_digib_font_16 = {
#else
lv_font_t ds_digib_font_16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 16,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &ds_digib_font_16,
#endif
    .user_data = NULL,
};



#endif /*#if DS_DIGIB_FONT_16*/

