# Hardware
This directory contains the hardware design and manufacturing files necessary to understand and create
the PC boards for the readerboard and busylight devices.

## Readerboard
In the `readerboard` subdirectory, there is:
 * Schematic capture files prepared with KiCAD: `readerboard.kicad_*`
 * PCB layout design file prepared with the open source PCB program: `readerboard.pcb`
 * A `fab` subdirectory containing archives of files ready to be sent to a fab to be turned into physical circuit boards:
    * `readerboard-3.3.0.zip` Revision 3.3.0, can be assembled as RGB or single-color.
 * A PDF file `readerboard.pdf` produced from the `readerboard.pcb` file which shows all the board layers and drill locations.
 * A PDF file `readerboard_schematic.pdf` produced from the KiCAD files which is a human-readable schematic diagram.

## Busylight
In the `busylight` subdirectory, there is:
 * Schematic capture files prepared with KiCAD: `busyight.kicad_*` 
 * PCB layout design files prepared with the open source PCB program: `busylight.pcb`, `lighttree.pcb`, `lighttree-smd.pcb`
    * The `-smd` version is a variant which uses surface-mount resistors instead of through-hole components.
 * A `fab` subdirectory containing archives of files ready to be sent to a fab to be turned into physical circuit boards:
    * `busylight-2.1.1.zip` Revision 2.1.1 Busylight shield for the SparkFun Pro Micro.
    * `lighttree-1.1.zip` Revision 1.1 light tree octagonal disc to hold 8 LEDs for a level of the busylight unit.
    * `lighttree-1.1-S.zip` As above, but the alternate version of the board which uses surface-mount components.
 * PDF files `busylight.pdf`, `lighttree.pdf`, and `lighttree-smd.pdf` produced from the PCB files which shows the board layers and drill locations.
 * A PDF file `busylight_schematic.pdf` produced from the KiCAD files which is a human-readable schematic diagram.
