//TODO bounds checking on setting font index
//TODO eeprom
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
#include "fonts.h"
#include "readerboard.h"
#if HAS_I2C_EEPROM
# include "I2C_eeprom.h"
I2C_eeprom xEEPROM(0x50, I2C_DEVICESIZE_24LC256);
#elif HW_MC != HW_MC_DUE
# include <EEPROM.h>
#endif
#include "commands.h"

byte USB_baud_rate_code = EE_DEFAULT_USB_SPEED;
byte RS485_baud_rate_code = EE_DEFAULT_485_SPEED;
byte my_device_address = EE_DEFAULT_ADDRESS;
byte global_device_address =  EE_DEFAULT_GLOBAL_ADDRESS;
int USB_baud_rate = 0;
int RS485_baud_rate = 0;
bool RS485_enabled = false;
bool setup_completed = false;

#if IS_READERBOARD
void debug_image_buffer(byte buf[N_ROWS][N_COLS]);
void debug_hw_buffer(void);
#endif

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
#define EE_ADDR_SERIAL_NO (0x06)
#define EE_LENGTH_SERIAL_NO (7)
#define EE_ADDR_SENTINEL2  (EE_ADDR_SERIAL_NO+EE_LENGTH_SERIAL_NO)
char serial_number[EE_LENGTH_SERIAL_NO] = "";

/* I/O port to use for full 8-bit write operations to the sign board */
/* On the Mega 2560, PORTF<7:0> corresponds to <A7:A0> pins */
/* On the Due, we just toggle the bits separately. */
/* TODO consider if using PORTF is even a good idea considering interrupts and such */
#if IS_READERBOARD
# if HW_MC == HW_MC_MEGA_2560
#  define MATRIX_DATA_PORT    PORTF   
# endif
#endif

#define LENGTH_OF(A) (sizeof(A) / sizeof(A[0]))

#if IS_READERBOARD
/* Refresh the display every REFRESH_MS milliseconds. For an 8-row
 * display, an overall refresh rate of 30 FPS =  4.16 mS
 *                                     25 FPS =  5.00 mS
 *                                     20 FPS =  6.26 mS
 *                                     15 FPS =  8.33 mS
 *                                     10 FPS = 12.50 mS
 *                                      5 FPS = 25.00 mS
 */
const int ROW_HOLD_TIME_US = 500;   /* how long in microseconds to hold the row on during refresh */
#endif

#if HW_MC != HW_MC_PRO
const int PIN_STATUS_LED = 13;
#endif

// Hardware notes
// DUE    MEGA                 READERBOARD              DUE    MEGA     PRO
//                                                  LED 13~    13~
//                                                      12~    12~
//                                                      11~    11~
//                                                      10~    10~
//                                    L4                09~    09~      06
//                                    L5                08~    08~      07
//     
//                                    L6                07~    07~      10
//                                    L7                06~    06~      --
//  A0/54  A0/54                 D7   L3                05~    05~      08
//  A1/55  A0/55                 D6   L2                04~    04~      09
//  A2/56  A0/56                 D5   L1                03~    03~      14
//  A3/57  A0/57                 D4   L0                02~    02~      16
//  A4/58  A0/58                 D3   USB TxD           01/TX0 01/TX0~
//  A5/59  A0/59                 D2   USB RxD           00/RX0 00/RX0~
//  A6/60  A0/60                 D1                     
//  A7/61  A0/61                 D0   485 TxD           14/TX3 14/TX3
//                                    485 RxD           15/RX3 15/RX3
//  A8/62  A0/62              SRCLK   485 DE            16/TX2 16/TX2   02
//  A9/63  A0/53               /CLR   485 /RE           17/RX2 17/RX2   03
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
const int PIN_R3    = 19;   // row address 3
const int PIN_L0    =  2;   // discrete LED 0 (bottom)
const int PIN_L1    =  3;   // discrete LED 1 
const int PIN_L2    =  4;   // discrete LED 2
const int PIN_L3    =  5;   // discrete LED 3
const int PIN_L4    =  9;   // discrete LED 4
const int PIN_L5    =  8;   // discrete LED 5
const int PIN_L6    =  7;   // discrete LED 6
const int PIN_L7    =  6;   // discrete LED 7 (top) 
const int PIN_DE    = 16;   // RS-485 driver enable (1=enabled)
const int PIN__RE   = 17;   // RS-485 ~receiver enable (0=enabled)
#elif HW_CONTROL_LOGIC == HW_CONTROL_LOGIC_B_1xx
const int PIN_L0    = 16;   // discrete LED 0 (bottom)
const int PIN_L1    = 14;   // discrete LED 1 
const int PIN_L2    =  9;   // discrete LED 2 
const int PIN_L3    =  8;   // discrete LED 3 
const int PIN_L4    =  6;   // discrete LED 4 
const int PIN_L5    =  7;   // discrete LED 5 
const int PIN_L6    = 10;   // discrete LED 6 (top)
#elif HW_CONTROL_LOGIC == HW_CONTROL_LOGIC_B_2xx
const int PIN_L0    = 16;   // discrete LED 0 (bottom)
const int PIN_L1    = 14;   // discrete LED 1 
const int PIN_L2    =  9;   // discrete LED 2 
const int PIN_L3    =  8;   // discrete LED 3 
const int PIN_L4    =  6;   // discrete LED 4 
const int PIN_L5    =  7;   // discrete LED 5 
const int PIN_L6    = 10;   // discrete LED 6 (top)
const int PIN_DE    =  2;   // RS-485 driver enable (1=enabled)
const int PIN__RE   =  3;   // RS-485 ~receiver enable (0=enabled)
#else
# error "HW_CONTROL_LOGIC not defined to supported model"
#endif

#if IS_READERBOARD
const int discrete_led_set[8] = {PIN_L0, PIN_L1, PIN_L2, PIN_L3, PIN_L4, PIN_L5, PIN_L6, PIN_L7};
const byte discrete_led_labels[8] = {
    R_STATUS_LED_COLOR_L0,
    R_STATUS_LED_COLOR_L1,
    R_STATUS_LED_COLOR_L2,
    R_STATUS_LED_COLOR_L3,
    R_STATUS_LED_COLOR_L4,
    R_STATUS_LED_COLOR_L5,
    R_STATUS_LED_COLOR_L6,
    R_STATUS_LED_COLOR_L7,
};
const byte column_block_set[8] = {PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_D4, PIN_D5, PIN_D6, PIN_D7};
#else
const int discrete_led_set[7] = {PIN_L0, PIN_L1, PIN_L2, PIN_L3, PIN_L4, PIN_L5, PIN_L6};
const byte discrete_led_labels[7] = {
# ifdef SN_B0001
    '0',
    '1',
    'G',
    'Y',
    'R',
    'r',
    'B',
# else
    B_STATUS_LED_COLOR_L0,
    B_STATUS_LED_COLOR_L1,
    B_STATUS_LED_COLOR_L2,
    B_STATUS_LED_COLOR_L3,
    B_STATUS_LED_COLOR_L4,
    B_STATUS_LED_COLOR_L5,
    B_STATUS_LED_COLOR_L6,
# endif
};
#endif

//
// setup_pins()
//   Setup the I/O pin modes and initialize the sign hardware.
//
void setup_pins(void)
{
#if HW_MC != HW_MC_PRO
	pinMode(PIN_STATUS_LED, OUTPUT);
#endif
#if IS_READERBOARD
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
#endif
#if HW_MODEL != MODEL_BUSYLIGHT_1
    pinMode(PIN_DE, OUTPUT);
    pinMode(PIN__RE, OUTPUT);
#endif
    pinMode(PIN_L0, OUTPUT);
    pinMode(PIN_L1, OUTPUT);
    pinMode(PIN_L2, OUTPUT);
    pinMode(PIN_L3, OUTPUT);
    pinMode(PIN_L4, OUTPUT);
    pinMode(PIN_L5, OUTPUT);
    pinMode(PIN_L6, OUTPUT);
#if IS_READERBOARD
    pinMode(PIN_L7, OUTPUT);
#endif
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
    digitalWrite(PIN_L7, LOW);
#endif
    digitalWrite(PIN_L0, LOW);      // turn off discrete LEDs
    digitalWrite(PIN_L1, LOW);
    digitalWrite(PIN_L2, LOW);
    digitalWrite(PIN_L3, LOW);
    digitalWrite(PIN_L4, LOW);
    digitalWrite(PIN_L5, LOW);
    digitalWrite(PIN_L6, LOW);
#if HW_MODEL != MODEL_BUSYLIGHT_1
    digitalWrite(PIN_DE, LOW);      // RS-485 driver disabled
    digitalWrite(PIN__RE, HIGH);    // RS-485 receiver disabled
#endif
}

