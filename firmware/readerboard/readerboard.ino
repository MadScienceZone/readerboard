/* 
 *  ____  _____    _    ____  _____ ____  ____   ___    _    ____  ____  
 * |  _ \| ____|  / \  |  _ \| ____|  _ \| __ ) / _ \  / \  |  _ \|  _ \
 * | |_) |  _|   / _ \ | | | |  _| | |_) |  _ \| | | |/ _ \ | |_) | | | |
 * |  _ <| |___ / ___ \| |_| | |___|  _ <| |_) | |_| / ___ \|  _ <| |_| |
 * |_| \_\_____/_/   \_\____/|_____|_| \_\____/ \___/_/   \_\_| \_\____/ 
 *   __   _  _       ___  
 *  / /_ | || |__  _( _ ) 
 * | '_ \| || |\ \/ / _ \
 * | (_) |__   _>  < (_) |
 *  \___/   |_|/_/\_\___/ 
 *                        
 * Arduino Mega 2560/Due (or equiv.) firmware to drive revision 3 readerboards.
 * Previous versions of this code also supported my older readerboard designs
 * including 64x7 matrix layouts and 64x8 monochrome revision 1 and 2 boards.
 * These are no longer supported.
 *
 * Steve Willoughby (c) 2023, 2024
 */

#include <TimerEvent.h>
//#include <EEPROM.h>
//#include "fonts.h"
//#include "commands.h"
#include "readerboard.h"


//
// EEPROM locations
// $00 0x4B (sentinel)
// $01 0x00 (EEPROM layout version)
// $02 USB baud rate code
// $03 485 baud rate code
// $04 device address
// $05 global address
// $06 0x4B (sentinel)
//
#define EE_VALUE_LAYOUT (0)
#define EE_VALUE_SENTINEL (0x4b)

#define EE_ADDR_SENTINEL  (0x00)
#define EE_ADDR_LAYOUT (0x01)
#define EE_ADDR_USB_SPEED (0x02)
#define EE_ADDR_485_SPEED (0x03)
#define EE_ADDR_DEVICE_ADDR (0x04)
#define EE_ADDR_GLOBAL_ADDR (0x05)
#define EE_ADDR_SENTINEL2  (0x06)

/* Hardware model selected for this firmware.
 * 2xx logic 
 */

#define HW_CONTROL_LOGIC_3xx (1)

#if HW_MODEL == MODEL_3xx_MONOCHROME || HW_MODEL == MODEL_3xx_RGB
# define HW_CONTROL_LOGIC HW_CONTROL_LOGIC_3xx
#else
# if HW_MODEL == MODEL_LEGACY_64x7 || HW_MODEL == MODEL_LEGACY_64x8 || HW_MODEL == MODEL_LEGACY_64x8_INTEGRATED
#  error "Legacy readerboard hardware model no longer supported."
# else
#  error "HW_MODEL not set to a supported hardware configuration"
# endif
#endif

/* I/O port to use for full 8-bit write operations to the sign board */
/* On the Mega 2560, PORTF<7:0> corresponds to <A7:A0> pins */
/* On the Due, we just toggle the bits separately. */
/* TODO consider if using PORTF is even a good idea considering interrupts and such */
#if HW_MC == HW_MC_MEGA_2560
# define MATRIX_DATA_PORT    PORTF   
#endif

#define LENGTH_OF(A) (sizeof(A) / sizeof(A[0]))

/* Refresh the display every REFRESH_MS milliseconds. For an 8-row
 * display, an overall refresh rate of 30 FPS =  4.16 mS
 *                                     25 FPS =  5.00 mS
 *                                     20 FPS =  6.26 mS
 *                                     15 FPS =  8.33 mS
 *                                     10 FPS = 12.50 mS
 *                                      5 FPS = 25.00 mS
 */
//const int REFRESH_MS = 25;
const int ROW_HOLD_TIME_US = 500;   /* how long in microseconds to hold the row on during refresh */

const int PIN_STATUS_LED = 13;


// Hardware notes
// DUE    MEGA                 READERBOARD              DUE    MEGA
//                                                  LED 13~    13~
//                                                      12~    12~
//                                                      11~    11~
//                                                      10~    10~
//                                    L4                09~    09~
//                                    L5                08~    08~
//     
//                                    L6                07~    07~
//                                    L7                06~    06~
//  A0/54  A0/54                 D7   L3                05~    05~
//  A1/55  A0/55                 D6   L2                04~    04~
//  A2/56  A0/56                 D5   L1                03~    03~
//  A3/57  A0/57                 D4   L0                02~    02~
//  A4/58  A0/58                 D3   USB TxD           01/TX0 01/TX0~
//  A5/59  A0/59                 D2   USB RxD           00/RX0 00/RX0~
//  A6/60  A0/60                 D1                     
//  A7/61  A0/61                 D0   485 TxD           14/TX3 14/TX3
//                                    485 RxD           15/RX3 15/RX3
//  A8/62  A0/62              SRCLK   485 DE            16/TX2 16/TX2
//  A9/63  A0/53               /CLR   485 /RE           17/RX2 17/RX2
// A10/64 A10/64                  /G                    18/TX1 18/TX1
// A11/65 A11/65                RCLK   R3               19/RX1 19/RX1
//     66 A12/66                  R0                    20/SDA 20/SDA
//     67 A13/67                  R1                    21/SCL 21/SCL
//     68 A14/68                  R2
//     69 A15/69                  R4
// 
//     --    4K  EEPROM
//    96K    8K  SRAM
//   512K  256K  flash memory
//     32    16  word size (bits)
//    ARM ATmga  architecture
// (Due uC is Atmel SAM3X8E ARM Cortex-M3 at 84MHz)
// (Mega uC is ATmega2560 at 16MHz)
// Use programming port for USB connection
//
// Auto-reset
// Classic Arduinos reset when DTR or RTS is triggered on connection. Prevent with 10uF cap on RST to GND.
// Leonardo resets when port is "touched" at 1200bps, not merely opened.
// NOTE: "while (!Serial);" waits for the port to be connected to.
// Due native port doesn't reset the system, so using that prevents resets
// Due programming port: open at 1200 to erase; other opens cause reset like other boards. Cap doesn't work.
// but a 1K resistor between RST and 3.3V will keep reset high.



