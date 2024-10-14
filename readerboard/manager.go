package readerboard

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"time"

	moon "github.com/pdevine/goMoonPhase"
)

type ManagedMessage struct {
	ID         string
	Text       string
	Targets    []int
	Transition byte
	Color      byte // TODO

	Until     TimeSpec
	Hold      time.Duration
	Visible   [][2]TimeSpec
	Show      time.Duration
	Repeat    time.Duration
	NextEvent time.Time
}

type DayOfWeek byte

const (
	AnyDay DayOfWeek = iota
	Sunday
	Monday
	Tuesday
	Wednesday
	Thursday
	Friday
	Saturday
)

type TimeSpec struct {
	IsRelative bool
	Weekday    DayOfWeek
	Hours      int
	Minutes    int
	Seconds    int
	Duration   time.Duration
	Baseline   time.Time
}

//
// ParseWeekday interprets a number of usual ways of entering names of days of the week.
// If the string is empty, AnyDay is returned.
//
func ParseWeekday(wd string) (DayOfWeek, error) {
	switch wd {
	case "":
		return AnyDay, nil
	case "U", "u", "Sunday", "SUNDAY", "sunday", "sun", "Sun", "SUN":
		return Sunday, nil
	case "M", "m", "Monday", "MONDAY", "monday", "mon", "Mon", "MON":
		return Monday, nil
	case "T", "t", "Tuesday", "TUESDAY", "tuesday", "tue", "Tue", "TUE":
		return Tuesday, nil
	case "W", "w", "Wednesday", "WEDNESDAY", "wednesday", "wed", "Wed", "WED":
		return Wednesday, nil
	case "R", "r", "Thursday", "THURSDAY", "thursday", "thu", "Thu", "THU":
		return Thursday, nil
	case "F", "f", "Friday", "FRIDAY", "friday", "fri", "Fri", "FRI":
		return Friday, nil
	case "S", "s", "Saturday", "SATURDAY", "saturday", "sat", "Sat", "SAT":
		return Saturday, nil
	}
	return AnyDay, fmt.Errorf("invalid day of the week")
}

//
// ParseTimeSpec returns a TimeSpec from string which has one of the forms
//   [<day>@][<hour>]:<min>[:<sec>]   absolute time, maybe on a day of the week
//   <day>                            start of the named day of the week
//   <duration>                       relative time duration
//
func ParseTimeSpec(s string) (ts TimeSpec, err error) {
	//                                            1       2      3        4
	timeSpecPattern := regexp.MustCompile(`^\s*(?:(\w+)@)?(\d+)?:(\d+)(?::(\d+))?\s*$`)

	if fields := timeSpecPattern.FindStringSubmatch(s); fields != nil {
		if fields[1] != "" {
			if ts.Weekday, err = ParseWeekday(fields[1]); err != nil {
				return
			}
		}
		if fields[2] != "" {
			if ts.Hours, err = strconv.Atoi(fields[2]); err != nil {
				return ts, fmt.Errorf("unable to understand hours: %v", err)
			}
		}
		if ts.Minutes, err = strconv.Atoi(fields[3]); err != nil {
			return ts, fmt.Errorf("unable to understand minutes: %v", err)
		}
		if fields[4] != "" {
			if ts.Seconds, err = strconv.Atoi(fields[4]); err != nil {
				return ts, fmt.Errorf("unable to understand seconds: %v", err)
			}
		}
		return
	}

	if ts.Weekday, err = ParseWeekday(s); err == nil {
		return
	}

	if ts.Duration, err = time.ParseDuration(s); err != nil {
		return ts, fmt.Errorf("can't make sense of the time value")
	}
	ts.IsRelative = true
	return
}

//
// TimeUntil returns the length of time between now and the time indicated by the TimeSpec.
//
func (t TimeSpec) TimeUntil() time.Duration {
	if t.IsRelative {
		return t.Baseline.Add(t.Duration).Sub(time.Now())
	}
	return 0
}

