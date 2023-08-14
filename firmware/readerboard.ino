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

/*
** INITIAL SKETCH-OUT OF IDEAS - NOT YET COMPLETE OR TESTED
*/

#define MODEL_64x8 (0)
#define MODEL_64x7 (1)
#define HW_MODEL (MODEL_64x7)

#if HW_MODEL == MODEL_64x7
const int N_ROWS	= 7;	// number of rows on the display
const int COL_BLK_0 = 0;	// leftmost column block is MSB
const int COL_BLK_1 = 1;	//                |
const int COL_BLK_2 = 2;	//                |
const int COL_BLK_3 = 3;	//                |
const int COL_BLK_4 = 4;	//                |
const int COL_BLK_5 = 5;	//                |
const int COL_BLK_6 = 6;	//                V
const int COL_BLK_7 = 7;	// rightmost column block is LSB
const int PIN_PS0   = A8;	// control line PS0
const int PIN_PS1   = A9;	// control line PS1
#else
# if HW_MODEL == MODEL_64x8
const int N_ROWS	= 8;	// number of rows on the display
const int COL_BLK_7 = 0;	// rightmost column block is MSB byte
const int COL_BLK_6 = 1;	//                |
const int COL_BLK_5 = 2;	//                |
const int COL_BLK_4 = 3;	//                |
const int COL_BLK_3 = 4;	//                |
const int COL_BLK_2 = 5;	//                |
const int COL_BLK_1 = 6;	//                V
const int COL_BLK_0 = 7;	// leftmost column block is LSB byte
const int PIN_SRCLK = A12;	// shift register clock (strobe)
const int PIN_RCLK  = A13;	// storage register clock (strobe)
const int PIN__G    = A14;	// column output ~enable
const int PIN__SRCLR= A15;	// shift register ~clear
const int PIN_REN   = A10;	// row output enable
const int PIN_R0    = A8;	// row address bit 0
const int PIN_R1    = A9;	// row address bit 1
const int PIN_R2    = A11;	// row address bit 2
# else
#  error "HW_MODEL not set to supported hardware configuration"
# endif
#endif
const int PIN_D0    = A0;	// column data bit 0
const int PIN_D1    = A1;	// column data bit 1
const int PIN_D2    = A2;	// column data bit 2
const int PIN_D3    = A3;	// column data bit 3
const int PIN_D4    = A4;	// column data bit 4
const int PIN_D5    = A5;	// column data bit 5
const int PIN_D6    = A6;	// column data bit 6
const int PIN_D7    = A7;	// column data bit 7

