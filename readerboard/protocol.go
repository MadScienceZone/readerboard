// Protocol support for busylight/readerboard device communications.
// The protocol and command set are more thoroughly documented in the accompanying
// doc/readerboard.pdf document.
package readerboard

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"strconv"
	"strings"
)

func EncodeInt6(n int) byte {
	if n < 0 || n > 63 {
		return '.'
	}
	return byte(n) + '0'
}

// parseBaudRateCode reads a one-byte baud rate code and returns the baud rate it
// represents.
func parseBaudRateCode(code byte) (int, error) {
	switch code {
	case '0':
		return 300, nil
	case '1':
		return 600, nil
	case '2':
		return 1200, nil
	case '3':
		return 2400, nil
	case '4':
		return 4800, nil
	case '5':
		return 9600, nil
	case '6':
		return 14400, nil
	case '7':
		return 19200, nil
	case '8':
		return 28800, nil
	case '9':
		return 31250, nil
	case 'A':
		return 38400, nil
	case 'B':
		return 57600, nil
	case 'C':
		return 115200, nil
	default:
		return 0, fmt.Errorf("invalid baud rate code")
	}
}

func EncodeBaudRateCode(speed int) (byte, error) {
	switch speed {
	case 300:
		return '0', nil
	case 600:
		return '1', nil
	case 1200:
		return '2', nil
	case 2400:
		return '3', nil
	case 4800:
		return '4', nil
	case 9600:
		return '5', nil
	case 14400:
		return '6', nil
	case 19200:
		return '7', nil
	case 28800:
		return '8', nil
	case 31250:
		return '9', nil
	case 38400:
		return 'A', nil
	case 57600:
		return 'B', nil
	case 115200:
		return 'C', nil
	default:
		return 0, fmt.Errorf("invalid baud rate")
	}
}

// Escape485 translates arbitrary 8-bit byte sequences to be sent over RS-485 so
// they conform with the 7-bit data constraint imposed by the protocol.
// Since the protocol uses the MSB to indicate the start of a new
// command, that byte can't be escaped using this function since it MUST have its MSB set.
func Escape485(in []byte) []byte {
	out := make([]byte, 0, len(in))
	for _, b := range in {
		if b == 0x7e || b == 0x7f {
			// byte is one of our escape codes; escape it.
			out = append(out, 0x7f)
			out = append(out, b)
		} else if (b & 0x80) != 0 {
			// MSB set: send 7E then the byte without the MSB
			out = append(out, 0x7e)
			out = append(out, b&0x7f)
		} else {
			out = append(out, b)
		}
	}
	return out
}

// Unescape485 is the inverse of Escape485; it resolves the escape bytes in the byte sequence
// passed to it, returning the original full-8-bit data stream as it was before escaping.
func Unescape485(in []byte) []byte {
	out := make([]byte, 0, len(in))
	literalNext := false
	setNextMSB := false

	for _, b := range in {
		if literalNext {
			out = append(out, b)
			literalNext = false
			continue
		}
		if setNextMSB {
			out = append(out, b|0x80)
			setNextMSB = false
			continue
		}

		switch b {
		case 0x7f:
			literalNext = true
			continue

		case 0x7e:
			setNextMSB = true
			continue

		default:
			out = append(out, b)
		}
	}
	return out
}

// reqInit initializes, and reads the target list from, the client's posted data.
// It returns a slice of integer device target numbers or an error if that couldn't happen.
func reqInit(r *http.Request, globalAddress int) ([]int, error) {
	var targets []int

	if err := r.ParseForm(); err != nil {
		return nil, err
	}

	if !r.Form.Has("a") {
		return nil, fmt.Errorf("request missing device target address list")
	}

	for i, target := range strings.Split(r.Form.Get("a"), ",") {
		t, err := strconv.Atoi(target)
		if err != nil {
			return nil, fmt.Errorf("request device target #%d invalid: %v", i, err)
		}
		if t < 0 || t > 63 {
			return nil, fmt.Errorf("request device target #%d value %d out of range [0,63]", i, t)
		}

		// If the global addr is anywhere in the list, return a list with ONLY that address
		if t == globalAddress {
			return []int{globalAddress}, nil
		}
		targets = append(targets, t)
	}

	if len(targets) == 0 {
		return nil, fmt.Errorf("request device target address list is empty")
	}

	return targets, nil
}

// ledList extracts the LED list from the parameter "l" in the HTTP request.
// This is expected to be a string of individual 7-bit ASCII characters whose meanings
// are device dependent.
//
// Returns the list of LED codes followed by a terminating '$' character. The list may
// be the empty string.
//
// In order for this to work, the form data must already be in the http.Request field Form
// which can be arranged by calling reqInit first.
func ledList(r url.Values) ([]byte, error) {
	leds := r.Get("l")
	llist := []byte(leds)

	for i, ch := range leds {
		if ch < 32 || ch > 127 {
			return nil, fmt.Errorf("LED #%d ID %d out of range [32,127]", i, ch)
		}
		if ch == '$' {
			return nil, fmt.Errorf("LED #%d ID not allowed to be '$'", i)
		}
	}

	return append(llist, '$'), nil
}

type JSONErrorResponse struct {
	Errors  int
	Message string
}

func reportErrorJSON(w http.ResponseWriter, errors int, message string) {
	resp, _ := json.Marshal(JSONErrorResponse{
		Errors:  errors,
		Message: message,
	})
	io.WriteString(w, string(resp)+"\n")
}