#if IS_READERBOARD
byte image_buffer[N_ROWS][N_COLS];              // one pixel per element, value = <frgb> bit-encoded
byte hw_buffer[N_COLORS][N_ROWS][N_COLBYTES];   // pixels arranged by color plane as convenient for display refresh
byte hw_active_color_planes = 0;                // which color planes are currently needing to be included in refresh?

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
//
// setup_buffers()
//   Initialize buffers to zero.
//
void setup_buffers(void)
{
    clear_all_buffers();
}

void clear_all_buffers(void)
{
    clear_image_buffer();
    clear_hw_buffer();
}

void clear_image_buffer(void)
{
    transitions.stop();
    for (int row=0; row<N_ROWS; row++) {
        for (int col=0; col<N_COLS; col++) {
            image_buffer[row][col] = 0;
        }
    }
}

void clear_hw_buffer(void)
{
    for (int plane=0; plane<N_COLORS; plane++) {
        for (int row=0; row<N_ROWS; row++) {
            for (int col=0; col<N_COLBYTES; col++) {
                hw_buffer[plane][row][col] = 0;
            }
        }
    }
    hw_active_color_planes = 0;
}


void TransitionManager::set_stage(void)
{
    for (int row=0; row < N_ROWS; row++) {
        for (int col=0; col < N_COLS; col++) {
            stage[row][col] = (*src)[row][col];
        }
    }
}

//
// Special internal transition effect to scroll a text buffer
// across the screen
//
// We keep a pointer to the source text for the current character
// we're rendering. We start by rendering at the far right column
// then scroll and render the same character one position to the left
// until we reach the full character space, at which point we move
// to the next character and repeat, cycling back to the start of
// the string if requested.
void TransitionManager::start_scrolling_text(const char *text, int len, bool repeat, byte font, byte color, int delay_mS)
{
    scroll_src = text;
    scroll_repeat = repeat;
    scroll_pos = 0;
    scroll_col = N_COLS - 1;
    scroll_font = font;
    scroll_color = color;
    scroll_len = len;
    transition = _TransScrollText;
    next(false);
    timer.reset();
    timer.setPeriod(delay_mS);
    timer.enable();
}

//void next_transition(void);
//
TransitionManager::TransitionManager(void)
{
	src = &image_buffer;
	transition = NoTransition;
	timer.set(0, next_transition);
	timer.disable();
}

void TransitionManager::update(void)
{
	timer.update();
}

void TransitionManager::stop(void)
{
	timer.disable();
}

void TransitionManager::start_transition(TransitionEffect trans, int delay_ms)
{
	transition = trans;
	if (trans == NoTransition) {
		stop();
        commit_image_buffer(*src);
	}
	else {
		next(true);
		timer.reset(); 
		timer.setPeriod(delay_ms); 
		timer.enable();
	}
}

void TransitionManager::next(bool reset_column)
{
    if (transition == _TransScrollText) {
        if (scroll_pos >= scroll_len) {
            scroll_pos = 0;
            scroll_col = N_COLS - 1;
            if (!scroll_repeat) {
                stop();
                return;
            }
        }
        while (scroll_pos < scroll_len) {
            switch (scroll_src[scroll_pos]) {
                case 0x03:
                case 0x08:
                case 0x0c:
                    if (scroll_pos += 2 >= scroll_len)
                        return;
                    break;

                case 0x06:
                    if (++scroll_pos >= scroll_len)
                        return;
                    scroll_font = min(decode_int6(scroll_src[scroll_pos++]), N_FONTS-1);
                    break;

                case 0x0b:
                    if (++scroll_pos >= scroll_len)
                        return;
                    scroll_color = decode_rgb(scroll_src[scroll_pos++]);
                    break;

                default:
                    unsigned char l, s;
                    unsigned short o;

                    if (!get_font_metric_data(scroll_font, scroll_src[scroll_pos], &l, &s, &o)) {
                        ++scroll_pos;
                        scroll_col = N_COLS - 1;
                        return;
                    }
                    if (scroll_col + s < N_COLS) {
                        scroll_col = N_COLS - 1;
                        if (++scroll_pos >= scroll_len) {
                            scroll_pos = 0;
                            if (!scroll_repeat) {
                                stop();
                                return;
                            }
                        }
                        continue;   // re-evaluate the next byte in case it's a control sequence
                    }

                    shift_left(*src);
                    draw_character(scroll_col, scroll_font, scroll_src[scroll_pos], *src, scroll_color);
                    commit_image_buffer(*src);
                    --scroll_col;
                    return;
            }
        }
        return;
    }

    // We call this current_column because for most transitions, it's the column being moved
    // in the transition effect. In some, though, it is actually the row being moved.
	static int current_column = 0;

	if (reset_column) {
		current_column = 0;
	} else {
        ++current_column;
    }


	if (current_column >= N_COLS) {
		stop();
		return;
	}

	switch (transition) {
	case TransWipeLeft:
        for (int row=0; row<N_ROWS; row++) {
            stage[row][current_column] = (*src)[row][current_column];
        }
        break;

    case TransWipeRight:
        for (int row=0; row<N_ROWS; row++) {
            stage[row][N_COLS-1-current_column] = (*src)[row][N_COLS-1-current_column];
        }
        break;

	case TransWipeLeftRight:
        {
            int mid = N_COLS/2;
            if (mid+current_column >= N_COLS) {
                stop();
                return;
            }
            for (int row=0; row<N_ROWS; row++) {
                stage[row][mid+current_column] = (*src)[row][mid+current_column];
                stage[row][mid-1-current_column] = (*src)[row][mid-1-current_column];
            }
        }
        break;

    case TransWipeDown:
        if (current_column >= N_ROWS) {
            stop();
            return;
        }
        for (int col=0; col<N_COLS; col++) {
            stage[current_column][col] = (*src)[current_column][col];
        }
        break;

    case TransWipeUp:
        if (current_column >= N_ROWS) {
            stop();
            return;
        }
        for (int col=0; col<N_COLS; col++) {
            stage[N_ROWS-1-current_column][col] = (*src)[N_ROWS-1-current_column][col];
        }
        break;

    case TransWipeUpDown:
        {
            int mid = N_ROWS/2;
            if (mid+current_column >= N_ROWS) {
                stop();
                return;
            }
            for (int col=0; col < N_COLS; col++) {
                stage[mid+current_column][col] = (*src)[mid+current_column][col];
                stage[mid-1-current_column][col] = (*src)[mid-1-current_column][col];
            }
        }
        break;

	case TransScrollLeft:
        shift_left(stage);
        for (int row=0; row<N_ROWS; row++) {
            stage[row][N_COLS-1] = (*src)[row][current_column];
        }
        break;

	case TransScrollRight:
        shift_right(stage);
        for (int row=0; row<N_ROWS; row++) {
            stage[row][0] = (*src)[row][N_COLS-1-current_column];
        }
        break;

	case TransScrollUp:
        if (current_column >= N_ROWS) {
            stop();
            return;
        }
        shift_up(stage);
        for (int col=0; col<N_COLS; col++) {
            stage[N_ROWS-1][col] = (*src)[current_column][col];
        }
        break;

	case TransScrollDown:
        if (current_column >= N_ROWS) {
            stop();
            return;
        }
        shift_down(stage);
        for (int col=0; col<N_COLS; col++) {
            stage[0][col] = (*src)[N_ROWS-1-current_column][col];
        }
        break;

	default:
		stop();
		return;
	}
    commit_image_buffer(stage);
}

//
// draw_character(col, font, codepoint, buffer, color, mergep=false) -> col'
//   Given a codepoint in a font, set the bits in the buffer for the pixels
//   of that character glyph, and return the starting column position for
//   the next character. 
//
//   If there is no such font or codepoint, nothing is done.
//
byte draw_character(byte col, byte font, byte codepoint, byte buffer[N_ROWS][N_COLS], byte color, bool mergep)
{
    unsigned char l, s;
    unsigned short o;

    if (col >= N_COLS) {
        return col;
    }

    if (!get_font_metric_data(font, codepoint, &l, &s, &o)) {
        return col;
    }
    for (byte i=0; i<l; i++) {
        if (col+i < N_COLS) {
            draw_column(col+i, get_font_bitmap_data(o+i), color, mergep, buffer);
        }
    }
    return col+s;
}

