# c64-c-emu

A cycle-accurate Commodore 64 emulator in C — the real memory map
(PLA bank-switching), a **scanline-accurate** VIC-II (not a once-per-
frame snapshot), the SID sound chip, both CIA I/O chips, and, as a
stretch goal, true second-CPU emulation of a 1541 disk drive over an
emulated IEC serial bus. Target: boot the real KERNAL/BASIC ROMs and run
real commercial software, keeping up with the real PAL clock rate.

## Status

**Phases 1 (6502 CPU core) and 2 (memory map/PLA bank-switching) are
done; Phase 3 (CIA 6526) is next.** Read `CLAUDE.md` first, then
`docs/roadmap.md` for the phase-by-phase build order.

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

## Building and running

Build system: a plain Makefile (gcc/clang, no other dependency). There's
no full machine to run yet — Phases 1-2 built the 6502 CPU core and the
real C64 memory map/PLA bank-switching, with tests for each.

```sh
make            # builds and runs the hand-written unit test suite
make unit-test  # same (CPU + memory map, no ROMs needed)

make fetch-dormann  # fetches Klaus Dormann's 6502 functional test suite
                    # on demand (GPLv3, never vendored into this repo)
make dormann        # builds and runs it against the CPU core

make integration    # real-ROM tier -- needs scripts/stage_roms.sh run
                    # first with your own dumps; SKIPs (not a failure)
                    # if they aren't staged
```

All three currently pass (or, for `integration`, SKIP cleanly with no
ROMs staged): 110 hand-written unit-test assertions (70 CPU + 40 memory
map, including all 8 rows of the bank-switching truth table), and the
full Dormann suite (traps at its documented success address, `$3469`,
after 96,241,367 cycles). See `docs/6502-reference.md`,
`docs/memory-map.md`, and `docs/testing-strategy.md` for details.

**Keep this section current as each phase lands** — a README describing
an aspiration instead of what actually works is worse than a short "not
built yet" note.

## License

See [`LICENSE`](LICENSE). This covers this project's own code only —
it does not and cannot grant any rights to Commodore's copyrighted ROMs,
which are never included here regardless of license.