func WrapReplyHandler(f func() (func(url.Values, HardwareModel, deviceTargetSet, *ConfigData) ([]byte, error), func(HardwareModel, []byte) (any, error)), config *ConfigData) func(http.ResponseWriter, *http.Request) {
	return func(w http.ResponseWriter, r *http.Request) {
		sender, parser := f()
		var received []byte
		var err error
		var dt HardwareModel
		replylist := make(map[int][]byte)

		targetNetworks, errors, _ := getTargetList(r, config)
		if errors > 0 {
			reportErrorJSON(w, errors, "Unable to get target list for command")
			return
		}

		for netID, devs := range targetNetworks {
			for _, dev := range devs {
				reply, err := func() ([]byte, error) {
					dt = netID.DeviceType

					// force command execution to target ONE device only.
					tNet := make(deviceTargetSet)
					tNet[netID] = []int{dev}

					lockNetwork(netID.NetworkID)
					defer unlockNetwork(netID.NetworkID)
					newErrors := sendCommandToHardware(sender, tNet, r, config, false)
					if newErrors > 0 {
						errors += newErrors
						return nil, fmt.Errorf("failed to send command to target hardware device %d.", dev)
					}
					received, err = config.Networks[netID.NetworkID].driver.Receive()
					if err != nil {
						errors++
						return nil, err
					}
					return received, nil
				}()
				if err != nil {
					reportErrorJSON(w, errors+1, fmt.Sprintf("failed to query target hardware device %d (%v)", dev, err))
					return
				}

				if responseData, err := parser(dt, reply); err == nil {
					resp, err := json.Marshal(responseData)
					if err != nil {
						reportErrorJSON(w, errors+1, fmt.Sprintf("Unable to marshal device %d response (%v)", dev, err))
						return
					}
					replylist[dev] = resp
				} else {
					reportErrorJSON(w, errors+1, fmt.Sprintf("Error querying device %d (%v)", dev, err))
				}
			}
		}
		io.WriteString(w, "{")
		first := true
		for devID, devResponse := range replylist {
			if first {
				first = false
			} else {
				io.WriteString(w, ",")
			}
			io.WriteString(w, fmt.Sprintf("\"%d\":%s", devID, string(devResponse)))
		}
		io.WriteString(w, "}\n")
	}
}

type deviceTargetSet map[netTargetKey][]int

type netTargetKey struct {
	NetworkID  string
	DeviceType HardwareModel
}

func WrapHandler(f func(url.Values, HardwareModel, deviceTargetSet, *ConfigData) ([]byte, error), config *ConfigData, globalAllowed bool) func(http.ResponseWriter, *http.Request) {
	return func(w http.ResponseWriter, r *http.Request) {
		targetNetworks, errors, isGlobal := getTargetList(r, config)
		if isGlobal && !globalAllowed {
			io.WriteString(w, "command may not be targetted to the global address.\n")
			return
		}
		if errors += sendCommandToHardware(f, targetNetworks, r, config, true); errors > 0 {
			io.WriteString(w, fmt.Sprintf("%d error%s occurred while trying to carry out this operation.\n",
				errors, func(n int) string {
					if n == 1 {
						return ""
					} else {
						return "s"
					}
				}(errors)))
		}
	}
}

func collapseTargetList(targets deviceTargetSet) []int {
	var t []int

	for _, targetIDs := range targets {
		for _, targetID := range targetIDs {
			t = append(t, targetID)
		}
	}

	return t
}

func getTargetList(r *http.Request, config *ConfigData) (deviceTargetSet, int, bool) {
	var errors int
	isGlobal := false

	targets, err := reqInit(r, config.GlobalAddress)
	if err != nil {
		log.Printf("invalid request: %v", err)
		return nil, 1, isGlobal
	}

	// organize our target list by the networks they're attached to, grouped together by device model
	// so that we can optimize by sending a single multi-target command where that sort of thing is
	// possible but send separate commands to targets when we need to.
	targetNetworks := make(deviceTargetSet)
	if targets[0] == config.GlobalAddress {
		// add everything
		isGlobal = true
		for target, dev := range config.Devices {
			targetNetworks[netTargetKey{dev.NetworkID, dev.DeviceType}] = append(targetNetworks[netTargetKey{dev.NetworkID, dev.DeviceType}], target)
		}
	} else {
		for _, target := range targets {
			dev, isInConfig := config.Devices[target]
			if !isInConfig {
				errors++
				log.Printf("command targets device with ID %d, but that device does not exist in the server's configuration (ignored)", target)
				continue
			}
			targetNetworks[netTargetKey{dev.NetworkID, dev.DeviceType}] = append(targetNetworks[netTargetKey{dev.NetworkID, dev.DeviceType}], target)
		}
	}

	return targetNetworks, errors, isGlobal
}

