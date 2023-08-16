# Readerboard Firmware
The files in the `readerboard` sketch directory assume the Arduino Mega 2560 (because I think that has adequate power for this task but mostly because I have a drawer full of them looking for something to do).

The font bitmaps are specified in `*.font` files in an easy-to-edit format and then "compiled" into `*.ino` files using the `mkfont` script.
Just run `make` in the `readerboard` directory to rebuild the font files if needed.
