package readerboard

import (
	"testing"
	"time"
)

func TestParseDate(t *testing.T) {
	for i, tcase := range []struct {
		input    string
		expected TimeSpec
	}{
		{"Tuesday", TimeSpec{Weekday: Tuesday}},
		{"Tue", TimeSpec{Weekday: Tuesday}},
		{"T", TimeSpec{Weekday: Tuesday}},
		{"", TimeSpec{}},
		{":13", TimeSpec{Minutes: 13}},
		{":13:12", TimeSpec{Minutes: 13, Seconds: 12}},
		{"3:15", TimeSpec{Hours: 3, Minutes: 15}},
		{"3:15:00", TimeSpec{Hours: 3, Minutes: 15}},
		{"3:15:02", TimeSpec{Hours: 3, Minutes: 15, Seconds: 2}},
		{"wed@3:15:02", TimeSpec{Weekday: Wednesday, Hours: 3, Minutes: 15, Seconds: 2}},
		{"4h32m42s", TimeSpec{IsRelative: true, Duration: 4*time.Hour + 32*time.Minute + 42*time.Second}},
	} {
		actual, err := ParseTimeSpec(tcase.input)
		if err != nil {
			t.Fatalf("test case %d: %v", i, err.Error())
		}
		if actual != tcase.expected {
			t.Errorf("test case %d: expected %v but got %v", i, tcase.expected, actual)
		}
	}
}

func TestTokens(t *testing.T) {
	for i, tcase := range []struct {
		input string
	}{
		{"abc"},
		{""},
		{"{"},
		{"{big}1234{normal}123"},
		{"{color3}{time12}"},
		{"{color3}{big}{time12}"},
		{"x{}y{xyzzy}{$abc}"},
		{"{ymd};{time24};{pom}"},
	} {
		t.Errorf("%d: %q", i, ReplaceTokens([]byte(tcase.input), 0, nil))
	}
}