//
// until   TimeSpec      expiration
// hold    time.Duration display this long before moving on
// show    time.Duration keep the message for this long before hiding
// repeat  time.Duration show again after being hidden this long
// visible [][2]TimeSpec regardless of the above, suppress except between these time range(s)
//
func WrapInternalHandler(f func(deviceTargetSet, url.Values, http.ResponseWriter, *ConfigData) error, config *ConfigData, targetsRequired bool) func(http.ResponseWriter, *http.Request) {
	return func(w http.ResponseWriter, r *http.Request) {
		var (
			targetNetworks deviceTargetSet
			errors         int
		)

		if targetsRequired {
			targetNetworks, errors, _ = getTargetList(r, config)
			if errors > 0 {
				log.Printf("invalid request: could not understand target list")
				reportErrorJSON(w, errors, "could not understand target list")
				return
			}
		} else {
			if err := r.ParseForm(); err != nil {
				log.Printf("invalid request: could not parse HTTP request data")
				reportErrorJSON(w, errors, "could not parse HTTP request data")
				return
			}
		}
		if err := f(targetNetworks, r.Form, w, config); err != nil {
			log.Printf("requested internal command failed: %v", err)
			reportErrorJSON(w, 1, err.Error())
		}
	}
}

// Current returns what we last told a device to display on its discrete LEDs
//
//  /readerboard/v1/current?a=<targets>
//
func Current(targets deviceTargetSet, _ url.Values, w http.ResponseWriter, config *ConfigData) error {
	currentState := make(map[string]DiscreteLEDStatus)

	for _, devs := range targets {
		for _, devID := range devs {
			func() {
				lockDevice(devID)
				defer unlockDevice(devID)
				currentState[strconv.Itoa(devID)] = config.Devices[devID].LastKnownState
			}()
		}
	}
	response, err := json.Marshal(currentState)
	if err != nil {
		return err
	}
	io.WriteString(w, string(response)+"\n")
	return nil
}

