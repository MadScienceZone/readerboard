#include "readerboard.h"
#include "commands.h"

//
// Set this for each individual unit with the hardware version and firmware version.
// Also set the unit's serial number in place of the XXXXX.  Serial numbers 100-299 are
// reserved for the author's use.
//
#define SERIAL_VERSION_STAMP "V1.0.0R0.0.0SXXXXX"
//                             \___/ \___/ \___/
//                               |     |     |
//                  Hardware version   |     |
//                        Firmware version   |
//                                 Serial number
//
//
// Recognized commands:
//	   <col> ::= <hexbyte>              (LSB=top)
//	   <digit> ::= '0' | '1' | ... | '9'
//	   <light> ::= '0' | '1' | '2' | ... '7'
//	   <merge> ::= '.' | 'M'            (.=clear, M=merge)
//	   <model> ::= 'L' | 'M'			(L=legacy, M=matrix64x8)
//	   <onoff> ::= '0' | '1'			(0=off, 1=on)
//	   <pos> ::= 0-63 encoded as '0'=0, '1'=1, ... 'o'=63
//	   <romversion> ::= <semver>
//	   <running> ::= '0' | '1'
//	   <sequence> ::= 'X' | <pos> '@' <light>*
//	   <serial> ::= <alphanumeric>*
//	   <status> ::= <running> <sequence>
//	   <trans> ::= '.'					(.=none)
//	   <version> ::= <semver>
//     <l> ::= '0' | '1' | ... | '7'	(discrete light ID)
//     $ ::= '$' | ESCAPE
//
//   Busylight Compatibility
//     'F' <l>* $					Flash one or more lights in sequence
//     'S' <l>						Turn on one LED steady; stop flasher
//     '*' <l>* $					Strobe one or more lights in sequence
//     'X'							Turn off all LEDs
//     '?'							Report state of discrete LEDs (returns 'L' <onoff>* 'F' <status (flasher)> 'S' <status (strober)> '\n')
//
//   New Discrete LED Commands
//     'L' <l>* $					Turn on multiple LEDs steady
//
//   Matrix Display Commands
//     'C'                          Clear matrix display
//     'H' <digit>                  Draw bar graph data point
//     'I' <merge> <col> <trans> <col>* $
//									Draw bitmap image starting at <col>
//     'Q'							Query status (returns 'Q' <model> 'V' <version> 'R' <romversion> 'S' <serial> '\n')
//
// NOTES
//	 Care must be taken when parsing the discrete LED status query response. In the flasher
//   and strober status data, a leading 'X' means there is no defined sequence
//   ONLY if the next character is not '@'. However, if the start of the status field
//   is 'X@', then the 'X' is the <pos> value indicating how far into the sequence 
//   the display is.
//
const int CSM_BUFSIZE = 64;
class CommandStateMachine {
private:
	enum StateCode { 
		IdleState, 
		BarGraphState,
		FlashState, 
		ImageStateCol,
		ImageStateData,
		ImageStateMerge,
		ImageStateTransition,
		LightOnState, 
		LightSetState,
		StrobeState, 
	} state;
	byte LEDset;
	byte command_in_progress;
	bool merge;
	enum TransitionEffect { NoTransition } transition;
	byte buffer[CSM_BUFSIZE];
	byte buffer_idx;
	bool nybble;
	byte bytebuf;
	byte column;

public:
	void accept(int inputchar);
	bool accept_hex_nybble(int inputchar);
	void begin(void);
	void commit_graph_datapoint(int value);
	void commit_image_data(void);
	void report_state(void);
	void reset(void);
	void set_lights(byte lights);
	void set_light_sequence(byte which);
	void error(void);
};

void CommandStateMachine::begin(void)
{
	reset();
}

bool CommandStateMachine::accept_hex_nybble(int inputchar)
{
	byte val;

	if (inputchar >= '0' && inputchar <= '9')
		val = inputchar - '0';
	else if (inputchar >= 'A' && inputchar <= 'F')
		val = inputchar - 'A' + 10;
	else if (inputchar >= 'a' && inputchar <= 'f')
		val = inputchar - 'a' + 10;
	else {
		error();
		return false;
	}

	if (nybble) {
		bytebuf = (bytebuf << 4) | val;
		nybble = false;
		return true;
	}
	bytebuf = val;
	nybble = true;
	return false;
}

void CommandStateMachine::reset(void)
{
	column = 0;
	state = IdleState;
	command_in_progress = 0;
	transition = NoTransition;
	buffer_idx = 0;
	merge = false;
	LEDset = 0;
	nybble = false;
	bytebuf = 0;
	for (int i=0; i<CSM_BUFSIZE; i++)
		buffer[i] = 0;
}

