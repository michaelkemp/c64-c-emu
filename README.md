# c64-c-emu

A cycle-accurate Commodore 64 emulator in C — the real memory map
(PLA bank-switching), a **scanline-accurate** VIC-II (not a once-per-
frame snapshot), the SID sound chip, both CIA I/O chips, and, as a
stretch goal, true second-CPU emulation of a 1541 disk drive over an
emulated IEC serial bus. Target: boot the real KERNAL/BASIC ROMs and run
real commercial software, keeping up with the real PAL clock rate.

## Status

**Phases 1-5 are done** (6502 CPU core; C64 memory map/PLA bank-
switching; the MOS 6526 CIA ×2, keyboard matrix, and joystick;
PAL VIC-II; the MOS 6581 SID) — **Phase 6 (the real machine loop) is
next.** Read `CLAUDE.md` first, then `docs/roadmap.md` for the phase-
by-phase build order. Note: Phases 2-4 each have a real-ROM
verification step blocked on the user staging their own ROM dumps (see
`docs/memory-map.md`'s, `docs/cia.md`'s, and `docs/vic-ii.md`'s status
sections), and none of the built chips are wired together into one
running machine yet — that's Phase 6 (VIC-II is a partial exception:
see `tools/demos/`).

## Why this project exists

Real C64 software depends on two things a lot of "get it booting"
emulators defer: video effects that depend on VIC-II registers changing
*mid-frame* (raster bars, split-screen colors), and disk games that use
a custom fastloader instead of the stock KERNAL loader. Both require
tight, cycle-level interleaving between multiple state machines (CPU +
video chip, and eventually two independent CPUs) rather than "run some
cycles, then compute a whole frame/file transfer in one shot." This
project is built around that interleaving from the start — see
`CLAUDE.md` for the full reasoning.

## Getting the ROMs

This project **never downloads or bundles** Commodore's KERNAL, BASIC,
or Character ROMs (or, later, a 1541 DOS ROM) — they're copyrighted, and
a reference emulator bundling them isn't proof of a redistribution
license. If you own a real C64 (or a legitimate license to its ROMs):

```sh
scripts/stage_roms.sh --kernal /path/to/kernal.bin \
                       --basic  /path/to/basic.bin \
                       --chargen /path/to/chargen.bin
make integration   # then: the real Phase 2 boot verification target
```

See `CLAUDE.md`'s license discipline section for the full reasoning.
No real ROMs have been staged or used in developing this project itself
— `make integration` prints a clear `SKIP` instead of failing when
they're absent, which is the default state of a fresh clone.

## Documentation

- [`CLAUDE.md`](CLAUDE.md) — goals, key decisions, repo map, status.
- [`docs/roadmap.md`](docs/roadmap.md) — the phase-by-phase build plan;
  start here.
- [`docs/references-and-gotchas.md`](docs/references-and-gotchas.md) —
  read this second: primary-source links (VIC-II article, datasheets,
  format references) and specific, hard-won gotchas.
- [`docs/6502-reference.md`](docs/6502-reference.md),
  [`docs/memory-map.md`](docs/memory-map.md),
  [`docs/cia.md`](docs/cia.md),
  [`docs/vic-ii.md`](docs/vic-ii.md),
  [`docs/sid.md`](docs/sid.md),
  [`docs/machine.md`](docs/machine.md),
  [`docs/peripherals.md`](docs/peripherals.md),
  [`docs/cartridge.md`](docs/cartridge.md),
  [`docs/disk.md`](docs/disk.md) — one reference doc per chip/subsystem,
  written as (or just before) it's implemented.
- [`docs/testing-strategy.md`](docs/testing-strategy.md) — how
  correctness gets validated at every phase, and the full license-
  discipline rules for ROMs and reference-emulator source.
- [`docs/sources.md`](docs/sources.md) — a running log of external
  documents/URLs actually fetched and read during development, and what
  each one settled — kept up to date as new sources get used.

## Building and running

Build system: a plain Makefile (gcc/clang, no other dependency; links
`-lm` for the SID's filter). There's no full machine to run yet —
Phases 1-5 built the 6502 CPU core, the real C64 memory map/PLA bank-
switching, the MOS 6526 CIA (×2) + keyboard matrix + joystick, the PAL
VIC-II, and the MOS 6581 SID, with tests for each. None of the chips
are wired into the bus/CPU together yet — that's Phase 6.

```sh
make            # builds and runs the hand-written unit test suite
make unit-test  # same (CPU + memory map + CIA + keyboard + VIC-II + SID, no ROMs needed)

make fetch-dormann  # fetches Klaus Dormann's 6502 functional test suite
                    # on demand (GPLv3, never vendored into this repo)
make dormann        # builds and runs it against the CPU core

make integration    # real-ROM tier -- needs scripts/stage_roms.sh run
                    # first with your own dumps; SKIPs (not a failure)
                    # if they aren't staged

make demo           # ad-hoc visual smoke test (needs ca65/ld65 from the
                    # cc65 suite) -- runs a small self-contained 6502
                    # program (no ROMs needed) through the CPU+VIC-II
                    # and dumps the real rendered output as an image;
                    # see tools/demos/README.md
```

All three test tiers currently pass (or, for `integration`, SKIP
cleanly with no ROMs staged): 232 hand-written unit-test assertions
(70 CPU + 40 memory map + 59 CIA + 15 keyboard/joystick + 24 VIC-II +
24 SID), and the full Dormann suite (traps at its documented success
address, `$3469`, after 96,241,367 cycles). See
`docs/6502-reference.md`, `docs/memory-map.md`, `docs/cia.md`,
`docs/vic-ii.md`, `docs/sid.md`, and `docs/testing-strategy.md` for
details.

`make demo` is the fastest way to actually *see* something: it renders
real "HELLO C64" text plus a raster-split border effect, entirely
through the Phase 1-4 modules built so far (see
`tools/demos/hello_c64_screenshot.png` for a checked-in reference of
correct output).

**Keep this section current as each phase lands** — a README describing
an aspiration instead of what actually works is worse than a short "not
built yet" note.

## License

See [`LICENSE`](LICENSE). This covers this project's own code only —
it does not and cannot grant any rights to Commodore's copyrighted ROMs,
which are never included here regardless of license.
