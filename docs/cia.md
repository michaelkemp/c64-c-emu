# MOS 6526 CIA (Phase 3)

Two independent instances: **CIA 1** (`$DC00-$DCFF`) drives the
keyboard matrix and joystick port 1/2 reads and generates IRQs; **CIA 2**
(`$DD00-$DDFF`) drives the serial (IEC) bus lines, RS-232 user port, and
VIC-II bank selection, and generates NMIs instead of IRQs. Same chip,
different wiring on the board — implement one `Cia` module and
instantiate it twice with different interrupt-line behavior at the
integration point (Phase 6).

Primary reference: the official preliminary MOS 6526 CIA datasheet
(widely available from 6502-hardware-reference archives — cite the
specific register/section when you pull a behavior from it).

## Register map (offsets within each CIA's 256-byte range; low byte only actually decodes, mirrored 16x — real hardware detail, safe to model or to ignore and just use the low 4 bits)

| Offset | Register |
|---|---|
| `$0` | Port A data |
| `$1` | Port B data |
| `$2` | Port A data direction |
| `$3` | Port B data direction |
| `$4`/`$5` | Timer A low/high |
| `$6`/`$7` | Timer B low/high |
| `$8`-`$B` | TOD clock: tenths, seconds, minutes, hours (hour byte's bit 7 = AM/PM) |
| `$C` | Serial shift register |
| `$D` | Interrupt control register (ICR) |
| `$E` | Control register A (timer A start/mode/etc.) |
| `$F` | Control register B (timer B start/mode/etc., incl. TOD-alarm-set bit) |

## Timers

Two independent 16-bit down-counters (Timer A, Timer B), each with four
real run modes selected by its control register:
1. One-shot: counts down once, stops, sets its ICR flag.
2. Continuous: reloads from its latch and keeps counting.
3. Timer B only: count CNT pin pulses instead of PHI2 cycles (real
   hardware detail, low priority — safe to stub if nothing exercises it
   yet, but document the stub).
4. Timer B only: count Timer A underflows instead of PHI2 cycles (used
   for chained/cascaded timing — real software, including the KERNAL's
   own jiffy clock and some music routines, can depend on this).

**The KERNAL's own jiffy-clock IRQ** is driven by CIA1 Timer A in
continuous mode, reloaded to fire at real ~60Hz (NTSC)/~50Hz-ish (PAL,
though the real jiffy rate is tied to the video standard, not exactly
50Hz — verify the actual reload value the real KERNAL programs rather
than assuming a round number) — this is the concrete, verifiable target
for "does my timer implementation actually work," since a wrong timer
means the boot screen's clock-driven behavior (cursor blink, keyboard
repeat/debounce) will be visibly wrong.

## ICR (interrupt control register) semantics

Reading the ICR returns which interrupt sources are currently flagged
**and clears all of them as a side effect** (real, documented,
easy-to-get-wrong behavior — a naive implementation that clears only
the bit that was checked will cause spurious repeated IRQs). Writing the
ICR is a *mask* write, not a flag write: bit 7 set means "set the
following bits in the mask," bit 7 clear means "clear the following bits
in the mask" — a different write semantics from every other register in
this chip, worth a code comment where it's implemented, not just here.

## TOD (time-of-day) clock

A real, independently-running clock, distinct from the timers, advancing
in real elapsed time regardless of anything else the CPU does. Drive it
from the same "real elapsed PHI2 cycles" value the main loop already
threads through for the timers (Phase 6) — **use integer arithmetic**,
not a float accumulator: `PAL_CLOCK_HZ` doesn't divide evenly into
common sub-second tick rates, and a float accumulator subtracting a
non-exact value repeatedly will drift and eventually fire a tick early
or late at an exact time boundary. An all-integer "accumulate
cycles×10, subtract PAL_CLOCK_HZ per tenth-tick" style approach avoids
this — verify with a test that asserts an *exact* one-second boundary
produces *exactly* the expected number of tenth-ticks, not "approximately
right," since that's precisely the class of bug an inexact accumulator
produces.

## Keyboard matrix + joystick (CIA1)

The C64 keyboard is an 8×8 matrix: CIA1 Port A selects which column(s)
are being scanned (writing a 0 bit to select a column) and Port B reads
back which rows have a key held (0 = pressed) for the selected
column(s), or vice versa depending on which port the KERNAL's scan
routine is currently configured to drive — get the actual direction
(which port is "select," which is "read back") from the datasheet/KERNAL
disassembly rather than assuming.

**The exact key-to-matrix-position layout is a real, disputed-in-the-
community fact** — at least two commonly-cited community layouts
disagree on some key positions, and neither should be trusted blindly.
Cross-check against the datasheet's own wiring diagram, and once
Phase 2's ROMs are staged, verify empirically: drive each of the 64
matrix positions alone and confirm the character the real KERNAL's own
character-input routine reports back matches the expected key. This is
worth the effort — a wrong layout produces a keyboard that *looks*
plausible (most keys land somewhere reasonable) but is subtly wrong in
ways that are maddening to debug later from symptoms alone.

Digital joysticks (in port 1 and/or port 2 — real hardware convention:
port 2 is CIA1 Port A, port 1 is CIA1 Port B, and single-joystick
software conventionally expects port 2) pull the same port bits low for
up/down/left/right/fire, active-low, diagonals asserting two direction
bits simultaneously exactly like real microswitches. If keyboard keys
are used to simulate a joystick (e.g. numpad), reference-count each
direction bit across overlapping held keys rather than a plain boolean,
so releasing one key doesn't clobber a direction another held key still
wants asserted.

## Verification target

Booting the real staged KERNAL: both CIAs initialize their timers/ICR
exactly as the datasheet describes (trace and confirm the real init
writes happen), the jiffy clock counter (`$A0`-`$A2` in zero page)
advances at the correct real-world rate once BASIC reaches its
keyboard-wait loop, and a synthetic keypress on each of the 64 matrix
positions produces the real KERNAL's own correct character back.

## Known gaps to disclose as you build

- Serial-port (`$C` shift register) — real hardware detail for IEC bus
  bit-banging; can stub until Phase 9b needs it for real.
- RS-232 user-port support via CIA2 — out of scope unless something
  specific needs it.
- CNT-pin-driven timer modes — likely safe to stub; document if you do.