void setup_pins(void)
{
	pinMode(PIN_D0, OUTPUT);
	pinMode(PIN_D1, OUTPUT);
	pinMode(PIN_D2, OUTPUT);
	pinMode(PIN_D3, OUTPUT);
	pinMode(PIN_D4, OUTPUT);
	pinMode(PIN_D5, OUTPUT);
	pinMode(PIN_D6, OUTPUT);
	pinMode(PIN_D7, OUTPUT);
#if HW_MODEL == MODEL_64x7
	pinMode(PIN_PS0, OUTPUT);
	pinMode(PIN_PS1, OUTPUT);
	digitalWrite(PIN_PS0, LOW);		// PS0 PS1
	digitalWrite(PIN_PS1, HIGH);	//  0   1	RESET
	digitalWrite(PIN_PS0, HIGH);	//  1   1   (shift)
	digitalWrite(PIN_PS1, LOW);		//  1   0	IDLE
#else
# if HW_MODEL == MODEL_64x8
	pinMode(PIN_SRCLK,  OUTPUT);
	pinMode(PIN_RCLK,   OUTPUT);
	pinMode(PIN__G,     OUTPUT);
	pinMode(PIN__SRCLR, OUTPUT);
	pinMode(PIN_REN,    OUTPUT);
	pinMode(PIN_R0,     OUTPUT);		//            _ _____
	pinMode(PIN_R1,     OUTPUT);		// SRCLK RCLK G SRCLR REN
	pinMode(PIN_R2,     OUTPUT);		//   X     X  X   X    X    
	digitalWrite(PIN__G,     HIGH);		//   X     X  1   X    X	column outputs off
	digitalWrite(PIN__SRCLR, LOW);		//   X     X  1   0    X	reset shift registers
	digitalWrite(PIN_REN,    LOW);		//   X     X  1   0    0	row outputs off
	digitalWrite(PIN_SRCLK,  LOW);		//   0     X  1   0    0	
	digitalWrite(PIN_RCLK,   LOW);		//   0     0  1   0    0
	digitalWrite(PIN__SRCLR, HIGH);		//   0     0  1   1    0	ready
	digitalWrite(PIN_R0,     LOW);
	digitalWrite(PIN_R1,     LOW);
	digitalWrite(PIN_R2,     LOW);
# else
#  error "HW_MODEL not set to supported hardware configuration"
# endif
#endif

byte image_buffer[8 * N_ROWS];		// image to render onto
byte hw_buffer[8 * N_ROWS];			// image to refresh onto hardware

void LED_row_off(void)
{
#if HW_MODEL == MODEL_64x7
	PORTK = 0;						// PS0 PS1
	digitalWrite(PIN_PS0, HIGH);	//  1   X
	digitalWrite(PIN_PS1, LOW);		//  1   0	idle
	digitalWrite(PIN_PS0, LOW);		//  0   0	gate 0 to turn off rows
	digitalWrite(PIN_PS0, HIGH);	//  1   0	idle
#else									//            _____ _
# if HW_MODEL == MODEL_64x8				// SRCLK RCLK SRCLR G REN
	digitalWrite(PIN_RCLK, LOW);		//   X     0    X   X  X
	digitalWrite(PIN_SRCLK, LOW);		//   0     0    X   X  X	
	digitalWrite(PIN__SRCLR, HIGH);		//   0     0    1   X  X
	digitalWrite(PIN__G, HIGH);			//   0     0    1   1  X	disable column output
	digitalWrite(PIN_REN, LOW);			//   0     0    1   1  0	disable row output
# else
#  error "HW_MODEL not set to supported hardware configuration"
# endif
#endif
}

void LED_row_on(int row, byte *buf)
{
#if HW_MODEL == MODEL_64x7
	/* shift out columns */
	if (row > 0) {
		for (int i=0; i<8; i++) {
			PORTK = buf[row*8+i];
			digitalWrite(PIN_PS0, HIGH);	// PS0 PS1
			digitalWrite(PIN_PS1, HIGH);	//  1   1	shift data into columns
			digitalWrite(PIN_PS1, LOW);		//  1   0	idle
		}
		/* address row and turn on */
		PORTK = ((row+1) << 5) & 0xe0;	// PS0 PS1
		digitalWrite(PIN_PS0, LOW);		//  0   0	show row
	}
#else
# if HW_MODEL == MODEL_64x8
	/* shift out columns */
	PORTK = buf[row*8+i];				//            _____ _
	digitalWrite(PIN__G, HIGH);			// SRCLK RCLK SRCLR G REN
	digitalWrite(PIN__SRCLR, LOW);		//   X     X    0   1  X
	digitalWrite(PIN_REN, LOW);			//   X     X    0   1  0	reset, all off
	digitalWrite(PIN__SRCLR, HIGH);		//   X     X    1   1  0	all off
	digitalWrite(PIN_SRCLK, LOW);		//   0     X    1   1  0	all off
	digitalWrite(PIN_RCLK, LOW);		//   0     0    1   1  0	all off

	for (int i=0; i<8; i++) {
		PORTK = buf[row*8+i];
		digitalWrite(PIN_SRCLK, HIGH);	//   1     0    1   1  0	shift into columns
		digitalWrite(PIN_SRCLK, LOW);	//   0     0    1   1  0	
	}
	digitalWrite(PIN_SRCLK, HIGH);		//   0     1    1   1  0	latch column outputs
	digitalWrite(PIN_SRCLK, LOW);		//   0     0    1   1  0	
	digitalWrite(PIN__G, LOW);			//   0     0    1   0  0	enable column output
	digitalWrite(PIN_R0, row & 1);		//							address row
	digitalWrite(PIN_R1, row & 2);		//							address row
	digitalWrite(PIN_R2, row & 4);		//							address row
	digitalWrite(PIN_REN, HIGH);		//   0     0    1   0  1	enable row output
# else
#  error "HW_MODEL not set to supported hardware configuration"
# endif
#endif
}

void copy_all_rows(byte *src, byte *dst)
{
	for (int i = 0; i < N_ROWS; i++) {
		copy_row_bits(i, src, dst);
	}
}

/* copy a single row of matrix data from src to dst.
 * src is arranged as the matrix is physically viewed:
 * | byte 0 | byte 1 | ... | byte 7 |
 * |76543210|76543210| ... |76543210|
 *  ^_leftmost pixel     rightmost_^
 *
 * dst is arranged as expected by the hardware so that the
 * bytes can be directly output during refresh.
 */

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