func sendCommandToHardware(f func(url.Values, HardwareModel, deviceTargetSet, *ConfigData) ([]byte, error), targetNetworks deviceTargetSet, r *http.Request, config *ConfigData, doLocks bool) int {
	var rawBytes []byte
	errors := 0

	// Try sending the commands to the devices
	for targetNetwork, targetList := range targetNetworks {
		commands, err := f(r.Form, targetNetwork.DeviceType, targetNetworks, config)
		if err != nil {
			errors++
			log.Printf("error preparing request for %s: %v", targetNetwork.NetworkID, err)
			continue
		}
		if len(commands) < 1 {
			log.Printf("internal error preparing bytestream for %s: nil output", targetNetwork.NetworkID)
			errors++
			continue
		}

		if commands[0] == 0xff {
			// special case for "all lights off" command
			rawBytes, err = config.Networks[targetNetwork.NetworkID].driver.AllLightsOffBytes(targetList, commands[1:])
			if err != nil {
				if b, err := Off(nil, targetNetwork.DeviceType, targetNetworks, config); err != nil {
					if rawBytes, err = config.Networks[targetNetwork.NetworkID].driver.Bytes(targetList, b); err != nil {
						if b, err = Clear(nil, targetNetwork.DeviceType, targetNetworks, config); err != nil {
							if rb, err := config.Networks[targetNetwork.NetworkID].driver.Bytes(targetList, b); err != nil {
								rawBytes = append(rawBytes, rb...)
							} else {
								errors++
								log.Printf("error preparing bytestream for %s (plan B CLEAR can't be set up): %v", targetNetwork.NetworkID, err)
								continue
							}
						}
					} else {
						errors++
						log.Printf("error preparing bytestream for %s (plan B OFF can't be set up): %v", targetNetwork.NetworkID, err)
						continue
					}
				} else {
					errors++
					log.Printf("error preparing bytestream for %s (plans A and B both failed): %v", targetNetwork.NetworkID, err)
					continue
				}
			}
		} else {
			rawBytes, err = config.Networks[targetNetwork.NetworkID].driver.Bytes(targetList, commands)
			if err != nil {
				errors++
				log.Printf("error preparing bytestream for %s: %v", targetNetwork.NetworkID, err)
				continue
			}
		}

		if err = func() error {
			if doLocks {
				lockNetwork(targetNetwork.NetworkID)
				defer unlockNetwork(targetNetwork.NetworkID)
			}
			return config.Networks[targetNetwork.NetworkID].driver.SendBytes(rawBytes)
		}(); err != nil {
			errors++
			log.Printf("error transmitting bytestream to %s: %v", targetNetwork.NetworkID, err)
			continue
		}
	}
	return errors
}

func boolParam(r url.Values, key string, ifFalse, ifTrue byte) byte {
	if !r.Has(key) {
		return ifFalse
	}

	v := r.Get(key)
	if v == "" || v == "true" || v == "yes" || v == "on" {
		return ifTrue
	}
	return ifFalse
}

func intParam(r url.Values, key string) (int, error) {
	v := r.Get(key)
	if v == "" {
		return 0, nil
	}
	return strconv.Atoi(v)
}

func floatParam(r url.Values, key string) (float64, error) {
	v := r.Get(key)
	if v == "" {
		return 0.0, nil
	}
	return strconv.ParseFloat(v, 64)
}

func intParamOrUnderscore(r url.Values, key string) (int, bool, error) {
	v := r.Get(key)
	if v == "_" {
		return 0, false, nil
	}
	if v == "" {
		return 0, true, nil
	}
	n, err := strconv.Atoi(v)
	if err != nil {
		return n, false, err
	}
	return n, true, nil
}

func stringParam(r url.Values, key string) ([]byte, error) {
	t := r.Get(key)
	text := make([]byte, 0, len(t)+1)
	// This can't just be UTF-8 encoded; it is just a series of 8-bit character values
	// and we disallow codepoints > 255.
	for _, ch := range t {
		if ch == 0o33 || ch == 4 {
			return nil, fmt.Errorf("%s parameter contains illegal character(s)", key)
		}
		if ch <= 255 {
			text = append(text, byte(ch&0xff))
		}
	}
	return append(text, 0o33), nil
}

func textParam(r url.Values) ([]byte, error) {
	return stringParam(r, "t")
}

func posParam(r url.Values) (byte, error) {
	pos := r.Get("pos")
	if len(pos) != 1 {
		return 0, fmt.Errorf("position must be a single character")
	}
	if (pos[0] < '0' || pos[0] > 'o') && pos[0] != '~' {
		return 0, fmt.Errorf("position %q out of range ['0','o'] or '~'", pos)
	}
	return pos[0], nil
}

// AllLightsOff turns all lights off on the specified device(s). This extinguishes the status LEDs
// and the matrix.
//
//	/readerboard/v1/alloff?a=<targets>
//
// This is a bit of a hack: the initial 0xff byte signals that this is an AllLightsOff
// signal which can be very different on RS-485 networks; for direct connections this
// can be sent as multiple commands. So we assume RS-485 drivers completely ignore our
// output and USB ones ignore the first byte and allow embedded ^D terminators.
func AllLightsOff(_ url.Values, hw HardwareModel, targets deviceTargetSet, config *ConfigData) ([]byte, error) {
	// note that we are turning these off
	if config != nil && targets != nil {
		for _, devIDs := range targets {
			for _, devID := range devIDs {
				func() {
					lockDevice(devID)
					defer unlockDevice(devID)
					config.Devices[devID].LastKnownState.clear(config.Devices[devID].LightsInstalled)
				}()
			}
		}
	}

	if IsBusylightModel(hw) {
		return []byte{0xff, 'X'}, nil
	}
	return []byte{0xff, 'C', 0x04, 'X'}, nil
}

// Clear turns off all the LEDs in the display matrix.
//
//	/readerboard/v1/clear?a=<targets>
//	-> C
func Clear(_ url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	if !IsReaderboardModel(hw) {
		return nil, fmt.Errorf("clear command not supported for hardware type %v", hw)
	}
	return []byte{'C'}, nil
}

// Dim sets one or all dimmer values.
//
//  /readerboard/v1/dim?a=<targets>&l=<led>|*|_&d=<level>
//  -> D led h h
//
func Dim(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	l, err := ledList(r)
	if err != nil {
		return nil, err
	}
	if len(l) != 2 {
		return nil, fmt.Errorf("l parameter must contain exactly ONE LED name, *, or _")
	}
	value, err := intParam(r, "d")
	if err != nil {
		return nil, err
	}

	return []byte{'D', l[0], encode_hex_nybble(byte(value >> 4)), encode_hex_nybble(byte(value))}, nil
}

func encode_hex_nybble(v byte) byte {
	v &= 0x0f
	if v > 10 {
		return 'A' + v - 10
	}
	return '0' + v
}

