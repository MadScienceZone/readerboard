# Readerboard Hardware Files
The rev 3 board includes:
 * Consolidation of Arduino shield with the main board (the Arduino now mounts directly to the back of the display board).
 * Upgrade from single-color LEDs to RGB.

The rev 2 board set included:
 * The addition of a half-duplex RS-485 transceiver to the shield board.
 * A shrink of the matrix display board for more economical manufacturing and reduced finished product footprint.
 * Replacement of obsolete shift register chip with newer version.

## Schematic Capture Files
 * Prepared in kicad `matrix64x8/matrix64x8_rgb.kicad_sch` 
 * `matrix64x8_rgb_300_schematic.pdf` PDF file of schematic.

## Ready-for-fab PCB Gerber Files
 * Prepared using gEDA PCB (because I only just started using kicad after I already made these):
   * `matrix64x8_300.zip` Display board v3.2.0 (the prototype board I'm using for development)
   * `matrix64x8_rgb_300.pdf` Illustration of foil patterns, drilling, and component placement.
