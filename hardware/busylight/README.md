# Busylight
This device is the author's original "busylight" project which gives a visual indicator of when he is on a video call, has an open microphone, or is otherwise
busy, so those nearby are aware. It has many other uses, such as indicating the status of long-running processes, occupancy status of rooms, etc.

It consists of a main board to which a SparkFun Pro Micro controller board is attached, and a set of "light tree" boards which make up the LED display itself.

## Directory Layout
The schematics and PCB layout files (in Kicad and gEDA PCB formats respectively) are in the main directory. The `fab` subdirectory contains zip archives
of the gerber files needed to have the actual printed circuit boards manufactured.

Two sets of light tree PCB files are provided. The ones with `-smt` added to their name use surface-mount resistors while the others use through-hole SIP resistor array packages.
Both are set up to use 3mm or 5mm through-hole LEDs, but these are actually mounted perpendicular to the board edges, with the leads soldered to the top and bottom board surfaces.
