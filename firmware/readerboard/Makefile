FONTSRC=standard.font standard_variable.font symbol.font
FWSRC=commands.h commands.ino fonts.h fonts.ino readerboard.h readerboard.ino

fonts.ino: $(FONTSRC)
	./mkfont $(FONTSRC) > fonts.ino

compile: $(FWSRC)
	arduino-cli compile -b arduino:sam:arduino_due_x_dbg

upload:
	arduino-cli upload -b arduino:sam:arduino_due_x_dbg -p /dev/ttyACM1 -t