#if HW_CONTROL_LOGIC == HW_CONTROL_LOGIC_3xx
const int PIN_D0    = 61;   // column data bit 0
const int PIN_D1    = 60;   // column data bit 1
const int PIN_D2    = 59;   // column data bit 2
const int PIN_D3    = 58;   // column data bit 3
const int PIN_D4    = 57;   // column data bit 4
const int PIN_D5    = 56;   // column data bit 5
const int PIN_D6    = 55;   // column data bit 6
const int PIN_D7    = 54;   // column data bit 7
const int PIN_SRCLK = 62;   // strobe bits into shift reg
const int PIN__SRCLR= 63;   // ~clear shift register
const int PIN__G    = 64;   // ~output enable
const int PIN_RCLK  = 65;   // strobe shift reg to output buffer
const int PIN_R0    = 66;   // row address 0
const int PIN_R1    = 67;   // row address 1
const int PIN_R2    = 68;   // row address 2
const int PIN_R4    = 69;   // row address 4
const int PIN_R3    = 19;    // row address 3
const int PIN_L0    =  2;     // discrete LED 0 (bottom) (green)
const int PIN_L1    =  3;     // discrete LED 1 (yellow)
const int PIN_L2    =  4;     // discrete LED 2 (yellow)
const int PIN_L3    =  5;     // discrete LED 3 (red)
const int PIN_L4    =  9;     // discrete LED 4 (red)
const int PIN_L5    =  8;     // discrete LED 5 (blue)
const int PIN_L6    =  7;     // discrete LED 6 (blue)
const int PIN_L7    =  6;     // discrete LED 7 (top) (white)
const int PIN_DE    = 16;    // RS-485 driver enable (1=enabled)
const int PIN__RE   = 17;    // RS-485 ~receiver enable (0=enabled)
#else
# error "HW_CONTROL_LOGIC not defined to supported model"
#endif

const int discrete_led_set[8] = {PIN_L0, PIN_L1, PIN_L2, PIN_L3, PIN_L4, PIN_L5, PIN_L6, PIN_L7};
const byte discrete_led_labels[8] = {'G', 'Y', 'y', 'R', 'r', 'B', 'b', 'W'};

//
// setup_pins()
//   Setup the I/O pin modes and initialize the sign hardware.
//
void setup_pins(void)
{
	pinMode(PIN_STATUS_LED, OUTPUT);
    pinMode(PIN_D0, OUTPUT);
    pinMode(PIN_D1, OUTPUT);
    pinMode(PIN_D2, OUTPUT);
    pinMode(PIN_D3, OUTPUT);
    pinMode(PIN_D4, OUTPUT);
    pinMode(PIN_D5, OUTPUT);
    pinMode(PIN_D6, OUTPUT);
    pinMode(PIN_D7, OUTPUT);
    pinMode(PIN_SRCLK, OUTPUT);
    pinMode(PIN__SRCLR, OUTPUT);
    pinMode(PIN__G, OUTPUT);
    pinMode(PIN_RCLK, OUTPUT);
    pinMode(PIN_R0, OUTPUT);
    pinMode(PIN_R1, OUTPUT);
    pinMode(PIN_R2, OUTPUT);
    pinMode(PIN_R3, OUTPUT);
    pinMode(PIN_R4, OUTPUT);
    pinMode(PIN_DE, OUTPUT);
    pinMode(PIN__RE, OUTPUT);
    pinMode(PIN_L0, OUTPUT);
    pinMode(PIN_L1, OUTPUT);
    pinMode(PIN_L2, OUTPUT);
    pinMode(PIN_L3, OUTPUT);
    pinMode(PIN_L4, OUTPUT);
    pinMode(PIN_L5, OUTPUT);
    pinMode(PIN_L6, OUTPUT);
    pinMode(PIN_L7, OUTPUT);
#if HW_CONTROL_LOGIC == HW_CONTROL_LOGIC_3xx
    //                              //            _ _____
    //                              // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
    digitalWrite(PIN__G, HIGH);     //   X    X   1   X    X  X  X  X  X    disable column drains
    digitalWrite(PIN__SRCLR, LOW);  //   X    X   1   0    X  X  X  X  X    clear shift registers
    digitalWrite(PIN_R4, HIGH);     //   X    X   1   0    1  X  X  X  X    
    digitalWrite(PIN_R3, HIGH);     //   X    X   1   0    1  1  X  X  X    disable row source outputs
    digitalWrite(PIN_R2, LOW);      //   X    X   1   0    1  1  0  X  X
    digitalWrite(PIN_R1, LOW);      //   X    X   1   0    1  1  0  0  X
    digitalWrite(PIN_R0, LOW);      //   X    X   1   0    1  1  0  0  0
    digitalWrite(PIN_SRCLK, LOW);   //   0    X   1   0    1  1  0  0  0    clock idle
    digitalWrite(PIN_RCLK, LOW);    //   0    0   1   0    1  1  0  0  0    clock idle
    digitalWrite(PIN_DE, LOW);      // RS-485 driver disabled
    digitalWrite(PIN__RE, HIGH);    // RS-485 receiver disabled
#else
# error "HW_CONTROL_LOGIC not set to a supported model"
#endif
    digitalWrite(PIN_L0, LOW);      // turn off discrete LEDs
    digitalWrite(PIN_L1, LOW);
    digitalWrite(PIN_L2, LOW);
    digitalWrite(PIN_L3, LOW);
    digitalWrite(PIN_L4, LOW);
    digitalWrite(PIN_L5, LOW);
    digitalWrite(PIN_L6, LOW);
    digitalWrite(PIN_L7, LOW);
}

