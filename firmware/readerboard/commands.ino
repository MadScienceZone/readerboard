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
//         $9a      start of normal command addressed to unit a
//         $Ba      start of command addressed to explicit list of addresses
//
// State Machine
//     -> ERR       signal error condition and go to ERROR state
//     -> END       go to END state
//     +            change transition and then re-examine current input byte
//                                                                             ____ 
//          $8a/$8g (485) _      $Bg (485)        _________  N    ____________|_   | Ad[0]-Ad[N-2]
//                       | |  +----------------->|Collect N|---->|CollectAddress|<-+
//        MSB=1 (485)+   | |  |                  |_________|     |______________|
//  _____  ^D (USB)     _|_V__|_   $9a/$9g (485)  _____             |Ad[N-1]             
// |ERROR|----------->||Idle    ||-------------->|Start|<-----------+-------------------> END
// |_____|<-----------||________||               |_____|                                 
//   ^ |       *            |                        |
//   |_|*                   |<-----------------------+     _ 
//          ?/Q (USB)       |            ____             | |*  MSB=1 (485)+
//  END <-------------------+ '*'   ____|_   | led       _V_|_  ^D (USB)
//                          +----->|Strobe|<-+      $   |END  |-----------> Idle
//   ____________  = (USB)  |      |______|------------>|_____|
//  |Set         |<---------+  C                         
//  |____________|          +-----> END                        ____ 
//    | Ad/'.'  |#          |  <    ______  loop      ________|_   | char 
//   _V_____   _V___        +----->|Scroll|--------->|ScrollText|<-+       ESC
//  |SetUspd| |SetSN|___*   |      |______|          |__________|-------------> END
//  |_______| |_____|<-+    |  @    ______  pos
//    | speed   |$          +----->|SetCol|---------> END
//   _V_____   _V____       |      |______|
//  |SetRspd| |SetSN2|      |  A    __________  n
//  |_______| |______|      +----->|SelectFont|-----> END
//    | speed   |#          |      |__________|
//   _V_____   _V____       |           ____
//  |SetDg  | |SetSN3|      |  F    ___|_   |led
//  |_______| |______|      +----->|Flash|<-+      $
//    | Addr    |=          |      |_____|----------> END
//    V         V           |  H    ________  n
//   END       END          +----->|BarGraph|-------> END
//                          |      |________|                                                         
//                          |           |K
//                          |           |<------+
//                          |       ____V___    |rgb0-rgb6
//                          |      |BarColor|___+    
//                          |      |        |------------------> END
//                          |      |________|     rgb7
//                          |
//                          |                                                                          ____
//                          |  I    __________ merge   ________ pos     _______________ trans  _______|_   |nybble (x3)
//                          +----->|ImageMerge|------>|ImageCol|------>|ImageTransition|----->|ImageData|<-+    $
//                          |      |__________|       |________|       |_______________|      |_________|---------> END
//                          |             ____
//                          |  L    _____|_   |led
//                          +----->|LightSet|<-+     $
//                          |      |________|-----------> END
//                          |  S    ________ 
//                          +----->|LightOn |----------> END
//                          |      |________|
//                          |    
//                          |  T    _________ merge  _________ align   ______________ trans   ______|_   |char
//                          +----->|TextMerge|----->|TextAlign|------>|TextTransition|------>|TextData|<-+     ESC
//                          |      |_________|      |_________|       |______________|       |________|------------> END
//                          |  X
//                          +-----> END
//                          |
//                          |  %
//                          +-----> END
//                          |
//                          |  K    ________  rgb
//                          +----->|SetColor|--------> END
//                                 |________|
//
// Recognized commands:
//     <addr>         ::= <eint>          (device address)
//     <align>        ::= '.' | '<' | '|' | '>' (.=none, <=left, |=center, >=right)
//     <alphanumeric> ::= <digit> | 'a' | 'b' | ... | 'z' | 'A' | 'B' | ... | 'Z'
//     <baud>         ::= <digit> | 'A' | 'B' | 'C'
//     <col>          ::= <hexdigit> <hexdigit>      (LSB=top pixel)
//     <colorstatus>  ::= <signstatus> 'M' <col>* $ <col>* $ <col>* $ <col>* $
//     <digit>        ::= '0' | '1' | ... | '9'
//     <eeprom>       ::= '_' | 'I' | 'X'
//     <eint>         ::= '0' | ... | 'o' (binary numeric value in range 0-63 + 48 as ASCII character)
//     <flashstat>    ::= <isrunning> <sequence>
//     <hexdigit>     ::= <digit> | 'A' | 'B' | 'C' | 'D' | 'E' | 'F' | 'a' | 'b' | 'c' | 'd' | 'e' | 'f'
//     <isrunning>    ::= 'S' | 'R'
//     <glob>         ::= <eint>          (global address)
//     <l>            ::= <alphanumeric>   (status LED position digit or defined color code letter)
//     <loop>         ::= '.' | 'L'             (.=once, L=loop)
//     <merge>        ::= '.' | 'M'
//     <monostatus>   ::= <signstatus> 'M' <col>* $ <col>* $
//     <pos>          ::= <eint> | '~'
//     <qreply>       ::= 'Q' ('M' <monostatus> | 'C' <colorstatus>) '\n'
//     <rgb>          ::= 0011fbgr        (encoded byte f=flashing, b=blue, g=green, r=red)
//     <rspd>         ::= <baud>          (RS-485 speed)
//     <sequence>     ::= '_' | <eint> '@' <l>*
//     <signstatus>   ::= '=' <addr> <uspd> <rspd> <glob> <eeprom> $ 'V' <string> $ 'R' <string> $ 'S' <string> $
//     <statusreply>  ::= 'L' <l>* '$' 'F' <flashstat> '$' 'S' <strobestat> '$' '\n'
//     <string>       ::= any sequence of characters except ^D or ESCAPE, with the following special sequences recognized:
//                        '^C' <pos>      move cursor to absolute column number
//                        '^D'            never allowed in commands (this is a command terminator)
//                        '^F' <digit>    switch font
//                        '^H' <pos>      move cursor back a number of columns
//                        '^L' <pos>      move cursor forward a number of columns
//                        '^['            (aka ESCAPE) not allowed in string (it terminates the string value itself).
//     <strobestat>   ::= <isrunning> <sequence>
//     <trans>        ::= '.' | '>' | '<' | '^' | 'v' | 'L' | 'R' | 'U' | 'D' | '|' | '-' | '?'
//     <uspd>         ::= <baud>          (usb speed)
//     $              ::= '$' | ESCAPE
//
//   Busylight Compatibility
//     'F' <l>* $                   Flash one or more lights in sequence
//     'S' <l>                      Turn on one LED steady; stop flasher
//     '*' <l>* $                   Strobe one or more lights in sequence
//     'X'                          Turn off all LEDs
//     '?'                          Report state of discrete LEDs (replies with <statusreply>)
//
//   New Discrete LED Commands
//     'L' <l>* $                   Turn on multiple LEDs steady
//
//   Matrix Display Commands
//     'C'                          Clear matrix display
//     'H' (<digit> | 'K' <rgb>*8)  Draw bar graph data point
//     'I' <merge> <pos> <trans> <col>* $ <col>* $ <col>* $
//                                  Draw bitmap image starting at <pos>
//::   'K' <rgb>                    Change current color
//     'Q'                          Query status (replies with <qreply>)
//::   '<' <loop> <string> ESCAPE   Scroll <string> across display, optionally looping repeatedly.
//::   'T' <merge> <align> <trans> <string>* ESCAPE 
//::                                Print string in current font from current cursor position
//::   '@' <pos>                    Move cursor to specified column number
//::   'A' <digit>                  Select font
//     '%'                          Run test pattern set
//     '=' <addr> <uspd> <rspd> <glob>
//
//     '^D'                         Mark end of command(s). 
//                                  The readerboard will ignore all further input until the next ^D character is received.
//                                  A ^D also aborts any incomplete command in progress.
//
#if IS_READERBOARD
const int CSM_BUFSIZE = 1024;
#else
const int CSM_BUFSIZE = 128;
#endif