//
// draw_column(col, bits, mergep, buffer)
//   Draw bits (LSB=top) onto the specified buffer column. If mergep is true,
//   merge with existing pixels instead of overwriting them.
//
void draw_column(byte col, byte bits, byte color, bool mergep, byte buffer[N_ROWS][N_COLS])
{
    if (col < N_COLS) {
        for (byte row=0; row<N_ROWS; row++) {
            if (bits & (1 << (row))) {
                buffer[row][col] = color;
            }
            else if (!mergep) {
                buffer[row][col] = 0;
            }
        }
    }
}
#endif /* IS_READERBOARD */

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
    void report_state(void (*sendfunc)(byte));
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

void LightBlinker::report_state(void (*sendfunc)(byte))
{
    (*sendfunc)(timer.isEnabled()? 'R' : 'S');
    if (sequence_length > 0) {
        (*sendfunc)(encode_int6(cur_index));
        (*sendfunc)('@');
        for (int i = 0; i < sequence_length; i++) {
            (*sendfunc)(encode_led(sequence[i]));
        }
    } else {
        (*sendfunc)('_');
    }
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

#if IS_READERBOARD
TransitionManager transitions;

void next_transition(void)
{
	transitions.next();
}

//
// display_buffer(src, transition)
//   Copies the contents of the image buffer src to the hardware buffer, so that
//   is now what will be displayed on the LEDs.
//
//   If a transition effect is specified, the update to the hardware buffer will
//   be performed gradually to produce the desired visual effect.
//
void display_buffer(byte src[N_ROWS][N_COLS], TransitionEffect transition)
{
	if (transition == NoTransition) {
        transitions.stop();
        commit_image_buffer(src);
	}
	else {
		transitions.start_transition(transition, 100);
	}
}
#endif /* IS_READERBOARD */

void discrete_all_off(bool stop_blinkers)
{
  if (stop_blinkers) {
    flasher.stop();
    strober.stop();
  }
  for (int i=0; i < LENGTH_OF(discrete_led_set); i++) {
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
    for (int i=0; i<LENGTH_OF(discrete_led_set); i++) {
        digitalWrite(discrete_led_set[i], LOW);
    }
    for (int i=0; i<LENGTH_OF(discrete_led_set); i++) {
        digitalWrite(discrete_led_set[i], HIGH);
        delay(100);
        digitalWrite(discrete_led_set[i], LOW);
    }

    digitalWrite(discrete_led_set[LENGTH_OF(discrete_led_set)-1], HIGH);
}

void flag_ready(void)
{
    for (int i=0; i<LENGTH_OF(discrete_led_set); i++) {
        digitalWrite(discrete_led_set[i], LOW);
    }
}

void flag_test(void)
{
#if IS_READERBOARD
    for (int i = 0; i < LENGTH_OF(discrete_led_set); i++) {
        digitalWrite(discrete_led_set[i], HIGH);
        delay(100);
    }
    for (int i = 0; i < LENGTH_OF(discrete_led_set); i++) {
        digitalWrite(discrete_led_set[i], LOW);
        delay(100);
    }
#else
    for (int i = 0; i < LENGTH_OF(discrete_led_set); i++) {
        digitalWrite(discrete_led_set[i], LOW);
    }
#endif

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
}

#if IS_READERBOARD
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
#endif

void signal_eeprom_access(byte color, int hold_time=500, bool showp=false)
{
#if IS_READERBOARD
    render_text(image_buffer, N_COLS-7, 2, "\x8f", color, true);
    commit_image_buffer(image_buffer);
    if (!setup_completed) {
        show_hw_buffer(hold_time);
    } else if (showp) {
        delay(hold_time);
    }
#endif
}

void reset_eeprom_values(void)
{
    signal_eeprom_access(3);
#if HAS_I2C_EEPROM
    if (xEEPROM.writeByte(EE_ADDR_LAYOUT, EE_VALUE_LAYOUT) != 0
    || xEEPROM.writeByte(EE_ADDR_USB_SPEED, EE_DEFAULT_USB_SPEED) != 0
    || xEEPROM.writeByte(EE_ADDR_485_SPEED, EE_DEFAULT_485_SPEED) != 0
    || xEEPROM.writeByte(EE_ADDR_DEVICE_ADDR, EE_DEFAULT_ADDRESS) != 0
    || xEEPROM.writeByte(EE_ADDR_GLOBAL_ADDR, EE_DEFAULT_GLOBAL_ADDRESS) != 0
    || xEEPROM.writeByte(EE_ADDR_SENTINEL, EE_VALUE_SENTINEL) != 0
    || xEEPROM.writeByte(EE_ADDR_SENTINEL2, EE_VALUE_SENTINEL) != 0
    || xEEPROM.writeByte(EE_ADDR_SERIAL_NO, 0) != 0) {
        signal_eeprom_access(9, 5000, true);     /* signal error */
    }
#elif HW_MC != HW_MC_DUE
    EEPROM.write(EE_ADDR_LAYOUT, EE_VALUE_LAYOUT);
    EEPROM.write(EE_ADDR_USB_SPEED, EE_DEFAULT_USB_SPEED);
    EEPROM.write(EE_ADDR_485_SPEED, EE_DEFAULT_485_SPEED);
    EEPROM.write(EE_ADDR_DEVICE_ADDR, EE_DEFAULT_ADDRESS);
    EEPROM.write(EE_ADDR_GLOBAL_ADDR, EE_DEFAULT_GLOBAL_ADDRESS);
    EEPROM.write(EE_ADDR_SENTINEL, EE_VALUE_SENTINEL);
    EEPROM.write(EE_ADDR_SENTINEL2, EE_VALUE_SENTINEL);
    EEPROM.write(EE_ADDR_SERIAL_NO, 0);
#else
    signal_eeprom_access(9, 5000, true);     /* signal error */
#endif
}

void store_serial_number(const char *sn)
{
    signal_eeprom_access(3);
#if HAS_I2C_EEPROM
    if (xEEPROM.writeBlock(EE_ADDR_SERIAL_NO, (const byte *)sn, EE_LENGTH_SERIAL_NO) != 0) {
        signal_eeprom_access(9, 5000, true);     /* signal error */
    }
    strncpy(serial_number, sn, EE_LENGTH_SERIAL_NO);
    serial_number[EE_LENGTH_SERIAL_NO-1] = '\0';
#elif HW_MC == HW_MC_DUE
    signal_eeprom_access(9, 5000, true);     /* signal error */
#else
    int i;
    for (i=0; i<EE_LENGTH_SERIAL_NO-1 && sn[i] != '\0'; i++) {
# if HW_MC != HW_MC_DUE
        EEPROM.write(EE_ADDR_SERIAL_NO+i, sn[i]);
# endif
        serial_number[i] = sn[i];
    }
    for (; i<EE_LENGTH_SERIAL_NO; i++) {
# if HW_MC != HW_MC_DUE
        EEPROM.write(EE_ADDR_SERIAL_NO+i, 0);
# endif
        serial_number[i] = '\0'; 
    }
#endif
}

void save_eeprom(void)
{
#if HAS_I2C_EEPROM
    signal_eeprom_access(2);
    if (xEEPROM.readByte(EE_ADDR_SENTINEL) != EE_VALUE_SENTINEL
    ||  xEEPROM.readByte(EE_ADDR_SENTINEL2) != EE_VALUE_SENTINEL
    ||  xEEPROM.readByte(EE_ADDR_LAYOUT) != EE_VALUE_LAYOUT) {
        // apparently unset; store "factory default" values now
        reset_eeprom_values();
    }

    signal_eeprom_access(3);
    if (xEEPROM.writeByte(EE_ADDR_USB_SPEED, USB_baud_rate_code) != 0
    || xEEPROM.writeByte(EE_ADDR_485_SPEED, RS485_baud_rate_code) != 0
    || xEEPROM.writeByte(EE_ADDR_DEVICE_ADDR, my_device_address) != 0
    || xEEPROM.writeByte(EE_ADDR_GLOBAL_ADDR, global_device_address) != 0) {
        signal_eeprom_access(9, 5000, true);     /* signal error */
    }
#elif HW_MC == HW_MC_MEGA_2560
# error "Support for Mega 2560 not implemented"
    signal_eeprom_access(9, 5000, true);     /* signal error */
#elif HW_MC == HW_MC_DUE
    signal_eeprom_access(9, 5000, true);     /* signal error */
# warning "Arduino Due without external EEPROM module is selected!"
# warning "*** The unit will not be able to persistently store device settings or S/N."
# warning "*** Configure any desired settings here in firmware as 'defaults'."
#elif HW_MC == HW_MC_PRO
    if (EEPROM.read(EE_ADDR_SENTINEL) != EE_VALUE_SENTINEL
    ||  EEPROM.read(EE_ADDR_SENTINEL2) != EE_VALUE_SENTINEL
    ||  EEPROM.read(EE_ADDR_LAYOUT) != EE_VALUE_LAYOUT) {
        // apparently unset; store "factory default" values now
        reset_eeprom_values();
    }

    EEPROM.write(EE_ADDR_USB_SPEED, USB_baud_rate_code);
    EEPROM.write(EE_ADDR_485_SPEED, RS485_baud_rate_code);
    EEPROM.write(EE_ADDR_DEVICE_ADDR, my_device_address);
    EEPROM.write(EE_ADDR_GLOBAL_ADDR, global_device_address);
#else
# error "No valid HW_MC configured"
#endif
}

void setup_eeprom(void) 
{
    USB_baud_rate_code = EE_DEFAULT_USB_SPEED;
    RS485_baud_rate_code = EE_DEFAULT_485_SPEED;
    my_device_address = EE_DEFAULT_ADDRESS;
    global_device_address = EE_DEFAULT_GLOBAL_ADDRESS;
    USB_baud_rate = parse_baud_rate_code(USB_baud_rate_code);
    RS485_baud_rate = parse_baud_rate_code(RS485_baud_rate_code);
#ifdef BESPOKE_SERIAL_NUMBER
    strncpy(serial_number, BESPOKE_SERIAL_NUMBER, EE_LENGTH_SERIAL_NO);
    serial_number[EE_LENGTH_SERIAL_NO-1] = '\0';
#endif


#if HAS_I2C_EEPROM
    signal_eeprom_access(2);
    if (xEEPROM.readByte(EE_ADDR_SENTINEL) != EE_VALUE_SENTINEL
    ||  xEEPROM.readByte(EE_ADDR_SENTINEL2) != EE_VALUE_SENTINEL
    ||  xEEPROM.readByte(EE_ADDR_LAYOUT) != EE_VALUE_LAYOUT) {
        // apparently unset; store "factory default" values now
        reset_eeprom_values();
    }

    USB_baud_rate_code = xEEPROM.readByte(EE_ADDR_USB_SPEED);
    RS485_baud_rate_code = xEEPROM.readByte(EE_ADDR_485_SPEED);
    USB_baud_rate = parse_baud_rate_code(USB_baud_rate_code);
    RS485_baud_rate = parse_baud_rate_code(RS485_baud_rate_code);
    if (USB_baud_rate == 0) {
        signal_eeprom_access(3);
        if (xEEPROM.writeByte(EE_ADDR_USB_SPEED, EE_DEFAULT_USB_SPEED) != 0) {
            signal_eeprom_access(9, 5000, true);     /* signal error */
        }
        USB_baud_rate_code = EE_DEFAULT_USB_SPEED;
        USB_baud_rate = parse_baud_rate_code(USB_baud_rate_code);
    }
    if (RS485_baud_rate == 0) {
        signal_eeprom_access(3);
        if (xEEPROM.writeByte(EE_ADDR_485_SPEED, EE_DEFAULT_485_SPEED) != 0) {
            signal_eeprom_access(9, 5000, true);     /* signal error */
        }
        USB_baud_rate_code = EE_DEFAULT_485_SPEED;
        RS485_baud_rate = parse_baud_rate_code(RS485_baud_rate_code);
    }

    xEEPROM.readBlock(EE_ADDR_SERIAL_NO, (byte *)serial_number, EE_LENGTH_SERIAL_NO);
    serial_number[EE_LENGTH_SERIAL_NO-1] = '\0';
#elif HW_MC == HW_MC_MEGA_2560
# error "Support for Mega 2560 not implemented"
    signal_eeprom_access(9, 5000, true);     /* signal error */
#elif HW_MC == HW_MC_DUE
    signal_eeprom_access(9, 5000, true);     /* signal error */
# warning "Arduino Due without external EEPROM module selected"
#elif HW_MC == HW_MC_PRO
    if (EEPROM.read(EE_ADDR_SENTINEL) != EE_VALUE_SENTINEL
    ||  EEPROM.read(EE_ADDR_SENTINEL2) != EE_VALUE_SENTINEL
    ||  EEPROM.read(EE_ADDR_LAYOUT) != EE_VALUE_LAYOUT) {
        // apparently unset; store "factory default" values now
        reset_eeprom_values();
    }

    USB_baud_rate_code = EEPROM.read(EE_ADDR_USB_SPEED);
    RS485_baud_rate_code = EEPROM.read(EE_ADDR_485_SPEED);
    USB_baud_rate = parse_baud_rate_code(USB_baud_rate_code);
    RS485_baud_rate = parse_baud_rate_code(RS485_baud_rate_code);
    if (USB_baud_rate == 0) {
        EEPROM.write(EE_ADDR_USB_SPEED, EE_DEFAULT_USB_SPEED);
        USB_baud_rate_code = EE_DEFAULT_USB_SPEED;
        USB_baud_rate = parse_baud_rate_code(USB_baud_rate_code);
    }
    if (RS485_baud_rate == 0) {
        EEPROM.write(EE_ADDR_485_SPEED, EE_DEFAULT_485_SPEED);
        USB_baud_rate_code = EE_DEFAULT_485_SPEED;
        RS485_baud_rate = parse_baud_rate_code(RS485_baud_rate_code);
    }

    for (int i=0; i<EE_LENGTH_SERIAL_NO; i++) {
        serial_number[i] = EEPROM.read(EE_ADDR_SERIAL_NO+i);
    }
#else
# error "No valid HW_MC configured"
#endif
}

#if IS_READERBOARD
//
// display_text(font, string, color, mS_delay)
// Replace the displayed image with the given text and delay (blocking) for a time.
// This is used when the sign isn't in normal operational mode (e.g., startup)
// and doesn't rely on any background tasks (including the refresh) to be running.
// 
void display_text(byte font, const char *string, byte color, int mS_delay)
{
    clear_image_buffer();
	render_text(image_buffer, 0, font, string, color);
    commit_image_buffer(image_buffer);
#ifdef SERIAL_DEBUG
    debug_hw_buffer();
#endif
    
    /* display the hardware buffer for the delay time */
    show_hw_buffer(mS_delay);
}

//
// render_text(buffer, pos, font, string, color)
// draw text at the given starting position in the image buffer.
//
byte render_text(byte buffer[N_ROWS][N_COLS], byte pos, byte font, const char *string, byte color, bool mergep)
{
    if (string == NULL) {
        return pos;
    }

    /* draw characters onto the image buffer */
    for (; *string != '\0'; string++) {
        if (*string == '\003') {
            if (*++string == '\0')
                break;
            pos = decode_pos(*string, pos);
        }
        else if (*string == '\006') {
            if (*++string == '\0')
                break;
            font = decode_int6(*string);
        }
        else if (*string == '\010') {
            if (*++string == '\0')
                break;
            pos -= decode_int6(*string);
        }
        else if (*string == '\013') {
            if (*++string == '\0')
                break;
            color = decode_rgb(*string);
        }
        else if (*string == '\014') {
            if (*++string == '\0')
                break;
            pos += decode_int6(*string);
        }
        else if (*string == '\030') {
            char n1, n2;
            if ((n1 = *++string) == '\0' || (n2 = *++string) == '\0')
                break;
            pos = draw_character(pos, font, parse_hex_nybble_pair(n1, n2), buffer, color, mergep);
        }
        else {
            pos = draw_character(pos, font, *string, buffer, color, mergep);
        }
    }

    /* transfer to the hardware buffer */
#ifdef SERIAL_DEBUG
    debug_image_buffer(buffer);
#endif
    return pos;
}


//
// show_hw_buffer(int milliseconds)
// directly show the hardware buffer on the display. Uses no background routines
// and so should never be used when the hardware is in normal operating mode.
//
void show_hw_buffer(int duration_mS)
{
    unsigned long deadline = millis() + duration_mS;

    while (millis() < deadline) {
        //                                  //            _ _____
        //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
        digitalWrite(PIN__G, HIGH);         //   X     X  1   X    X  X  X  X  X  disable column drains
        digitalWrite(PIN__SRCLR, LOW);      //   X     X  1   0    X  X  X  X  X  reset shift registers
        digitalWrite(PIN__SRCLR, HIGH);     //   X     X  1   1    X  X  X  X  X  |
        for (int plane = 0; plane < N_COLORS; plane++) {
            int planebit = 1 << plane;
            if (plane == N_FLASHING_PLANE)
                continue;

#if HW_MODEL == MODEL_3xx_MONOCHROME
            //                                  //            _ _____
            //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
            digitalWrite(PIN_R4, HIGH);         //   X     X  1   1    1  X  X  X  X  set blue plane
            digitalWrite(PIN_R3, LOW);          //   X     X  1   1    1  0  X  X  X  |
#else
# if HW_MODEL == MODEL_3xx_RGB
            if (!(hw_active_color_planes & planebit)) {
                continue;
            }
            if (planebit == BIT_RGB_RED) {
                //                                  //            _ _____
                //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
                digitalWrite(PIN_R4, LOW);          //   X     X  1   1    0  X  X  X  X  set red plane
                digitalWrite(PIN_R3, LOW);          //   X     X  1   1    0  0  X  X  X  |
            } else if (planebit == BIT_RGB_GREEN) {
                //                                  //            _ _____
                //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
                digitalWrite(PIN_R4, LOW);          //   X     X  1   1    0  X  X  X  X  set green plane
                digitalWrite(PIN_R3, HIGH);         //   X     X  1   1    0  1  X  X  X  |
            } else if (planebit == BIT_RGB_BLUE) {
                //                                  //            _ _____
                //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
                digitalWrite(PIN_R4, HIGH);         //   X     X  1   1    1  X  X  X  X  set blue plane
                digitalWrite(PIN_R3, LOW);          //   X     X  1   1    1  0  X  X  X  |
            } else {
                //                                  //            _ _____
                //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
                digitalWrite(PIN_R4, HIGH);         //   X     X  1   1    1  X  X  X  X  set NO plane
                digitalWrite(PIN_R3, HIGH);         //   X     X  1   1    1  1  X  X  X  |
            }
# else
#  error "hw model not set"
# endif
#endif

            /* now push out the column data */
            for (int row=0; row < N_ROWS; row++) {
                for (int cblk=0; cblk < N_COLBYTES; cblk++) {
                    digitalWrite(PIN_D0, (hw_buffer[plane][row][cblk] & 0x01) ? HIGH : LOW);
                    digitalWrite(PIN_D1, (hw_buffer[plane][row][cblk] & 0x02) ? HIGH : LOW);
                    digitalWrite(PIN_D2, (hw_buffer[plane][row][cblk] & 0x04) ? HIGH : LOW);
                    digitalWrite(PIN_D3, (hw_buffer[plane][row][cblk] & 0x08) ? HIGH : LOW);
                    digitalWrite(PIN_D4, (hw_buffer[plane][row][cblk] & 0x10) ? HIGH : LOW);
                    digitalWrite(PIN_D5, (hw_buffer[plane][row][cblk] & 0x20) ? HIGH : LOW);
                    digitalWrite(PIN_D6, (hw_buffer[plane][row][cblk] & 0x40) ? HIGH : LOW);
                    digitalWrite(PIN_D7, (hw_buffer[plane][row][cblk] & 0x80) ? HIGH : LOW);
                    //                                  //            _ _____
                    //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
                    digitalWrite(PIN_SRCLK, HIGH);      //   1     X  1   1    p  p  X  X  X  shift data into shift registers
                    digitalWrite(PIN_SRCLK, LOW);       //   0     X  1   1    p  p  X  X  X  shift data into shift registers
                }

                /* light up the row */
                //                                  //            _ _____
                //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
                digitalWrite(PIN_RCLK, HIGH);       //   0     1  1   1    p  p  X  X  X  latch shift register outputs
                digitalWrite(PIN_RCLK, LOW);        //   0     0  1   1    p  p  X  X  X  |
                digitalWrite(PIN_R2, (row & 0x04)?HIGH:LOW);  //  1   1    p  p  r  X  X  turn on anode supply for row
                digitalWrite(PIN_R1, (row & 0x02)?HIGH:LOW);  //  1   1    p  p  r  r  X  |
                digitalWrite(PIN_R0, (row & 0x01)?HIGH:LOW);  //  1   1    p  p  r  r  r  |
                digitalWrite(PIN__G, LOW);          //   0     1  0   1    p  p  r  r  r  enable column sinks
                delayMicroseconds(ROW_HOLD_TIME_US);
                digitalWrite(PIN__G, HIGH);         //   0     1  1   1    p  p  r  r  r  disable column sinks
            }
        }
    }
}

//
// refresh_hw_buffer()
// Call this when you want to display the next row on the display, from a background timer
// or other mechanism. When called, the next hardware row is pushed out and held.
//
#define MATRIX_FLASH_INTERVAL (1000)
void refresh_hw_buffer(void)
{
    static int plane = 0;
    static int planebit = 1;
    static int row = 0;
	static int flash_counter = 0;
	static bool flash_off = false;

	if ((++flash_counter % MATRIX_FLASH_INTERVAL) == 0) {
		flash_off = !flash_off;
	}

    //                                  //            _ _____
    //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
    digitalWrite(PIN__G, HIGH);         //   X     X  1   X    X  X  X  X  X  disable column drains
    digitalWrite(PIN__SRCLR, LOW);      //   X     X  1   0    X  X  X  X  X  reset shift registers
    digitalWrite(PIN__SRCLR, HIGH);     //   X     X  1   1    X  X  X  X  X  |

    if (hw_active_color_planes == 0)
        return;

    if (++row >= N_ROWS) {
        do {
            if (++plane >= N_COLORS) {
                plane = 0;
            }
            planebit = 1 << plane;
        } while (plane == N_FLASHING_PLANE || !(hw_active_color_planes & planebit));
        row = 0;
    }

#if HW_MODEL == MODEL_3xx_MONOCHROME
    //                                  //            _ _____
    //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
    digitalWrite(PIN_R4, HIGH);         //   X     X  1   1    1  X  X  X  X  set blue plane
    digitalWrite(PIN_R3, LOW);          //   X     X  1   1    1  0  X  X  X  |
#else
# if HW_MODEL == MODEL_3xx_RGB
    if (planebit == BIT_RGB_RED) {
        //                                  //            _ _____
        //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
        digitalWrite(PIN_R4, LOW);          //   X     X  1   1    0  X  X  X  X  set red plane
        digitalWrite(PIN_R3, LOW);          //   X     X  1   1    0  0  X  X  X  |
    } else if (planebit == BIT_RGB_GREEN) {
        //                                  //            _ _____
        //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
        digitalWrite(PIN_R4, LOW);          //   X     X  1   1    0  X  X  X  X  set green plane
        digitalWrite(PIN_R3, HIGH);         //   X     X  1   1    0  1  X  X  X  |
    } else if (planebit == BIT_RGB_BLUE) {
        //                                  //            _ _____
        //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
        digitalWrite(PIN_R4, HIGH);         //   X     X  1   1    1  X  X  X  X  set blue plane
        digitalWrite(PIN_R3, LOW);          //   X     X  1   1    1  0  X  X  X  |
    } else {
        //                                  //            _ _____
        //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
        digitalWrite(PIN_R4, HIGH);         //   X     X  1   1    1  X  X  X  X  set NO plane
        digitalWrite(PIN_R3, HIGH);         //   X     X  1   1    1  1  X  X  X  |
    }
# else
#  error "hw model not set"
# endif
#endif
    /* now push out the column data */
    for (int cblk=0; cblk < N_COLBYTES; cblk++) {
		if (flash_off) {
			digitalWrite(PIN_D0, (hw_buffer[N_FLASHING_PLANE][row][cblk] & 0x01 ? LOW : (hw_buffer[plane][row][cblk] & 0x01) ? HIGH : LOW));
			digitalWrite(PIN_D1, (hw_buffer[N_FLASHING_PLANE][row][cblk] & 0x02 ? LOW : (hw_buffer[plane][row][cblk] & 0x02) ? HIGH : LOW));
			digitalWrite(PIN_D2, (hw_buffer[N_FLASHING_PLANE][row][cblk] & 0x04 ? LOW : (hw_buffer[plane][row][cblk] & 0x04) ? HIGH : LOW));
			digitalWrite(PIN_D3, (hw_buffer[N_FLASHING_PLANE][row][cblk] & 0x08 ? LOW : (hw_buffer[plane][row][cblk] & 0x08) ? HIGH : LOW));
			digitalWrite(PIN_D4, (hw_buffer[N_FLASHING_PLANE][row][cblk] & 0x10 ? LOW : (hw_buffer[plane][row][cblk] & 0x10) ? HIGH : LOW));
			digitalWrite(PIN_D5, (hw_buffer[N_FLASHING_PLANE][row][cblk] & 0x20 ? LOW : (hw_buffer[plane][row][cblk] & 0x20) ? HIGH : LOW));
			digitalWrite(PIN_D6, (hw_buffer[N_FLASHING_PLANE][row][cblk] & 0x40 ? LOW : (hw_buffer[plane][row][cblk] & 0x40) ? HIGH : LOW));
			digitalWrite(PIN_D7, (hw_buffer[N_FLASHING_PLANE][row][cblk] & 0x80 ? LOW : (hw_buffer[plane][row][cblk] & 0x80) ? HIGH : LOW));
		} else {
			digitalWrite(PIN_D0, (hw_buffer[plane][row][cblk] & 0x01) ? HIGH : LOW);
			digitalWrite(PIN_D1, (hw_buffer[plane][row][cblk] & 0x02) ? HIGH : LOW);
			digitalWrite(PIN_D2, (hw_buffer[plane][row][cblk] & 0x04) ? HIGH : LOW);
			digitalWrite(PIN_D3, (hw_buffer[plane][row][cblk] & 0x08) ? HIGH : LOW);
			digitalWrite(PIN_D4, (hw_buffer[plane][row][cblk] & 0x10) ? HIGH : LOW);
			digitalWrite(PIN_D5, (hw_buffer[plane][row][cblk] & 0x20) ? HIGH : LOW);
			digitalWrite(PIN_D6, (hw_buffer[plane][row][cblk] & 0x40) ? HIGH : LOW);
			digitalWrite(PIN_D7, (hw_buffer[plane][row][cblk] & 0x80) ? HIGH : LOW);
		}
        //                                  //            _ _____
        //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
        digitalWrite(PIN_SRCLK, HIGH);      //   1     X  1   1    p  p  X  X  X  shift data into shift registers
        digitalWrite(PIN_SRCLK, LOW);       //   0     X  1   1    p  p  X  X  X  shift data into shift registers
    }

    /* light up the row */
    //                                  //            _ _____
    //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
    digitalWrite(PIN_RCLK, HIGH);       //   0     1  1   1    p  p  X  X  X  latch shift register outputs
    digitalWrite(PIN_RCLK, LOW);        //   0     0  1   1    p  p  X  X  X  |
    digitalWrite(PIN_R2, (row & 0x04)?HIGH:LOW);  //  1   1    p  p  r  X  X  turn on anode supply for row
    digitalWrite(PIN_R1, (row & 0x02)?HIGH:LOW);  //  1   1    p  p  r  r  X  |
    digitalWrite(PIN_R0, (row & 0x01)?HIGH:LOW);  //  1   1    p  p  r  r  r  |
    digitalWrite(PIN__G, LOW);          //   0     1  0   1    p  p  r  r  r  enable column sinks
    delayMicroseconds(ROW_HOLD_TIME_US);
    digitalWrite(PIN__G, HIGH);         //   0     1  1   1    p  p  r  r  r  disable column sinks
}

//
// commit_image_buffer(src_buf)
// Writes contents of the image buffer into the hardware buffer that's being refreshed onto the display.
// 
void commit_image_buffer(byte buffer[N_ROWS][N_COLS])
{
    clear_hw_buffer();
    for (int row=0; row < N_ROWS; row++) {
        for (int col=0; col < N_COLS; col++) {
#if HW_MODEL == MODEL_3xx_MONOCHROME
            if (buffer[row][col] & (BIT_RGB_RED | BIT_RGB_GREEN | BIT_RGB_BLUE)) {
                hw_buffer[0][row][7-(col & 0x07)] |= 1 << ((col >> 3) & 0x07);
                hw_active_color_planes |= 1;
            }
            if (buffer[row][col] & BIT_RGB_FLASHING) {
                hw_buffer[1][row][7-(col & 0x07)] |= 1 << ((col >> 3) & 0x07);
                hw_active_color_planes |= 2;
            }
#else
# if HW_MODEL == MODEL_3xx_RGB
            for (int plane=0; plane<N_COLORS; plane++) {
                byte planebit = 1 << plane;
                if (plane == N_FLASHING_PLANE) {
                    if (buffer[row][col] & BIT_RGB_FLASHING) {
                        hw_active_color_planes |= BIT_RGB_FLASHING;
                        hw_buffer[plane][row][7-(col & 0x07)] |= 1 << ((col >> 3) & 0x07);
                    }
                } else if (buffer[row][col] & planebit) {
                    hw_active_color_planes |= planebit;
                    hw_buffer[plane][row][7-(col & 0x07)] |= 1 << ((col >> 3) & 0x07);
                }
            }
# else
# error "hw model not set"
# endif
#endif
        }
    }
}
#endif /* IS_READERBOARD */

//
// setup()
//   Called after reboot to set up the device.
//
void setup(void)
{
#if HAS_I2C_EEPROM
    Wire.begin();
    xEEPROM.begin();
#endif
    setup_pins();
    setup_eeprom();
    flasher.stop();
    strober.stop();
    setup_commands();
#if IS_READERBOARD
    setup_buffers();
	status_timer.set(0, strobe_status);
	status_timer.reset();
	status_timer.setPeriod(10);
	status_timer.enable();
#endif

	start_usb_serial();
    setup_485_serial();

    flag_init();
#if IS_READERBOARD
    show_banner();
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
# else
#  error "hw control logic not set"
# endif
#endif /* IS_READERBOARD */

#ifdef START_TEST_PATTERN
    test_pattern();
# if IS_READERBOARD
    render_text(image_buffer, 0, 2, "XYZZY\013:012", BIT_RGB_RED);
    commit_image_buffer(image_buffer);
# endif
    flasher.append(1);
    flasher.append(2);
    flasher.start();
#endif
    flag_ready();
    flasher.stop();

#ifdef SERIAL_DEBUG
    Serial.write("EEPROM\r\n");
#if HAS_I2C_EEPROM
    byte block[64];
    char bbuf[64];
    Serial.write(xEEPROM.isConnected() ? "connected " : "MISSING ");
    xEEPROM.readBlock(0, block, 64);
    for (int i=0; i<64; i++) {
        sprintf(bbuf, "%02X ", block[i]);
        Serial.write(bbuf);
    }
    Serial.write("\r\n");
#endif
#endif
    setup_completed = true;
}

void show_banner(void) {
    char rbuf[32];
#if IS_READERBOARD
	display_text(1, BANNER_HARDWARE_VERS, BIT_RGB_BLUE, 1500);
	display_text(1, BANNER_FIRMWARE_VERS, BIT_RGB_BLUE, 1500);
    sprintf(rbuf, "S/N %s", serial_number);
    display_text(1, rbuf, BIT_RGB_BLUE, 1500);
    if (my_device_address == EE_ADDRESS_DISABLED) {
        display_text(0, "ADDRESS \0133\234\234", BIT_RGB_RED, 3000);
    } else {
        sprintf(rbuf, "ADDRESS \0137%02d", my_device_address);
        display_text(0, rbuf, BIT_RGB_RED, 3000);
    }
    sprintf(rbuf, "GLOBAL  \0137%02d", global_device_address);
	display_text(0, rbuf, BIT_RGB_RED, 3000);
    sprintf(rbuf, "USB \0137%6d", USB_baud_rate);
	display_text(0, rbuf, BIT_RGB_RED, 3000);
    if (my_device_address == EE_ADDRESS_DISABLED) {
        display_text(0, "485    \0133OFF", BIT_RGB_RED, 3000);
    } else {
        sprintf(rbuf, "485 \0137%6d", RS485_baud_rate);
        display_text(0, rbuf, BIT_RGB_RED, 3000);
    }
	display_text(1, "MadScience", BIT_RGB_GREEN,  1500);
	display_text(1, "Zone \0062\100\00612024",  BIT_RGB_GREEN, 1500);
    clear_all_buffers();
#else
    send_morse(BANNER_HARDWARE_VERS, 10);
    send_morse(BANNER_FIRMWARE_VERS, 10);
    sprintf(rbuf, "S/N %s", serial_number);
    send_morse(rbuf, 32);

    if (my_device_address == EE_ADDRESS_DISABLED) {
        send_morse("NO ADDRESS", 10);
    } else {
        sprintf(rbuf, "ADDRESS %d", my_device_address);
        send_morse(rbuf, 32);
    }
    sprintf(rbuf, "GLOBAL  %2d", global_device_address);
    send_morse(rbuf, 32);
    sprintf(rbuf, "USB %d", USB_baud_rate);
    send_morse(rbuf, 32);
    if (my_device_address == EE_ADDRESS_DISABLED) {
        send_morse("RS485 OFF", 10);
    } else {
        sprintf(rbuf, "RS485 %d", RS485_baud_rate);
        send_morse(rbuf, 32);
    }
    send_morse("MADSCIENCE ZONE 2024", 20);
#endif
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

//
// Initialize the USB serial port so we can receive commands from there.
// This should be safe to call again if the baud rate is changed.
//
void start_usb_serial(void) {
	Serial.begin(USB_baud_rate);
}

//
// Setup the RS-485 serial port (we don't call it "start" because this
// function will start OR stop the port depending on the current configuration
// of the device. Call this whenever the address is updated so the port
// will follow that setting.
//
void setup_485_serial(void) {
#if HW_MODEL != MODEL_BUSYLIGHT_1
    if (my_device_address == EE_ADDRESS_DISABLED) {
        digitalWrite(PIN_DE, LOW);      // disable driver
        digitalWrite(PIN__RE, HIGH);    // disable receiver
        if (RS485_enabled) {
            SERIAL_485.end();
        }
        RS485_enabled = false;
    } else {
        SERIAL_485.begin(RS485_baud_rate);
        digitalWrite(PIN_DE, LOW);      // disable driver
        digitalWrite(PIN__RE, LOW);     // enable receiver
        RS485_enabled = true;
    }
#endif
}

// loop()
//   Main loop of the firmware
//
void loop(void)
{
#if IS_READERBOARD
    refresh_hw_buffer();
	transitions.update();
	status_timer.update();
#endif
    /* flash/strobe discrete LEDs as needed */
    flasher.update();
    strober.update();

    /* receive commands via serial port */
    if (Serial.available() > 0) {
        receive_serial_data(FROM_USB);
    }

#if HW_MODEL != MODEL_BUSYLIGHT_1
    if (RS485_enabled && SERIAL_485.available() > 0) {
        receive_serial_data(FROM_485);
    }
#endif
//
//
//          MEGA    DUE
// Serial   RX/TX   RX/TX       USB Direct
// Serial1  Rx1/Tx1 Rx1/Tx1
// Serial2  Rx2/Tx2 Rx2/Tx2
// Serial3  Rx3/Tx3 Rx3/Tx3     RS-485
// Serial4  --
}

void start_485_reply(void)
{
#if HW_MODEL != MODEL_BUSYLIGHT_1
    if (!RS485_enabled)
        return;
    
    digitalWrite(PIN_DE, HIGH); // enable driver
    if (my_device_address >= 16) {
        SERIAL_485.write(0xf0 | global_device_address);
        SERIAL_485.write(0x01);
        SERIAL_485.write(my_device_address & 0x3f);
    }
    else {
        SERIAL_485.write(0xd0 | my_device_address);
    }
#endif
}

void send_485_byte(byte x)
{
#if HW_MODEL != MODEL_BUSYLIGHT_1
    if (!RS485_enabled)
        return;

    if (x == 0x7f || x == 0x7e) {
        SERIAL_485.write(0x7f);
        SERIAL_485.write(x);
    }
    else if ((x & 0x80) == 0) {
        SERIAL_485.write(x);
    }
    else {
        SERIAL_485.write(0x7e);
        SERIAL_485.write(x & 0x7f);
    }
#endif
}

void end_485_reply(void)
{
#if HW_MODEL != MODEL_BUSYLIGHT_1
    digitalWrite(PIN_DE, LOW); // disable driver
#endif
}

void start_usb_reply(void)
{
}

void send_usb_byte(byte x)
{
    Serial.write(x);
}

void end_usb_reply(void)
{
}

void test_pattern(void) 
{
	flag_test();
#if IS_READERBOARD
    clear_all_buffers();
    //                                  //            _ _____
    //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
    digitalWrite(PIN__SRCLR, LOW);	    //   X    X   1   0    X  X  X  X  X    reset shift register
    digitalWrite(PIN__SRCLR, HIGH);	    //   X    X   1   1    X  X  X  X  X    |
    //
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
	test_sweep();
#endif
}

#if IS_READERBOARD
void test_sweep()
{
	// Run a single column right and then left
	for (int block=0; block<8; block++) {
		digitalWrite(column_block_set[block], LOW);
	}
    //                                  //            _ _____
    //                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
    digitalWrite(PIN__SRCLR, LOW);	    //   X    X   1   0    X  X  X  X  X    reset shift register
    digitalWrite(PIN__SRCLR, HIGH);	    //   X    X   1   1    X  X  X  X  X    |
	for (int block=0; block<8; block++) {
		for (int col=0; col<8; col++) {
			//                                  //            _ _____
			//                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
			digitalWrite(column_block_set[block], col==0 ? HIGH : LOW);
			//                                  //            _ _____
			//									// SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
			digitalWrite(PIN_SRCLK, HIGH);		//   1    0   1   1    X  X  X  X  X    clock in bit
			digitalWrite(PIN_SRCLK, LOW);		//   0    0   1   1    X  X  X  X  X    |
			//                                  //            _ _____
			//                                  // SRCLK RCLK G SRCLR R4 R3 R2 R1 R0
			digitalWrite(PIN_RCLK, HIGH);   	//   0    1   1   1    X  X  X  X  X    clock data to output buffer
			digitalWrite(PIN_RCLK, LOW);    	//   0    0   1   1    X  X  X  X  X    |

            for (int i=0; i<10; i++) {
#if HW_MODEL == MODEL_3xx_RGB
                test_sweep_col(LOW, LOW);
                test_sweep_col(LOW, HIGH);
                test_sweep_col(HIGH, LOW);
#else
# if HW_MODEL == MODEL_3xx_MONOCHROME
                test_sweep_col(HIGH, LOW);
# else
#  error "hw model not set"
# endif
#endif
            }
		}
	}
	digitalWrite(PIN_R4, HIGH); // off
	digitalWrite(PIN_R3, HIGH);
}

void test_sweep_col(int r4, int r3)
{
    digitalWrite(PIN__G, HIGH);
    digitalWrite(PIN_R4, r4);
    digitalWrite(PIN_R3, r3);
    for (int r=0; r<8; r++) {
        digitalWrite(PIN_R2, (r&0x04)?HIGH:LOW);
        digitalWrite(PIN_R1, (r&0x02)?HIGH:LOW);
        digitalWrite(PIN_R0, (r&0x01)?HIGH:LOW);
        digitalWrite(PIN__G, LOW);
        delayMicroseconds(ROW_HOLD_TIME_US);
        digitalWrite(PIN__G, HIGH);
    }
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
#endif /* IS_READERBOARD */

byte encode_led(byte n)
{
    if (n >= LENGTH_OF(discrete_led_set)) {
        return '_';
    }
    if (n >= LENGTH_OF(discrete_led_labels)) {
        return encode_int6(n);
    }
    return discrete_led_labels[n];
}

byte parse_led_name(byte ch)
{
    int i;

    if (ch == '_') {
        return STATUS_LED_OFF;
    }
    for (i = 0; i < LENGTH_OF(discrete_led_labels); i++) {
        if (ch == discrete_led_labels[i]) {
            return i;
        }
    }
    if ((i = decode_int6(ch)) >= LENGTH_OF(discrete_led_set)) {
        return STATUS_LED_OFF;
    }
    return i;
}

byte encode_int6(byte n)
{
    return (n & 0x3f) + '0';
}

byte parse_hex_nybble(byte n)
{
    if (n >= '0' && n <= '9')
        return n - '0';
    if (n >= 'a' && n <= 'f')
        return n - 'a' + 10;
    if (n >= 'A' && n <= 'F')
        return n - 'A' + 10;
    return 0;
}

byte parse_hex_nybble_pair(byte n1, byte n2)
{
    return (((parse_hex_nybble(n1) << 4) & 0xf0)
           |((parse_hex_nybble(n2)     ) & 0x0f));
}

byte encode_hex_nybble(byte n)
{
    if (n < 10) {
        return n + '0';
    }
    return (n-10) + 'a';
}

byte decode_int6(byte n)
{
    if (n > 'o') {
        return 0;
    }
    return n-'0';
}

#if IS_READERBOARD
byte decode_pos(byte n, byte current)
{
    if (n == '~')
        return current;
    return decode_int6(n);
}

byte decode_rgb(byte n)
{
    return decode_int6(n) & 0x0f;
}


void debug_image_buffer(byte buf[N_ROWS][N_COLS])
{
    char rbuf[16];
    Serial.write("image buffer\r\n");
    for (int row=0; row<N_ROWS; row++) {
        sprintf(rbuf, "[%d] ", row);
        Serial.write(rbuf);
        for (int col=0; col<N_COLS; col++) {
            switch (buf[row][col]) {
                case 0: Serial.write(".");break;
                case 1: Serial.write("R");break;
                case 2: Serial.write("G");break;
                case 3: Serial.write("Y");break;
                case 4: Serial.write("B");break;
                case 5: Serial.write("M");break;
                case 6: Serial.write("C");break;
                case 7: Serial.write("W");break;
                case 8: Serial.write("f");break;
                case 9: Serial.write("r");break;
                case 10: Serial.write("g");break;
                case 11: Serial.write("y");break;
                case 12: Serial.write("b");break;
                case 13: Serial.write("m");break;
                case 14: Serial.write("c");break;
                case 15: Serial.write("w");break;
                default: Serial.write("?");break;
            }
        }
        Serial.write("\r\n");
    }
}

void debug_hw_buffer(void)
{
    char rbuf[16];
    Serial.write("hardware buffer\r\n");
    for (int plane=0; plane < N_COLORS; plane++) {
        Serial.write(plane==0? "RED\r\n":(plane==1? "GREEN\r\n":(plane==2? "BLUE\r\n":(plane==3? "FLASHING\r\n" : "UNKNOWN\r\n"))));
        for (int row=0; row < N_ROWS; row++) {
            sprintf(rbuf, "[%d] ", row);
            Serial.write(rbuf);
            for (int cblk=0; cblk < N_COLBYTES; cblk++) {
                sprintf(rbuf, " %02X", hw_buffer[plane][row][cblk]);
                Serial.write(rbuf);
            }
            Serial.write("\r\n");
        }
    }
}

void shift_left(byte buffer[N_ROWS][N_COLS])
{
    for (int col=0; col < N_COLS - 1; col++) {
        for (int row=0; row < N_ROWS; row++) {
            buffer[row][col] = buffer[row][col+1];
        }
    }
    for (int row=0; row < N_ROWS; row++) {
        buffer[row][N_COLS-1] = 0;
    }
}

void shift_right(byte buffer[N_ROWS][N_COLS])
{
    for (int col=N_COLS-1; col > 0; col--) {
        for (int row=0; row < N_ROWS; row++) {
            buffer[row][col] = buffer[row][col-1];
        }
    }
    for (int row=0; row < N_ROWS; row++) {
        buffer[row][0] = 0;
    }
}

void shift_up(byte buffer[N_ROWS][N_COLS])
{
    for (int row=0; row < N_ROWS-1; row++) {
        for (int col=0; col < N_COLS; col++) {
            buffer[row][col] = buffer[row+1][col];
        }
    }
    for (int col=0; col < N_COLS; col++) {
        buffer[N_ROWS-1][col] = 0;
    }
}

void shift_down(byte buffer[N_ROWS][N_COLS])
{
    for (int row=N_ROWS-1; row > 0; row--) {
        for (int col=0; col < N_COLS; col++) {
            buffer[row][col] = buffer[row-1][col];
        }
    }
    for (int col=0; col < N_COLS; col++) {
        buffer[0][col] = 0;
    }
}
#endif /* IS_READERBOARD */

const char *morse[] = {
    /* 00 */ "",
    /* 01 */ "",
    /* 02 */ "",
    /* 03 */ "",
    /* 04 */ "",
    /* 05 */ "",
    /* 06 */ "",
    /* 07 */ "",
    /* 08 */ "",
    /* 09 */ "",
    /* 0A */ "",
    /* 0B */ "",
    /* 0C */ "",
    /* 0D */ "",
    /* 0E */ "",
    /* 0F */ "",
    /* 10 */ "",
    /* 11 */ "",
    /* 12 */ "",
    /* 13 */ "",
    /* 14 */ "",
    /* 15 */ "",
    /* 16 */ "",
    /* 17 */ "",
    /* 18 */ "",
    /* 19 */ "",
    /* 1A */ "",
    /* 1B */ "",
    /* 1C */ "",
    /* 1D */ "",
    /* 1E */ "",
    /* 1F */ "",
    /* 20 */ "",
    /* 21 ! */ "=@=@==",
    /* 22 " */ "@=@@=@",
    /* 23 # */ "",
    /* 24 $ */ "@@@=@@=",
    /* 25 % */ "",
    /* 26 & */ "@=@@@",
    /* 27 ' */ "@====@",
    /* 28 ( */ "=@==@",
    /* 29 ) */ "=@==@=",
    /* 2A * */ "",
    /* 2B + */ "@=@=@",
    /* 2C , */ "==@@==",
    /* 2D - */ "=@@@@=",
    /* 2E . */ "@=@=@=",
    /* 2F / */ "=@@=@",
    /* 30 0 */ "=====",
    /* 31 1 */ "@====",
    /* 32 2 */ "@@===",
    /* 33 3 */ "@@@==",
    /* 34 4 */ "@@@@=",
    /* 35 5 */ "@@@@@",
    /* 36 6 */ "=@@@@",
    /* 37 7 */ "==@@@",
    /* 38 8 */ "===@@",
    /* 39 9 */ "====@",
    /* 3A : */ "===@@@",
    /* 3B ; */ "=@=@=@",
    /* 3C < */ "",
    /* 3D = */ "",
    /* 3E > */ "",
    /* 3F ? */ "@@==@@",
    /* 40 @ */ "@==@=@",
    /* 41 A */ "@=",
    /* 42 B */ "=@@@",
    /* 43 C */ "=@=@",
    /* 44 D */ "=@@",
    /* 45 E */ "@",
    /* 46 F */ "@@=@",
    /* 47 G */ "==@",
    /* 48 H */ "@@@@",
    /* 49 I */ "@@",
    /* 4A J */ "@===",
    /* 4B K */ "=@=",
    /* 4C L */ "@=@@",
    /* 4D M */ "==",
    /* 4E N */ "=@",
    /* 4F O */ "===",
    /* 50 P */ "@==@",
    /* 51 Q */ "==@=",
    /* 52 R */ "@=@",
    /* 53 S */ "@@@",
    /* 54 T */ "=",
    /* 55 U */ "@@=",
    /* 56 V */ "@@@=",
    /* 57 W */ "@==",
    /* 58 X */ "=@@=",
    /* 59 Y */ "=@==",
    /* 5A Z */ "==@@",
    /* 5B [ */ "",
    /* 5C \ */ "",
    /* 5D ] */ "",
    /* 5E ^ */ "",
    /* 5F _ */ "@@==@=",
    /* 60 ` */ "",
    /* 61 a */ "@=",
    /* 62 b */ "=@@@",
    /* 63 c */ "=@=@",
    /* 64 d */ "=@@",
    /* 65 e */ "@",
    /* 66 f */ "@@=@",
    /* 67 g */ "==@",
    /* 68 h */ "@@@@",
    /* 69 i */ "@@",
    /* 6A j */ "@===",
    /* 6B k */ "=@=",
    /* 6C l */ "@=@@",
    /* 6D m */ "==",
    /* 6E n */ "=@",
    /* 6F o */ "===",
    /* 70 p */ "@==@",
    /* 71 q */ "==@=",
    /* 72 r */ "@=@",
    /* 73 s */ "@@@",
    /* 74 t */ "=",
    /* 75 u */ "@@=",
    /* 76 v */ "@@@=",
    /* 77 w */ "@==",
    /* 78 x */ "=@@=",
    /* 79 y */ "=@==",
    /* 7A z */ "==@@",
    /* 7B { */ "",
    /* 7C | */ "",
    /* 7D } */ "",
    /* 7E ~ */ "",
    /* 7F   */ "@@@=@=",    /* SK */
};

/* 5 wpm Farnsworth spacing, 13 wpm characters */
const int dit =         92;
const int dah =        277;
//const int intrachar =   92;
const int intrachar =  200;
const int interchar = 1443;
const int interword = 3367 - interchar;

void send_morse_char(byte ch)
{
    if (*morse[ch] == '\0') {
        return;
    }

    for (const char *c = morse[ch]; *c != '\0'; c++) {
        discrete_set(0, true);
        if (*c == '@') {
            delay(dit);
        } else if (*c == '=') {
            delay(dah);
        }
        discrete_set(0, false);
        delay(intrachar);
    }
}

void send_morse(const char *text, int maxlen)
{
    discrete_all_off(true);
    for (int i = 0; i<maxlen && text[i] != '\0'; i++) {
        if (text[i] == ' ') {
            delay(interword);
        }
        else {
            send_morse_char(text[i] & 0x7f);
            delay(interchar);
        }
    }
    delay(interword);
    send_morse_char(0x7f);
}
