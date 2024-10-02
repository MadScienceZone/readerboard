//
// Utility functions to support readerboard and busylight units
//

package readerboard

import (
	"fmt"
)

// ImageFromASCII reads an image source which contains a bitmap in ASCII form
// where each character represents a pixel whose color is denoted by the letters
// . (off), R, G, B, M, C, Y, W, or their lower-case equivalents for flashing pixels.
//
// Alternatively, monochrome bitmaps may be specified using . (off), @ (on), and # (flashing).
//
// Returns an ImageBitmap.
func ImageFromASCII(src []string, depth int) (ImageBitmap, error) {
	if depth != 2 && depth != 4 {
		return ImageBitmap{}, fmt.Errorf("depth must be 2 or 4")
	}
	img := ImageBitmap{
		Depth: depth,
	}
	for row, rowdata := range src {
		for col, coldata := range rowdata {
			if depth == 2 {
				switch coldata {
					case '@'
					case '.'

	
	return ImageBitmap{}, nil
}

// SketchImage renders an ImageBitmap value in ASCII, returned as a slice of strings, each element being a row of the image.
func SketchImage(i ImageBitmap, color bool) ([]string, error) {
	if i.Width == 0 || i.Depth == 0 {
		return nil, nil
	}

	if i.Depth != 2 && i.Depth != 4 {
		return nil, fmt.Errorf("image has %d color depth which is not supported", i.Depth)
	}
	if i.Depth != len(i.Planes) {
		return nil, fmt.Errorf("image bitplane depth %d does not match color depth %d", len(i.Planes), i.Depth)
	}

	var sketch []string
	for row := 0; row < 8; row++ {
		var line string
		for col := 0; col < i.Width; col++ {
			if i.Depth == 2 {
				line += sketch2Color(color, i.Planes[0][col], i.Planes[1][col], 1<<row)
			} else {
				line += sketch4Color(color, i.Planes[0][col], i.Planes[1][col], i.Planes[2][col], i.Planes[3][col], 1<<row)
			}
		}
		sketch = append(sketch, line)
	}
	return sketch, nil
}

func sketch4Color(colorize bool, r, g, b, f, bit byte) string {
	const codes = ".RGYBMCW.rgybmcw"
	c := 0
	if (r & bit) != 0 {
		c |= 1
	}
	if (g & bit) != 0 {
		c |= 2
	}
	if (b & bit) != 0 {
		c |= 4
	}
	if (f & bit) != 0 {
		c |= 8
	}

	if c >= len(codes) {
		return "?"
	}
	if colorize && c != 0 {
		s := "\033["
		if (f & bit) != 0 {
			s += "5;"
		}
		return fmt.Sprintf("%s3%dm%c\033[0m", s, c&0x07, codes[c])
	}
	return string(codes[c])
}

func sketch2Color(colorize bool, p, f, bit byte) string {
	if (p & bit) == 0 {
		return "."
	}

	if colorize && (f&bit) != 0 {
		return "\033[1;5m#\033[0m"
	}
	if (f & bit) != 0 {
		return "#"
	}

	return "@"
}

// ImageBitmap describes a monochrome or color bitmap such as would be usable with readerboards.
type ImageBitmap struct {
	Depth  int      // number of bitplanes (2 for monochrome, 4 for color)
	Width  int      // image width
	Planes [][]byte // column data with LSB of each byte representing the top pixel in the column
}
