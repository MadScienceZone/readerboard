#include "readerboard.h"
#include "commands.h"

//
// Set this for each individual unit with the hardware version and firmware version.
// Also set the unit's serial number in place of the XXXXX.  Serial numbers 000-299 are
// reserved for the author's use.
//
// Each of these fields are variable width, do not necessarily have leading zeroes,
// and the version numbers may be any string conforming to semantic versioning 2.0.0
// (see semver.org).
//
//
// Implementation Notes
//     Commands are accepted on the USB and RS-485 interfaces (if the latter is enabled).
//     We use a common parser based around a simple state machine in either case, but
//     it knows what input source it's in the middle of reading a command from. If any
//     data are received on the other interface than the one an incomplete command is still
//     being expected from, the interpreter shifts to the error state and then switches
//     to start reading the new incoming command instead.
//
// USB
//     Commands received via USB must be terminated by a ^D. If an error is encountered,
//     the interpreter will ignore data until a ^D is received before starting to interpret
//     anything further.
//
// RS-485
//     When enabled, the device has a designated address 00-15 (Ad) so that it can pay
//     attention to commands intended for it while ignoring commands for other devices.
//     There is also a global address (Ag) which the device will also respond to, so that
//     commands may be addressed to multiple units at once.
//
//     On this interface, commands start with a byte that has the MSB set. All other bytes
//     have MSB cleared. If an error is encountered, the interpreter will ignore data until
//     finding a byte with MSB set.
//
//     The following escape codes are recognized:
//         $7E xx   accept xx with MSB set
//         $7F xx   accept xx without further interpretation
//
//     The recognized start bytes are: (a=Ad or Ag)
//         $8a      turn off all lights (no command follows; this single byte suffices)
//         $9a      start of normal command addressed to unit a (TODO: accept Ag and remove $Dg?)
//         $Ba      start of command addressed to explicit list of addresses
//         $Dg      start of normal command addressed to all units
//
// State Machine
//     -> ERR       signal error condition and go to ERROR state
//     -> END       go to END state
//     +            change transition and then re-examine current input byte
//                                                                             ____ 
//          $8a/$8g (485) _      $Bg (485)        _________  N    ____________|_   | Ad[0]-Ad[N-2]
//                       | |  +----------------->|Collect N|---->|CollectAddress|<-+
//        MSB=1 (485)+   | |  |                  |_________|     |______________|
//  _____  ^D (USB)     _|_V__|_   $9a/$Dg (485)  _____             |Ad[N-1]
// |ERROR|----------->||Idle    ||-------------->|Start|<-----------+
// |_____|<-----------||________||               |_____|
//   ^ |       *            |                        |
//   |_|*                   |<-----------------------+     _ 
//          ?/Q (USB)       |            ____             | |*  MSB=1 (485)+
//  END <-------------------+ '*'   ____|_   | led       _V_|_  ^D (USB)
//                          +----->|Strobe|<-+      $   |END  |-----------> Idle
//   _____    = (USB)       |      |______|------------>|_____|
//  |Set  |<----------------+  C                         
//  |_____|                 +-----> END                        ____ 
//    | Ad/'_'              |  <    ______  loop      ________|_   | char
//   _V_____                +----->|Scroll|--------->|ScrollText|<-+       ESC
//  |SetUspd|               |      |______|          |__________|-------------> END
//  |_______|               |  @    ______  pos
//    | speed               +----->|SetCol|---------> END
//   _V_____                |      |______|
//  |SetRspd|               |  A    __________  n
//  |_______|               +----->|SelectFont|-----> END
//    | speed               |      |__________|
//   _V_____                |           ____
//  |SetDg  |               |  F    ___|_   |led
//  |_______|               +----->|Flash|<-+      $
//    | Addr                |      |_____|----------> END
//    V                     |  H    ________  n
//   END                    +----->|BarGraph|-------> END
//                          |      |________|                                                          ____
//                          |  I    __________ merge   ________ pos     _______________ trans  _______|_   |nybble
//                          +----->|ImageMerge|------>|ImageCol|------>|ImageTransition|----->|ImageData|<-+    $
//                          |      |__________|       |________|       |_______________|      |_________|---------> END
//                          |             ____
//                          |  L    _____|_   |led
//                          +----->|LightOn|<-+     $
//                          |      |_______|-----------> END
//                          |  S    ________ led
//                          +----->|LightSet|----------> END
//                          |      |________|                                                        ____
//                          |  T    _________ merge  _________ align   ______________ trans   ______|_   |char
//                          +----->|TextMerge|----->|TextAlign|------>|TextTransition|------>|TextData|<-+     ESC
//                          |      |_________|      |_________|       |______________|       |________|------------> END
//                          |  X
//                          +-----> END
//
// Recognized commands:
//     <align> ::= '.' | '<' | '|' | '>' (.=none, <=left, |=center, >=right)
//     <alphanumeric> ::= <digit> | 'a' | 'b' | ... | 'z' | 'A' | 'B' | ... | 'Z'
//     <col> ::= <hexbyte>              (LSB=top)
//     <digit> ::= '0' | '1' | ... | '9'
//     <light> ::= '0' | '1' | '2' | ... '7'
//     <loop> ::= '.' | 'L'             (.=once, L=loop)
//     <merge> ::= '.' | 'M'            (.=clear, M=merge)
//     <model> ::= 'L' | 'M'            (L=legacy, M=matrix64x8)
//     <onoff> ::= '0' | '1'            (0=off, 1=on)
//     <pos> ::= '~' | numeric value 0-63 encoded as '0'=0, '1'=1, ... 'o'=63 by ASCII sequence (~=current cursor position) [1]
//     <romversion> ::= <semver>
//     <running> ::= '0' | '1'
//     <sequence> ::= 'X' | <pos> '@' <light>*          [2]
//     <serial> ::= <alphanumeric>+
//     <semver> ::= version string conforming to semver.org specification 2.0.0
//     <status> ::= <running> <sequence>
//     <string> ::= any sequence of characters except ^D or ESCAPE, with the following special sequences recognized:
//                  '^C' <pos>      move cursor to absolute column number
//                  '^D'            never allowed in commands (this is a command terminator)
//                  '^F' <digit>    switch font
//                  '^H' <pos>      move cursor back a number of columns
//                  '^L' <pos>      move cursor forward a number of columns
//                  '^['            (aka ESCAPE) not allowed in string (it terminates the string value itself).
//     <trans> ::= '.'                  (.=none)
//     <hwversion> ::= <semver>
//     <l> ::= '0' | '1' | ... | '7'    (discrete light ID)
//     $ ::= '$' | ESCAPE
//
//   Busylight Compatibility (terminating ^D recommended but not required)
//     'F' <l>* $                   Flash one or more lights in sequence
//     'S' <l>                      Turn on one LED steady; stop flasher
//     '*' <l>* $                   Strobe one or more lights in sequence
//     'X'                          Turn off all LEDs
//     '?'                          Report state of discrete LEDs (returns 'L' <onoff>* 'F' <status (flasher)> 'S' <status (strober)> '\n') [2]
//
//   New Discrete LED Commands
//     'L' <l>* $                   Turn on multiple LEDs steady
//
//   Matrix Display Commands
//     'C'                          Clear matrix display
//     'H' <digit>                  Draw bar graph data point
//     'I' <merge> <pos> <trans> <col>* $
//                                  Draw bitmap image starting at <pos>
//     'Q'                          Query status (returns 'Q' <model> 'V' <hwversion> 'R' <romversion> 'S' <serial> 'M' <matrixcols> '\n')
//::   '<' <loop> <string>* ESCAPE  Scroll <string> across display, optionally looping repeatedly.
//::   'T' <merge> <align> <trans> <string>* ESCAPE 
//::                                Print string in current font from current cursor position
//::   '@' <pos>                    Move cursor to specified column number
//::   'A' <digit>                  Select font
//     '^D'                         Mark end of command(s). Not strictly needed after every command, but if a parser error is encountered,
//                                  the readerboard will ignore all further input until the next ^D character is received, so at least place ^D after
//                                  each logcal set of commands that together represent a sign update. A ^D also aborts any incomplete command in
//                                  progress.
//
// NOTES
// [1] Numeric encoding used:
//      00 '0' | 10 ':' | 20 'D' | 30 'N' | 40 'X' | 50 'b' | 60 'l'
//      01 '1' | 11 ';' | 21 'E' | 31 'O' | 41 'Y' | 51 'c' | 61 'm'
//      02 '2' | 12 '<' | 22 'F' | 32 'P' | 42 'Z' | 52 'd' | 62 'n'
//      03 '3' | 13 '=' | 23 'G' | 33 'Q' | 43 '[' | 53 'e' | 63 'o'
//      04 '4' | 14 '>' | 24 'H' | 34 'R' | 44 '\' | 54 'f' | current cursor position '~'
//      05 '5' | 15 '?' | 25 'I' | 35 'S' | 45 ']' | 55 'g'
//      06 '6' | 16 '@' | 26 'J' | 36 'T' | 46 '^' | 56 'h'
//      07 '7' | 17 'A' | 27 'K' | 37 'U' | 47 '_' | 57 'i'
//      08 '8' | 18 'B' | 28 'L' | 38 'V' | 48 '`' | 58 'j'
//      09 '9' | 19 'C' | 29 'M' | 39 'W' | 49 'a' | 59 'k'
//
// [2] Care must be taken when parsing the discrete LED status query response. In the flasher
//     and strober status data, a leading 'X' means there is no defined sequence
//     ONLY if the next character is not '@'. However, if the start of the status field
//     is 'X@', then the 'X' is the <pos> value 40 in the sequence.
//
const int CSM_BUFSIZE = 64;
class CommandStateMachine {
private:
    enum StateCode { 
        IdleState, 
        ErrorState,
        BarGraphState,
        FlashState, 
        ImageStateCol,
        ImageStateData,
        ImageStateMerge,
        ImageStateTransition,
        LightOnState, 
        LightSetState,
        StrobeState, 
		EndState,
    } state;
    byte LEDset;
    byte command_in_progress;
    bool merge;
    TransitionEffect transition;
    byte buffer[CSM_BUFSIZE];
    byte buffer_idx;
    bool nybble;
    byte bytebuf;
    byte column;

public:
    void accept(int inputchar);
    bool accept_hex_nybble(int inputchar);
    bool accept_encoded_int63(int inputchar);
    void begin(void);
    void commit_graph_datapoint(int value);
    void commit_image_data(void);
    void report_state(void);
    void reset(void);
	void end_cmd(void);
    void set_lights(byte lights);
    void set_light_sequence(byte which);
    void error(void);
};