//
// ReplaceTokens replaces {...} tokens in a string with values taken from
// our set of posted message tokens which are computable or constant, along
// with a set of user-defined variables.
//
// Tokens which do not exist are retained as-is in the source text, but
// references to user-defined variables {$...} which are undefined are removed,
// as if all possible variable names are defined by default to the empty string.
//
// Literal { and } characters may be entered as {{ and }}.
//
func ReplaceTokens(t []byte, font int, config *ConfigData) []byte {
	s := make([]byte, 0, len(t))
	i := 0
	bigMode := false

	for i < len(t) {
		part := t[i:]
		openBrace := bytes.IndexByte(part, '{')
		closeBraces := bytes.Index(part, []byte{'}', '}'})

		if closeBraces >= 0 && (openBrace < 0 || closeBraces < openBrace) {
			// we have a }} before the next {
			// write out the string up to the first }, then drop the second.

			s = renderBigFonts(s, part[0:closeBraces+1], font, bigMode)
			i += closeBraces + 2
			continue
		}

		if openBrace < 0 {
			// no { remaining either; we have nothing else left to do.
			s = renderBigFonts(s, part, font, bigMode)
			break
		}

		// found a {. Write everything up to that point and then start processing the token.
		if openBrace > 0 {
			s = renderBigFonts(s, part[0:openBrace], font, bigMode)
		}

		if len(part) > openBrace+1 && part[openBrace+1] == '{' {
			// It was actually a {{; write a literal { and start over.
			s = append(s, '{')
			i += openBrace + 2
			continue
		}

		closeBrace := bytes.IndexByte(part[openBrace:], '}')

		if closeBrace < 0 {
			// no matching }! This is an error so we'll bail out here.
			s = renderBigFonts(s, part[openBrace:], font, bigMode)
			break
		}
		closeBrace += openBrace

		token := string(part[openBrace+1 : closeBrace])
		i += closeBrace + 1

		if token == "" {
			// no token at all?
			s = append(s, '{')
			s = append(s, '}')
			continue
		}

		if token[0] == '$' {
			if len(token) < 2 {
				// {$} isn't a valid variable but it looks like a variable so we'll elide it.
				continue
			}

			if config != nil {
				func() {
					lockVariables()
					defer unlockVariables()
					s = renderBigFonts(s, []byte(config.UserVariables[token[1:]]), font, bigMode)
				}()
			}
			continue
		}

		// built-in tokens
		// colorN
		if strings.HasPrefix(token, "color") {
			if color, err := strconv.Atoi(token[5:]); err == nil && color < 16 {
				s = append(s, 0x0b)
				s = append(s, byte(color)+'0')
			}
			continue
		}

		// fontN
		if strings.HasPrefix(token, "font") {
			if f, err := strconv.Atoi(token[4:]); err == nil && font < 10 {
				s = append(s, 0x06)
				s = append(s, byte(f)+'0')
			}
			continue
		}

		// @[+-]N
		if token[0] == '@' && len(token) > 1 {
			if n, err := strconv.Atoi(token[1:]); err == nil {
				if n < 0 {
					s = append(s, 0x08)
					s = append(s, byte(-n)+'0')
				} else if token[1] == '+' {
					s = append(s, 0x0c)
					s = append(s, byte(n)+'0')
				} else {
					s = append(s, 0x03)
					s = append(s, byte(n)+'0')
				}
			}
		}

		// #n
		// #$xx
		if token[0] == '#' && len(token) > 1 {
			if token[1] == '$' && len(token) > 2 {
				if n, err := strconv.ParseInt(token[2:], 16, 8); err == nil {
					s = append(s, byte(n&0xff))
				}
			} else {
				if n, err := strconv.Atoi(token[1:]); err == nil {
					s = append(s, byte(n&0xff))
				}
			}
		}

		switch token {
		case "big":
			bigMode = true
		case "date":
			s = renderBigFonts(s, []byte(time.Now().Format("02-Jan-2006")), font, bigMode)
		case "normal":
			bigMode = false
		case "mdy":
			s = renderBigFonts(s, []byte(time.Now().Format("01/02/2006")), font, bigMode)
		case "pom":
			phase, ok := map[string]byte{
				"New Moon":        152,
				"Waxing Crescent": 153,
				"First Quarter":   154,
				"Waxing Gibbous":  155,
				"Full Moon":       156,
				"Waning Gibbous":  157,
				"Third Quarter":   158,
				"Waning Crescent": 159,
			}[moon.New(time.Now()).PhaseName()]
			if !ok {
				phase = 139 // punt to this symbol if we don't know
			}

			if font != 2 {
				s = append(s, 6)
				s = append(s, '2')
				s = append(s, phase)
				s = append(s, 6)
				s = append(s, byte(font)+'0')
			} else {
				s = append(s, phase)
			}

		case "time12":
			ampm := func(h int) byte {
				if h >= 12 {
					return 145
				}
				return 144
			}
			hr12 := func(h int) int {
				if h = h % 12; h == 0 {
					return 12
				}
				return h
			}

			now := time.Now()
			s = renderBigFonts(s, []byte(fmt.Sprintf("%2d:%02d", hr12(now.Hour()), now.Minute())), font, bigMode)
			if font != 2 {
				s = append(s, 6)
				s = append(s, '2')
				s = append(s, ampm(now.Hour()))
				s = append(s, 6)
				s = append(s, byte(font)+'0')
			} else {
				s = append(s, ampm(now.Hour()))
			}

		case "time24":
			s = renderBigFonts(s, []byte(time.Now().Format("15:04")), font, bigMode)
		case "ymd":
			s = renderBigFonts(s, []byte(time.Now().Format("2006-01-02")), font, bigMode)
		default:
			s = append(s, '{')
			s = append(s, []byte(token)...)
			s = append(s, '}')
		}
	}
	return s
}