//
//byte image_buffer[8 * N_ROWS];      // image to render onto
//byte hw_buffer[8 * N_ROWS];         // image to refresh onto hardware
//
//// Row addressing
//// R4 R3 R2 R1 R0
////  0  0  0  0  0 RED row 0
////  0  0  0  0  1 RED row 1
////  0  0  0  1  0 RED row 2
////  0  0  0  1  1 RED row 3
////  0  0  1  0  0 RED row 4
////  0  0  1  0  1 RED row 5
////  0  0  1  1  0 RED row 6
////  0  0  1  1  1 RED row 7
////  0  1  0  0  0 GRN row 0
////  0  1  0  0  1 GRN row 1
////  0  1  0  1  0 GRN row 2
////  0  1  0  1  1 GRN row 3
////  0  1  1  0  0 GRN row 4
////  0  1  1  0  1 GRN row 5
////  0  1  1  1  0 GRN row 6
////  0  1  1  1  1 GRN row 7
////  1  0  0  0  0 BLU row 0  \
////  1  0  0  0  1 BLU row 1   |
////  1  0  0  1  0 BLU row 2   |
////  1  0  0  1  1 BLU row 3   | Monochrome
////  1  0  1  0  0 BLU row 4   | Configuration
////  1  0  1  0  1 BLU row 5   |
////  1  0  1  1  0 BLU row 6   |
////  1  0  1  1  1 BLU row 7  /
////  1  1  x  x  x all rows off
//
////
//// setup_buffers()
////   Initialize buffers to zero.
////
//void setup_buffers(void)
//{
//    for (int row=0; row<N_ROWS; row++) {
//        for (int col=0; col<8; col++) {
//            image_buffer[row*8 + col] = hw_buffer[row*8 + col] = 0;
//        }
//    }
//}
//
////
//// clear_buffer(buf)
////   Resets all bits in buf to zero.
////
//void clear_buffer(byte *buf)
//{
//    for (int i=0; i<N_ROWS*8; i++)
//        buf[i] = 0;
//}
//
////
//// LED_row_off()
////   During sign refresh cycle, this turns off the displayed row after the
////   required length of time has elapsed.
////
//void LED_row_off(void)
//{
//#if HW_MODEL == MODEL_LEGACY_64x7
//    MATRIX_DATA_PORT = 0;           // PS0 PS1
//    digitalWrite(PIN_PS0, HIGH);    //  1   X
//    digitalWrite(PIN_PS1, LOW);     //  1   0   idle
//    digitalWrite(PIN_PS0, LOW);     //  0   0   gate 0 to turn off rows
//    digitalWrite(PIN_PS0, HIGH);    //  1   0   idle
//#else                                   //            _____ _
//# if HW_MODEL == MODEL_CURRENT_64x8 || HW_MODEL == MODEL_CURRENT_64x8_INTEGRATED
//										// SRCLK RCLK SRCLR G REN
//    digitalWrite(PIN_RCLK, LOW);        //   X     0    X   X  X
//    digitalWrite(PIN_SRCLK, LOW);       //   0     0    X   X  X    
//    digitalWrite(PIN__SRCLR, HIGH);     //   0     0    1   X  X
//    digitalWrite(PIN__G, HIGH);         //   0     0    1   1  X    disable column output
//    digitalWrite(PIN_REN, LOW);         //   0     0    1   1  0    disable row output
//# else
//#  error "HW_MODEL not set to supported hardware configuration"
//# endif
//#endif
//}
//
////
//// LED_row_on(row, buf)
////   During sign refresh cycle, this turns on the displayed row by
////   sending it the column data stored in the hardware refresh buffer
////   pointed to by buf, with the specified row number (0=top row
////   and 7=bottom row for the current board. On the legacy board,
////   there is no row 0 so the rows are numbered 1-7 top to bottom).
////
//void LED_row_on(int row, byte *buf)
//{
//#if HW_MODEL == MODEL_LEGACY_64x7
//    /* shift out columns */
//    if (row > 0) {
//        for (int i=0; i<8; i++) {
//            MATRIX_DATA_PORT = buf[row*8+i];
//            digitalWrite(PIN_PS0, HIGH);            // PS0 PS1
//            digitalWrite(PIN_PS1, HIGH);            //  1   1   shift data into columns
//            digitalWrite(PIN_PS1, LOW);             //  1   0   idle
//        }
//        /* address row and turn on */
//        MATRIX_DATA_PORT = ((row+1) << 5) & 0xe0;   // PS0 PS1
//        digitalWrite(PIN_PS0, LOW);                 //  0   0   show row
//    }
//#else
//# if HW_MODEL == MODEL_CURRENT_64x8 || HW_MODEL == MODEL_CURRENT_64x8_INTEGRATED
//    /* shift out columns */
//    digitalWrite(PIN__G, HIGH);         // SRCLK RCLK SRCLR G REN
//    digitalWrite(PIN__SRCLR, LOW);      //   X     X    0   1  X
//    digitalWrite(PIN_REN, LOW);         //   X     X    0   1  0    reset, all off
//    digitalWrite(PIN__SRCLR, HIGH);     //   X     X    1   1  0    all off
//    digitalWrite(PIN_SRCLK, LOW);       //   0     X    1   1  0    all off
//    digitalWrite(PIN_RCLK, LOW);        //   0     0    1   1  0    all off
//
//    for (int i=0; i<8; i++) {
//        MATRIX_DATA_PORT = buf[row*8+i];
//        digitalWrite(PIN_SRCLK, HIGH);  //   1     0    1   1  0    shift into columns
//        digitalWrite(PIN_SRCLK, LOW);   //   0     0    1   1  0    
//    }
//    digitalWrite(PIN_SRCLK, HIGH);      //   0     1    1   1  0    latch column outputs
//    digitalWrite(PIN_SRCLK, LOW);       //   0     0    1   1  0    
//    digitalWrite(PIN__G, LOW);          //   0     0    1   0  0    enable column output
//    digitalWrite(PIN_R0, row & 1);      //                          address row
//    digitalWrite(PIN_R1, row & 2);      //                          address row
//    digitalWrite(PIN_R2, row & 4);      //                          address row
//    digitalWrite(PIN_REN, HIGH);        //   0     0    1   0  1    enable row output
//# else
//#  error "HW_MODEL not set to supported hardware configuration"
//# endif
//#endif
//}
//
////
//// Transition effect manager. This manages incrementally
//// copying data from the image buffer to the hardware refresh
//// buffer in visually interesting patterns.
////
//class TransitionManager {
//	TransitionEffect transition;
//	TimerEvent       timer;
//	byte            *src;
//public:
//	TransitionManager(void);
//	void update(void);
//	void start_transition(byte *, TransitionEffect, int);
//	void stop(void);
//	void next(bool reset_column = false);
//};
//
//void next_transition(void);
//
//TransitionManager::TransitionManager(void)
//{
//	src = image_buffer;
//	transition = NoTransition;
//	timer.set(0, next_transition);
//	timer.disable();
//}
//
//void TransitionManager::update(void)
//{
//	timer.update();
//}
//
//void TransitionManager::stop(void)
//{
//	timer.disable();
//}
//
//void TransitionManager::start_transition(byte *buffer, TransitionEffect trans, int delay_ms)
//{
//	transition = trans;
//	src = buffer;
//	if (trans == NoTransition) {
//		stop();
//		copy_all_rows(src, hw_buffer);
//	}
//	else {
//		next(true);
//		timer.reset(); 
//		timer.setPeriod(delay_ms); 
//		timer.enable();
//	}
//}
//
//void TransitionManager::next(bool reset_column)
//{
//	static int current_column = 0;
//
//	if (reset_column) {
//		current_column = 0;
//	}
//
//	if (current_column > 63) {
//		stop();
//		return;
//	}
//
//	switch (transition) {
//	case TransWipeLeft:
//		copy_row_bits(63-(current_column++), src, hw_buffer);
//		break;
//
//	case TransWipeRight:
//		copy_row_bits(current_column++, src, hw_buffer);
//		break;
//
//	case TransScrollLeft:
//	case TransScrollRight:
//	case TransScrollUp:
//	case TransScrollDown:
//	case TransWipeUp:
//	case TransWipeDown:
//	case TransWipeUpDown:
//	case TransWipeLeftRight:
//	default:
//		stop();
//		return;
//	}
//}
//
//
////
//// copy_all_rows(src, dst)
////   Copies all rows of data from the image buffer to the hardware buffer
////   as described for copy_row_bits()
////
//void copy_all_rows(byte *src, byte *dst)
//{
//    for (int i = 0; i < N_ROWS; i++) {
//        copy_row_bits(i, src, dst);
//    }
//}
//
////
//// copy_row_bits(row, src, dst)
////   Copy a single row of matrix data from src to dst.
////   src is the image buffer we use for rendering what will
////   be displayed. It is arranged as the matrix is physically viewed:
////   | byte 0 | byte 1 | ... | byte 7 |
////   |76543210|76543210| ... |76543210|
////    ^_leftmost pixel     rightmost_^
////
////   dst is the hardware refresh buffer. It is arranged as expected 
////   by the hardware so that the bytes can be directly output during refresh.
////
////   The different hardware models arrange the column bits in a
////   different order, hence the COL_BLK_x defined symbols which
////   are hardware-dependent.
////
//void copy_row_bits(unsigned int row, byte *src, byte *dst)
//{
//    byte *d = dst + row * 8;
//    byte *s = src + row * 8;
//
//    d[0] = ((s[COL_BLK_0] & 0x01) << 7)
//         | ((s[COL_BLK_1] & 0x01) << 6)
//         | ((s[COL_BLK_2] & 0x01) << 5)
//         | ((s[COL_BLK_3] & 0x01) << 4)
//         | ((s[COL_BLK_4] & 0x01) << 3)
//         | ((s[COL_BLK_5] & 0x01) << 2)
//         | ((s[COL_BLK_6] & 0x01) << 1)
//         | ((s[COL_BLK_7] & 0x01) << 0);
//
//    d[1] = ((s[COL_BLK_0] & 0x02) << 6)
//         | ((s[COL_BLK_1] & 0x02) << 5)
//         | ((s[COL_BLK_2] & 0x02) << 4)
//         | ((s[COL_BLK_3] & 0x02) << 3)
//         | ((s[COL_BLK_4] & 0x02) << 2)
//         | ((s[COL_BLK_5] & 0x02) << 1)
//         | ((s[COL_BLK_6] & 0x02) << 0)
//         | ((s[COL_BLK_7] & 0x02) >> 1);
//
//    d[2] = ((s[COL_BLK_0] & 0x04) << 5)
//         | ((s[COL_BLK_1] & 0x04) << 4)
//         | ((s[COL_BLK_2] & 0x04) << 3)
//         | ((s[COL_BLK_3] & 0x04) << 2)
//         | ((s[COL_BLK_4] & 0x04) << 1)
//         | ((s[COL_BLK_5] & 0x04) << 0)
//         | ((s[COL_BLK_6] & 0x04) >> 1)
//         | ((s[COL_BLK_7] & 0x04) >> 2);
//
//    d[3] = ((s[COL_BLK_0] & 0x08) << 4)
//         | ((s[COL_BLK_1] & 0x08) << 3)
//         | ((s[COL_BLK_2] & 0x08) << 2)
//         | ((s[COL_BLK_3] & 0x08) << 1)
//         | ((s[COL_BLK_4] & 0x08) << 0)
//         | ((s[COL_BLK_5] & 0x08) >> 1)
//         | ((s[COL_BLK_6] & 0x08) >> 2)
//         | ((s[COL_BLK_7] & 0x08) >> 3);
//
//    d[4] = ((s[COL_BLK_0] & 0x10) << 3)
//         | ((s[COL_BLK_1] & 0x10) << 2)
//         | ((s[COL_BLK_2] & 0x10) << 1)
//         | ((s[COL_BLK_3] & 0x10) << 0)
//         | ((s[COL_BLK_4] & 0x10) >> 1)
//         | ((s[COL_BLK_5] & 0x10) >> 2)
//         | ((s[COL_BLK_6] & 0x10) >> 3)
//         | ((s[COL_BLK_7] & 0x10) >> 4);
//
//    d[5] = ((s[COL_BLK_0] & 0x20) << 2)
//         | ((s[COL_BLK_1] & 0x20) << 1)
//         | ((s[COL_BLK_2] & 0x20) << 0)
//         | ((s[COL_BLK_3] & 0x20) >> 1)
//         | ((s[COL_BLK_4] & 0x20) >> 2)
//         | ((s[COL_BLK_5] & 0x20) >> 3)
//         | ((s[COL_BLK_6] & 0x20) >> 4)
//         | ((s[COL_BLK_7] & 0x20) >> 5);
//
//    d[6] = ((s[COL_BLK_0] & 0x40) << 1)
//         | ((s[COL_BLK_1] & 0x40) << 0)
//         | ((s[COL_BLK_2] & 0x40) >> 1)
//         | ((s[COL_BLK_3] & 0x40) >> 2)
//         | ((s[COL_BLK_4] & 0x40) >> 3)
//         | ((s[COL_BLK_5] & 0x40) >> 4)
//         | ((s[COL_BLK_6] & 0x40) >> 5)
//         | ((s[COL_BLK_7] & 0x40) >> 6);
//
//    d[7] = ((s[COL_BLK_0] & 0x80) << 0)
//         | ((s[COL_BLK_1] & 0x80) >> 1)
//         | ((s[COL_BLK_2] & 0x80) >> 2)
//         | ((s[COL_BLK_3] & 0x80) >> 3)
//         | ((s[COL_BLK_4] & 0x80) >> 4)
//         | ((s[COL_BLK_5] & 0x80) >> 5)
//         | ((s[COL_BLK_6] & 0x80) >> 6)
//         | ((s[COL_BLK_7] & 0x80) >> 7);
//}
//
////
//// draw_character(col, font, codepoint, buffer, mergep=false) -> col'
////   Given a codepoint in a font, set the bits in the buffer for the pixels
////   of that character glyph, and return the starting column position for
////   the next character. 
////
////   If there is no such font or codepoint, nothing is done.
////
//int draw_character(byte col, byte font, unsigned int codepoint, byte *buffer, bool mergep=false)
//{
//    byte l, s;
//    unsigned int o;
//
//    if (!get_font_metric_data(font, codepoint, &l, &s, &o)) {
//        return col;
//    }
//    for (byte i=0; i<l; i++) {
//        if (col+i < 64) {
//            draw_column(col+i, get_font_bitmap_data(o+i), mergep, buffer);
//        }
//    }
//    return col+s;
//}
//
////
//// draw_column(col, bits, mergep, buffer)
////   Draw bits (LSB=top) onto the specified buffer column. If mergep is true,
////   merge with existing pixels instead of overwriting them.
////
//void draw_column(byte col, byte bits, bool mergep, byte *buffer)
//{
//    if (col < 64) {
//        byte bufcol = col / 8;
//        byte mask = 1 << (7 - (col % 8));
//
//        for (byte row=0; row<N_ROWS; row++) {
//            if (bits & (1 << row)) {
//                buffer[row*8+bufcol] |= mask;
//            }
//            else if (!mergep) {
//                buffer[row*8+bufcol] &= ~mask;
//            }
//        }
//    }
//}
//
//void shift_left(byte *buffer)
//{
//    for (byte row=0; row<N_ROWS; row++) {
//        for (byte col=0; col<8; col++) {
//            buffer[row*8+col] <<= 1;
//            if (col<7) {
//                if (buffer[row*8+col+1] & 0x80) {
//                    buffer[row*8+col] |= 0x01;
//                }
//                else {
//                    buffer[row*8+col] &= ~0x01;
//                }
//            }
//        }
//    }
//}
//
//
//
// The following LightBlinker support was adapted from the
// author's busylight project, which this project is intended
// to be compatible with.
//
const int LED_SEQUENCE_LEN = 64;
class LightBlinker {
    unsigned int on_period;
    unsigned int off_period;
    bool         cur_state;
    byte         cur_index;
    byte         sequence_length;
    byte         sequence[LED_SEQUENCE_LEN];
    TimerEvent   timer;

public:
    LightBlinker(unsigned int on, unsigned int off, void (*callback)(void));
    void update(void);
    void stop(void);
    void append(byte);
    int  length(void);
    void advance(void);
    void start(void);
    void report_state(void);
};