// Test runs a test pattern on the target device.
//
//	/readerboard/v1/test?a=<targets>
func Test(_ url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	if IsReaderboardModel(hw) || BusylightModelVersion(hw) > 1 {
		return []byte{'%'}, nil
	}
	return nil, fmt.Errorf("test command not supported for hardware type %v", hw)
}

// Flash sets a flash pattern on the busylight status LEDs.
//
//	/readerboard/v1/flash?a=<targets>&l=<leds>[&up=<sec>&on=<sec>&down=<sec>&off=<sec>]
//	-> F l0 l1 ... lN $
//  -> F / up on down off l0 l1 ... lN $
//
func Flash(r url.Values, _ HardwareModel, targets deviceTargetSet, config *ConfigData) ([]byte, error) {
	l, err := ledList(r)
	if err != nil {
		return nil, err
	}
	up, err := floatParam(r, "up")
	if err != nil {
		return nil, err
	}
	on, err := floatParam(r, "on")
	if err != nil {
		return nil, err
	}
	down, err := floatParam(r, "down")
	if err != nil {
		return nil, err
	}
	off, err := floatParam(r, "off")
	if err != nil {
		return nil, err
	}

	// note that we are turning these on
	if config != nil && targets != nil {
		for _, devIDs := range targets {
			for _, devID := range devIDs {
				func() {
					lockDevice(devID)
					defer unlockDevice(devID)
					if len(l) == 0 || l[0] == '$' {
						config.Devices[devID].LastKnownState.FlasherStatus.clear()
					} else {
						config.Devices[devID].LastKnownState.setLights(l[0:1], config.Devices[devID].LightsInstalled)
						config.Devices[devID].LastKnownState.FlasherStatus.set(l[:len(l)-1], up, on, down, off)
					}
				}()
			}
		}
	}

	if up == 0.0 && on == 0.0 && down == 0.0 && off == 0.0 {
		return append([]byte{'F'}, l...), nil
	} else {
		return append([]byte{'F', '/', EncodeInt6(int(up * 10)), EncodeInt6(int(on * 10)), EncodeInt6(int(down * 10)), EncodeInt6(int(off * 10))}, l...), nil
	}
}

// Sound plays a sound on the speaker.
//
//  /readerboard/v1/sound?a=<targets>[&loop]&notes=...
//  -> B L|. notes... $
func Sound(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	loop := boolParam(r, "loop", '.', 'L')
	notes, err := stringParam(r, "notes")
	if err != nil {
		return nil, err
	}
	return append([]byte{'B', loop}, notes...), nil
}

// Font selection to indexed font table.
//
//	/readerboard/v1/font?a=<targets>&idx=<digit>
//	-> A digit
func Font(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	if !IsReaderboardModel(hw) {
		return nil, fmt.Errorf("font command not supported for hardware type %v", hw)
	}
	idx := r.Get("idx")
	if len(idx) != 1 || idx[0] < '0' || idx[0] > '9' {
		return nil, fmt.Errorf("font index %q out of range [0,9]", idx)
	}
	return []byte{'A', idx[0]}, nil
}

func colorParam8(r url.Values, key string) ([]byte, error) {
	rgbString := r.Get(key)
	if strings.ContainsRune(rgbString, ',') {
		// comma-separated list of color names
		rgbList := strings.Split(rgbString, ",")
		if len(rgbList) != 8 {
			return nil, fmt.Errorf("colors parameter requires eight color values")
		}

		var colors []byte
		for _, code := range rgbList {
			colors = append(colors, parseColorCode(code))
		}
		return colors, nil
	}

	if len(rgbString) != 8 {
		return nil, fmt.Errorf("colors parameter requires eight color values")
	}
	return []byte(rgbString), nil
}

func colorParam(r url.Values, key string) byte {
	return parseColorCode(r.Get(key))
}

func parseColorCode(code string) byte {
	switch code {
	case "0", "off", "black", "bk", "k":
		return '0'
	case "1", "red", "r":
		return '1'
	case "2", "green", "g":
		return '2'
	case "3", "amber", "yellow", "a", "y":
		return '3'
	case "4", "blue", "bl", "b":
		return '4'
	case "5", "magenta", "m":
		return '5'
	case "6", "cyan", "c":
		return '6'
	case "7", "white", "w":
		return '7'
	case "8", "flashing-off", "flashing-black", "fbk", "fk":
		return '8'
	case "9", "flashing-red", "fr":
		return '9'
	case "10", ":", "flashing-green", "fg":
		return ':'
	case "11", ";", "flashing-amber", "flashing-yellow", "fa", "fy":
		return ';'
	case "12", "<", "flashing-blue", "fb", "fbl":
		return '<'
	case "13", "=", "flashing-magenta", "fm":
		return '='
	case "14", ">", "flashing-cyan", "fc":
		return '>'
	case "15", "?", "flashing-white", "fw":
		return '?'
	default:
		return '1'
	}
}

// Graph plots a histogram graph data point on the display.
//
//	/readerboard/v1/graph?a=<targets>&v=<n>[&colors=<rgb>...]
//	-> H n
//	-> H K rgb0 ... rgb7
func Graph(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	if !IsReaderboardModel(hw) {
		return nil, fmt.Errorf("graph command not supported for hardware type %v", hw)
	}
	if r.Has("colors") {
		rgb, err := colorParam8(r, "colors")
		if err != nil {
			return nil, err
		}
		return append([]byte{'H', 'K'}, rgb...), nil
	}

	value, err := intParam(r, "v")
	if err != nil {
		return nil, err
	}
	if value < 0 {
		value = 0
	} else if value > 8 {
		value = 8
	}
	return []byte{'H', byte(value + '0')}, nil
}

