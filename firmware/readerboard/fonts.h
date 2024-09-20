#ifndef READERBOARD_FONTS_H
#define READERBOARD_FONTS_H

extern bool get_font_metric_data(byte font, byte codepoint, byte *length, byte *space, unsigned int *offset);
extern unsigned char get_font_bitmap_data(unsigned short offset);
extern const int N_FONTS;
#endif