LightBlinker::LightBlinker(unsigned int on, unsigned int off, void (*callback)(void))
{
    timer.set(0, callback);
    timer.disable();
    cur_state = false;
    cur_index = 0;
    sequence_length = 0;
    on_period = on;
    off_period = off;
}

int LightBlinker::length(void)
{
    return sequence_length;
}

void LightBlinker::append(byte v)
{
    if (sequence_length < LED_SEQUENCE_LEN) {
        sequence[sequence_length++] = v;
    }
}

void LightBlinker::report_state(void)
{
/*    int i = 0;
    Serial.write(cur_state ? '1' : '0');
    if (sequence_length > 0) {
        Serial.write(cur_index + '0');
        Serial.write('@');
        for (i = 0; i < sequence_length; i++) {
            Serial.write(sequence[i] + '0');
        }
    }
    else {
        Serial.write('X');
    }*/
}

void discrete_set(byte l, bool value)
{
    if (l < 8) {
        digitalWrite(discrete_led_set[l], value? HIGH : LOW);
    }
}

bool discrete_query(byte l)
{
    if (l < 8) {
        return digitalRead(discrete_led_set[l]) == HIGH;
    }
    return false;
}
        
void LightBlinker::advance(void)
{
    if (sequence_length < 2) {
        if (cur_state) {
            discrete_set(sequence[0], false);
            cur_state = false;
            if (off_period > 0)
                timer.setPeriod(off_period);
        }
        else {
            discrete_set(sequence[0], true);
            cur_state = true;
            if (off_period > 0)
                timer.setPeriod(on_period);
        }
        return;
    }

    if (sequence_length > LED_SEQUENCE_LEN)
        sequence_length = LED_SEQUENCE_LEN;
    
    if (off_period == 0) {
        cur_state = true;
        discrete_set(sequence[cur_index], false);
        cur_index = (cur_index + 1) % sequence_length;
        discrete_set(sequence[cur_index], true);
    }
    else {
        if (cur_state) {
            discrete_set(sequence[cur_index], false);
            timer.setPeriod(off_period);
            cur_state = false;
        }
        else {
            cur_index = (cur_index + 1) % sequence_length;
            discrete_set(sequence[cur_index], true);
            timer.setPeriod(on_period);
            cur_state = true;
        }
    }
}

