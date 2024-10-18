FONTSRC=standard.font standard_variable.font symbol.font
FWSRC=commands.h commands.ino fonts.h fonts.ino readerboard.h readerboard.ino
BOARD=arduino:sam:arduino_due_x_dbg
#BOARD=SparkFun:avr:promicro
#BOARD=arduino:avr:leonardo
#BOARD=arduino:avr:mega
PORT=2

fonts.ino: $(FONTSRC)
	./mkfont $(FONTSRC) > fonts.ino

compile: $(FWSRC)
	arduino-cli compile -b $(BOARD)

upload:
	arduino-cli upload -b $(BOARD) -p /dev/ttyACM$(PORT) -t