func renderBigFonts(s, text []byte, font int, bigMode bool) []byte {
	if !bigMode || font == 2 {
		s = append(s, text...)
		return s
	}

	curfont := font

	for _, ch := range text {
		if (ch >= '0' && ch <= '9') ||
			(ch >= 'A' && ch <= 'Z') ||
			ch == '!' || ch == '.' || ch == ':' || ch == ',' || ch == ' ' {
			if curfont != 2 {
				curfont = 2
				s = append(s, 6)
				s = append(s, '2')
			}
			switch ch {
			case ' ':
				s = append(s, 168)
			case '.':
				s = append(s, 47)
			case ':':
				s = append(s, 58)
			case ',':
				s = append(s, 45)
			default:
				s = append(s, ch)
			}
		} else {
			if curfont != font {
				curfont = font
				s = append(s, 6)
				s = append(s, byte(font)+'0')
			}
			s = append(s, ch)
		}
	}
	if curfont != font {
		curfont = font
		s = append(s, 6)
		s = append(s, byte(font)+'0')
	}
	return s
}

//
// We run a background task to manage the schedule of messages. It sleeps
// until its timer for the next change expires, or it receives a variable
// update, or it receives a new post.
//  ________________
// |MessageScheduler|<---- next update timer
// |                |<---- post message {id, text, attrs}
// |                |<---- unpost message {id}
// |                |<---- update var {var}
// |                |----> call Text(), Clear()
// |                |
// |                |
// |________________|
//

type PostIDs struct {
	MessageID string
	DeviceIDs []int
}

func ManageMessageQueue(config *ConfigData) {
	func() {
		lockVariables()
		defer unlockVariables()

		config.messageManager.post = make(chan ManagedMessage)
		config.messageManager.unpost = make(chan PostIDs)
		config.messageManager.update = make(chan string)
	}()

	for {
		select {
		case msg := <-config.messageManager.post:
			log.Printf("XXX post %v", msg)
		case id := <-config.messageManager.unpost:
			log.Printf("XXX unpost %v", id)
		case name := <-config.messageManager.update:
			log.Printf("XXX update %v", name)
		}
	}
}

// Post displays a message
//
//  /readerboard/v1/post?a=<targets>&t=<text>&id=<id>[&trans=<transition>][&until=<dt>][&hold=<dur>][&color=<rgb>][&visible=<dur>][&show=<dur>][&repeat=<dur>]
//
//  <dt> ::= [<day>@][<hour>]:<min>[:<sec>]
//        |  <day>
//        |  <duration>
//
func Post(_ deviceTargetSet, _ url.Values, _ http.ResponseWriter, _ *ConfigData) error {
	return fmt.Errorf("not yet implemented")
}

//
// Postlist returns the message queue
//
//  /readerboard/v1/postlist?a=<targets>[&id={/<regex> | <id>}]
//
func PostList(_ deviceTargetSet, _ url.Values, _ http.ResponseWriter, _ *ConfigData) error {
	return fmt.Errorf("not yet implemented")
}

//
// Unpost removes a message
//
//  /readerboard/v1/unpost?a=<targets>&id={/ <regex> | <id>}
//
func Unpost(targets deviceTargetSet, r url.Values, _ http.ResponseWriter, config *ConfigData) error {
	msgID := r.Get("id")
	if msgID == "" {
		return fmt.Errorf("Message ID required")
	}

	config.messageManager.unpost <- PostIDs{
		MessageID: msgID,
		DeviceIDs: collapseTargetList(targets),
	}
	return nil
}

//
// Update sets user variables inside messages
//
//  /readerboard/v1/update?name0=value0&name1=value1&...&nameN=valueN
//
func Update(_ deviceTargetSet, r url.Values, _ http.ResponseWriter, config *ConfigData) error {
	lockVariables()
	defer unlockVariables()

	if config.UserVariables == nil {
		config.UserVariables = make(map[string]string)
		log.Print("created map")
	}

	for name, val := range r {
		if len(val) == 0 || val[0] == "" {
			delete(config.UserVariables, name)
			log.Printf("undefined {$%s}", name)
		} else {
			config.UserVariables[name] = val[0]
			log.Printf("defined {$%s}=\"%s\"", name, val[0])
		}
		config.messageManager.update <- name
	}
	return nil
}