void LightBlinker::update(void)
{
    timer.update();
}

void LightBlinker::stop(void)
{
    timer.disable();
    sequence_length = 0;
}

void LightBlinker::start(void)
{
    if (sequence_length > 0) {
        cur_index = 0;
        cur_state = true;
        discrete_set(sequence[0], true);
        timer.reset();
        timer.setPeriod(on_period);
        timer.enable();
    }
    else {
        stop();
    }
}

void flash_lights(void);
void strobe_lights(void);
LightBlinker flasher(200,   0, flash_lights);
LightBlinker strober(50, 2000, strobe_lights);
//TransitionManager transitions;
//
//void next_transition(void)
//{
//	transitions.next();
//}
//
////
//// display_buffer(src, transition)
////   Copies the contents of the image buffer src to the hardware buffer, so that
////   is now what will be displayed on the LEDs.
////
////   If a transition effect is specified, the update to the hardware buffer will
////   be performed gradually to produce the desired visual effect.
////
//void display_buffer(byte *src, TransitionEffect transition)
//{
//	if (transition == NoTransition) {
//		transitions.stop();
//		copy_all_rows(src, hw_buffer);
//	}
//	else {
//		transitions.start_transition(src, transition, 100);
//	}
//}

void discrete_all_off(bool stop_blinkers)
{
  if (stop_blinkers) {
    flasher.stop();
    strober.stop();
  }
  for (int i=0; i < 8; i++) {
    digitalWrite(discrete_led_set[i], LOW);
  }
}

