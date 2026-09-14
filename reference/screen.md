# Screen / memory layout (real hardware facts, for writing programs)

See `reference/README.md` for how this differs from `docs/memory-map.md`
and `docs/vic-ii.md` (this emulator's own implementation). This file is
about the real default memory layout a BASIC program actually runs
inside of.

## Default layout (cold-start, unmodified)

| Range | Contents |
|---|---|
| `$0000`/`$0001` | 6510 I/O port direction/data -- controls ROM/RAM bank visibility, not screen-related, but the most-poked two bytes on the machine; leave alone unless you mean to bank-switch. |
| `$0277`-`$0280` | Keyboard buffer (10 bytes) -- the real KERNAL's IRQ-driven key scan pushes decoded characters here; `$C6` (198) holds the current count. Real "type-ahead" tricks POKE directly into this buffer + set `$C6`, letting BASIC's own input routine consume it as if actually typed. |
| `$0400`-`$07E7` | Default screen RAM, 1000 bytes (40x25 screen codes -- see `petscii.md`). |
| `$07E8`-`$07FF` | Unused tail of the screen-RAM page (24 bytes) -- conventionally repurposed for the 8 sprite-pointer bytes, `$07F8`-`$07FF` (2040-2047). |
| `$0801` | Default BASIC program start (`2049`) -- program text, then variables/arrays grow upward from wherever the program ends. |
| `$D800`-`$DBE7` | Color RAM -- a fixed 1000-byte overlay, **always** at this address regardless of bank-switching state (unlike the rest of the `$D000`-`$DFFF` I/O area). Only the low nibble of each byte is meaningful (4-bit color). |
| `$D000`-`$D3FF` | VIC-II registers (mirrored across the block). |
| `$D400`-`$D7FF` | SID registers (mirrored). |
| `$DC00`-`$DCFF` | CIA 1 (keyboard/joystick, Timer A/B, TOD). |
| `$DD00`-`$DDFF` | CIA 2 (serial bus, VIC-II bank select, Timer A/B, TOD, RS-232 lines). |

Sprite/character/screen data placed by a BASIC program for POKE-based
graphics work (sprite shapes, custom fonts) is conventionally put well
above the program+variables area to avoid collateral corruption as the
program grows -- `$3000` (12288) is a comfortable, commonly-used choice
for a modest-sized listing (`programs/basic/sprite_test.bas` and
`sprite_test2.bas` both use it).

## VIC-II bank selection

The VIC-II does not see the CPU's own bank-switched view -- it has its
own separate 16KB window into memory, selected by **CIA2 Port A bits
0-1** (`$DD00`, inverted logic): `11` = bank 0 (`$0000`-`$3FFF`, the
default), `10` = bank 1, `01` = bank 2, `00` = bank 3. Sprite data
pointers (`$07F8`-`$07FF`) and the screen/charset pointers in `$D018`
are addresses **relative to whichever bank is currently selected**, not
absolute addresses -- a POKE'd pointer value that looks right can
silently point at the wrong physical memory if something has changed
the VIC-II bank since. Everything in `programs/basic/` so far assumes
the real cold-start default (bank 0) and never touches `$DD00`.

## Border vs. background vs. color RAM

`$D020` (53280) = border color, `$D021` (53281) = background color 0
(the base background in standard text/bitmap mode; `$D022`-`$D024` add
backgrounds 1-3 for multicolor modes). These are whole-screen colors,
distinct from per-character-cell color RAM (`$D800`+) and distinct
again from per-sprite color (`$D027`+, see `sprites.md`).