void CommandStateMachine::begin(void)
{
    column = 0;
    reset();
}

//
// (CSM) accept_encoded_int63(inputchar)
//   Reads the input character as an ASCII-encoded value in the range [0,63] as described
//   above. If successful, the decoded value is placed in bytebuf and true is returned.
//
//   In case of error, the state machine is reset with an error condition, and false is returned.
//
bool CommandStateMachine::accept_encoded_int63(int inputchar)
{
    nybble = false;
    if (inputchar == '~') {
        bytebuf = min(column, 63);
        return true;
    }
    if (inputchar >= '0' && inputchar <= 'o') {
        bytebuf = inputchar - '0';
        return true;
    }
    error();
    return false;
}

//
// (CSM) accept_hex_nybble(inputchar)
//   Reads the input character as a hex nybble. This is designed to be part of a sequence
//   that collects hex bytes a nybble at a time. This function returns true when a full
//   byte has been collected, with the byte's value in bytebuf. (The nybble object attribute
//   is used to determine if we've only collected one nybble of the byte at this point.)
//
//   If an error is encountered, the state machine is reset with an error condition, which
//   should interrupt whatever operation was in progress automatically.
//
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

//
// (CSM) end_cmd()
//   Wait for the ^D command terminator before beginning the next command.
//
void CommandStateMachine::end_cmd(void)
{
	state = EndState;
}