void flash_lights(void)
{
    flasher.advance();
}

void strobe_lights(void)
{
    strober.advance();
}

//#endif /* MODEL_CURRENT_64x8 */
//
//
// flag_init()
// flag_ready()
//   Indicate status flags using the discrete LEDs
//            GYYRRBBW
//            01234567
//     init   -------X initializing device (incl. waiting for UART)
//     ready  -------- ready for operation
//
void flag_init(void)
{
#if HW_CONTROL_LOGIC == HW_CONTROL_LOGIC_3xx
    digitalWrite(PIN_L0, LOW);
    digitalWrite(PIN_L1, LOW);
    digitalWrite(PIN_L2, LOW);
    digitalWrite(PIN_L3, LOW);
    digitalWrite(PIN_L4, LOW);
    digitalWrite(PIN_L5, LOW);
    digitalWrite(PIN_L6, LOW);
    digitalWrite(PIN_L7, HIGH);
#else
# error "HW_CONTROL_LOGIC not set to supported model"
#endif
}

void flag_ready(void)
{
#if HW_CONTROL_LOGIC == HW_CONTROL_LOGIC_3xx
    digitalWrite(PIN_L0, LOW);
    digitalWrite(PIN_L1, LOW);
    digitalWrite(PIN_L2, LOW);
    digitalWrite(PIN_L3, LOW);
    digitalWrite(PIN_L4, LOW);
    digitalWrite(PIN_L5, LOW);
    digitalWrite(PIN_L6, LOW);
    digitalWrite(PIN_L7, LOW);
#else
# error "HW_CONTROL_LOGIC not set to supported model"
#endif
}

void flag_test(void)
{
#if HW_CONTROL_LOGIC == HW_CONTROL_LOGIC_3xx
    for (int i = 0; i < LENGTH_OF(discrete_led_set); i++) {
        digitalWrite(discrete_led_set[i], HIGH);
        delay(100);
    }
    for (int i = 0; i < LENGTH_OF(discrete_led_set); i++) {
        digitalWrite(discrete_led_set[i], LOW);
        delay(100);
    }
    for (int i = 0; i < LENGTH_OF(discrete_led_set); i++) {
        digitalWrite(discrete_led_set[i], HIGH);
        delay(100);
        digitalWrite(discrete_led_set[i], LOW);
    }
    for (int i = LENGTH_OF(discrete_led_set) - 1; i >= 0; i--) {
        digitalWrite(discrete_led_set[i], HIGH);
        delay(100);
        digitalWrite(discrete_led_set[i], LOW);
    }
#endif
}

TimerEvent status_timer;
void strobe_status(void)
{
	static int status_value = 0;
	static int increment = 1;

	status_value += increment;
	if (status_value < 0) {
		status_value = 1;
		increment = 1;
	} 
	else if (status_value > 255) {
		status_value = 254;
		increment = -1;
	}
	analogWrite(PIN_STATUS_LED, status_value);
}

void setup_eeprom(void) 
{
#if HW_MC == HW_MC_MEGA_2560
# error "Support for Mega 2560 not implemented"
#else
# if HW_MC == HW_MC_DUE
#  if HAS_I2C_EEPROM
#   error "Support for Due w/external EEPROM not implemented"
#  else
#   warning "Arduino Due without external EEPROM module selected; cannot configure any system parameters!"
#   warning "*** Configure desired parameters into this firmware image as the 'default' values ***"
    USB_baud_rate_code = EE_DEFAULT_USB_SPEED;
    RS485_baud_rate_code = EE_DEFAULT_485_SPEED;
    my_device_address = EE_DEFAULT_ADDRESS;
    global_device_address = EE_DEFAULT_GLOBAL_ADDRESS
#  endif
# else
#  error "No valid HW_MC configured"
# endif
#endif
  /*
	if (EEPROM.read(EE_ADDR_SENTINEL) != EE_VALUE_SENTINEL) {
		// apparently unset; set to "factory defaults"
		EEPROM.write(EE_ADDR_USB_SPEED, EE_DEFAULT_SPEED);
		EEPROM.write(EE_ADDR_SENTINEL, EE_VALUE_SENTINEL);
		return;
	}

	int speed = EEPROM.read(EE_ADDR_USB_SPEED);
	if (!((speed >= '0' && speed <= '9') 
	|| (speed >= 'a' && speed <= 'c') 
	|| (speed >= 'A' && speed <= 'C'))) {
		// baud rate setting invalid; return to default
		EEPROM.write(EE_ADDR_USB_SPEED, EE_DEFAULT_SPEED);
	}
  */
    USB_baud_rate = parse_baud_rate_code(USB_baud_rate_code);
    RS485_baud_rate = parse_baud_rate_code(RS485_baud_rate_code);
}


//
// setup()
//   Called after reboot to set up the device.
//
void setup(void)
{
    setup_pins();
    flag_init();
    setup_eeprom();
//	default_eeprom_settings();

    flasher.stop();
    strober.stop();
//    setup_commands();
//    setup_buffers();
	status_timer.set(0, strobe_status);
	status_timer.reset();
	status_timer.setPeriod(10);
	status_timer.enable();

//	start_usb_serial();
//
//	// TODO If RS-485 is enabled, start that UART too
//
	flag_test();
	delay(ROW_HOLD_TIME_US);
    flag_ready();
//	// TODO display_text(font, string, mS_delay)
//	// TODO clear_matrix()
//	display_text(0, BANNER_HARDWARE_VERS,  500);
//	display_text(0, BANNER_FIRMWARE_VERS,  500);
//	display_text(0, BANNER_SERIAL_NUMBER, 1000);
//	display_text(0, "ADDRESS XX", 1000);	// 00-15 or --
//	display_text(0, "GLOBAL  XX", 1000);	// 00-15 or --
//	display_text(0, "USB XXXXXX", 1000);	// speed
//	display_text(0, "485 XXXXXX", 1000);	// speed or OFF
//	display_text(0, "MadScience",  500);
//	display_text(0, "Zone \xa92023",  500);
//	clear_matrix();
//	// 300 600 1200 2400 4800 9600 14400 19200 28800 31250 38400 57600 115200 OFF
}

