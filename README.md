# Readerboard + Busylight
An Electronic Readerboard 64x8 Sign with Discrete "busylight status" LEDs (Arduino-powered).
There is also a smaller, standalone Busylight unit which just provides a status indicator with a 360° view. This was
previously developed in its own dedicated [github project](https://github.com/MadScienceZone/busylight), but it is
now merged into this one.

## Description
This is mostly just a thing I put together for [hack value](http://www.catb.org/~esr/jargon/html/H/hack-value.html).
This includes the desgin specs for a 64x8 LED matrix display with 8 status leds available under the main matrix display,
a shield board to interface the display board to an Arduino Mega 2560 or equivalent, and the necessary Arduino firmware
to run the display using simple text commands sent to it over the USB port. A sample client program which can manipulate
the display is also included.

## Current Status
I'm still developing the firmware and have a prototype set of PC boards back from the fab which I'm building up.

There's a lot of pre-release development work underway which makes it to the `develop` branch occasionally.
This is where you can find the prototypes and experimental software and firmware images before they're ready
for full release (in which time they will be moved to the `main` branch).

## Copyright
This is all the original work of the author (Steve Willoughby). Copyright © 2023-2024, all rights reserved. Based, at least in principle,
on prior work by the same author © 2008-2012. Released as open-source hardware and software under the terms and conditions of the 
BSD 3-Clause license.