class CommandStateMachine {
private:
    enum StateCode { 
        IdleState, 
		EndState,
        ErrorState,
        CollectNState,
        CollectAddressState,
        StartState,
        SetState,
        SetUspdState,
        SetRspdState,
        SetDgState,
        StrobeState, 
        FlashState, 
        LightOnState, 
        SetSNState,
        SetSN2State,
        SetSN3State,
#if IS_READERBOARD
        ScrollState,
        ScrollTextState,
        SetColState,
        SelectFontState,
        BarColorState,
        TextMergeState,
        TextAlignState,
        TextTransitionState,
        TextDataState,
        SetColorState,
        BarGraphState,
        ImageStateCol,
        ImageStateData,
        ImageStateMerge,
        ImageStateTransition,
        LightSetState,
        ShowBannerState,
#endif
    } state;
    byte LEDset;
    byte command_in_progress;
#if IS_READERBOARD
    bool merge;
    TransitionEffect transition;
    byte scrolling_buffer[CSM_BUFSIZE];
    byte column;
    byte font;
    byte color;
    bool repeat;
    bool nybble;
#endif
    byte buffer[CSM_BUFSIZE];
    byte buffer_idx;
    byte bytebuf;
    bool esc_literal;
    bool esc_msb;
    bool target_is_global;
    int  k;
    serial_source_t cmd_source;

public:
    void accept(serial_source_t source, int inputchar);
#if IS_READERBOARD
    bool accept_hex_nybble(int inputchar);
    bool accept_encoded_pos(int inputchar);     // 6-bit position or ~ for current
    bool accept_encoded_rgb(int inputchar);     // 4-bit color code
    bool accept_encoded_transition(int inputchar);
    void commit_graph_datapoint(int value);
    void commit_graph_datacolors(void);
#endif
    bool accept_encoded_int6(int inputchar);    // 6-bit unsigned integer
    void begin(void);
    void report_state(bool);
    void reset(void);
	void end_cmd(void);
    void set_lights(byte lights);
    void set_light_sequence(byte which);
    void error(void);
    void append_bytebuf(void);
    void append_byte(byte n);
};

