# Readerboard
An Electronic Readerboard 64x8 Sign with Discrete LEDs (Arduino-powered).
This project also includes the Busylight indicator device which used to be a separate project but
now shares a common command set and firmware source code base with the Readerboard devices.

## Description
This is mostly just a thing I put together for [hack value](http://www.catb.org/~esr/jargon/html/H/hack-value.html).

The project include source code for the firmware that runs on the Busylight and Readerboard units,
PCB fabrication files, schematics, and support programs. The latter includes a server which manages
multiple devices attached to a central computer. Requests may be made over the local network to the
server to request changes to what the units are displaying.

The construction and operation of the system, including the protocol used to communicate with the hardware
and the server, is fully documented.

### 64x8 RGB Readerboard
This implements a 64x8 LED matrix display with 8 status LEDs available alongside the main matrix display,
which is driven by an Arduino microcontroller board attached directly to the back of the PCB.

It accepts commands sent via direct USB connection or over RS-485 serial network. The latter can accommodate multiple
daisy-chained units.  A full system can address up to 63 devices, although multiple RS-485 and/or USB circuits
will be required to have that many devices active at once.

### 64x8 Monochrome Readerboard
You can assemble a readerboard using the same PCB as the RGB version, but using monochrome LEDs instead of
RGB ones. A firmare source switch and recompile is needed for the microcontroller to correctly drive the hardware
in this configuration.

### Busylight
The Busylight unit is a 7-LED status indicator with 360° field of visibility. These can be used, for example,
to indicate if someone is busy or available to visitors, or on the phone. Or it could indicate the status
of a process, room occupancy, etc. (The 8 LED status LEDs on the Readerboard units are intended to achieve the
same purpose and accept the same commands as the Busylight units do.)

## Current Status
I have a prototype Busylight and Readerboard built and being debugged.

## Copyright
This is all the original work of the author (Steve Willoughby). Copyright © 2023, 2024, All rights reserved. 
Portions based on prior work by the same author © 2008-2012. Released as open-source hardware and software 
under the terms and conditions of the BSD 3-Clause license.
