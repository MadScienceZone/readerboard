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
 * Arduino Mega 2560 (or equiv.) firmware to drive
 * 64x7 (my older design) or (current) 64x8 matrix displays.
 *
 * Steve Willoughby 2023
 */

#include <TimerEvent.h>
#include <EEPROM.h>
#include "fonts.h"
#include "commands.h"
#include "readerboard.h"


//
// EEPROM locations
// $00 0x4B
// $01 baud rate code
//
#define EE_ADDR_SENTINEL  (0x00)
#define EE_ADDR_USB_SPEED (0x01)
#define EE_VALUE_SENTINEL (0x4b)
#define EE_DEFAULT_SPEED  ('5')

/* Hardware model selected for this firmware.
 * The interface is similar enough to the older project I put together
 * out of salvaged and spare parts which made a 7-row display that we
 * can support that as well as the newer 8-row display. Set HW_MODEL
 * to the desired hardware. Be sure to use the correct Arduino shield 
 * for the selected hardware model. The two are not compatible.
 */

#define MODEL_LEGACY_64x7 (0)
#define MODEL_CURRENT_64x8 (1)
#define HW_MODEL (MODEL_CURRENT_64x8)

#if HW_MODEL != MODEL_CURRENT_64x8
# if HW_MODEL == MODEL_LEGACY_64x7
#  warning "Generating code for legacy 64x7 readerboards: this is probably not what you want."
# else
#  error "HW_MODEL not set to a supported hardware configuration"
# endif
#endif

/* I/O port to use for full 8-bit write operations to the sign board */
/* On the Mega 2560, PORTF<7:0> corresponds to <A7:A0> pins */
#define MATRIX_DATA_PORT    PORTF   

/* Refresh the display every REFRESH_MS milliseconds. For an 8-row
 * display, an overall refresh rate of 30 FPS =  4.16 mS
 *                                     25 FPS =  5.00 mS
 *                                     20 FPS =  6.26 mS
 *                                     15 FPS =  8.33 mS
 *                                     10 FPS = 12.50 mS
 *                                      5 FPS = 25.00 mS
 */
const int REFRESH_MS = 25;

const int PIN_STATUS_LED = 13;

#if HW_MODEL == MODEL_LEGACY_64x7
const int N_ROWS    = 7;    // number of rows on the display
const int COL_BLK_0 = 0;    // leftmost column block is MSB
const int COL_BLK_1 = 1;    //                |
const int COL_BLK_2 = 2;    //                |
const int COL_BLK_3 = 3;    //                |
const int COL_BLK_4 = 4;    //                |
const int COL_BLK_5 = 5;    //                |
const int COL_BLK_6 = 6;    //                V
const int COL_BLK_7 = 7;    // rightmost column block is LSB
const int PIN_PS0   = A8;   // control line PS0
const int PIN_PS1   = A9;   // control line PS1
#else
# if HW_MODEL == MODEL_CURRENT_64x8
const int N_ROWS    = 8;    // number of rows on the display
const int COL_BLK_7 = 0;    // rightmost column block is MSB byte
const int COL_BLK_6 = 1;    //                |
const int COL_BLK_5 = 2;    //                |
const int COL_BLK_4 = 3;    //                |
const int COL_BLK_3 = 4;    //                |
const int COL_BLK_2 = 5;    //                |
const int COL_BLK_1 = 6;    //                V
const int COL_BLK_0 = 7;    // leftmost column block is LSB byte
const int PIN_SRCLK = A12;  // shift register clock (strobe)
const int PIN_RCLK  = A13;  // storage register clock (strobe)
const int PIN__G    = A14;  // column output ~enable
const int PIN__SRCLR= A15;  // shift register ~clear
const int PIN_REN   = A10;  // row output enable
const int PIN_R0    = A8;   // row address bit 0
const int PIN_R1    = A9;   // row address bit 1
const int PIN_R2    = A11;  // row address bit 2
const int PIN_L0    = 9;    // discrete LED L0 (white)
const int PIN_L1    = 8;    // discrete LED L1 (blue)
const int PIN_L2    = 7;    // discrete LED L2 (blue)
const int PIN_L3    = 6;    // discrete LED L3 (red)
const int PIN_L4    = 2;    // discrete LED L4 (red)
const int PIN_L5    = 3;    // discrete LED L5 (yellow)
const int PIN_L6    = 4;    // discrete LED L6 (yellow)
const int PIN_L7    = 5;    // discrete LED L7 (green)
const int discrete_led_set[8] = {PIN_L0, PIN_L1, PIN_L2, PIN_L3, PIN_L4, PIN_L5, PIN_L6, PIN_L7};
# else
#  error "HW_MODEL not set to supported hardware configuration"
# endif
#endif
const int PIN_D0    = A0;   // column data bit 0
const int PIN_D1    = A1;   // column data bit 1
const int PIN_D2    = A2;   // column data bit 2
const int PIN_D3    = A3;   // column data bit 3
const int PIN_D4    = A4;   // column data bit 4
const int PIN_D5    = A5;   // column data bit 5
const int PIN_D6    = A6;   // column data bit 6
const int PIN_D7    = A7;   // column data bit 7

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
#if HW_MODEL == MODEL_LEGACY_64x7
    pinMode(PIN_PS0, OUTPUT);
    pinMode(PIN_PS1, OUTPUT);
    digitalWrite(PIN_PS0, LOW);     // PS0 PS1
    digitalWrite(PIN_PS1, HIGH);    //  0   1   RESET
    digitalWrite(PIN_PS0, HIGH);    //  1   1   (shift)
    digitalWrite(PIN_PS1, LOW);     //  1   0   IDLE
