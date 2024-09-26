/*
 ____  _____    _    ____  _____ ____  ____   ___    _    ____  ____  
|  _ \| ____|  / \  |  _ \| ____|  _ \| __ ) / _ \  / \  |  _ \|  _ \ 
| |_) |  _|   / _ \ | | | |  _| | |_) |  _ \| | | |/ _ \ | |_) | | | |
|  _ <| |___ / ___ \| |_| | |___|  _ <| |_) | |_| / ___ \|  _ <| |_| |
|_| \_\_____/_/   \_\____/|_____|_| \_\____/ \___/_/   \_\_| \_\____/ 
                                                                      
Libraries Required:
	TimerEvent 0.5.0
*/


#ifndef READERBOARD_CORE
#define READERBOARD_CORE


// Readerboard hardware models for HW_MODEL
#define MODEL_3xx_MONOCHROME (3)
#define MODEL_3xx_RGB (4)
#define MODEL_BUSYLIGHT_1 (5)
#define MODEL_BUSYLIGHT_2 (6)

/* LEGACY models NO LONGER supported. Do not use these. */
#define MODEL_LEGACY_64x7 (0)
#define MODEL_LEGACY_64x8 (1)
#define MODEL_LEGACY_64x8_INTEGRATED (2)

// Microcontroller Models for HW_MC
#define HW_MC_MEGA_2560 (0)
#define HW_MC_DUE (1)
#define HW_MC_PRO (2)

// BEGIN CONFIGURATION SECTION
// TODO: Set these before compiling for a particular hardware configuration
#define HW_MODEL (MODEL_3xx_RGB)
#define HW_MC (HW_MC_DUE)
//#define HW_MODEL (MODEL_BUSYLIGHT_1)
//#define HW_MC (HW_MC_PRO)
#define HAS_I2C_EEPROM (false)

// TODO: Set these default settings (this will be the "factory default settings")
//       On Due-based systems without external EEPROM, this is the only way to
//       make these settings at all. On every other model, this is just the default
//       that can be overridden by configuring the unit since the new configuration
//       values can be saved in EEPROM.
//
// Default baud rate. Allowed values are '0'=300, '1'=600, '2'=1200, '3'=2400,
//                    '4'=4800, '5'=9600, '6'=14400, '7'=19200, '8'=28800,
//                    '9'=31250, 'A'=38400, 'B'=57600, 'C'=115200.
#define EE_DEFAULT_USB_SPEED  ('5')	/* usb connections 9600 baud */
#define EE_DEFAULT_485_SPEED ('5')  /* RS-485 connections 9600 baud */
//
#define EE_ADDRESS_DISABLED (0xff) /* interface disabled */
// Default device address (may be any value from 0-63 except the global address, or EE_ADDRESS_DISABLED if
// you won't be using RS-485 at all.)
#define EE_DEFAULT_ADDRESS (EE_ADDRESS_DISABLED)
//
// Default global device address (may be any value from 0-15).
#define EE_DEFAULT_GLOBAL_ADDRESS (15)
//
// TODO: Adjust these for the colors of discrete status LEDs on this unit. These
//       can be used for color values in commands sent to the device.
#define STATUS_LED_COLOR_L0 ('G')
#define STATUS_LED_COLOR_L1 ('y')
#define STATUS_LED_COLOR_L2 ('Y')
#define STATUS_LED_COLOR_L3 ('r')
#define STATUS_LED_COLOR_L4 ('R')
#define STATUS_LED_COLOR_L5 ('b')
#define STATUS_LED_COLOR_L6 ('B')
#define STATUS_LED_COLOR_L7 ('W')
//
// TODO: Adjust these for your version and serial number
#if HW_MODEL == MODEL_3xx_MONOCHROME || HW_MODEL == MODEL_3xx_RGB
#define BANNER_HARDWARE_VERS "HW 3.2.2  "
#define BANNER_FIRMWARE_VERS "FW 2.1.2  "
#define BANNER_SERIAL_NUMBER "S/N RB0000"
#endif
#define SERIAL_VERSION_STAMP "V3.2.2$R2.1.1$SRB0000$"
//                             \___/  \___/  \____/
//                               |      |      |
//                  Hardware version    |      |
//                         Firmware version    |
//                                 Serial number
//
// END CONFIGURATION SECTION

// some definitions for known devices and their hardware configurations
#define SN_B0001

#ifdef SN_B0001
# define HW_MODEL (MODEL_BUSYLIGHT_1)
# define SERIAL_VERSION_STAMP "V1.0.2$R2.1.2$SB0001$"
# define HW_MC (HW_MC_PRO)
#endif
#ifdef SN_RB0000
# define SERIAL_VERSION_STAMP "V3.2.2$R2.1.2$SRB0000$"
# define BANNER_HARDWARE_VERS "HW 3.2.2  "
# define BANNER_SERIAL_NUMBER "S/N RB0000"
# define HW_MC (HW_MC_DUE)
# define HAS_I2C_EEPROM (false)
#endif

