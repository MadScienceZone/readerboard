# Adafruit Matrix Support
The Arduino shield boards in this directory are experimental. If they work as expected, they can be used with the firmware as yet another model of readerboard
which uses the Adafruit 32x16, 32x32, and 64x64 RGB matrix boards, as well as a 64x16 display formed from two 32x16 boards connected together.

There are two shield designs here:
## adashield
This is an adaptation of the readerboard shield for our original 64x8 RGB matrix, but without any LEDs, and a ribbon cable connector intended to be
attached to one of the Adafruit matrix boards instead. It supports optional external EEPROM and RS-485 chips as the original 64x8 readerboard does,
as well as an 8-LED status "busylight" display (which is also external to the shield board, so a ribbon cable connector is supplied for that as well).
It also accommodates the same speaker output as the original readerboard.

## adashield_qs
This is a slight variation on the above shield for another of the author's personal projects which also uses readerboards and LED displays. It differs
in that it does not offer audio output, but supports 13 external discrete LEDs in addition to the RGB matrix, and an I2C-bus-connected LED alphanumeric
display.

## Directory Layout
The schematics and PCB layout files (in Kicad and gEDA PCB formats respectively) are in the main directory. The `fab` subdirectory contains zip archives
of the gerber files needed to have the actual printed circuit boards manufactured.