void CommandStateMachine::begin(void)
{
#if IS_READERBOARD
    column = 0;
    font = 0;
    color = 1;
#endif
    reset();
}

//
// (CSM) accept_encoded_pos(inputchar)
//   Reads the input character as an ASCII-encoded value in the range [0,63] as described
//   above. If successful, the decoded value is placed in bytebuf and true is returned.
//
//   In case of error, the state machine is reset with an error condition, and false is returned.
//
#if IS_READERBOARD
bool CommandStateMachine::accept_encoded_pos(int inputchar)
{
    nybble = false;
    if (inputchar == '~') {
        bytebuf = min(column, 63);
        return true;
    }
    return accept_encoded_int6(inputchar);
}
#endif

bool CommandStateMachine::accept_encoded_int6(int inputchar)
{
#if IS_READERBOARD
    nybble = false;
#endif
    if (inputchar >= '0' && inputchar <= 'o') {
        bytebuf = inputchar - '0';
        return true;
    }
    error();
    return false;
}

#if IS_READERBOARD
bool CommandStateMachine::accept_encoded_rgb(int inputchar)
{
    if (!accept_encoded_int6(inputchar))
        return false;
    if (bytebuf > 16)
        return false;
    return true;
}

bool CommandStateMachine::accept_encoded_transition(int inputchar)
{
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
        return false;
    }
    return true;
}
#endif

void CommandStateMachine::append_bytebuf(void)
{
    append_byte(bytebuf);
    bytebuf = 0;
}

void CommandStateMachine::append_byte(byte n)
{
    if (buffer_idx < CSM_BUFSIZE) {
        buffer[buffer_idx++] = n;
    }
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
#if IS_READERBOARD
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
#endif

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
#if IS_READERBOARD
    transition = NoTransition;
    merge = false;
    nybble = false;
    repeat = false;
#endif
    buffer_idx = 0;
    LEDset = 0;
    bytebuf = 0;
    esc_literal = false;
    esc_msb = false;
    cmd_source = FROM_USB;
    for (int i=0; i<CSM_BUFSIZE; i++)
        buffer[i] = 0;
}