#else
# if HW_MODEL == MODEL_CURRENT_64x8
    pinMode(PIN_SRCLK,  OUTPUT);
    pinMode(PIN_RCLK,   OUTPUT);
    pinMode(PIN__G,     OUTPUT);
    pinMode(PIN__SRCLR, OUTPUT);
    pinMode(PIN_REN,    OUTPUT);
    pinMode(PIN_R0,     OUTPUT);        //            _ _____
    pinMode(PIN_R1,     OUTPUT);        // SRCLK RCLK G SRCLR REN
    pinMode(PIN_R2,     OUTPUT);        //   X     X  X   X    X    
    digitalWrite(PIN__G,     HIGH);     //   X     X  1   X    X    column outputs off
    digitalWrite(PIN__SRCLR, LOW);      //   X     X  1   0    X    reset shift registers
    digitalWrite(PIN_REN,    LOW);      //   X     X  1   0    0    row outputs off
    digitalWrite(PIN_SRCLK,  LOW);      //   0     X  1   0    0    
    digitalWrite(PIN_RCLK,   LOW);      //   0     0  1   0    0
    digitalWrite(PIN__SRCLR, HIGH);     //   0     0  1   1    0    ready
    digitalWrite(PIN_R0,     LOW);
    digitalWrite(PIN_R1,     LOW);
    digitalWrite(PIN_R2,     LOW);
    pinMode(PIN_L0, OUTPUT);
    pinMode(PIN_L1, OUTPUT);
    pinMode(PIN_L2, OUTPUT);
    pinMode(PIN_L3, OUTPUT);
    pinMode(PIN_L4, OUTPUT);
    pinMode(PIN_L5, OUTPUT);
    pinMode(PIN_L6, OUTPUT);
    pinMode(PIN_L7, OUTPUT);
    digitalWrite(PIN_L0, LOW);
    digitalWrite(PIN_L1, LOW);
    digitalWrite(PIN_L2, LOW);
    digitalWrite(PIN_L3, LOW);
    digitalWrite(PIN_L4, LOW);
    digitalWrite(PIN_L5, LOW);
    digitalWrite(PIN_L6, LOW);
    digitalWrite(PIN_L7, LOW);
# else
#  error "HW_MODEL not set to supported hardware configuration"
# endif
#endif
}

byte image_buffer[8 * N_ROWS];      // image to render onto
byte hw_buffer[8 * N_ROWS];         // image to refresh onto hardware

//
// setup_buffers()
//   Initialize buffers to zero.
//
void setup_buffers(void)
{
    for (int row=0; row<N_ROWS; row++) {
        for (int col=0; col<8; col++) {
            image_buffer[row*8 + col] = hw_buffer[row*8 + col] = 0;
        }
    }
}

//
// clear_buffer(buf)
//   Resets all bits in buf to zero.
//
void clear_buffer(byte *buf)
{
    for (int i=0; i<N_ROWS*8; i++)
        buf[i] = 0;
}

