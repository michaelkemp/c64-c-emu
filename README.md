# c64-c-emu

A cycle-accurate Commodore 64 emulator in C — the real memory map
(PLA bank-switching), a **scanline-accurate** VIC-II (not a once-per-
frame snapshot), the SID sound chip, both CIA I/O chips, and, as a
stretch goal, true second-CPU emulation of a 1541 disk drive over an
emulated IEC serial bus. Target: boot the real KERNAL/BASIC ROMs and run
real commercial software, keeping up with the real PAL clock rate.

## Status

**Documentation and planning phase — no C code yet.** Every design
decision needed to start implementing is written down in `docs/`; read
`CLAUDE.md` first, then `docs/roadmap.md` for the phase-by-phase build
order. This section will be replaced with real build/run instructions
as each phase lands — see `docs/roadmap.md`'s Phase 1 for what's next.

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
license. If you own a real C64 (or a legitimate license to its ROMs),
`scripts/stage_roms.sh` copies your own dumps into a gitignored `roms/`
directory once it exists as a usable tool (Phase 2). See `CLAUDE.md`'s
license discipline section for the full reasoning.

## Documentation

- [`CLAUDE.md`](CLAUDE.md) — goals, key decisions, repo map, status.
- [`docs/roadmap.md`](docs/roadmap.md) — the phase-by-phase build plan;
  start here.
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

Not yet applicable — no build system exists yet. Phase 1 (see
`docs/roadmap.md`) is where the build system gets chosen and this
section gets filled in with real commands. **Keep this section current
as each phase lands** — a README describing an aspiration instead of
what actually works is worse than a short "not built yet" note.

## License

See [`LICENSE`](LICENSE). This covers this project's own code only —
it does not and cannot grant any rights to Commodore's copyrighted ROMs,
which are never included here regardless of license.