//
// (CSM) accept(inputchar)
//   Accepts an input character from the host. This is fed into the state machine
//   as it parses the incoming commands, eventually executing them when a complete
//   command has been received.
//
void CommandStateMachine::accept(serial_source_t source, int inputchar)
{
    if (inputchar < 0) {
        return;
    }

    // If we are already receiving from RS-485 and receive a byte with its MSB set,
    // that means we are abandoning the current command and starting a new one.
    if (cmd_source == FROM_485 && source == FROM_485 && (inputchar & 0x80) && state != EndState) {
        error();
        reset();
    }

    // Same if we were in the middle of an RS-485 command and something showed up on USB.
    if (cmd_source == FROM_485 && source == FROM_USB && state != EndState) {
        error();
        reset();
    }

    cmd_source = source;
    if (cmd_source == FROM_485) {
        if (inputchar & 0x80) {
            // start of command. is it ours?
            if ((inputchar & 0x0f) != my_device_address && (inputchar & 0x0f) != global_device_address) {
                // no, just ignore everything else until start of next command.
                end_cmd();
                return;
            }
            target_is_global = ((inputchar & 0x0f) == global_device_address);


            switch (inputchar & 0xf0) {
                case 0x80:
#if IS_READERBOARD
                    clear_all_buffers();
#endif
                    discrete_all_off(true);
                    end_cmd();
                    return;

                case 0xd0:
                case 0xf0:
                    // someone else's response; ignore
                    end_cmd();
                    return;

                case 0x90:
                    // start of command
                    state = StartState;
                    break;

                case 0xb0:
                    // start of target address block
                    state = CollectNState;
                    return;

                default:
                    error();
                    return;
            }
        } else {
            // subsequent bytes
            if (esc_literal) {
                esc_literal = false;
            } else if (esc_msb) {
                esc_msb = false;
                inputchar |= 0x80;
            } else if (inputchar == 0x7e) {
                esc_msb = true;
                return;
            } else if (inputchar == 0x7f) {
                esc_literal = true;
                return;
            }
        }
    } else {
        /* USB: if ^D received, abandon current command */
        target_is_global = false;
        if (inputchar == '\004') {
            if (state != IdleState && state != EndState && state != ErrorState) {
                error();
            }
        }
    }

    switch (state) {
    // stuck in error state until end of command
	case EndState:
    case ErrorState:
        if (source == FROM_USB && inputchar == '\x04') {
            reset();
        }
        break;

    // parsing RS-485 headers
    case CollectNState:
        if (accept_encoded_int6(inputchar) && bytebuf > 0) {
            append_bytebuf();
            state = CollectAddressState;
            break;
        }
        error();
        break;

    case CollectAddressState:
        if (accept_encoded_int6(inputchar)) {
            append_bytebuf();
            if (buffer_idx > buffer[0]) {
                state = StartState;
            }
            return;
        }
        error();
        break;

    case IdleState:
        if (source == FROM_USB) {
            if (inputchar == '\x04') {
                reset();
                break;
            }
            state = StartState;
            /* fallthrough */
        } else {
            break;
        }

    // start of command
    case StartState:
        switch (inputchar) {
        case '*':
            state = StrobeState;
            strober.stop();
            break;

#if IS_READERBOARD
        case 'C':
            clear_all_buffers();
            column = 0;
            end_cmd();
            break;
#endif

        case '=':
            if (source != FROM_USB) {
                error();
                break;
            }
            state = SetState;
            break;

#if IS_READERBOARD
        case '<':
          state = ScrollState;
          break;

        case '@':
            state = SetColState;
            break;

        case 'A':
            state = SelectFontState;
            break;

        case 'H':
            state = BarGraphState;
            break;

        case 'I':
            state = ImageStateMerge;
            break;
#endif

        case 'Q':
            if (!target_is_global) {
                void (*sendbyte)(byte);

                if (cmd_source == FROM_485) {
                    start_485_reply();
                    sendbyte = send_485_byte;
                } else {
                    start_usb_reply();
                    sendbyte = send_usb_byte;
                }

                sendbyte('Q');
#if HW_MODEL == MODEL_3xx_MONOCHROME
                sendbyte('M');
#elif HW_MODEL == MODEL_3xx_RGB
                sendbyte('C');
#elif HW_MODEL == MODEL_BUSYLIGHT_1 || HW_MODEL == MODEL_BUSYLIGHT_2
                sendbyte('B');
#else
# error "hardware model not supported"
#endif
                sendbyte('=');
                if (my_device_address == EE_ADDRESS_DISABLED)
                    sendbyte('.');
                else
                    sendbyte(encode_int6(my_device_address));
                sendbyte(USB_baud_rate_code);
                sendbyte(RS485_baud_rate_code);
                sendbyte(encode_int6(global_device_address));
#if HAS_I2C_EEPROM
                sendbyte('X');
#elif HW_MC == HW_MC_DUE
                sendbyte('_');
#else
                sendbyte('I');
#endif
                sendbyte('$');
                for (const char *c = SERIAL_VERSION_STAMP; *c != '\0'; c++) {
                    sendbyte(*c);
                }
                sendbyte('S');
                for (const char *c = serial_number; *c != '\0'; c++) {
                    sendbyte(*c);
                }
                sendbyte('$');
                report_state(false);
#if IS_READERBOARD
                sendbyte('M');
                for (int plane=0; plane<N_COLORS; plane++) {
                    byte planebit = 1 << plane;
                    for (int col=0; col<N_COLS; col++) {
                        sendbyte(encode_hex_nybble(
                                      ((image_buffer[4][col] & planebit)? 0x01:0) |
                                      ((image_buffer[5][col] & planebit)? 0x02:0) |
                                      ((image_buffer[6][col] & planebit)? 0x04:0) |
                                      ((image_buffer[7][col] & planebit)? 0x08:0)));
                        sendbyte(encode_hex_nybble(
                                      ((image_buffer[0][col] & planebit)? 0x01:0) |
                                      ((image_buffer[1][col] & planebit)? 0x02:0) |
                                      ((image_buffer[2][col] & planebit)? 0x04:0) |
                                      ((image_buffer[3][col] & planebit)? 0x08:0)));
                    }
                    sendbyte('$');
                }
#endif
                sendbyte('\n');
                if (cmd_source == FROM_485) {
                    end_485_reply();
                } else {
                    end_usb_reply();
                }
            }
            end_cmd();
            break;

        case 'F':
        case 'f':
            state = FlashState;
            flasher.stop();
            break;

#if IS_READERBOARD
        case 'L':
            flasher.stop();
            discrete_all_off(false);
            state = LightSetState;
            break;
#endif

        case 'S':
        case 's':
            flasher.stop();
            discrete_all_off(false);
            state = LightOnState;
            break;

#if IS_READERBOARD
        case 'T':
            state = TextMergeState;
            break;
#endif

        case 'X':
        case 'x':
            discrete_all_off(true);
            end_cmd();
            break;

        case '%':
            test_pattern();
            end_cmd();
            break;

#if IS_READERBOARD
        case 'K':
            state = SetColorState;
            break;
#endif

        case '?':
            if (!target_is_global) {
                report_state(true);
            }
            end_cmd();
            break;

        default:
            error();
        }
        break;
//
//    // Collecting LED numbers to form a flash or strobe sequence
    case FlashState:
    case StrobeState:
        if (inputchar == '$' || inputchar == '\x1b') {
            discrete_all_off(false);
            if (state == FlashState) {
                flasher.start();
            }
            else {
                strober.start();
            }
            end_cmd();
            break;
        }

        if (state == FlashState) {
            flasher.append(parse_led_name(inputchar));
        }
        else {
            strober.append(parse_led_name(inputchar));
        }
        break;

    // Set operational parameters
    case SetState:
        if (inputchar == '#') {
            state = SetSNState;
            break;
        }
#if IS_READERBOARD
        if (inputchar == '*') {
            state = ShowBannerState;
            break;
        }
#endif
        state = SetUspdState;
        if (inputchar == '.') {
            append_byte(EE_ADDRESS_DISABLED);
        } else if (accept_encoded_int6(inputchar)) {
            append_bytebuf();
            break;
        } else {
            error();
        }
        break;

#if IS_READERBOARD
    case ShowBannerState:
        if (inputchar == '=') {
            show_banner();
            end_cmd();
        } else {
            error();
        }
        break;
#endif

    case SetSNState:
        if (inputchar == '\033' || inputchar == '$') {
            state = SetSN2State;
            break;
        }
        if (buffer_idx >= 6) {
            error();
            break;
        }
        append_byte(inputchar);
        break;

    case SetSN2State:
        if (inputchar != '#') {
            error();
            break;
        }
        state = SetSN3State;
        break;

    case SetSN3State:
        if (inputchar != '=') {
            error();
            break;
        }
        append_byte(0);
        store_serial_number((const char *) buffer);
        end_cmd();
        break;

    case SetUspdState:
        append_byte(inputchar);
        state = SetRspdState;
        break;

    case SetRspdState:
        append_byte(inputchar);
        state = SetDgState;
        break;

    case SetDgState:
        // 0=addr
        // 1=usb baud code
        // 2=485 baud code
        int b1, b2;
        if ((b1 = parse_baud_rate_code(buffer[1])) == 0
        ||  (b2 = parse_baud_rate_code(buffer[2])) == 0) {
            error();
            break;
        }
        if (accept_encoded_int6(inputchar)) {
            global_device_address = bytebuf;
            my_device_address = buffer[0];
            USB_baud_rate_code = buffer[1];
            USB_baud_rate = b1;
            RS485_baud_rate_code = buffer[2];
            RS485_baud_rate = b2;
            save_eeprom();
            start_usb_serial();
            setup_485_serial();
            end_cmd();
        } else {
            error();
        }
        break;

#if IS_READERBOARD
    case SetColState:
        if (accept_encoded_pos(inputchar)) {
            column = bytebuf;
            end_cmd();
        } else {
            error();
        }
        break;

    case SelectFontState:
        if (accept_encoded_int6(inputchar)) {
            font = min(bytebuf, N_FONTS - 1);
            end_cmd();
        } else {
            error();
        }
        break;

    case LightSetState:
        if (inputchar == '$' || inputchar == '\x1b') {
            end_cmd();
            break;
        }
        discrete_set(parse_led_name(inputchar), 1);
        break;
#endif

   case LightOnState:
        discrete_set(parse_led_name(inputchar), 1);
        end_cmd();
        break;

#if IS_READERBOARD
    case BarGraphState:
        if (inputchar == 'K') {
            state = BarColorState;
            break;
        }
        if (inputchar >= '0' && inputchar <= '9') {
            commit_graph_datapoint(inputchar - '0');
            end_cmd();
        }
        else {
            error();
        }
        break;

    case BarColorState:
        if (!accept_encoded_rgb(inputchar)) {
            error();
            break;
        }
        append_bytebuf();
        if (buffer_idx >= 8) {
            commit_graph_datacolors();
            end_cmd();
        }
        break;

    case ImageStateMerge:
        state = ImageStateCol;
        if (inputchar == '.') 
            merge = false;
        else if (inputchar == 'M')
            merge = true;
        else 
            error();
        break;

    case ImageStateCol:
        if (accept_encoded_pos(inputchar)) {
            column = bytebuf;
            state = ImageStateTransition;
        } else {
            error();
        }
        break;

    case ImageStateTransition:
        if (accept_encoded_transition(inputchar)) {
            state = ImageStateData;
            k = 0;
        } else {
            error();
        }
        break;

    case ImageStateData:
        if (inputchar == '$' || inputchar == '\x1b') {
            if (nybble) {
                error();
                break;
            }

            if (k >= N_COLORS) {
                error();
                break;
            }

            for (int col = column, i=0; col < N_COLS && i < buffer_idx; col++, i++) {
                for (int row = 0; row < N_ROWS; row++) {
                    if (k == 0 && !merge) {
                        image_buffer[row][col] = 0;
                    }
                    if (buffer[i] & (1 << row)) {
                        image_buffer[row][col] |= 1 << k;
                    }
                }
            }
            buffer_idx = 0;

            if (++k >= N_COLORS) {
                display_buffer(image_buffer, transition);
                end_cmd();
            }
            break;
        }
        if (accept_hex_nybble(inputchar)) {
            append_bytebuf();
            break;
        }
        break;

    case TextMergeState:
        state = TextAlignState;
        if (inputchar == '.') {
            merge = false;
        } else if (inputchar == 'M') {
            merge = true;
        } else {
            error();
        }
        break;

    case TextAlignState:
        //TODO parse alignment specifier
        state = TextTransitionState;
        break;

    case TextTransitionState:
        if (accept_encoded_transition(inputchar)) {
            state = TextDataState;
            transitions.set_stage();
        } else {
            error();
        }
        break;

    case TextDataState:
        if (inputchar == '\0') {
            // ignore nulls in input
            break;
        }
        if (inputchar == '\x1b') {
            append_byte(0);
            column = render_text(image_buffer, column, font, (const char *) buffer, color, merge);
            //commit_image_buffer(image_buffer);
            display_buffer(image_buffer, transition);
            end_cmd();
            break;
        }
        append_byte(inputchar);
        break;

    case SetColorState:
        if (!accept_encoded_rgb(inputchar)) {
            error();
            break;
        }
        color = bytebuf;
        end_cmd();
        break;

    case ScrollState:
        state = ScrollTextState;
        if (inputchar == '.')
            repeat = false;
        else if (inputchar == 'L')
            repeat = true;
        else 
            error();
        break;

    case ScrollTextState:
        if (inputchar == '\x1b') {
            append_byte(0);
            strncpy((char *)scrolling_buffer, (const char *) buffer, CSM_BUFSIZE);
            scrolling_buffer[CSM_BUFSIZE-1] = '\0';
            transitions.start_scrolling_text((const char *) scrolling_buffer, strlen((const char *)scrolling_buffer), repeat, font, color);
            end_cmd();
            break;
        }
        if (inputchar != '\0')
            append_byte(inputchar);
        break;
#endif

    default:
        error();
    }
}

