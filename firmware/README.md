# Readerboard Firmware
The files in the `readerboard` sketch directory assume the Arduino Mega 2560 or Due (because I think that has adequate power for this task but mostly because I have a drawer full of them looking for something to do). My prototype that I actually built is based on the Due. I intend to make another and test it with a 2560 as well.

The font bitmaps are specified in `*.font` files in an easy-to-edit format and then "compiled" into `*.ino` files using the `mkfont` script.
Just run `make` in the `readerboard` directory to rebuild the font files if needed.