int parse_baud_rate_code(byte code)
{
    switch (code) {
        case '0': return 300;
        case '1': return 600;
        case '2': return 1200;
        case '3': return 2400;
        case '4': return 4800;
        case '5': return 9600;
        case '6': return 14400;
        case '7': return 19200;
        case '8': return 28800;
        case '9': return 31250;
        case 'a':
        case 'A': return 38400;
        case 'b':
        case 'B': return 57600;
        case 'c':
        case 'C': return 115200;
    }
    return 0;
}

//void start_usb_serial(void) {
//	Serial.begin(speed);
//	while (!Serial);
//}
//
//
// loop()
//   Main loop of the firmware
//
void loop(void)
{
    flag_init();
    delay(250);
    flag_ready();
    delay(750);

	/* test readerboard columns individually */
#if HW_CONTROL_LOGIC == HW_CONTROL_LOGIC_3xx
    //                              //            _ _____
    //                              // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
    digitalWrite(PIN__G, HIGH);     //   X    X   1   X    X  X  X  X  X    disable column drains
    digitalWrite(PIN__SRCLR, LOW);  //   X    X   1   0    X  X  X  X  X    clear shift registers
    digitalWrite(PIN__SRCLR, HIGH); //   X    X   1   1    X  X  X  X  X    |
    digitalWrite(PIN_R4, HIGH);     //   X    X   1   1    1  X  X  X  X    disable row source outputs
    digitalWrite(PIN_R3, HIGH);     //   X    X   1   1    1  1  X  X  X    |
    digitalWrite(PIN_R2, LOW);      //   X    X   1   1    1  1  0  X  X	Select row 0
    digitalWrite(PIN_R1, LOW);      //   X    X   1   1    1  1  0  0  X    |
    digitalWrite(PIN_R0, LOW);      //   X    X   1   1    1  1  0  0  0    |
    digitalWrite(PIN_SRCLK, LOW);   //   0    X   1   1    1  1  0  0  0    clock idle
    digitalWrite(PIN_RCLK, LOW);    //   0    0   1   1    1  1  0  0  0    clock idle
                                    //
    test_pattern();
}

void test_pattern(void) 
{
	digitalWrite(PIN_D0, HIGH);		//   0    0   1   1    1  1  0  0  0    shift "on" bit in column 0
	digitalWrite(PIN_D1, HIGH);		//   0    0   1   1    1  1  0  0  0    shift "on" bit in column 0
	digitalWrite(PIN_D2, HIGH);		//   0    0   1   1    1  1  0  0  0    shift "on" bit in column 0
	digitalWrite(PIN_D3, HIGH);		//   0    0   1   1    1  1  0  0  0    shift "on" bit in column 0
	digitalWrite(PIN_D4, HIGH);		//   0    0   1   1    1  1  0  0  0    shift "on" bit in column 0
	digitalWrite(PIN_D5, HIGH);		//   0    0   1   1    1  1  0  0  0    shift "on" bit in column 0
	digitalWrite(PIN_D6, HIGH);		//   0    0   1   1    1  1  0  0  0    shift "on" bit in column 0
	digitalWrite(PIN_D7, HIGH);		//   0    0   1   1    1  1  0  0  0    shift "on" bit in column 0
	digitalWrite(PIN_SRCLK, HIGH);	//   1    0   1   1    1  1  0  0  0    clock in bit
	digitalWrite(PIN_SRCLK, LOW);	//   0    0   1   1    1  1  0  0  0    |
	digitalWrite(PIN_RCLK, HIGH);	//   0    1   1   1    1  1  0  0  0    clock data to output buffer
	digitalWrite(PIN_RCLK, LOW);	//   0    0   1   1    1  1  0  0  0    |
	digitalWrite(PIN_D0, LOW);		//   0    0   1   1    1  1  0  0  0    set up to shift "off" bits next
	digitalWrite(PIN_D1, LOW);		//   0    0   1   1    1  1  0  0  0    set up to shift "off" bits next
	digitalWrite(PIN_D2, LOW);		//   0    0   1   1    1  1  0  0  0    set up to shift "off" bits next
	digitalWrite(PIN_D3, LOW);		//   0    0   1   1    1  1  0  0  0    set up to shift "off" bits next
	digitalWrite(PIN_D4, LOW);		//   0    0   1   1    1  1  0  0  0    set up to shift "off" bits next
	digitalWrite(PIN_D5, LOW);		//   0    0   1   1    1  1  0  0  0    set up to shift "off" bits next
	digitalWrite(PIN_D6, LOW);		//   0    0   1   1    1  1  0  0  0    set up to shift "off" bits next
	digitalWrite(PIN_D7, LOW);		//   0    0   1   1    1  1  0  0  0    set up to shift "off" bits next
    for (int i=0; i<8; i++) {
        test_sequence_rows();
        digitalWrite(PIN_SRCLK, HIGH);	//   1    0   1   1    1  1  0  0  0    clock in bit
        digitalWrite(PIN_SRCLK, LOW);	//   0    0   1   1    1  1  0  0  0    |
        digitalWrite(PIN_RCLK, HIGH);	//   0    1   1   1    1  1  0  0  0    clock data to output buffer
        digitalWrite(PIN_RCLK, LOW);	//   0    0   1   1    1  1  0  0  0    |
    }
    test_sequence_rows();

    /* test high-speed full-matrix refresh */
#if HW_MODEL == MODEL_3xx_MONOCHROME
    test_row(0x04, 0xaa, 0x55);
    test_row(0x04, 0x55, 0xaa);
#else
    for (byte color=1; color <=7; color++) {
        test_row(color, 0xaa, 0x55);
    }
    for (byte color=1; color <=7; color++) {
        test_row(color, 0x55, 0xaa);
    }
#endif
    test_row(7, 0x01, 0x01);
    test_row(7, 0x02, 0x02);
    test_row(7, 0x04, 0x04);
    test_row(7, 0x08, 0x08);
    test_row(7, 0x10, 0x10);
    test_row(7, 0x20, 0x20);
    test_row(7, 0x40, 0x40);
    test_row(7, 0x80, 0x80);
}

