#ifndef READERBOARD_CORE
#define READERBOARD_CORE

#define BANNER_HARDWARE_VERS "HW 2.0.0  "
#define BANNER_FIRMWARE_VERS "FW 0.0.0  "
#define BANNER_SERIAL_NUMBER "S/N XXXXXX"
#define SERIAL_VERSION_STAMP "V2.0.0$R0.0.0$SXXXXX$"
//                             \___/  \___/  \___/
//                               |      |      |
//                  Hardware version    |      |
//                         Firmware version    |
//                                 Serial number
//

extern const int N_ROWS;
extern byte image_buffer[];

extern void clear_buffer(byte *buffer);
extern void display_buffer(byte *src);
extern int  draw_character(byte col, byte font, unsigned int codepoint, byte *buffer);
extern void draw_column(byte col, byte bits, bool mergep, byte *buffer);
extern void shift_left(byte *buffer);
extern void setup_buffers(void);

#if HW_MODEL == MODEL_CURRENT_64x8
extern void discrete_all_off(bool stop_blinkers);
extern bool discrete_query(byte lightno);
extern void discrete_set(byte lightno, bool value);
#endif

typedef enum { 
		NoTransition,
		TransScrollLeft,
		TransScrollRight,
		TransScrollUp,
		TransScrollDown,
		TransWipeLeft,
		TransWipeRight,
		TransWipeUp,
		TransWipeDown,
		TransWipeLeftRight,
		TransWipeUpDown,
} TransitionEffect;

#endif
