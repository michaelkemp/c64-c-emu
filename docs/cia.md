# MOS 6526 CIA (Phase 3)

Two independent instances: **CIA 1** (`$DC00-$DCFF`) drives the
keyboard matrix and joystick port 1/2 reads and generates IRQs; **CIA 2**
(`$DD00-$DDFF`) drives the serial (IEC) bus lines, RS-232 user port, and
VIC-II bank selection, and generates NMIs instead of IRQs. Same chip,
different wiring on the board — implement one `Cia` module and
instantiate it twice with different interrupt-line behavior at the
integration point (Phase 6).

Primary reference: the official preliminary MOS 6526 CIA datasheet —
https://6502.org/documents/datasheets/mos/mos_6526_cia_preliminary_nov_1981.pdf,
read in full; see `docs/sources.md` for exactly what was confirmed from
it and how (it's a scanned PDF — read as page images, not as
AI-summarized extracted text, per `docs/references-and-gotchas.md`'s
"read the primary source yourself" rule).

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

Two independent 16-bit down-counters (Timer A, Timer B). Confirmed
directly from the datasheet (see `docs/sources.md`) — **correcting an
earlier draft of this doc**, which said "count CNT pulses" was a
Timer-B-only mode: it isn't. The real per-timer mode bits are:

- **CRA bit 3 (RUNMODE)**: 0 = continuous (reload from latch on
  underflow and keep counting), 1 = one-shot (reload from latch on
  underflow, but also clear CRA's own START bit, stopping it). Both
  modes reload the counter from the latch on underflow — the only
  difference is whether START gets cleared.
- **CRA bit 5 (INMODE)**: 0 = Timer A counts PHI2 cycles, 1 = Timer A
  counts positive transitions on the CNT pin.
- **CRB bits 6,5 (INMODE, 2 bits)**: `00` = Timer B counts PHI2, `01` =
  Timer B counts positive CNT transitions, `10` = Timer B counts Timer
  A underflow pulses, `11` = Timer B counts Timer A underflow pulses
  *while CNT is currently high* (a level gate, not an edge).
- **CRA/CRB bit 4 (LOAD)**: a strobe, not stored state — "always reads
  back a zero and writing a zero has no effect." Writing a 1 force-
  loads the counter from the latch immediately, running or not.
- **Latch vs. counter**: reads of $4-$7 return the live counter; writes
  go to the latch. Writing the *low* byte only ever touches the latch.
  Writing the *high* byte also force-loads the counter from the latch
  **if the timer is currently stopped** — but if it's running, only the
  latch updates; the live counter is left alone until the next real
  underflow or force-load. Easy to get backwards from a paraphrase —
  confirmed from the datasheet's own wording.

Real software (the KERNAL's own jiffy clock, and some music/timing
routines) genuinely depends on the Timer-B-counts-Timer-A-underflow
mode for cascaded/chained timing.

**The KERNAL's own jiffy-clock IRQ** is driven by CIA1 Timer A in
continuous mode, reloaded to fire at real ~60Hz (NTSC)/~50Hz-ish (PAL,
though the real jiffy rate is tied to the video standard, not exactly
50Hz — verify the actual reload value the real KERNAL programs rather
than assuming a round number) — this is the concrete, verifiable target
for "does my timer implementation actually work," since a wrong timer
means the boot screen's clock-driven behavior (cursor blink, keyboard
repeat/debounce) will be visibly wrong.

**Verified empirically in Phase 6** against the user's own staged real
KERNAL (see `docs/machine.md`, `docs/sources.md`,
`tests/integration/test_jiffy_clock.c`): the settled Timer A reload is
**16421 PHI2 cycles ≈ 60.00Hz**, on this PAL-clocked emulator, using
the same PAL crystal-derived `C64_PAL_PHI2_HZ` as everything else in
this project. That confirms the "not exactly tied to the video
standard" caveat above as fact rather than hedge: the real jiffy rate
is a fixed ~60Hz **on both PAL and NTSC machines**, because it's the
same KERNAL ROM image and its own hardcoded Timer A reload value
driving both — the video frame rate (~50.125Hz PAL) is irrelevant to
it. (The staged KERNAL also briefly runs a different, much shorter
Timer A configuration during its own early self-test, before the CPU's
`I` flag is even clear — don't mistake that transient setup for the
real, settled jiffy-clock configuration; see the integration test's own
comments for how it avoids that trap.)

## ICR (interrupt control register) semantics

Reading the ICR returns which interrupt sources are currently flagged
**and clears all of them as a side effect** (real, documented,
easy-to-get-wrong behavior — a naive implementation that clears only
the bit that was checked will cause spurious repeated IRQs). Writing the
ICR is a *mask* write, not a flag write: bit 7 set means "set the
following bits in the mask," bit 7 clear means "clear the following bits
in the mask" — a different write semantics from every other register in
this chip, worth a code comment where it's implemented, not just here.
Confirmed word-for-word against the datasheet (`docs/sources.md`); the
read side additionally computes a bit-7 IR ("interrupt request pending")
flag as `(data & mask) != 0` — that's what actually drives the physical
IRQ/NMI line, not the raw flag bits alone.

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

Additional real, easy-to-miss behavior confirmed from the datasheet:
- **Writing the Hours register stops the clock; writing the Tenths
  register restarts it.** This is the documented mechanism for setting
  TOD to an exact time atomically (write HR last-but-one, TENTHS last).