func transitionParam(r url.Values) byte {
	switch r.Get("trans") {
	case "", "none", "_", ".":
		return '.'
	case ">", "scroll-right", "sr":
		return '>'
	case "<", "scroll-left", "sl":
		return '<'
	case "^", "scroll-up", "su":
		return '^'
	case "v", "V", "scroll-down", "sd":
		return 'v'
	case "L", "l", "wipe-left", "wl":
		return 'L'
	case "R", "r", "wipe-right", "wr":
		return 'R'
	case "U", "u", "wipe-up", "wu":
		return 'U'
	case "D", "d", "wipe-down", "wd":
		return 'D'
	case "|", "wipe-horiz", "wh":
		return '|'
	case "-", "wipe-vert", "wv":
		return '-'
	default:
		return '.'
	}
}

func alignmentParam(r url.Values) byte {
	switch r.Get("align") {
	case "", ".", "none", "_":
		return '.'
	case "<", "left":
		return '<'
	case ">", "right":
		return '>'
	case "^", "center":
		return '^'
	case "R", "r", "local-right", "lr":
		return 'R'
	case "L", "l", "local-center-left", "lcl", "cl":
		return 'L'
	case "C", "c", "local-center-right", "lcr", "cr":
		return 'C'
	default:
		return '.'
	}
}

// Bitmap displays a bitmap image on the display
//
//	/readerboard/v1/bitmap?a=<targets>[&merge=<bool]&pos=<pos>[&trans=<trans>]&image=<redcols>$<greencols>$<bluecols>$<flashcols>
//	-> I M/. pos trans R0 ... RN $ G0 ... GN $ B0 ... BN $ F0 ... FN $
func Bitmap(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	if !IsReaderboardModel(hw) {
		return nil, fmt.Errorf("bitmap command not supported for hardware type %v", hw)
	}
	merge := boolParam(r, "merge", '.', 'M')
	trans := transitionParam(r)
	image := r.Get("image")
	pos, err := posParam(r)
	if err != nil {
		return nil, err
	}

	currentColor := "red"
	savedLength := 1
	for i := 0; i < len(image); i++ {
		if image[i] == '$' {
			if ((i - savedLength - 1) % 2) != 0 {
				return nil, fmt.Errorf(fmt.Sprintf("the %s color plane is not an even number of hex digits", currentColor))
			}
			savedLength = i

			switch currentColor {
			case "red":
				if IsReaderboardMonochrome(hw) {
					currentColor = "flashing"
				} else {
					currentColor = "green"
				}
			case "green":
				currentColor = "blue"
			case "blue":
				currentColor = "flashing"
			case "flashing":
				return nil, fmt.Errorf("too many color planes or separators")
			}
		} else {
			if !((image[i] >= '0' && image[i] <= '9') || (image[i] >= 'a' && image[i] <= 'f') || (image[i] >= 'A' && image[i] <= 'F')) {
				return nil, fmt.Errorf("invalid hex character %q in %s image bitplane", image[i], currentColor)
			}
		}
	}
	if currentColor != "flashing" {
		return nil, fmt.Errorf("not enough color bitplanes provided (ended at %s)", currentColor)
	}
	return append(append([]byte{'I', merge, pos, trans}, []byte(image)...), '$'), nil
}

// Color sets the current drawing color.
//
//	/readerboard/v1/color?a=<targets>&color=<rgb>
//	-> K rgb
func Color(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	if !IsReaderboardModel(hw) {
		return nil, fmt.Errorf("color command not supported for hardware type %v", hw)
	}
	color := colorParam(r, "color")
	return []byte{'K', color}, nil
}

// Move repositions the text cursor.
//
//	/readerboard/v1/move?a=<targets>&pos=<pos>
//	-> @ pos
func Move(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	if !IsReaderboardModel(hw) {
		return nil, fmt.Errorf("move command not supported for hardware type %v", hw)
	}
	pos, err := posParam(r)
	if err != nil {
		return nil, err
	}
	return []byte{'@', pos}, nil
}

// Off turns off the status LEDs.
//
//	/readerboard/v1/off?a=<targets>
//	-> X
func Off(r url.Values, _ HardwareModel, targets deviceTargetSet, config *ConfigData) ([]byte, error) {
	// note that we are turning these off
	if config != nil && targets != nil {
		for _, devIDs := range targets {
			for _, devID := range devIDs {
				func() {
					lockDevice(devID)
					defer unlockDevice(devID)
					config.Devices[devID].LastKnownState.clear(config.Devices[devID].LightsInstalled)
				}()
			}
		}
	}
	return []byte{'X'}, nil
}

// Scroll scrolls a text message across the display.
//
//	/readerboard/v1/scroll?a=<targets>&t=<text>[&loop=<bool>]
//	-> < L/. text
func Scroll(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	if !IsReaderboardModel(hw) {
		return nil, fmt.Errorf("scroll command not supported for hardware type %v", hw)
	}
	loop := boolParam(r, "loop", '.', 'L')
	t, err := textParam(r)
	if err != nil {
		return nil, err
	}
	return append([]byte{'<', loop}, t...), nil
}

// Display POST banners again
//
//  /readerboard/v1/diag-banners
//  -> = * =
//
func DiagBanners(_ url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	return []byte{'=', '*', '='}, nil
}

// Save current settings to EEPROM
//
//  /readerboard/v1/save?a=<targets>&type="D"
//  --> = & D =
//
func Save(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	settingType := r.Get("type")
	if settingType == "" {
		return nil, fmt.Errorf("save type parameter is required")
	}
	if settingType != "D" {
		return nil, fmt.Errorf("save data type %v not supported", settingType)
	}
	return []byte{'=', '&', 'D', '='}, nil
}