//
// (CSM) reset()
//   Reset the state machine. This may be called
//   upon the completion of an operation to reset the machine
//   to its idle state, ready for the start of a new command.
//
void CommandStateMachine::reset(void)
{
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

//
// (CSM) accept(inputchar)
//   Accepts an input character from the host. This is fed into the state machine
//   as it parses the incoming commands, eventually executing them when a complete
//   command has been received.
//
void CommandStateMachine::accept(int inputchar)
{
    if (inputchar < 0) {
        return;
    }

    switch (state) {
    // stuck in error state until end of command
	case EndState:
    case ErrorState:
        if (inputchar == '\x04') {
            reset();
        }
        break;

    // start of command
    case IdleState:
        switch (inputchar) {
        case '\x04':
            reset();
            break;

        case 'C':
            clear_buffer(image_buffer);
            display_buffer(image_buffer, NoTransition);
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
			Serial.write('M');
			for (int i=0; i<64; i++) {
				Serial.print(image_buffer[i], HEX);
			}
            Serial.write('\n');
            end_cmd();
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
            end_cmd();
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
        if (accept_encoded_int63(inputchar)) {
            column = bytebuf;
            state = ImageStateTransition;
        }
        break;

    case ImageStateTransition:
		switch (inputchar) {
		case '.': transition = NoTransition; break;
		case '<': transition = TransScrollLeft; break;
		case '>': transition = TransScrollRight; break;
		case '^': transition = TransScrollUp; break;
		case 'v': transition = TransScrollDown; break;
		case 'L': transition = TransWipeLeft; break;
		case 'R': transition = TransWipeRight; break;
		case 'U': transition = TransWipeUp; break;
		case 'D': transition = TransWipeDown; break;
		case '|': transition = TransWipeLeftRight; break;
		case '-': transition = TransWipeUpDown; break;
		case '?': 
			// random transition pattern
			switch (millis() % 10) {
			case 0: transition = TransScrollLeft; break;
			case 1: transition = TransScrollRight; break;
			case 2: transition = TransScrollUp; break;
			case 3: transition = TransScrollDown; break;
			case 4: transition = TransWipeLeft; break;
			case 5: transition = TransWipeRight; break;
			case 6: transition = TransWipeUp; break;
			case 7: transition = TransWipeDown; break;
			case 8: transition = TransWipeLeftRight; break;
			case 9: transition = TransWipeUpDown; break;
			default: transition = NoTransition; break;
			}
			break;
		default:
			error();
        }

		state = ImageStateData;
        break;

    case ImageStateData:
        if (inputchar == '$' || inputchar == '\x1b') {
            if (nybble) {
                error();
                break;
            }
            commit_image_data();
            end_cmd();
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

//
// (CSM) report_state()
//   Reports the discrete LED state to the host.
//   This is the action that results from the '?' command.
//
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

//
// (CSM) error()
//   Handles an error condition encountered when running input
//   through the state machine. This signals an error if possible
//   on the discrete LEDs and halts further interpretation until
//   the next ^D is received.
//
void CommandStateMachine::error(void)
{
    set_lights(0b00011001);
    state = ErrorState;
}

//
// (CSM) set_lights(bits)
//   Turn on/off the discrete LEDs according to the 1/0 bits
//   in the bits parameter, with the LSB being LED L0.
//
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

//
// (CSM) commit_image_data()
//   Send the collected bytes of raw bitmap image data in buffer[0,buffer_idx)
//   to the hardware display. Advances the current cursor position to be past the
//   end of the image data.
//
void CommandStateMachine::commit_image_data(void)
{
    for (byte c=0; c < buffer_idx; c++) {
        draw_column(c+column, buffer[c], merge, image_buffer);
    }
    display_buffer(image_buffer, transition);
    column = min(column + buffer_idx, 63);
}

//
// (CSM) commit_graph_datapoint(value)
//    Plots the data value in the range [0,8] by lighting up
//    that number of lights from the bottom row, displaying them
//    in the far right column, shifting the display one column left.
//
void CommandStateMachine::commit_graph_datapoint(int value)
{
    byte bits = 0;

    shift_left(image_buffer);
    switch (value) {
	case 0: bits = 0x00; break;
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
    display_buffer(image_buffer, NoTransition);
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
