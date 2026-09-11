# c64-c-emu

## Goal

Build a cycle-accurate Commodore 64 emulator in C: the real memory map
(including PLA bank-switching), the VIC-II video chip rendered
**scanline-by-scanline** (not a once-per-frame snapshot), the SID sound
chip, both CIA I/O chips, and — as a stretch goal once the C64 side is
solid — a true second-CPU emulation of a 1541 disk drive (its own 6502,
VIA chips, and DOS ROM) talking to the C64 over an emulated IEC serial
bus. The target is a machine that boots the real KERNAL/BASIC ROMs and
runs real commercial software, keeping up with the real PAL clock rate
(≈985,248 Hz / ≈50.125 fps) without needing shortcuts.

**This file is the living source of truth**: goals, decisions, and
current status. Detail lives in [docs/](docs/); update both as decisions
change. If you are an AI assistant picking this project up cold, **read
every file in `docs/` before writing code** — they are written in
build order and each one is a design spec for its phase, not background
reading you can skip. Read `docs/references-and-gotchas.md` right after
the roadmap — it's the sharpest, most concrete "things that will bite
you" doc and is easy to under-value next to the more architectural
per-chip docs.

## Why C, and why scanline-accurate from the start

Two things a naive "get it booting" emulator tends to defer are: (1) a
VIC-II that renders scanline-by-scanline instead of once per frame
(needed for real raster-interrupt effects and mid-frame register
changes many real programs rely on — split-screen colors, "more than 8
sprites," raster bars, etc.), and (2) true second-CPU 1541 drive
emulation (needed for commercial software using a custom fastloader
instead of the stock KERNAL loader). Both require the *main loop* to
interleave multiple cycle-stepped state machines tightly — CPU cycle N,
then VIC-II reacts to cycle N, then (eventually) the drive's own CPU
cycle N — rather than "run the CPU for a while, then compute a whole
frame's video output in one shot." That interleaving is exactly where a
managed/interpreted language's per-step call overhead adds up fastest.
C removes that ceiling and gives predictable, jitter-free timing.

**Decision: design the VIC-II and the main loop for scanline/cycle
accuracy from Phase 1 of that chip's work (Phase 4 below), not as a
later upgrade.** This is the central bet this project is making that a
"just get it booting first" emulator usually doesn't. See
`docs/vic-ii.md` and `docs/machine.md` for exactly how.

## What this project is not

- **Not a C compiler or assembler project.** No custom toolchain work
  beyond what's needed to assemble small test programs (a bundled x86
  assembler like `ca65`/`vasm` used as an external tool, not built here).
- **Not aiming for 100% chip-fidelity from day one.** Known, disclosed
  simplifications are fine and expected — see each chip's own doc for
  its "Known gaps" section, following the convention below. What's not
  fine is an *undisclosed* gap.
- **Not going to vendor copyrighted ROM images or GPL reference-emulator
  source code.** See "License discipline" below — this is a hard rule,
  not a style preference.

## License discipline (read this before touching ROMs or reference emulators)

- **Commodore's KERNAL, BASIC, and Character ROMs are copyrighted.**
  The fact that open-source emulators (VICE, etc.) bundle them is
  **not proof of a verified redistribution license** — that claim
  should be treated as unverified lore, not fact, unless you personally
  find and read a primary source that says otherwise (a real statement
  from the rights holder, not a README). Default assumption: no such
  license exists. This project **never fetches or vendors the original
  ROMs**. `scripts/stage_roms.sh` only copies ROM dumps **the user
  already legitimately owns** from wherever they point it into a
  gitignored `roms/` directory — it never downloads anything itself.
  Same rule for the 1541's own DOS ROM once Phase 9b (true drive
  emulation) starts.
- **Reference emulators (VICE, reSID, etc.) are GPL.** Read their
  documentation and source *for understanding* — e.g. to settle a
  disputed hardware behavior question — but do not copy code or
  copy verbatim data tables out of them into this project. If a fact
  you need (a truth table, a rate table, a timing constant) can be
  sourced from a primary datasheet/reverse-engineering article instead,
  prefer that citation in the docs here.