// Text displays a text message on the display.
//
//	/readerboard/v1/text?a=<targets>&t=<text>[&merge=<bool>][&align=<align>][&trans=<trans>]
//	-> T M/. align trans text
func Text(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	if !IsReaderboardModel(hw) {
		return nil, fmt.Errorf("text command not supported for hardware type %v", hw)
	}
	merge := boolParam(r, "merge", '.', 'M')
	align := alignmentParam(r)
	trans := transitionParam(r)
	text, err := textParam(r)
	if err != nil {
		return nil, err
	}
	return append([]byte{'T', merge, align, trans}, text...), nil
}

func ConfigureDevice(r url.Values, hw HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	Rspeed, err := intParam(r, "rspeed")
	if err != nil {
		return nil, fmt.Errorf("rspeed paramater invalid (%v)", err)
	}
	Uspeed, err := intParam(r, "uspeed")
	if err != nil {
		return nil, fmt.Errorf("uspeed paramater invalid (%v)", err)
	}
	MyAddr, MyAddrValid, err := intParamOrUnderscore(r, "address")
	if err != nil {
		return nil, fmt.Errorf("address paramater invalid (%v)", err)
	}
	GlobalAddr, err := intParam(r, "global")
	if err != nil {
		return nil, fmt.Errorf("global paramater invalid (%v)", err)
	}
	RspeedCode, err := EncodeBaudRateCode(Rspeed)
	if err != nil {
		return nil, fmt.Errorf("rspeed paramater invalid (%v)", err)
	}
	UspeedCode, err := EncodeBaudRateCode(Uspeed)
	if err != nil {
		return nil, fmt.Errorf("rspeed paramater invalid (%v)", err)
	}
	if MyAddrValid {
		return []byte{'=', EncodeInt6(MyAddr), UspeedCode, RspeedCode, EncodeInt6(GlobalAddr)}, nil
	} else {
		return []byte{'=', '.', UspeedCode, RspeedCode, EncodeInt6(GlobalAddr)}, nil
	}
}

// Light sets a static pattern on the busylight status LEDs.
//
//	/readerboard/v1/light?a=<targets>&l=<leds>
//	-> L l0 l1 ... lN $
func Light(r url.Values, hw HardwareModel, targets deviceTargetSet, config *ConfigData) ([]byte, error) {
	l, err := ledList(r)
	if err != nil {
		return nil, err
	}

	// note that we are turning these on
	if config != nil && targets != nil {
		for _, devIDs := range targets {
			for _, devID := range devIDs {
				func() {
					lockDevice(devID)
					defer unlockDevice(devID)
					config.Devices[devID].LastKnownState.setLights(l, config.Devices[devID].LightsInstalled)
					config.Devices[devID].LastKnownState.FlasherStatus.clear()
				}()
			}
		}
	}

	if len(l) == 2 {
		return []byte{'S', l[0]}, nil
	}
	if !IsReaderboardModel(hw) {
		return nil, fmt.Errorf("light command with more than one lit LED not supported for hardware type %v", hw)
	}
	return append([]byte{'L'}, l...), nil
}

// Morse sends a message in Morse code.
//
//  /readerboard/v1/morse?a=<targets>&t=<message>&l=<led>
//  -> M led message... $
//
func Morse(r url.Values, _ HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
	l, err := ledList(r)
	if err != nil {
		return nil, err
	}
	t, err := textParam(r)
	if err != nil {
		return nil, err
	}
	if len(l) != 2 {
		return nil, fmt.Errorf("l parameter must be ONE led name or _")
	}
	return append([]byte{'M', l[0]}, t...), nil
}

// Strobe sets a strobe pattern on the busylight status LEDs.
//
//	/readerboard/v1/strobe?a=<targets>&l=<leds>
//	-> * l0 l1 ... ln $
func Strobe(r url.Values, _ HardwareModel, targets deviceTargetSet, config *ConfigData) ([]byte, error) {
	l, err := ledList(r)
	if err != nil {
		return nil, err
	}

	// note that we are turning these on
	if config != nil && targets != nil {
		for _, devIDs := range targets {
			for _, devID := range devIDs {
				func() {
					lockDevice(devID)
					defer unlockDevice(devID)
					config.Devices[devID].LastKnownState.StroberStatus.set(l[:len(l)-1], 0, 0, 0, 0)
				}()
			}
		}
	}
	return append([]byte{'*'}, l...), nil
}

func extractString(src []byte, idx int, prefix string) (string, int, error) {
	if idx >= len(src) {
		return "", idx, fmt.Errorf("expected string field not found in data")
	}
	if prefix != "" {
		if !bytes.HasPrefix(src[idx:], []byte(prefix)) {
			return "", idx, fmt.Errorf("missing expected \"%s\" for string field", prefix)
		}
		idx += len(prefix)
	}
	if end := bytes.IndexAny(src[idx:], "$\033"); end >= 0 {
		return string(src[idx : idx+end]), idx + end + 1, nil
	}
	return "", idx, fmt.Errorf("missing string field terminator")
}