//
// (CSM) report_state()
//   Reports the discrete LED state to the host.
//   This is the action that results from the '?' command.
//
void CommandStateMachine::report_state(bool terminate)
{
    void (*send_byte)(byte);
    void (*send_end)(void);

    Stream *port;

    if (cmd_source == FROM_485) {
        start_485_reply();
        send_byte = send_485_byte;
        send_end = end_485_reply;
    } else {
        start_usb_reply();
        send_byte = send_usb_byte;
        send_end = end_usb_reply;
    }

    int i = 0;
    (*send_byte)('L');
    for (i = 0; i < LENGTH_OF(discrete_led_set); i++) {
        (*send_byte)(discrete_query(i) ? encode_led(i) : '_');
    }
    (*send_byte)('$');
    (*send_byte)('F');
    flasher.report_state(send_byte);
    (*send_byte)('$');
    (*send_byte)('S');
    strober.report_state(send_byte);
    (*send_byte)('$');
    if (terminate) 
        (*send_byte)('\n');
    (*send_end)();
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
#if IS_READERBOARD
    set_lights(0b10000000);
#else
    set_lights(0b01000000);
#endif
    strober.stop();
    strober.append(3);
    strober.append(4);
    strober.start();
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
    for (int i = 0; i < LENGTH_OF(discrete_led_set); i++) {
        if (bits & (1 << i)) {
            discrete_set(i, true);
        }
    }
}

//
// (CSM) commit_graph_datapoint(value)
//    Plots the data value in the range [0,8] by lighting up
//    that number of lights from the bottom row, displaying them
//    in the far right column, shifting the display one column left.
//
#if IS_READERBOARD
void CommandStateMachine::commit_graph_datapoint(int value)
{
    shift_left(image_buffer);
    for (int i=0; i<value && i<N_ROWS; i++) {
        image_buffer[7-i][N_COLS-1] = color;
    }
    display_buffer(image_buffer);
}

void CommandStateMachine::commit_graph_datacolors()
{
    shift_left(image_buffer);
    for (int i=0; i<buffer_idx && i<N_ROWS; i++) {
        image_buffer[i][N_COLS-1] = buffer[i];
    }
    display_buffer(image_buffer);
}
#endif

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
void receive_serial_data(serial_source_t source)
{
    switch (source) {
        case FROM_USB:
            if (Serial.available() > 0) {
                cmd.accept(source, Serial.read());
            }
            break;

#if HW_MODEL != MODEL_BUSYLIGHT_1
        case FROM_485:
            if (SERIAL_485.available() > 0) {
                cmd.accept(source, SERIAL_485.read());
            }
            break;
#endif
    }
}
