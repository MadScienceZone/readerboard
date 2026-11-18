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

// ManagedMessage describes a posted message in our managed message queue.
type ManagedMessage struct {
	ID         string          // unique identifier for the message
	Text       []byte          // message content, may include {tokens}
	Targets    deviceTargetSet // list of device IDs to display the message
	Transition byte            // transition to use to introduce the message
	Color      byte            // color to set for the message TODO
	Until      TimeSpec        // expiration time for the message
	Hold       time.Duration   // length of time to display this before moving on to next message
	Visible    [][2]TimeSpec   // set of time ranges during which message can be shown
	Show       time.Duration   // show the message for this long after posting, then hide (but leave in queue)
	Repeat     time.Duration   // unhide after being hidden for this long
	NextEvent  time.Time       // time when we need to do the next thing with this message; TODO do we need this?
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

// TimeSpec represents a time of day or day of week as used for scheduling
// posted messages.
type TimeSpec struct {
	// If IsRelative is true, Duration gives a relative time from Baseline for the event.
	// Otherwise, the other fields specify an absolute point in time.
	IsRelative bool
	Duration   time.Duration
	Baseline   time.Time

	// The absolute time of day
	Weekday DayOfWeek
	Hours   int
	Minutes int
	Seconds int
}

func (wd DayOfWeek) String() string {
	switch wd {
	case AnyDay:
		return "Any"
	case Sunday:
		return "Sunday"
	case Monday:
		return "Monday"
	case Tuesday:
		return "Tuesday"
	case Wednesday:
		return "Wednesday"
	case Thursday:
		return "Thursday"
	case Friday:
		return "Friday"
	case Saturday:
		return "Saturday"
	default:
		return "UNKNOWN"
	}
}

// ParseWeekday interprets a number of usual ways of entering names of days of the week.
// If the string is empty, AnyDay is returned.
func ParseWeekday(wd string) (DayOfWeek, error) {
	switch wd {
	case "", "Any", "AnyDay", "ANYDAY", "any", "anyday", "a", "A":
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

// ParseTimeSpec returns a TimeSpec from string which has one of the forms
//
//	[<day>@][<hour>]:<min>[:<sec>]   absolute time, maybe on a day of the week
//	<day>                            start of the named day of the week
//	<duration>                       relative time duration
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

// TimeUntil returns the length of time between now and the time indicated by the TimeSpec.
func (t TimeSpec) TimeUntil() time.Duration {
	if t.IsRelative {
		return time.Until(t.Baseline.Add(t.Duration))
	}

	now := time.Now()
	y, m, d := now.Date()
	sod := time.Date(y, m, d, 0, 0, 0, 0, now.Location())
	if t.Weekday != AnyDay {
		for t.Weekday.String() != sod.Weekday().String() {
			sod = sod.AddDate(0, 0, 1)
		}
	}
	return time.Until(sod.Add(time.Duration(t.Hours)*time.Hour +
		time.Duration(t.Minutes)*time.Minute +
		time.Duration(t.Seconds)*time.Second))
}

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
//	/readerboard/v1/current?a=<targets>
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

// ReplaceTokens replaces {...} tokens in a string with values taken from
// our set of posted message tokens which are computable or constant, along
// with a set of user-defined variables.
//
// Tokens which do not exist are retained as-is in the source text, but
// references to user-defined variables {$...} which are undefined are removed,
// as if all possible variable names are defined by default to the empty string.
//
// Literal { and } characters may be entered as {{ and }}.
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
			hr12 := func(h int) int {
				if h = h % 12; h == 0 {
					return 12
				}
				return h
			}

			now := time.Now()
			s = renderBigFonts(s, fmt.Appendf([]byte{}, "%2d:%02d", hr12(now.Hour()), now.Minute()), font, bigMode)

		case "ampm":
			now := time.Now()
			ampm := func(h int) byte {
				if h >= 12 {
					return 145
				}
				return 144
			}

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
// |                |<---- minute timer (triggers redraw if time-based tokens in use)
// |                |----> call Text(), Clear()
// |                |
// |                |
// |________________|
//
// The messages are maintained in a display queue which holds all of the messages we know about
// in the order they'll be displayed, alongside a timer to the next time we need to change to
// the next one in the list.
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

	// Set timer for time-based value updates
	updateTimes := time.NewTimer(time.Until(time.Now().Truncate(time.Minute).Add(time.Minute)))
	defer updateTimes.Stop()

	var displayQueue []ManagedMessage
	//var currentMessage int

	for {
		select {
		case msg := <-config.messageManager.post:
			displayQueue = append(displayQueue, msg)
			if len(displayQueue) == 1 {
				// this starts the display up
				//currentMessage = 0
				updateTimes.Reset(time.Until(time.Now().Truncate(time.Minute).Add(time.Minute)))
				// TODO set timer for next display event
				// TODO display this message now
			}
			log.Printf("post %v", msg)
		case id := <-config.messageManager.unpost:
			log.Printf("XXX unpost %v", id)
		case name := <-config.messageManager.update:
			log.Printf("XXX update %v", name)
		case <-updateTimes.C:
			//TODO if queue empty, stop the timer
			now := time.Now().Truncate(time.Minute)
			log.Printf("XXX update minute timer: {ampm}=%s, {mdy}=%s, {pom}=(%s), {time12}=%s, {time24}=%s, {ymd}=%s",
				func() string {
					if now.Hour() >= 12 {
						return "PM"
					}
					return "AM"
				}(), now.Format("01/02/2006"), moon.New(now).PhaseName(), now.Format("3:04"),
				now.Format("15:04"), now.Format("2006-01-02"))
			updateTimes.Reset(time.Until(now.Add(time.Minute)))
			// TODO on display timer: take action on current message
		}
	}
}

// Post displays a message. This adds the message to our display queue. Lower-level display controls
// suspend the display queue until the next Clear.
//
//			/readerboard/v1/post?a=<targets>&t=<text>&id=<id>[&trans=<transition>][&until=<dt>][&hold=<dur>][&color=<rgb>][&visible=<dt>-<dt>[,...]][&show=<dur>][&repeat=<dur>]
//
//		 <id> ::= unique string
//		 <transition> ::= <transition-code>
//	     <transition-code> ::= '.'=none
//	              | '>'=scroll right
//	              | '<'=scroll left
//	              | '^'=scroll up
//	              | 'v'=scroll down
//	              | 'L'=wipe left
//	              | 'R'=wipe right
//	              | 'U'=wipe up
//	              | 'D'=wipe down
//	              | '|'=wipe horiz
//	              | '-'=wipe vert
//	              | '?'=random
//			<dt> ::= [<day>@][<hour>]:<min>[:<sec>]
//			      |  <day>
//			      |  <duration> (as defined for Go time.Duration type)
//		 <rgb> ::= <color-id> | <color-code>
//		 <color-id> ::= 0=off | 1=red | 2=green | 3=amber | 4=blue | 5=magenta | 6=cyan | 7=white
//		             |  8-15=as 0-7 but flashing
//		 <color-code> ::= 0-9 as <color-id> | ':'=10 | ';'=11 | '<'=12 | '='=13 | '>'=14 | '?'=15
//
// hold: The message will remain on-screen for the specified length of time before moving on to the next message.
// show: The message will remain in the queue for this amount of time and will then be hidden (but not necessarily
// Unposted).
// repeat: If the message was hidden (as with the show parameter), it will re-appear again after the specified
// length of time has elapsed since it was hidden.
// until: The message will automatically Unpost at this time.
// visible: A range of dates (or set of them) between which the message will be visible.
func Post(targets deviceTargetSet, r url.Values, _ http.ResponseWriter, config *ConfigData) error {
	color := colorParam(r, "color")
	trans := transitionParam(r)
	//merge := boolParam(r, "merge", '.', 'M')
	text, err := textParam(r)
	if err != nil {
		return err
	}
	id := r.Get("id")
	if id == "" {
		return fmt.Errorf("missing ID for posted message")
	}
	until, err := dtParam(r, "until")
	if err != nil {
		return err
	}
	hold, err := durationParam(r, "hold")
	if err != nil {
		return err
	}
	visible, err := dtListParam(r, "visible")
	if err != nil {
		return err
	}
	show, err := durationParam(r, "show")
	if err != nil {
		return err
	}
	repeat, err := durationParam(r, "repeat")
	if err != nil {
		return err
	}

	config.messageManager.post <- ManagedMessage{
		ID:         id,
		Text:       text,
		Targets:    targets,
		Transition: trans,
		Color:      color,
		Until:      until,
		Hold:       hold,
		Visible:    visible,
		Show:       show,
		Repeat:     repeat,
	}
	return nil
}

// Postlist returns the message queue. Reports a JSON list of what's in the message queue.
//
//	/readerboard/v1/postlist?a=<targets>[&id={/<regex> | <id>}]
func PostList(_ deviceTargetSet, _ url.Values, _ http.ResponseWriter, _ *ConfigData) error {
	return fmt.Errorf("not yet implemented")
}

// Unpost removes a message or all messages matching a pattern from the queue.
//
//	/readerboard/v1/unpost?a=<targets>&id={/ <regex> | <id>}
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

// Update sets user variables inside messages
//
//	/readerboard/v1/update?name0=value0&name1=value1&...&nameN=valueN
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