//
// LED_row_off()
//   During sign refresh cycle, this turns off the displayed row after the
//   required length of time has elapsed.
//
void LED_row_off(void)
{
#if HW_MODEL == MODEL_LEGACY_64x7
    MATRIX_DATA_PORT = 0;           // PS0 PS1
    digitalWrite(PIN_PS0, HIGH);    //  1   X
    digitalWrite(PIN_PS1, LOW);     //  1   0   idle
    digitalWrite(PIN_PS0, LOW);     //  0   0   gate 0 to turn off rows
    digitalWrite(PIN_PS0, HIGH);    //  1   0   idle
#else                                   //            _____ _
# if HW_MODEL == MODEL_CURRENT_64x8     // SRCLK RCLK SRCLR G REN
    digitalWrite(PIN_RCLK, LOW);        //   X     0    X   X  X
    digitalWrite(PIN_SRCLK, LOW);       //   0     0    X   X  X    
    digitalWrite(PIN__SRCLR, HIGH);     //   0     0    1   X  X
    digitalWrite(PIN__G, HIGH);         //   0     0    1   1  X    disable column output
    digitalWrite(PIN_REN, LOW);         //   0     0    1   1  0    disable row output
# else
#  error "HW_MODEL not set to supported hardware configuration"
# endif
#endif
}

//
// LED_row_on(row, buf)
//   During sign refresh cycle, this turns on the displayed row by
//   sending it the column data stored in the hardware refresh buffer
//   pointed to by buf, with the specified row number (0=top row
//   and 7=bottom row for the current board. On the legacy board,
//   there is no row 0 so the rows are numbered 1-7 top to bottom).
//
void LED_row_on(int row, byte *buf)
{
#if HW_MODEL == MODEL_LEGACY_64x7
    /* shift out columns */
    if (row > 0) {
        for (int i=0; i<8; i++) {
            MATRIX_DATA_PORT = buf[row*8+i];
            digitalWrite(PIN_PS0, HIGH);            // PS0 PS1
            digitalWrite(PIN_PS1, HIGH);            //  1   1   shift data into columns
            digitalWrite(PIN_PS1, LOW);             //  1   0   idle
        }
        /* address row and turn on */
        MATRIX_DATA_PORT = ((row+1) << 5) & 0xe0;   // PS0 PS1
        digitalWrite(PIN_PS0, LOW);                 //  0   0   show row
    }
#else
# if HW_MODEL == MODEL_CURRENT_64x8
    /* shift out columns */
    digitalWrite(PIN__G, HIGH);         // SRCLK RCLK SRCLR G REN
    digitalWrite(PIN__SRCLR, LOW);      //   X     X    0   1  X
    digitalWrite(PIN_REN, LOW);         //   X     X    0   1  0    reset, all off
    digitalWrite(PIN__SRCLR, HIGH);     //   X     X    1   1  0    all off
    digitalWrite(PIN_SRCLK, LOW);       //   0     X    1   1  0    all off
    digitalWrite(PIN_RCLK, LOW);        //   0     0    1   1  0    all off

    for (int i=0; i<8; i++) {
        MATRIX_DATA_PORT = buf[row*8+i];
        digitalWrite(PIN_SRCLK, HIGH);  //   1     0    1   1  0    shift into columns
        digitalWrite(PIN_SRCLK, LOW);   //   0     0    1   1  0    
    }
    digitalWrite(PIN_SRCLK, HIGH);      //   0     1    1   1  0    latch column outputs
    digitalWrite(PIN_SRCLK, LOW);       //   0     0    1   1  0    
    digitalWrite(PIN__G, LOW);          //   0     0    1   0  0    enable column output
    digitalWrite(PIN_R0, row & 1);      //                          address row
    digitalWrite(PIN_R1, row & 2);      //                          address row
    digitalWrite(PIN_R2, row & 4);      //                          address row
    digitalWrite(PIN_REN, HIGH);        //   0     0    1   0  1    enable row output
# else
#  error "HW_MODEL not set to supported hardware configuration"
# endif
#endif
}

//
// Transition effect manager. This manages incrementally
// copying data from the image buffer to the hardware refresh
// buffer in visually interesting patterns.
//
class TransitionManager {
	TransitionEffect transition;
	TimerEvent       timer;
	byte            *src;
public:
	TransitionManager(void);
	void update(void);
	void start_transition(byte *, TransitionEffect, int);
	void stop(void);
	void next(bool reset_column = false);
};

void next_transition(void);