- **Klaus Dormann's 6502 functional test suite** (used to validate the
  CPU core, see `docs/testing-strategy.md`) is genuinely open
  (GPLv3) and is fine to fetch on demand via `scripts/
  fetch_dormann_tests.sh` — never vendored into git, always fetched
  fresh, same discipline as the ROMs even though the license situation
  is different and clearer.
- If in doubt about whether something is safe to include: don't include
  it, and write down the open question in the relevant doc's "Known
  gaps" / open-questions section instead of guessing.

## Documentation convention: one reference doc per chip

Write (or update) each chip's own doc **as, or just before,** it's
implemented — not after the fact. Each should cover: the register map,
the chip's actual documented behavior (cite the datasheet/article
section), and any deliberate simplification or known gap versus real
hardware. A chip implementation with no doc explaining its register
semantics is exactly the kind of undocumented decision this convention
exists to avoid. Each phase in `docs/roadmap.md` names the specific
doc(s) it owes.

## Repo map (target — build it out as each phase lands)

```
CLAUDE.md              # this file
README.md              # goal + quickstart -- keep the build/run instructions
                        # in this file current as each phase lands
LICENSE
.gitignore
docs/
  roadmap.md            # phase-by-phase build plan -- start here
  references-and-gotchas.md  # read this second -- primary-source links and
                         # specific hard lessons sharper than the per-chip docs
  6502-reference.md     # 6502 ISA notes + the Dormann validation gate
  memory-map.md         # C64 memory map, PLA bank-switching, 6510 I/O port
  cia.md                # MOS 6526 CIA: ports, timers, TOD, ICR, keyboard/joystick
  vic-ii.md             # MOS 6567/6569 VIC-II, scanline-accurate from the start
  sid.md                # MOS 6581/8580 SID: oscillators, ADSR, filter
  machine.md            # the main loop: cycle interleaving, real-time pacing,
                         # IRQ/NMI delivery
  peripherals.md        # SDL2: screen, audio, keyboard, joystick
  cartridge.md          # .crt format, generic/type-0 first
  disk.md               # 1541: KERNAL-trap strategy first, true drive
                         # emulation (VIA + IEC bus) as the stretch goal
  testing-strategy.md   # how correctness gets validated at every phase,
                         # incl. the license-discipline rules above
  sources.md            # running log of external URLs actually fetched
                         # and read, and what each one settled -- keep
                         # this current whenever a new source gets used
scripts/
  fetch_dormann_tests.sh # fetches the GPLv3 Klaus Dormann suite on demand
  stage_roms.sh          # copies the user's own local ROM dumps into
                          # gitignored roms/ -- never downloads ROMs itself
src/
  bus.h                 # generic Bus interface every chip module dispatches through
  cpu/
    cpu6502.h            # the 6502/6510 core's public interface (Phase 1)
    cpu6502.c            # cycle-stepped implementation -- see docs/6502-reference.md
  c64/
    memory.h             # C64Memory: the real PLA-driven address space (Phase 2)
    memory.c             # implements docs/memory-map.md's truth table, as a Bus
    cia.h                # MOS 6526 CIA: ports, timers, TOD, ICR (Phase 3)
    cia.c                # sourced directly from the primary datasheet, see docs/sources.md
    keyboard.h            # 8x8 keyboard matrix + digital joystick (Phase 3)
    keyboard.c
    vic_ii.h              # MOS 6567/6569 VIC-II, PAL timing (Phase 4)
    vic_ii.c              # sourced directly from Bauer's cycle-by-cycle article, see docs/sources.md
    palette.h             # VIC-II's 16-color palette as RGB8 (a disclosed approximation, see docs/sources.md)
    palette.c
    sid.h                 # MOS 6581 SID: oscillators, ADSR, filter (Phase 5)
    sid.c                 # sourced directly from the primary datasheet, see docs/sources.md
tests/
  unit/                 # hand-written unit tests (CPU + memory map), no ROMs needed
  dormann/              # runs the fetched Dormann suite against the CPU core
  integration/          # real-ROM tier -- needs scripts/stage_roms.sh run first
  vendor/               # gitignored, populated by scripts/fetch_dormann_tests.sh only
tools/
  demos/                # ad-hoc smoke-test tools, NOT permanent deliverables -- see
                         # tools/demos/README.md. Built between Phases 4 and 5 to get
                         # a visual "does the VIC-II actually render anything" check
                         # before Phase 6/7 exist for real; `make demo` runs it.
roms/                   # gitignored, populated by stage_roms.sh only
```

