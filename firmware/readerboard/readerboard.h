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

/* LEGACY models NO LONGER supported. Do not use these.
#define MODEL_LEGACY_64x7 (0)
#define MODEL_LEGACY_64x8 (1)
#define MODEL_LEGACY_64x8_INTEGRATED (2)
---------------------------------------------------------*/

// Microcontroller Models for HW_MC
#define HW_MC_MEGA_2560 (0)
#define HW_MC_DUE (1)

// BEGIN CONFIGURATION SECTION
// TODO: Set these before compiling for a particular hardware configuration
#define HW_MODEL (MODEL_3xx_RGB)
#define HW_MC (HW_MC_DUE)
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
#define BANNER_HARDWARE_VERS "HW 3.2.2  "
#define BANNER_FIRMWARE_VERS "FW 0.0.0  "
#define BANNER_SERIAL_NUMBER "S/N RB____"
#define SERIAL_VERSION_STAMP "V3.2.2$R0.0.0$SRB____$"
//                             \___/  \___/  \____/
//                               |      |      |
//                  Hardware version    |      |
//                         Firmware version    |
//                                 Serial number
//
// END CONFIGURATION SECTION

extern byte USB_baud_rate_code;
extern byte RS485_baud_rate_code;
extern byte my_device_address;
extern byte global_device_address;
extern int USB_baud_rate;
extern int RS485_baud_rate;

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
extern void discrete_all_off(bool stop_blinkers);
extern bool discrete_query(byte lightno);
extern void discrete_set(byte lightno, bool value);
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