#define HW_CONTROL_LOGIC_3xx (1)
#define HW_CONTROL_LOGIC_B_1xx (2)
#define HW_CONTROL_LOGIC_B_2xx (3)

#if HW_MODEL == MODEL_3xx_RGB
const int N_COLS = 64;              // number of physical columns
const int N_COLBYTES = 8;           // number of byte-size column blocks
const int N_ROWS = 8;               // number of physical rows
const int N_COLORS = 4;             // number of color planes
const int N_FLASHING_PLANE = 3;
# define IS_READERBOARD (true)
# define HW_CONTROL_LOGIC (HW_CONTROL_LOGIC_3xx)
# define SERIAL_485 (Serial3)
#elif HW_MODEL == MODEL_3xx_MONOCHROME
const int N_COLS = 64;              // number of physical columns
const int N_COLBYTES = 8;           // number of byte-size column blocks
const int N_ROWS = 8;               // number of physical rows
const int N_COLORS = 2;
const int N_FLASHING_PLANE = 1;
# define IS_READERBOARD (true)
# define HW_CONTROL_LOGIC (HW_CONTROL_LOGIC_3xx)
# define SERIAL_485 (Serial3)
#elif HW_MODEL == MODEL_BUSYLIGHT_1
# define IS_READERBOARD (false)
# define HW_CONTROL_LOGIC (HW_CONTROL_LOGIC_B_1xx)
# if HW_MC != HW_MC_PRO
#  error "The busylight 1 only used the Arduino Pro Micro uC"
# endif
#elif HW_MODEL == MODEL_BUSYLIGHT_2
# define IS_READERBOARD (false)
# define HW_CONTROL_LOGIC (HW_CONTROL_LOGIC_B_2xx)
# define SERIAL_485 (Serial1)
# if HW_MC != HW_MC_PRO
#  error "The busylight 2 only used the Arduino Pro Micro uC"
# endif
#else
# error "hw model not set"
#endif

#if IS_READERBOARD
const byte BIT_RGB_FLASHING = 0x08;
const byte BIT_RGB_BLUE = 0x04;
const byte BIT_RGB_GREEN = 0x02;
const byte BIT_RGB_RED = 0x01;

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
		_TransScrollText,
} TransitionEffect;

//
// Transition effect manager. This manages incrementally
// copying data from the image buffer to the hardware refresh
// buffer in visually interesting patterns.
//
class TransitionManager {
	TransitionEffect transition;
	TimerEvent       timer;
	byte           (*src)[N_ROWS][N_COLS];
	byte            stage[N_ROWS][N_COLS];
    const char     *scroll_src;
	int             scroll_pos;
	bool    	    scroll_repeat;
	byte    	    scroll_col;
	byte    	    scroll_font;
	byte    	    scroll_color;
	int             scroll_len;
public:
	TransitionManager(void);
	void update(void);
	void set_stage(void);
	void start_transition(TransitionEffect, int);
	void stop(void);
	void next(bool reset_column = false);
    void start_scrolling_text(const char *text, int len, bool repeat, byte font, byte color, int delay_mS=100);
};
extern TransitionManager transitions;
extern byte image_buffer[N_ROWS][N_COLS];
extern void clear_image_buffer();
extern void clear_display_buffer();
extern void display_buffer(byte buffer[N_ROWS][N_COLS], TransitionEffect transition=NoTransition);
extern byte draw_character(byte col, byte font, byte codepoint, byte buffer[N_ROWS][N_COLS], byte color, bool mergep=false);
extern void draw_column(byte col, byte bits, bool mergep, byte *buffer);
extern void shift_left(byte buffer[N_ROWS][N_COLS]);
extern void setup_buffers(void);
extern byte render_text(byte buffer[N_ROWS][N_COLS], byte pos, byte font, const char *string, byte color, bool mergep=false);
#endif /* IS_READERBOARD */

extern byte USB_baud_rate_code;
extern byte RS485_baud_rate_code;
extern byte my_device_address;
extern byte global_device_address;
extern int USB_baud_rate;
extern int RS485_baud_rate;

typedef enum {FROM_USB, FROM_485} serial_source_t;

extern void discrete_all_off(bool stop_blinkers);
extern bool discrete_query(byte lightno);
extern void discrete_set(byte lightno, bool value);
extern byte encode_int6(byte n);
extern byte encode_hex_nybble(byte n);
extern int parse_baud_rate_code(byte code);
extern void send_485_byte(byte x);
extern void start_485_reply(void);
extern void end_485_reply(void);
extern void send_usb_byte(byte x);
extern void start_usb_reply(void);
extern void end_usb_reply(void);
//extern void start_reply(Stream *);
//extern void end_reply(Stream *);
//extern void send_byte(Stream *, byte x);
//extern void send_string(Stream *, const char *string);
extern void test_pattern(void);
extern byte parse_led_name(byte ch);
const byte STATUS_LED_OFF = 0xff;

// #define SERIAL_DEBUG
// #define START_TEST_PATTERN

#endif
