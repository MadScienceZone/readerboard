#ifndef READERBOARD_FONTS_H
#define READERBOARD_FONTS_H

//typedef struct {
//public:
//	byte length;
//	byte space;
//	word offset;
//} FontMetrics;

extern bool get_font_metric_data(byte font, byte codepoint, byte *length, byte *space, unsigned int *offset);
extern byte get_font_bitmap_data(unsigned int offset);
extern const int N_FONTS;
#endif