void CommandStateMachine::accept(int inputchar)
{
	if (inputchar < 0) {
		return;
	}

	switch (state) {
	// start of command
	case IdleState:
		switch (inputchar) {
		case 'C':
			clear_buffer(image_buffer);
			display_buffer(image_buffer);
			break;

		case 'H':
			state = BarGraphState;
			break;

		case 'I':
			state = ImageStateMerge;
			break;

		case 'Q':
			Serial.write('Q');
#if HW_MODEL == MODEL_LEGACY_64x7
			Serial.write('L');
#else
# if HW_MODEL == MODEL_CURRENT_64x8
			Serial.write('M');
# else
#  error "hardware model not supported"
# endif
#endif
			Serial.write(SERIAL_VERSION_STAMP);
			Serial.write('\n');
			reset();
			break;

#if HW_MODEL == MODEL_CURRENT_64x8
		case 'F':
		case 'f':
			state = FlashState;
			flasher.stop();
			break;

		case '*':
			state = StrobeState;
			strober.stop();
			break;

		case 'L':
			LEDset = 0;
			state = LightSetState;
			break;

		case 'S':
		case 's':
			state = LightOnState;
			break;

		case 'X':
		case 'x':
			discrete_all_off(true);
			break;

		case '?':
			report_state();
			break;
#endif /* MODEL_CURRENT_64x8 */

		default:
			error();
		}
		break;

	// Collecting LED numbers to form a flash or strobe sequence
#if HW_MODEL == MODEL_CURRENT_64x8
	case FlashState:
	case StrobeState:
		switch (inputchar) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
			if (state == FlashState) {
				flasher.append(inputchar - '0');
			}
			else {
				strober.append(inputchar - '0');
			}
			break;

		case '$':
		case '\x1b':
			discrete_all_off(false);
			if (state == FlashState) {
				flasher.start();
			}
			else {
				strober.start();
			}
			reset();
			break;

		default:
			error();
		}
		break;

	// Collecting LED numbers to light at once
	case LightSetState:
		switch (inputchar) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
			LEDset |= (1 << (inputchar - '0'));
			break;

		case '$':
		case '\x1b':
			set_lights(LEDset);
			LEDset = 0;
			state = IdleState;
			break;

		default:
			error();
		}
		break;

	// waiting for a light number to turn on
	case LightOnState:
		switch (inputchar) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
			set_lights(1 << (inputchar - '0'));
			break;

		default:
			error();
		}
		reset();
		break;
#endif /* MODEL_CURRENT_64x8 */

	case BarGraphState:
		if (inputchar >= '0' && inputchar <= '9') {
			commit_graph_datapoint(inputchar - '0');
			reset();
		}
		else {
			error();
		}
		break;

	case ImageStateMerge:
		if (inputchar == '.') 
			merge = false;
		else if (inputchar == 'M')
			merge = true;
		else
			error();
		state = ImageStateCol;
		break;

	case ImageStateCol:
		if (accept_hex_nybble(inputchar)) {
			column = bytebuf;
			state = ImageStateTransition;
		}
		break;

	case ImageStateTransition:
		if (inputchar == '.') {
			transition = NoTransition;
			state = ImageStateData;
		}
		else {
			error();
		}
		break;

	case ImageStateData:
		if (inputchar == '$' || inputchar == '\x1b') {
			if (nybble) {
				error();
				break;
			}
			commit_image_data();
			reset();
			break;
		}
		if (accept_hex_nybble(inputchar)) {
			if (buffer_idx < CSM_BUFSIZE) {
				buffer[buffer_idx++] = bytebuf;
				bytebuf = 0;
				break;
			}
		}
		break;

	default:
		error();
	}
}
void CommandStateMachine::report_state(void)
{
	int i = 0;
	Serial.write('L');
	for (i = 0; i < 8; i++) {
		Serial.write(discrete_query(i) ? '1' : '0');
	}
	Serial.write('F');
	flasher.report_state();
	Serial.write('S');
	strober.report_state();
	Serial.write('\n');
}

// turn on both red lights + white to indicate a command error
void CommandStateMachine::error(void)
{
	set_lights(0b00011001);
	reset();
}

void CommandStateMachine::set_lights(byte bits)
{
	discrete_all_off(false);
	flasher.stop();
	for (int i = 0; i < 8; i++) {
		if (bits & (1 << i)) {
			discrete_set(i, true);
		}
	}
}

void CommandStateMachine::commit_image_data(void)
{
	for (byte c=0; c < buffer_idx; c++) {
		draw_column(c+column, buffer[c], merge, image_buffer);
	}
	display_buffer(image_buffer);
}

void CommandStateMachine::commit_graph_datapoint(int value)
{
	byte bits = 0;

	shift_left(image_buffer);
	switch (value) {
	case 1: bits = 0x80; break;
	case 2: bits = 0xc0; break;
	case 3: bits = 0xe0; break;
	case 4: bits = 0xf0; break;
	case 5: bits = 0xf8; break;
	case 6: bits = 0xfc; break;
	case 7: bits = 0xfe; break;
	case 8:
	case 9:
		bits = 0xff; 
		break;
	default:
		bits = 0xaa;
	}
#if HW_MODEL == MODEL_LEGACY_64x7
	bits >>= 1;
#endif
	draw_column(63, bits, false, image_buffer);
	display_buffer(image_buffer);
}

CommandStateMachine cmd;

//
// setup_commands()
//   Initialize this module.
//
void setup_commands(void)
{
	cmd.begin();
}
	
//
// receive_serial_data()
//   Read in a character from the serial port and buffer it up
//   until we have a complete command, then execute it.
//

void receive_serial_data(void)
{
	if (Serial.available() > 0) {
		cmd.accept(Serial.read());
	}
}
