package readerboard

import (
	"slices"
	"testing"
)

func TestSketchImage(t *testing.T) {
	for i, tcase := range []struct {
		input         ImageBitmap
		color         bool
		expected      []string
		errorExpected bool
	}{
		// 0
		{ImageBitmap{0, 0, nil}, false, nil, false},
		// 1
		{ImageBitmap{2, 6, [][]byte{
			{0x85, 0x49, 0x31, 0x31, 0x49, 0x85},
			{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}},
			false,
			[]string{
				"@@@@@@",
				"......",
				"@....@",
				".@..@.",
				"..@@..",
				"..@@..",
				".@..@.",
				"@....@"}, false},
		// 2
		{ImageBitmap{2, 6, [][]byte{
			{0x85, 0x49, 0x31, 0x31, 0x49, 0x85},
			{0x01, 0x01, 0x01, 0x30, 0x40, 0x80}}},
			false,
			[]string{
				"###@@@",
				"......",
				"@....@",
				".@..@.",
				"..@#..",
				"..@#..",
				".@..#.",
				"@....#"}, false},
		// 3
		{ImageBitmap{2, 6, [][]byte{
			{0x85, 0x49, 0x31, 0x31, 0x49, 0x85},
			{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}},
			true,
			[]string{
				"@@@@@@",
				"......",
				"@....@",
				".@..@.",
				"..@@..",
				"..@@..",
				".@..@.",
				"@....@"}, false},
		// 4
		{ImageBitmap{2, 6, [][]byte{
			{0x85, 0x49, 0x31, 0x31, 0x49, 0x85},
			{0x01, 0x01, 0x01, 0x30, 0x40, 0x80}}},
			true,
			[]string{
				"\033[1;5m#\033[0m\033[1;5m#\033[0m\033[1;5m#\033[0m@@@",
				"......",
				"@....@",
				".@..@.",
				"..@\033[1;5m#\033[0m..",
				"..@\033[1;5m#\033[0m..",
				".@..\033[1;5m#\033[0m.",
				"@....\033[1;5m#\033[0m"}, false},
		// 5
		{ImageBitmap{4, 6, [][]byte{
			{0x85, 0x49, 0x31, 0x31, 0x49, 0x85},
			{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}},
			false,
			[]string{
				"@@@@@@",
				"......",
				"@....@",
				".@..@.",
				"..@@..",
				"..@@..",
				".@..@.",
				"@....@"}, true},
		// 6
		{ImageBitmap{4, 6, [][]byte{
			{0x01, 0x01, 0x31, 0x31, 0x01, 0x01},
			{0x80, 0x40, 0x30, 0x30, 0x08, 0x04},
			{0x04, 0x08, 0x30, 0x31, 0x40, 0x80},
			{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}},
			false,
			[]string{
				"RRRMRR",
				"......",
				"B....G",
				".B..G.",
				"..WW..",
				"..WW..",
				".G..B.",
				"G....B"}, false},
		// 7
		{ImageBitmap{4, 6, [][]byte{
			{0x01, 0x01, 0x31, 0x31, 0x01, 0x01},
			{0x80, 0x40, 0x30, 0x30, 0x08, 0x04},
			{0x04, 0x08, 0x30, 0x31, 0x40, 0x80},
			{0x01, 0x01, 0x01, 0x30, 0x40, 0x80}}},
			false,
			[]string{
				"rrrMRR",
				"......",
				"B....G",
				".B..G.",
				"..Ww..",
				"..Ww..",
				".G..b.",
				"G....b"}, false},
		// 8
		{ImageBitmap{2, 6, [][]byte{
			{0x85, 0x49, 0x31, 0x31, 0x49, 0x85},
			{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}},
			true,
			[]string{
				"@@@@@@",
				"......",
				"@....@",
				".@..@.",
				"..@@..",
				"..@@..",
				".@..@.",
				"@....@"}, false},
		// 9
		{ImageBitmap{2, 6, [][]byte{
			{0x85, 0x49, 0x31, 0x31, 0x49, 0x85},
			{0x01, 0x01, 0x01, 0x30, 0x40, 0x80}}},
			true,
			[]string{
				"\033[1;5m#\033[0m\033[1;5m#\033[0m\033[1;5m#\033[0m@@@",
				"......",
				"@....@",
				".@..@.",
				"..@\033[1;5m#\033[0m..",
				"..@\033[1;5m#\033[0m..",
				".@..\033[1;5m#\033[0m.",
				"@....\033[1;5m#\033[0m"}, false},
	} {
		sketch, err := SketchImage(tcase.input, tcase.color)
		if err != nil {
			if !tcase.errorExpected {
				t.Fatalf("test case %d error: %v", i, err)
			}
			continue
		} else if tcase.errorExpected {
			t.Fatalf("test case %d expected to throw an error but it didn't.", i)
		}
		if slices.Compare(sketch, tcase.expected) != 0 {
			t.Errorf("test case %d returned %v but expected %v", i, sketch, tcase.expected)
		}
	}
}
func TestImageFromASCII(t *testing.T) {
}
