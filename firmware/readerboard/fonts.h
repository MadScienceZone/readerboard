#ifndef READERBOARD_FONTS_H
#define READERBOARD_FONTS_H

extern bool get_font_metric_data(byte font, byte codepoint, unsigned char *length, unsigned char *space, unsigned short *offset);
extern unsigned char get_font_bitmap_data(unsigned short offset);
extern const int N_FONTS;
#endif