TransitionManager::TransitionManager(void)
{
	src = image_buffer;
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

void TransitionManager::start_transition(byte *buffer, TransitionEffect trans, int delay_ms)
{
	transition = trans;
	src = buffer;
	if (trans == NoTransition) {
		stop();
		copy_all_rows(src, hw_buffer);
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
	static int current_column = 0;

	if (reset_column) {
		current_column = 0;
	}

	if (current_column > 63) {
		stop();
		return;
	}

	switch (transition) {
	case TransWipeLeft:
		copy_row_bits(63-(current_column++), src, hw_buffer);
		break;

	case TransWipeRight:
		copy_row_bits(current_column++, src, hw_buffer);
		break;

	case TransScrollLeft:
	case TransScrollRight:
	case TransScrollUp:
	case TransScrollDown:
	case TransWipeUp:
	case TransWipeDown:
	case TransWipeUpDown:
	case TransWipeLeftRight:
	default:
		stop();
		return;
	}
}


//
// copy_all_rows(src, dst)
//   Copies all rows of data from the image buffer to the hardware buffer
//   as described for copy_row_bits()
//
void copy_all_rows(byte *src, byte *dst)
{
    for (int i = 0; i < N_ROWS; i++) {
        copy_row_bits(i, src, dst);
    }
}

//
// copy_row_bits(row, src, dst)
//   Copy a single row of matrix data from src to dst.
//   src is the image buffer we use for rendering what will
//   be displayed. It is arranged as the matrix is physically viewed:
//   | byte 0 | byte 1 | ... | byte 7 |
//   |76543210|76543210| ... |76543210|
//    ^_leftmost pixel     rightmost_^
//
//   dst is the hardware refresh buffer. It is arranged as expected 
//   by the hardware so that the bytes can be directly output during refresh.
//
//   The different hardware models arrange the column bits in a
//   different order, hence the COL_BLK_x defined symbols which
//   are hardware-dependent.
//
void copy_row_bits(unsigned int row, byte *src, byte *dst)
{
    byte *d = dst + row * 8;
    byte *s = src + row * 8;

    d[0] = ((s[COL_BLK_0] & 0x01) << 7)
         | ((s[COL_BLK_1] & 0x01) << 6)
         | ((s[COL_BLK_2] & 0x01) << 5)
         | ((s[COL_BLK_3] & 0x01) << 4)
         | ((s[COL_BLK_4] & 0x01) << 3)
         | ((s[COL_BLK_5] & 0x01) << 2)
         | ((s[COL_BLK_6] & 0x01) << 1)
         | ((s[COL_BLK_7] & 0x01) << 0);

    d[1] = ((s[COL_BLK_0] & 0x02) << 6)
         | ((s[COL_BLK_1] & 0x02) << 5)
         | ((s[COL_BLK_2] & 0x02) << 4)
         | ((s[COL_BLK_3] & 0x02) << 3)
         | ((s[COL_BLK_4] & 0x02) << 2)
         | ((s[COL_BLK_5] & 0x02) << 1)
         | ((s[COL_BLK_6] & 0x02) << 0)
         | ((s[COL_BLK_7] & 0x02) >> 1);

    d[2] = ((s[COL_BLK_0] & 0x04) << 5)
         | ((s[COL_BLK_1] & 0x04) << 4)
         | ((s[COL_BLK_2] & 0x04) << 3)
         | ((s[COL_BLK_3] & 0x04) << 2)
         | ((s[COL_BLK_4] & 0x04) << 1)
         | ((s[COL_BLK_5] & 0x04) << 0)
         | ((s[COL_BLK_6] & 0x04) >> 1)
         | ((s[COL_BLK_7] & 0x04) >> 2);

    d[3] = ((s[COL_BLK_0] & 0x08) << 4)
         | ((s[COL_BLK_1] & 0x08) << 3)
         | ((s[COL_BLK_2] & 0x08) << 2)
         | ((s[COL_BLK_3] & 0x08) << 1)
         | ((s[COL_BLK_4] & 0x08) << 0)
         | ((s[COL_BLK_5] & 0x08) >> 1)
         | ((s[COL_BLK_6] & 0x08) >> 2)
         | ((s[COL_BLK_7] & 0x08) >> 3);

    d[4] = ((s[COL_BLK_0] & 0x10) << 3)
         | ((s[COL_BLK_1] & 0x10) << 2)
         | ((s[COL_BLK_2] & 0x10) << 1)
         | ((s[COL_BLK_3] & 0x10) << 0)
         | ((s[COL_BLK_4] & 0x10) >> 1)
         | ((s[COL_BLK_5] & 0x10) >> 2)
         | ((s[COL_BLK_6] & 0x10) >> 3)
         | ((s[COL_BLK_7] & 0x10) >> 4);

    d[5] = ((s[COL_BLK_0] & 0x20) << 2)
         | ((s[COL_BLK_1] & 0x20) << 1)
         | ((s[COL_BLK_2] & 0x20) << 0)
         | ((s[COL_BLK_3] & 0x20) >> 1)
         | ((s[COL_BLK_4] & 0x20) >> 2)
         | ((s[COL_BLK_5] & 0x20) >> 3)
         | ((s[COL_BLK_6] & 0x20) >> 4)
         | ((s[COL_BLK_7] & 0x20) >> 5);

    d[6] = ((s[COL_BLK_0] & 0x40) << 1)
         | ((s[COL_BLK_1] & 0x40) << 0)
         | ((s[COL_BLK_2] & 0x40) >> 1)
         | ((s[COL_BLK_3] & 0x40) >> 2)
         | ((s[COL_BLK_4] & 0x40) >> 3)
         | ((s[COL_BLK_5] & 0x40) >> 4)
         | ((s[COL_BLK_6] & 0x40) >> 5)
         | ((s[COL_BLK_7] & 0x40) >> 6);

    d[7] = ((s[COL_BLK_0] & 0x80) << 0)
         | ((s[COL_BLK_1] & 0x80) >> 1)
         | ((s[COL_BLK_2] & 0x80) >> 2)
         | ((s[COL_BLK_3] & 0x80) >> 3)
         | ((s[COL_BLK_4] & 0x80) >> 4)
         | ((s[COL_BLK_5] & 0x80) >> 5)
         | ((s[COL_BLK_6] & 0x80) >> 6)
         | ((s[COL_BLK_7] & 0x80) >> 7);
}

//
// draw_character(col, font, codepoint, buffer, mergep=false) -> col'
//   Given a codepoint in a font, set the bits in the buffer for the pixels
//   of that character glyph, and return the starting column position for
//   the next character. 
//
//   If there is no such font or codepoint, nothing is done.
//
int draw_character(byte col, byte font, unsigned int codepoint, byte *buffer, bool mergep=false)
{
    byte l, s;
    unsigned int o;

    if (!get_font_metric_data(font, codepoint, &l, &s, &o)) {
        return col;
    }
    for (byte i=0; i<l; i++) {
        if (col+i < 64) {
            draw_column(col+i, get_font_bitmap_data(o+i), mergep, buffer);
        }
    }
    return col+s;
}

//
// draw_column(col, bits, mergep, buffer)
//   Draw bits (LSB=top) onto the specified buffer column. If mergep is true,
//   merge with existing pixels instead of overwriting them.
//
void draw_column(byte col, byte bits, bool mergep, byte *buffer)
{
    if (col < 64) {
        byte bufcol = col / 8;
        byte mask = 1 << (7 - (col % 8));

        for (byte row=0; row<N_ROWS; row++) {
            if (bits & (1 << row)) {
                buffer[row*8+bufcol] |= mask;
            }
            else if (!mergep) {
                buffer[row*8+bufcol] &= ~mask;
            }
        }
    }
}

void shift_left(byte *buffer)
{
    for (byte row=0; row<N_ROWS; row++) {
        for (byte col=0; col<8; col++) {
            buffer[row*8+col] <<= 1;
            if (col<7) {
                if (buffer[row*8+col+1] & 0x80) {
                    buffer[row*8+col] |= 0x01;
                }
                else {
                    buffer[row*8+col] &= ~0x01;
                }
            }
        }
    }
}


//
// The following LightBlinker support was adapted from the
// author's busylight project, which this project is intended
// to be compatible with.
//
#if HW_MODEL == MODEL_CURRENT_64x8
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
    int i = 0;
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
LightBlinker flasher(200, 0, flash_lights);
LightBlinker strober(50,2000, strobe_lights);
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
void display_buffer(byte *src, TransitionEffect transition)
{
	if (transition == NoTransition) {
		transitions.stop();
		copy_all_rows(src, hw_buffer);
	}
	else {
		transitions.start_transition(src, transition, 100);
	}
}

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

#endif /* MODEL_CURRENT_64x8 */

//
// flag_init()
// flag_ready()
//   Indicate status flags using the discrete LEDs
//            01234567
//     init   X------- initializing device (incl. waiting for UART)
//     ready  -------- ready for operation
//
void flag_init(void)
{
#if HW_MODEL == MODEL_CURRENT_64x8
    digitalWrite(PIN_L0, HIGH);
    digitalWrite(PIN_L1, LOW);
    digitalWrite(PIN_L2, LOW);
    digitalWrite(PIN_L3, LOW);
    digitalWrite(PIN_L4, LOW);
    digitalWrite(PIN_L5, LOW);
    digitalWrite(PIN_L6, LOW);
    digitalWrite(PIN_L7, LOW);
#endif
}

void flag_ready(void)
{
#if HW_MODEL == MODEL_CURRENT_64x8
    digitalWrite(PIN_L0, LOW);
    digitalWrite(PIN_L1, LOW);
    digitalWrite(PIN_L2, LOW);
    digitalWrite(PIN_L3, LOW);
    digitalWrite(PIN_L4, LOW);
    digitalWrite(PIN_L5, LOW);
    digitalWrite(PIN_L6, LOW);
    digitalWrite(PIN_L7, LOW);
#endif
}

void flat_test(void)
{
#if HW_MODEL == MODEL_CURRENT_64x8
	digitalWrite(PIN_L0, HIGH);
	digitalWrite(PIN_L1, HIGH);
	digitalWrite(PIN_L2, HIGH);
	digitalWrite(PIN_L3, HIGH);
	digitalWrite(PIN_L4, HIGH);
	digitalWrite(PIN_L5, HIGH);
	digitalWrite(PIN_L6, HIGH);
	digitalWrite(PIN_L7, HIGH);
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

void default_eeprom_settings(void) {
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
}

//
// setup()
//   Called after reboot to set up the device.
//
void setup(void)
{
    setup_pins();
    flag_init();
	default_eeprom_settings();

#if HW_MODEL == MODEL_CURRENT_64x8
    flasher.stop();
    strober.stop();
#endif
    setup_commands();
    setup_buffers();
	status_timer.set(0, strobe_status);
	status_timer.reset();
	status_timer.setPeriod(10);
	status_timer.enable();

	start_usb_serial();

	// TODO If RS-485 is enabled, start that UART too

	flag_test();
	delay(500);
    flag_ready();
	// TODO display_text(font, string, mS_delay)
	// TODO clear_matrix()
	display_text(0, BANNER_HARDWARE_VERS,  500);
	display_text(0, BANNER_FIRMWARE_VERS,  500);
	display_text(0, BANNER_SERIAL_NUMBER, 1000);
	display_text(0, "ADDRESS XX", 1000);	// 00-15 or --
	display_text(0, "GLOBAL  XX", 1000);	// 00-15 or --
	display_text(0, "USB XXXXXX", 1000);	// speed
	display_text(0, "485 XXXXXX", 1000);	// speed or OFF
	display_text(0, "MadScience",  500);
	display_text(0, "Zone \xa92023",  500);
	clear_matrix();
	// 300 600 1200 2400 4800 9600 14400 19200 28800 31250 38400 57600 115200 OFF
}

void start_usb_serial(void) {
	int speed = 9600;

	switch (EEPROM.read(EE_ADDR_USB_SPEED)) {
		case '0': speed =    300; break;
		case '1': speed =    600; break;
		case '2': speed =   1200; break;
		case '3': speed =   2400; break;
		case '4': speed =   4800; break;
		case '5': speed =   9600; break;
		case '6': speed =  14400; break;
		case '7': speed =  19200; break;
		case '8': speed =  28800; break;
		case '9': speed =  31250; break;
		case 'a':
		case 'A': speed =  38400; break;
		case 'b':
		case 'B': speed =  57600; break;
		case 'c':
		case 'C': speed = 115200; break;
	}

	Serial.begin(speed);
	while (!Serial);
}

//
// loop()
//   Main loop of the firmware
//
void loop(void)
{
    unsigned long last_refresh = 0;
    int cur_row = 0;

    /* update the display every REFRESH_MS milliseconds */
    if (millis() - last_refresh >= REFRESH_MS) {
        LED_row_off();
        LED_row_on(cur_row, hw_buffer);
        cur_row = (cur_row + 1) % N_ROWS;
        last_refresh = millis();
    }

#if HW_MODEL == MODEL_CURRENT_64x8
    /* flash/strobe discrete LEDs as needed */
    flasher.update();
    strober.update();
#endif
	status_timer.update();
	transitions.update();

    /* receive commands via serial port */
    if (Serial.available() > 0) {
        receive_serial_data();
    }

	/* TODO receive commands via RS-485 port */
}
