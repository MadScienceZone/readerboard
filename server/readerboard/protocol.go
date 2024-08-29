package readerboard

import (
	"encoding/json"
	"unicode/utf8"
)

type LightStatus rune
type TransitionEffect rune
type ColorCode byte
type Alignment rune

const (
	AlignLeft Alignment = '<' // TODO: speculative, need to define these
)
const (
	LightOff           LightStatus = '_'
	LightStatusWhite               = 'W'
	LightStatusBlue1               = 'B'
	LightStatusBlue2               = 'b'
	LightStatusRed1                = 'R'
	LightStatusRed2                = 'r'
	LightStatusYellow1             = 'Y'
	LightStatusYellow2             = 'y'
	LightStatusGreen               = 'G'
	LightStatus0                   = '0'
	LightStatus1                   = '1'
	LightStatus2                   = '2'
	LightStatus3                   = '3'
	LightStatus4                   = '4'
	LightStatus5                   = '5'
	LightStatus6                   = '6'
	LightStatus7                   = '7'
)

const (
	TransNone              TransitionEffect = '.'
	TransScrollRight                        = '>'
	TransScrollLeft                         = '<'
	TransScrollUp                           = '^'
	TransScrollDown                         = 'v'
	TransWipeLeft                           = 'L'
	TransWipeRight                          = 'R'
	TransWipeUp                             = 'U'
	TransWipeDown                           = 'D'
	TransWipeOutHorizontal                  = '|'
	TransWipeOutVertical                    = '-'
	TransRandom                             = '?'
)

const (
	ColorBlack   ColorCode = 0
	ColorRed               = 1
	ColorGreen             = 2
	ColorYellow            = 3
	ColorBlue              = 4
	ColorMagenta           = 5
	ColorCyan              = 6
	ColorWhite             = 7
)

func (l LightStatus) MarshalJSON() ([]byte, error) {
}
func (l *LightStatus) UnmarshalJSON(data []byte) error {
}

type BaseMessage struct {
	MessageType string `json:"m"`
	Addresses   []int  `json:"a"`
}

type AllOffMessage struct {
	BaseMessage
	IsForStatusDevice bool `json:"status,omitempty"`
}

type BitmapMessage struct {
	BaseMessage
	Merge      bool             `json:"merge,omitempty"`
	Column     int              `json:"pos"`
	Transition TransitionEffect `json:"trans,omitempty"`
	Image      [][]int          `json:"image"`
}

type ClearMessage struct {
	BaseMessage
}

type ColorMessage struct {
	BaseMessage
	Color ColorCode `json:"color"`
}

type FlashMessage struct {
	BaseMessage
	Lights []LightStatus `json:"l"`
}

type FontMessage struct {
	BaseMessage
	FontID int `json:"idx"`
}

type GraphMessage struct {
	BaseMessage
	Value  int         `json:"v"`
	Colors []ColorCode `json:"colors,omitempty"`
}

type LightMessage struct {
	BaseMessage
	Lights []LightStatus `json:"l"`
}

type MoveMessage struct {
	BaseMessage
	Column int `json:"pos"`
}

type QueryMessage struct {
	BaseMessage
	IsForStatusDevice bool `json:"status,omitempty"`
}

type ScrollMessage struct {
	BaseMessage
	Loop bool   `json:"loop,omitempty"`
	Text string `json:"t"`
}

type StrobeMessage struct {
	BaseMessage
	Lights []LightStatus `json:"l"`
}

type TextMessage struct {
	BaseMessage
	Merge      bool             `json:"merge,omitempty"`
	Align      Alignment        `json:"align,omitempty"`
	Transition TransitionEffect `json:"trans,omitempty"`
	Text       string           `json:"t"`
}

func (v *LightStatus) MarshalJSON() ([]byte, error) {
	if *v == 0 {
		return json.Marshal("_")
	}
	return json.Marshal(string(*v))
}

func (v *TransitionEffect) MarshalJSON() ([]byte, error) {
	if *v == 0 {
		return json.Marshal(".")
	}
	return json.Marshal(string(*v))
}

func (v *Alignment) MarshalJSON() ([]byte, error) {
	if *v == 0 {
		return json.Marshal("<")
	}
	return json.Marshal(string(*v))
}

func (v *LightStatus) UnmarshalJSON(j []byte) error {
	a, err := unmarshalJSONRuneString(j, "_")
	*v = LightStatus(a)
	return err
}

func (v *TransitionEffect) UnmarshalJSON(j []byte) error {
	a, err := unmarshalJSONRuneString(j, ".")
	*v = TransitionEffect(a)
	return err
}

func (v *Alignment) UnmarshalJSON(j []byte) error {
	a, err := unmarshalJSONRuneString(j, "<")
	*v = Alignment(a)
	return err
}

func unmarshalJSONRuneString(j []byte, defval string) error {
	var s string
	if err := json.Unmarshal(j, &s); err != nil {
		return defval, err
	}
	if len(s) == 0 {
		return defval, nil
	}

	r, _ := utf8.DecodeRuneInString(s)
	return r, nil
}
