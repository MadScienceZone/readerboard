# Readerboard + Busylight
An Electronic Readerboard 64x8 Sign with Discrete "busylight status" LEDs (Arduino-powered).
There is also a smaller, standalone Busylight unit which just provides a status indicator with a 360° view. This was
previously developed in its own dedicated [github project](https://github.com/MadScienceZone/busylight), but it is
now merged into this one.

## Description
This is mostly just a thing I put together for [hack value](http://www.catb.org/~esr/jargon/html/H/hack-value.html).

I had been experimenting for years with ideas for LED matrix readerboard design and
implementation, mostly focused on a 64x7 or 64x8 array of single-color LEDs.

Meanwhile, another weekend's idea to solve my need for an office availability
indicator resulted in the Busylight project, which consisted of a set of seven
360° rings of LEDs stacked in a column. By lighting different LED colors or
flashing them in different patterns, a number of statuses could be indicated
to anyone in viewing range.

In late 2024, when the readerboard project was at a point where I had upgraded
it to a full-color RGB LED design and I had a working prototype, it became
apparent that the busylight project should be merged with this one since the
readerboard includes a busylight-style status indicator, making it a superset
of the other device. Now they share a common firmware code base.

The project includes source code for the firmware that runs on the Busylight and Readerboard units,
PCB fabrication files, schematics, and support programs. The latter includes a server which manages
multiple devices attached to a central computer. Requests may be made over the local network to the
server to request changes to what the units are displaying.

The construction and operation of the system, including the protocol used to communicate with the hardware
and the server, is fully documented.

### 64x8 RGB Readerboard
The main project point is to
implement a 64x8 LED matrix display with 8 status LEDs available alongside the main matrix display,
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

My original used these colors:
* Green: interrupt at will.
* Yellow: interrupt if important (this is shown if I happen to have anything scheduled on my Google calendars, such as a meeting or just some time blocked out to focus on a project)
* Red: in a meeting (this is shown if I'm actually connected to a video conference meeting)
* Flashing red: in a meeting, and my microphone is unmuted
* Flashing blue/red: urgent status
* Strobing green: (along with other lights) low-priority meeting, be aware I'm on camera but interruptions are ok


#### Software
On the PC, the normal mode of operation is to run the `busylightd` daemon. This monitors a set of Google calendars and reports busy/free times with the green and yellow
lights. It also responds to external signals from other processes which can inform it of other status changes.

In my case, I used [Hammerspoon](https://www.hammerspoon.org) which provides extensible automation capabilities for MacOS systems, including a very handy plugin that detects
when you join or leave a Zoom call, and tracks the state of the mute controls while in the meeting. I just configured that to send the appropriate signals to the
*busylightd* process when those statuses changed.

The upshot of that is that I can leave this running, put busy time on my calendars, join Zoom calls, and so forth, while the light indicator automatically displays
the appropriate colors.

##### API Access
To get access to the Google calendar API, you'll need to register with Google and get an API key. If I were distributing this as a pre-made app, I'd include my API key
with the distribution, but as a DIY project, if you make one of these based on my design, you're essentially creating your own app anyway so it makes sense to get a
separate API key for yours.

##### Documentation
There's a *busylight(1)* manual page included which explains the setup and operation of the software.

A schematic of how I wired up the hardware is also included, although it's fairly trivial, and can be adjusted to suit your needs.

##### GUI Tool
A simple tcl/tk script is supplied which provides a convenient front-end to the *busylight* program, with the addition of task time tracking.

A *blight(1)* manual page is provided to explain it.

# Release notes
See the `CHANGELOG.md` file.
## Current Status
I have a prototype Busylight and Readerboard built and being debugged.

There's a lot of pre-release development work underway which makes it to the `develop` branch occasionally.
This is where you can find the prototypes and experimental software and firmware images before they're ready
for full release (in which time they will be moved to the `main` branch).

## Copyright
This is all the original work of the author (Steve Willoughby). Copyright © 2023, 2024, All rights reserved. 
Portions based on prior work by the same author © 2008-2012. Released as open-source hardware and software 
under the terms and conditions of the BSD 3-Clause license.