func parseFlasherStatus(src string) (LEDSequence, error) {
	if len(src) < 2 {
		return LEDSequence{}, fmt.Errorf("flasher sequence data too short")
	}
	seq := LEDSequence{}
	idx := 0
	if src[0] == '/' {
		seq.CustomTiming.Enabled = true
		if len(src) < 5 {
			return LEDSequence{}, fmt.Errorf("flasher sequence data too short")
		}
		if src[1] < '0' || src[1] > 'o' {
			return seq, fmt.Errorf("flasher timing data invalid: up-time %v out of range", src[1])
		}
		seq.CustomTiming.Up = float64(src[1]-48) / 10.0
		if src[2] < '0' || src[2] > 'o' {
			return seq, fmt.Errorf("flasher timing data invalid: on-time %v out of range", src[2])
		}
		seq.CustomTiming.On = float64(src[2]-48) / 10.0
		if src[3] < '0' || src[3] > 'o' {
			return seq, fmt.Errorf("flasher timing data invalid: down-time %v out of range", src[3])
		}
		seq.CustomTiming.Down = float64(src[3]-48) / 10.0
		if src[4] < '0' || src[4] > 'o' {
			return seq, fmt.Errorf("flasher timing data invalid: off-time %v out of range", src[4])
		}
		seq.CustomTiming.Off = float64(src[4]-48) / 10.0
		idx = 5
	}

	if src[idx+0] == 'R' {
		seq.IsRunning = true
	} else if src[idx+0] != 'S' {
		return seq, fmt.Errorf("flasher sequence data invalid: run state value %v", src[idx+0])
	}

	if src[idx+1] == '_' {
		return seq, nil
	}

	if len(src) < idx+3 || src[idx+2] != '@' {
		return seq, fmt.Errorf("flasher sequence data invalid: can't read position marker")
	}

	if src[idx+1] < '0' || src[idx+1] > 'o' {
		return seq, fmt.Errorf("flasher sequence data invalid: position %v out of range", src[idx+1])
	}
	seq.Position = int(src[idx+1]) - '0'
	seq.Sequence = []byte(src[idx+3:])
	return seq, nil
}

func parseBitmapPlane(hex string) ([64]byte, error) {
	var b [64]byte

	if len(hex)%2 != 0 {
		return b, fmt.Errorf("hex string must have even number of characters")
	}
	if len(hex) > 128 {
		return b, fmt.Errorf("hex string too long")
	}

	for i := 0; i < 64 && i*2+2 <= len(hex); i++ {
		ui, err := strconv.ParseUint(hex[i*2:i*2+2], 16, 8)
		if err != nil {
			return b, fmt.Errorf("hex byte at index %d (%s) is invalid: %v", i*2, hex[i*2:i*2+2], err)
		}
		b[i] = byte(ui)
	}
	return b, nil
}

//
// Query inquires about the device status and returns a JSON represenation of the status
//    /readerboard/v1/query?a=<targets>&status
//    -> ?
//    <- L l0 l1 ... lN $ F R/S _/{pos @ l0 l1 ... lN} $ S R/S _/{pos @ l0 l1 ... lN} $ \n
//    /readerboard/v1/query?a=<targets>&status
//    -> Q
//    <- Q B = ad uspd rspd glb I/X/_ S/T/_ $ L ... $ V vers $ R vers $ S sn $ D ... $ \n
//    <- Q C = ad uspd rspd glb I/X/_ S/T/_ $ L ... $ V vers $ R vers $ S sn $ D ... $ M red... $ green... $ blue... $ flash... $ \n
//    <- Q M = ad uspd rspd glb I/X/_ S/T/_ $ L ... $ V vers $ R vers $ S sn $ D ... $ M bits... $ flash... $ \n
//
//    (485)  1101aaaa ...
//           1111gggg 00000001 00aaaaaa ...
//
// This is an extra level of abstraction than the non-reply commands; Query() returns two functions.
// The first is like the other (non-reply) ones that would be passed to WrapHandler; the second is a parser
// function that will be called to parse and validate the data received back from the device.

func parseStatusLEDs(in []byte, idx int) (DiscreteLEDStatus, int, error) {
	var fstat string
	var err error
	stat := DiscreteLEDStatus{}
	if fstat, idx, err = extractString(in, idx, "L"); err != nil {
		return stat, idx, fmt.Errorf("status query response status light string could not be extracted (%v)", err)
	}
	if len(fstat) < 2 || fstat[0] != '0' {
		return stat, idx, fmt.Errorf("status query response status light string could not be extracted (unsupported format version %c)", fstat[0])
	}

	stat.StatusLights = fstat[1:]
	if fstat, idx, err = extractString(in, idx, "F"); err != nil {
		return stat, idx, fmt.Errorf("status query response flasher string could not be extracted (%v)", err)
	}
	if stat.FlasherStatus, err = parseFlasherStatus(fstat); err != nil {
		return stat, idx, fmt.Errorf("status query response flasher string could not be parsed (%v)", err)
	}
	if fstat, idx, err = extractString(in, idx, "S"); err != nil {
		return stat, idx, fmt.Errorf("status query response strober string could not be extracted (%v)", err)
	}
	if stat.StroberStatus, err = parseFlasherStatus(fstat); err != nil {
		return stat, idx, fmt.Errorf("status query response strober string could not be parsed (%v)", err)
	}
	return stat, idx, nil
}

func QueryStatus() (func(url.Values, HardwareModel, deviceTargetSet, *ConfigData) ([]byte, error), func(HardwareModel, []byte) (any, error)) {
	return func(_ url.Values, _ HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
			return []byte{'?'}, nil
		}, func(hw HardwareModel, in []byte) (any, error) {
			// parse the response data, returning the data structure represented or the number of additional bytes
			// still needed before we can have a successful read
			var err error
			var idx int

			if len(in) < 9 {
				return DiscreteLEDStatus{}, fmt.Errorf("query response from hardware too short (%d)", len(in))
			}

			stat, idx, err := parseStatusLEDs(in, 0)
			if err != nil {
				return stat, fmt.Errorf("status query response not understood: %v", err)
			}
			if idx < len(in) {
				log.Printf("WARNING: received %d bytes from device but only %d were expected: %v", len(in), idx, in)
			}
			return stat, nil
		}
}