void test_col(byte bit_pattern) 
{
    //                                  //            _ _____
    //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
    digitalWrite(PIN__SRCLR, LOW);	    //   X    X   1   0    X  X  X  X  X    reset shift register
    digitalWrite(PIN__SRCLR, HIGH);	    //   X    X   1   1    X  X  X  X  X    |
    for (int bit=7; bit >= 0; bit--) {
        digitalWrite(PIN_D0, (bit_pattern & (1 << bit))==0?LOW:HIGH);
        digitalWrite(PIN_D1, (bit_pattern & (1 << bit))==0?LOW:HIGH);
        digitalWrite(PIN_D2, (bit_pattern & (1 << bit))==0?LOW:HIGH);
        digitalWrite(PIN_D3, (bit_pattern & (1 << bit))==0?LOW:HIGH);
        digitalWrite(PIN_D4, (bit_pattern & (1 << bit))==0?LOW:HIGH);
        digitalWrite(PIN_D5, (bit_pattern & (1 << bit))==0?LOW:HIGH);
        digitalWrite(PIN_D6, (bit_pattern & (1 << bit))==0?LOW:HIGH);
        digitalWrite(PIN_D7, (bit_pattern & (1 << bit))==0?LOW:HIGH);
        //                              //            _ _____
        //                              // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
        digitalWrite(PIN_SRCLK, HIGH);	//   1    0   1   1    X  X  X  X  X    clock in bit
        digitalWrite(PIN_SRCLK, LOW);	//   0    0   1   1    X  X  X  X  X    |
    }
    //                                  //            _ _____
    //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
    digitalWrite(PIN_RCLK, HIGH);   	//   0    1   1   1    X  X  X  X  X    clock data to output buffer
    digitalWrite(PIN_RCLK, LOW);    	//   0    0   1   1    X  X  X  X  X    |
}

void test_row(byte color, byte bit_pattern1, byte bit_pattern2) 
{
    int rep;
#if HW_MODEL == MODEL_3xx_MONOCHROME
    rep = 200;
    color = 4;
#else
    if (color == 7) {
        rep = 67;
    } else if (color == 1 || color == 2 || color == 4) {
        rep = 200;
    } else {
        rep = 100;
    }
#endif

    for (int i=0; i<rep; i++) {
        if (color & 1) {
            digitalWrite(PIN_R4, LOW);  // red plane
            digitalWrite(PIN_R3, LOW);
            test_rows(bit_pattern1, bit_pattern2);
        }
        if (color & 2) {
            digitalWrite(PIN_R4, LOW);  // green plane
            digitalWrite(PIN_R3, HIGH); // 
            test_rows(bit_pattern1, bit_pattern2);
        }
        if (color & 4) {
            digitalWrite(PIN_R4, HIGH); // blue plane
            digitalWrite(PIN_R3, LOW);  // 
            test_rows(bit_pattern1, bit_pattern2);
        }
        digitalWrite(PIN_R4, HIGH); // all rows off
        digitalWrite(PIN_R3, HIGH); // 
    }
}

void test_rows(byte bit_pattern1, byte bit_pattern2)
{
    for (int r=0; r<8; r++) {
        digitalWrite(PIN__G, HIGH);
        test_col((r%2)==0?bit_pattern1:bit_pattern2);
        digitalWrite(PIN_R0, (r&0x01)==0?LOW:HIGH);
        digitalWrite(PIN_R1, (r&0x02)==0?LOW:HIGH);
        digitalWrite(PIN_R2, (r&0x04)==0?LOW:HIGH);
        digitalWrite(PIN__G, LOW);
        delayMicroseconds(ROW_HOLD_TIME_US);
        digitalWrite(PIN__G, HIGH);
    }
}

void test_sequence_rows(void) 
{
#if HW_MODEL == MODEL_3xx_MONOCHROME
    for (int row=16; row<24; row++)
#else
	for (int row=0; row<24; row++)
#endif
    {
		//												//            _ _____
		//												// SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
		digitalWrite(PIN_R0, (row&0x01)==0? LOW:HIGH);  //   0     0  1   1    1  1  0  0  X address row
		digitalWrite(PIN_R1, (row&0x02)==0? LOW:HIGH);  //   0     0  1   1    1  1  0  X  X |
		digitalWrite(PIN_R2, (row&0x04)==0? LOW:HIGH);  //   0     0  1   1    1  1  X  X  X |
		digitalWrite(PIN_R3, (row&0x08)==0? LOW:HIGH);  //   0     0  1   1    1  X  X  X  X |
		digitalWrite(PIN_R4, (row&0x10)==0? LOW:HIGH);  //   0     0  1   1    X  X  X  X  X |
		digitalWrite(PIN__G, LOW);                      //   0     0  0   1    X  X  X  X  X turn on columns
		delay(100);
		digitalWrite(PIN__G, HIGH);                     //   0     0  0   1    X  X  X  X  X turn off columns
	}
}
#else
# error "HW_CONTROL_LOGIC not set to a supported model"
#endif


//    unsigned long last_refresh = 0;
//    int cur_row = 0;
//
//    /* update the display every REFRESH_MS milliseconds */
//    if (millis() - last_refresh >= REFRESH_MS) {
//        LED_row_off();
//        LED_row_on(cur_row, hw_buffer);
//        cur_row = (cur_row + 1) % N_ROWS;
//        last_refresh = millis();
//    }
//
//#if HW_MODEL == MODEL_CURRENT_64x8 || HW_MODEL == MODEL_CURRENT_64x8_INTEGRATED
//    /* flash/strobe discrete LEDs as needed */
//    flasher.update();
//    strober.update();
//#endif
//	status_timer.update();
//	transitions.update();
//
//    /* receive commands via serial port */
//    if (Serial.available() > 0) {
//        receive_serial_data();
//    }
//
//	/* TODO receive commands via RS-485 port */
//}
