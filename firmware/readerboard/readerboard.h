#ifndef READERBOARD_CORE
#define READERBOARD_CORE

#define BANNER_HARDWARE_VERS "HW 3.2.3  "
#define BANNER_FIRMWARE_VERS "FW 0.0.0  "
#define BANNER_SERIAL_NUMBER "S/N 100   "
#define SERIAL_VERSION_STAMP "V3.2.3$R0.0.0$S100$"
//                             \___/  \___/  \___/
//                               |      |      |
//                  Hardware version    |      |
//                         Firmware version    |
//                                 Serial number
//

#define MODEL_3xx_MONOCHROME (3)
#define MODEL_3xx_RGB (4)

// LEGACY models NO LONGER supported. Do not use these.
#define MODEL_LEGACY_64x7 (0)
#define MODEL_LEGACY_64x8 (1)
#define MODEL_LEGACY_64x8_INTEGRATED (2)

// MICROCONTROLLER MODEL
#define HW_MC_MEGA_2560 (0)
#define HW_MC_DUE (1)

// TODO: rename these
//#define MODEL_CURRENT_64x8 (1)
//#define MODEL_CURRENT_64x8_INTEGRATED (2)

#define HW_MODEL (MODEL_3xx_RGB)
#define HW_MC (HW_MC_DUE)

//extern const int N_ROWS;
//extern byte image_buffer[];
//
//extern void clear_buffer(byte *buffer);
//extern void display_buffer(byte *src);
//extern int  draw_character(byte col, byte font, unsigned int codepoint, byte *buffer);
//extern void draw_column(byte col, byte bits, bool mergep, byte *buffer);
//extern void shift_left(byte *buffer);
//extern void setup_buffers(void);
//
//#if HW_MODEL == MODEL_3xx_RGB
//extern void discrete_all_off(bool stop_blinkers);
//extern bool discrete_query(byte lightno);
//extern void discrete_set(byte lightno, bool value);
//#endif
//
//typedef enum { 
//		NoTransition,
//		TransScrollLeft,
//		TransScrollRight,
//		TransScrollUp,
//		TransScrollDown,
//		TransWipeLeft,
//		TransWipeRight,
//		TransWipeUp,
//		TransWipeDown,
//		TransWipeLeftRight,
//		TransWipeUpDown,
//} TransitionEffect;
//
#endif