func Query() (func(url.Values, HardwareModel, deviceTargetSet, *ConfigData) ([]byte, error), func(HardwareModel, []byte) (any, error)) {
	return func(_ url.Values, _ HardwareModel, _ deviceTargetSet, _ *ConfigData) ([]byte, error) {
			return []byte{'Q'}, nil
		}, func(_ HardwareModel, in []byte) (any, error) {
			// parse the response data, returning the data structure represented or the number of additional bytes
			// still needed before we can have a successful read
			var err error
			var idx int

			// try parsing full device status response
			if len(in) < 17 {
				return DeviceStatus{}, fmt.Errorf("query response from hardware too short (%d)", len(in))
			}
			if in[0] != 'Q' || in[3] != '=' || in[10] != '$' {
				return DeviceStatus{}, fmt.Errorf("query response is invalid (%v...)", in[0:9])
			}

			if in[1] != '0' {
				return DeviceStatus{}, fmt.Errorf("query response cound not be understood (unsupported format version %c)", in[1])
			}

			stat := DeviceStatus{
				DeviceModelClass: ModelClass(in[2]),
				DeviceAddress:    parseAddress(in[4]),
				GlobalAddress:    parseAddress(in[7]),
			}
			if stat.SpeedUSB, err = parseBaudRateCode(in[5]); err != nil {
				return stat, fmt.Errorf("query response usb baud rate code %c invalid (%v)", in[5], err)
			}
			if stat.Speed485, err = parseBaudRateCode(in[6]); err != nil {
				return stat, fmt.Errorf("query response rs-485 baud rate code %c invalid (%v)", in[6], err)
			}
			if stat.EEPROM, err = parseEEPROMType(in[8]); err != nil {
				return stat, fmt.Errorf("query response EEPROM type code %c invalid (%v)", in[8], err)
			}
			if stat.Sound, err = parseSoundType(in[9]); err != nil {
				return stat, fmt.Errorf("query response sound support type code %c invalid (%v)", in[9], err)
			}

			if stat.HardwareRevision, idx, err = extractString(in, 11, "V"); err != nil {
				return stat, fmt.Errorf("query response hardware version could not be parsed (%v)", err)
			}
			if stat.FirmwareRevision, idx, err = extractString(in, idx, "R"); err != nil {
				return stat, fmt.Errorf("query response firmware version could not be parsed (%v)", err)
			}
			if stat.Serial, idx, err = extractString(in, idx, "S"); err != nil {
				return stat, fmt.Errorf("query response serial number could not be parsed (%v)", err)
			}
			if stat.StatusLEDs, idx, err = parseStatusLEDs(in, idx); err != nil {
				return stat, fmt.Errorf("query response status LEDs could not be parsed (%v)", err)
			}

			var dimmerBytes string
			if dimmerBytes, idx, err = extractString(in, idx, "D"); err != nil {
				return stat, fmt.Errorf("query response dimmer settings could not be extracted (%v)", err)
			}
			if len(dimmerBytes)%2 != 0 {
				return stat, fmt.Errorf("query response dimmer settings could not be extracted (hex string must have even number of characters)")
			}
			for i := 0; i < len(dimmerBytes); i += 2 {
				if dimmerBytes[i] == '_' && dimmerBytes[i+1] == '_' {
					stat.Dimmers = append(stat.Dimmers, 0xff)
					stat.DimmerValid = append(stat.DimmerValid, false)
				} else {
					ui, err := strconv.ParseUint(string(dimmerBytes[i:i+2]), 16, 8)
					if err != nil {
						return stat, fmt.Errorf("query response dimmer settings could not be extracted (%v)", err)
					}
					stat.Dimmers = append(stat.Dimmers, DimmerSet(ui))
					stat.DimmerValid = append(stat.DimmerValid, true)
				}
			}

			if stat.DeviceModelClass == 'B' {
				if idx < len(in) {
					log.Printf("WARNING: read %d bytes of status from device but only %d was expected: %v", len(in), idx, in)
				}
				return stat, nil
			}
			var planeHexBytes string
			var planeBytes [64]byte
			if planeHexBytes, idx, err = extractString(in, idx, "M"); err != nil {
				return stat, fmt.Errorf("query response red bitmap plane could not be extracted (%v)", err)
			}
			if planeBytes, err = parseBitmapPlane(planeHexBytes); err != nil {
				return stat, fmt.Errorf("query response red bitmap plane could not be parsed (%v)", err)
			}
			stat.ImageBitmap = append(stat.ImageBitmap, planeBytes)
			if stat.DeviceModelClass == 'C' {
				if planeHexBytes, idx, err = extractString(in, idx, ""); err != nil {
					return stat, fmt.Errorf("query response green bitmap plane could not be extracted (%v)", err)
				}
				if planeBytes, err = parseBitmapPlane(planeHexBytes); err != nil {
					return stat, fmt.Errorf("query response green bitmap plane could not be parsed (%v)", err)
				}
				stat.ImageBitmap = append(stat.ImageBitmap, planeBytes)
				if planeHexBytes, idx, err = extractString(in, idx, ""); err != nil {
					return stat, fmt.Errorf("query response blue bitmap plane could not be extracted (%v)", err)
				}
				if planeBytes, err = parseBitmapPlane(planeHexBytes); err != nil {
					return stat, fmt.Errorf("query response blue bitmap plane could not be parsed (%v)", err)
				}
				stat.ImageBitmap = append(stat.ImageBitmap, planeBytes)
			}
			if planeHexBytes, idx, err = extractString(in, idx, ""); err != nil {
				return stat, fmt.Errorf("query response flash bitmap plane could not be extracted (%v)", err)
			}
			if planeBytes, err = parseBitmapPlane(planeHexBytes); err != nil {
				return stat, fmt.Errorf("query response flash bitmap plane could not be parsed (%v)", err)
			}
			stat.ImageBitmap = append(stat.ImageBitmap, planeBytes)
			if idx < len(in) {
				log.Printf("WARNING: read %d bytes of status from device but only %d was expected: %v", len(in), idx, in)
			}
			return stat, nil
		}
}

func parseAddress(b byte) byte {
	if b < '0' || b > 'o' {
		return 0xff
	}
	return b - '0'
}