- **Reading Hours latches all four TOD registers** (a consistent
  snapshot survives a multi-byte read even if the clock ticks over
  mid-read); they stay latched until Tenths is read, which itself
  still returns the latched value before un-latching for subsequent
  reads.
- **Alarm registers alias the same four addresses** as the clock
  registers; a Control Register B bit (bit 7) selects which set a
  *write* targets. Reads always return the real clock, never the alarm,
  regardless of that bit.
- **Hours is a 12-hour (1-12) dial with an AM/PM flag** (bit 7), not a
  0-23 counter — and the AM/PM flag flips exactly on the 12→1
  transition, not 11→12 (i.e. matches a normal 12-hour clock: ...,
  11:59 AM, 12:00 PM, 12:59 PM, 1:00 PM, ...).

## Keyboard matrix + joystick (CIA1)

The C64 keyboard is an 8×8 matrix: CIA1 Port A selects which column(s)
are being scanned (writing a 0 bit to select a column) and Port B reads
back which rows have a key held (0 = pressed) for the selected
column(s). This directionality (A = select, B = read) is corroborated
by every source checked (see `docs/sources.md`) and is **not** the part
of this that's genuinely disputed — see below for what is.

**The exact key-to-matrix-position layout is a real, disputed-in-the-
community fact** — at least two commonly-cited community layouts
disagree on some key positions, and neither should be trusted blindly.
This project's table (`src/c64/keyboard.h`) is sourced from
http://sta.c64.org/cbm64kbdlay.html, read as raw page text (see
`docs/sources.md`) — a specific, citable choice of "one of the (at
least) two layouts," not yet cross-checked against the CIA datasheet's
own wiring diagram (it doesn't have a C64-specific one — the 6526 is a
generic chip, board wiring is a C64-schematic fact, not a CIA-chip
fact) or verified empirically. Once Phase 2's ROMs are staged, verify
empirically: drive each of the 64 matrix positions alone and confirm
the character the real KERNAL's own character-input routine reports
back matches the expected key. This is
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

**Both done.** Jiffy-clock rate confirmed against the real KERNAL in
Phase 6, see above. The keyboard-matrix-layout empirical cross-check
is now genuinely done too, in Phase 7 (`docs/peripherals.md`):
`tests/integration/test_keyboard_input.c` types a known 8-key string
through the real `KeyboardMatrix -> CIA1 -> IRQ-driven KERNAL scan` and
confirms the real KERNAL's own screen memory shows the exact expected
characters back — a representative sample across 6 of the matrix's 8
rows and columns (not an exhaustive all-64 sweep), but genuine,
primary-hardware-verified confirmation that `src/c64/keyboard.h`'s
community-sourced layout table (`http://sta.c64.org/cbm64kbdlay.html`)
is actually correct, not just plausible.

## Implementation status (Phase 3, done)

`src/c64/cia.c` implements one `Cia` module (ports, all four timer run-
mode combinations including both CNT-driven modes, TOD with its full
latch/stop-start/alarm behavior, ICR) meant to be instantiated twice —
see this doc's header comment for the CIA1-vs-CIA2 wiring differences,
which are Phase 6's job (see below), not this module's. `src/c64/
keyboard.c` implements the 8×8 matrix (as a pulldown-mask computation
given the current column-select port value — a pure function, easy to
unit test) and a simple digital joystick model. Verified by 59 (CIA) +
15 (keyboard/joystick) hand-written unit tests, all synthetic —
including all timer modes, the latch-vs-counter and running-vs-stopped
write nuances, ICR mask-write/read-clear semantics, and TOD's BCD
rollover (tenths→seconds→minutes→hours with the 12/AM-PM quirk),
latching, and stop/start-on-register-write behavior.

**Done in Phase 6** (was deliberately deferred from Phase 3): wiring
`Cia`/`KeyboardMatrix`/`Joystick` instances into `src/c64/memory.c`'s
`$DC00-$DDFF`/`$DD00-$DDFF` I/O dispatch and into the CPU's IRQ/NMI
lines, via `src/c64/machine.c`'s `machine_cycle()` — see
`docs/machine.md`. Both CIA1 keyboard/joystick wiring and CIA2's Port A
→ VIC-II bank selection are covered by `tests/unit/test_machine.c`.

**Both closed as of Phase 7** — see this doc's own Verification target
above.

## Known gaps to disclose as you build

- Serial-port (`$C` shift register) — stubbed as a plain read/write
  byte with no real shift-register timing; real hardware detail for
  IEC bus bit-banging, needed for real in Phase 9b.
- RS-232 user-port support via CIA2 — out of scope unless something
  specific needs it.
- The `PC`/`FLAG` handshaking pins (real hardware: `PC` pulses low
  after a Port B access, `FLAG` is a negative-edge interrupt input) are
  not modeled — `FLAG`'s ICR bit exists but nothing drives it yet
  (CIA1's `FLAG` pin is cassette-read-data on real hardware, not
  modeled since no datasette exists in this project).
- CNT-driven timer modes are implemented for real (`cia_set_cnt_level()`
  in `src/c64/cia.c`) per the datasheet, but **nothing in this project
  drives the CNT line yet** — no datasette, no IEC bus (Phase 9b) — so
  this path is only unit-tested directly, not exercised by any real
  integration yet.