Build system: a plain Makefile (see the README and
`docs/testing-strategy.md` for the exact `make` targets) -- decided in
Phase 1 once CMake turned out not to be installed on the machine this
was first built on; either was acceptable per the roadmap.

## Status

- [x] **Phase 0** — repo scaffold + full docs.
- [x] **Phase 1** — 6502 CPU core: all legal opcodes/addressing modes,
      cycle-stepped (`cpu6502_cycle()`, one PHI2 cycle at a time),
      passes both the hand-written unit tests and the full Dormann
      suite. Build system: a plain Makefile. See `src/cpu/`, `tests/`.
- [~] **Phase 2** — C64 memory map/PLA bank-switching/6510 I/O port:
      done and unit-tested (all 8 truth-table rows, RAM write-through,
      the I/O-vs-RAM write asymmetry). See `src/c64/memory.c`. The real-
      ROM boot verification (`tests/integration/test_boot.c`) is only
      partially done — no real ROM dump was available to verify against
      in the session that built it; see that file's own comment and
      `docs/memory-map.md`'s "Implementation status" section.
- [~] **Phase 3** — MOS 6526 CIA (×2) + keyboard matrix + joystick:
      done and unit-tested (74 assertions, all synthetic), sourced
      directly from the primary datasheet — see `src/c64/cia.c`,
      `src/c64/keyboard.c`, `docs/cia.md`, `docs/sources.md`. Not wired
      into the bus/CPU yet (deliberately deferred to Phase 6, see
      `docs/cia.md`'s status section) and not empirically verified
      against real hardware (blocked on real ROMs, same as Phase 2).
- [~] **Phase 4** — VIC-II (PAL/6569): done and unit-tested (24
      assertions, all synthetic), sourced directly from Christian
      Bauer's primary cycle-by-cycle article — see `src/c64/vic_ii.c`,
      `docs/vic-ii.md`, `docs/sources.md`. Real per-cycle timing/bus-
      access state (badlines costing the real 43 cycles, VC/RC, sprite
      DMA, raster IRQ, both border flip-flops) with per-pixel
      compositing — see `src/c64/vic_ii.h`'s header comment for the
      exact granularity decision and what it does/doesn't reproduce.
      Not wired into the bus/CPU/CIA2 bank-select yet (deliberately
      deferred to Phase 6) and not empirically verified against real
      hardware (blocked on real ROMs, same as Phases 2-3). **Visually
      smoke-tested**, though: `c64memory_attach_vic()` now lets
      `C64Memory` route `$D000-$D3FF` to a real `VicII`, and
      `tools/demos/` (`make demo`) runs a small self-contained 6502
      program (no ROMs needed) through the CPU+VIC-II and dumps the
      real rendered output as an image — see `tools/demos/README.md`.
      This is a deliberately early, partial slice of Phase 6/7, built
      because there was otherwise no way to see whether the VIC-II
      actually renders anything correct; it is not those phases
      themselves.
- [~] **Phase 5** — MOS 6581 SID: done and unit-tested (24 assertions),
      sourced directly from the primary datasheet — see `src/c64/sid.c`,
      `docs/sid.md`, `docs/sources.md`. All four oscillators, hard sync,
      ring mod, ADSR (linear decay/release ramp — a disclosed
      simplification of the real non-linear shape), and a simple
      documented-approximation filter (not reSID's transistor-level
      model). Verified against the datasheet's own Appendix A frequency
      table (440Hz test, landed exactly on target). Not wired into the
      bus/CPU or an audio device yet (deliberately deferred to Phase 6/7,
      same pattern as the CIA/VIC-II).
- [ ] Everything else — see `docs/roadmap.md`.

## Running tests

Not applicable yet — no code exists. `docs/testing-strategy.md` and
`docs/roadmap.md` describe what the first test suite (the CPU core, in
Phase 1) needs to do before this section can be filled in for real.
