# Roadmap

Build order matters here: each phase depends on real, working output
from the one before it (e.g. you can't verify the VIC-II renders the
real boot screen correctly until the memory map and CIA are both real).
Don't skip ahead. Each phase names the doc(s) it owes — write/update
those as part of the phase, not after.

## Phase 0 — Repo scaffold + docs (this pass)

- [x] `CLAUDE.md`, this roadmap, and every doc listed in `CLAUDE.md`'s
      repo map, written before any C code exists.
- [ ] Decide and document the build system in Phase 1 (see below) —
      deliberately not decided in Phase 0.
- [ ] Public GitHub repo created, this scaffold pushed as the first
      commit.

## Phase 1 — 6502 CPU core

Owes: `docs/6502-reference.md` (already written this pass — read it
before starting), `docs/testing-strategy.md`.

- [ ] Pick and document a build system as the *first* thing this phase
      does (CMake is the most portable default for a C project with a
      test suite and later an SDL2 dependency; a hand-written Makefile
      is fine too if you want zero build-system dependency — either is
      acceptable, just write the decision and the exact commands down in
      the README and `docs/testing-strategy.md` once chosen, and don't
      leave both half-set-up).
- [ ] Implement the legal 6502 instruction set: all official opcodes,
      all addressing modes, correct cycle counts, correct flag
      behavior (including the documented decimal-mode quirks).
- [ ] Hand-written unit tests for a representative sample of
      instructions/addressing modes/flag edge cases *before* running the
      full Dormann suite — the full suite tells you pass/fail at a trap
      address, not which specific instruction is wrong, so you want
      finer-grained tests you wrote yourself to localize a failure fast.
- [ ] `scripts/fetch_dormann_tests.sh` fetches Klaus Dormann's suite
      on demand (never vendored — see `CLAUDE.md`'s license discipline).
      Assemble it with an external assembler (`ca65`/`vasm`/etc. — this
      project does not need its own 6502 assembler; that's real, needed
      only if hand-assembling small test programs later becomes
      annoying, see `docs/testing-strategy.md`).
- [ ] The core passes the Dormann suite: traps at its documented success
      address (`$3469` for the standard `6502_functional_test.a65` build
      — verify this against the suite's own listing/comments once
      fetched, don't hardcode it from memory alone).
- [ ] Decide NOW whether the CPU core exposes a "step exactly one cycle"
      interface or a "step exactly one instruction" interface — Phase 4
      (scanline-accurate VIC-II) needs the former. Retrofitting
      cycle-level stepping onto an instruction-stepped core later is a
      real rewrite, not a small patch — see `docs/machine.md`'s "Cycle
      interleaving" section for exactly what's needed and why. Get this
      right in Phase 1.
- [ ] Undocumented/"illegal" opcodes: explicitly **deferred**, tracked
      as Phase 10. The stock KERNAL/BASIC ROM never executes one; only
      some commercial software (often deliberately, as copy protection)
      does. Don't build them now — note the deferral in
      `docs/6502-reference.md` instead of half-implementing them.

## Phase 2 — Memory map, PLA bank-switching, 6510 I/O port

Owes: `docs/memory-map.md` (already written this pass).

- [ ] Implement the real C64 address space: RAM, the switchable
      BASIC/KERNAL/Character ROM views, and I/O space, gated by the
      6510's `$00`/`$01` port (`LORAM`/`HIRAM`/`CHAREN`) exactly per the
      truth table in `docs/memory-map.md`.
- [ ] Model the 6510 I/O port as a memory-mapped device the bus
      dispatches to at `$00`/`$01` — **do not modify the CPU core** to
      know about banking; the whole point of Phase 1's clean separation
      is that the CPU only ever calls `bus_read8`/`bus_write8` and has
      no opinion about what's behind them.
- [ ] `scripts/stage_roms.sh` stages the user's own legally-acquired
      KERNAL/BASIC/Character ROM dumps into gitignored `roms/c64/`.
- [ ] Verification target: with real staged ROMs, the CPU reaches the
      genuine KERNAL reset vector and starts executing real ROM code —
      confirm by tracing PC through a few thousand real instructions and
      recognizing the KERNAL's own known reset-routine addresses/behavior
      (see `docs/memory-map.md`'s verification section), not just "it
      doesn't crash."

## Phase 3 — CIA 6526 (×2)

Owes: `docs/cia.md`.

- [ ] Ports A/B, both 16-bit timers (all four run modes: one-shot,
      continuous, count-CNT-pulses, count-underflow-of-the-other-timer),
      the TOD (time-of-day) clock, and the ICR (interrupt control
      register) with correct IRQ-line-OR-of-both-CIAs behavior.
- [ ] Keyboard matrix (8×8) coupled to CIA1's ports, and digital
      joystick coupled the same way — **the matrix layout is a real,
      disputed-in-the-community fact**: at least two commonly-cited
      layouts disagree on some key positions. Don't trust a single
      secondary source blindly; cross-check against the CIA/keyboard
      datasheet and, once Phase 2's ROMs are staged, verify empirically
      (drive each of the 64 positions in turn against the real KERNAL's
      own `GETIN`-equivalent routine and confirm the character that
      comes back matches what real hardware documents for that
      position).
- [ ] Verification target: booting the real KERNAL, both CIAs initialize
      exactly as their datasheet describes.

## Phase 4 — VIC-II, scanline-accurate from the start

Owes: `docs/vic-ii.md` (already written this pass — it lays out the
scanline architecture in detail; read it fully before starting).

This is the phase that most needs to not take a shortcut. Build the
renderer as a per-cycle (or, at coarsest, per-scanline) state machine
driven by the main loop's cycle interleaving from Phase 1/6's design,
**not** a "run N cycles, then synthesize a whole frame" function. See
`docs/vic-ii.md` for the concrete cycle budget (63 PAL cycles/line ×
312 lines/frame) and how badlines/sprite DMA steal cycles from the CPU.

- [ ] Standard character-mode text rendering first — verify against the
      real boot screen (`**** COMMODORE 64 BASIC V2 ****` etc.) from
      genuine staged ROM content.
- [ ] Multicolor and bitmap modes.
- [ ] Sprites: fetch/render/expansion/multicolor/priority, sprite-sprite
      and sprite-background collision, correctly clipped by the border
      (the border has strictly higher display priority than every
      sprite on real hardware — verify this specific fact against a
      primary source, e.g. Christian Bauer's VIC-II article, section on
      display priority, not a secondhand summary; this is a genuinely
      easy fact to get backwards from a paraphrase).
- [ ] Raster IRQ (`$D012`/`$D011` bit 7) firing at the exact real
      scanline, and mid-frame register changes (e.g. `$D018` character
      set swaps partway down the screen) actually taking effect at the
      right scanline instead of only at frame-render time — this is the
      concrete capability a frame-snapshot design cannot provide, and
      the reason this phase exists in this shape.
- [ ] Badlines modeled as a real, cycle-stealing condition (not just a
      queryable flag) — they must actually cost the CPU cycles when they
      happen, since real programs' timing-sensitive code depends on it.
- [ ] Verification target: a real 6502 test program (hand-assembled)
      that changes a register mid-frame (e.g. border color) and produces
      a visibly split-color frame when rendered scanline-by-scanline —
      the concrete test that a frame-snapshot design would fail.

## Phase 5 — SID

Owes: `docs/sid.md`.

- [ ] Three oscillators (triangle/sawtooth/pulse/noise), ADSR envelope
      generators (per the datasheet's documented rate tables), hard
      sync, ring modulation, and a filter.
- [ ] **Filter and combined-waveform fidelity**: reSID's own
      transistor-level modeling is the real state of the art here, but
      it's GPL — per `CLAUDE.md`'s license discipline, don't vendor its
      tables/logic. A simpler documented-approximation filter/combined-
      waveform model is an acceptable, disclosed gap; write it up as one
      in `docs/sid.md` rather than silently shipping something that
      merely sounds plausible.
- [ ] Verification target: a hand-assembled test program poking a known
      frequency (e.g. 440Hz) produces output measurably at that
      frequency (zero-crossing count or FFT peak against the rendered
      samples) — a concrete, falsifiable check, not "it makes a sound."

## Phase 6 — The real machine: cycle interleaving + IRQ/NMI

Owes: `docs/machine.md` (already written this pass).

- [ ] Wire CPU + Bus + both CIAs + VIC-II + SID together, driven by real
      elapsed PHI2 cycles, with correct IRQ (level-triggered, OR of both
      CIAs and the VIC-II raster interrupt) and NMI (edge-triggered)
      delivery.
- [ ] Real-time pacing strategy decided and implemented (see
      `docs/machine.md`'s "Real-time pacing" section — the recommended
      approach is letting the audio output device's own real playback
      rate be the master clock once Phase 7 exists, with a host
      high-resolution timer as the pre-audio fallback).
- [ ] Verification target: booting the real KERNAL+BASIC and watching
      its own real jiffy-clock counter (`$A0`-`$A2`) actually increment
      at the correct real-world rate once BASIC reaches its
      keyboard-wait loop.

## Phase 7 — Peripherals (SDL2): screen, keyboard, audio, joystick

Owes: `docs/peripherals.md` (already written this pass).

- [ ] Screen: blit `VicII`'s per-frame (or per-scanline, if you want to
      watch it draw live) pixel buffer to an SDL2 window/texture, paced
      to the real PAL rate (≈50.125Hz, not a hardcoded 50 — see
      `docs/peripherals.md` for why the exact rate matters).
- [ ] Keyboard: SDL2 key events mapped to the CIA keyboard matrix from
      Phase 3.
- [ ] Audio: SID samples fed to an SDL2 audio callback (pull-based, not
      push — see `docs/peripherals.md` for why this matters for pacing
      and for avoiding audible glitches).
- [ ] Joystick: SDL2 joystick/gamepad API, or a keyboard-key fallback
      (e.g. numpad), coupled to the joystick model from Phase 3.
- [ ] Verification target: the real boot screen renders in a live
      window, a typed `LOAD"$",8` + BASIC command round-trips through
      real keyboard events, and a test tone is audible at the correct
      pitch.

## Phase 8 — Cartridge (`.crt`) support

Owes: `docs/cartridge.md` (already written this pass).

- [ ] Generic/type-0 (plain static ROM, no bank-switching registers)
      only, first. The `.crt` container format itself is an openly
      published, non-proprietary format (header + CHIP packets) — safe
      to implement directly from its public specification.
- [ ] Extend the Phase 2 bus/PLA logic so EXROM/GAME join the same
      LORAM/HIRAM-gated logic, including the real ROML/ROMH asymmetry
      (verify the exact truth table from a primary technical reference
      before implementing — don't assume symmetry between the two).
- [ ] Ultimax mode (`GAME=0`, `EXROM=1`) as a documented follow-up if a
      real cartridge you're testing against needs it — not required for
      the initial cut.
- [ ] Every other real `.crt` hardware type (100+ exist, most needing
      their own bank-switching register emulation) is out of scope;
      raise `UnsupportedCartridge`-equivalent with a specific reason
      rather than silently misbehaving.

## Phase 9 — Disk: 1541 support

Owes: `docs/disk.md` (already written this pass — it lays out both
strategies and the real, disclosed tradeoff between them).

### Phase 9a — KERNAL-trap "fake" loading (build this first)

- [ ] Intercept the KERNAL's `LOAD`/`SAVE` jump-table entries (`$FFD5`/
      `$FFD8`), parse/write `.d64` images directly, hand back bytes as
      if a real load happened. No drive CPU, no VIA, no serial bus.
- [ ] `.d64` format: implement directly from the standard public
      reference (35-track layout, BAM, directory entries, file chains —
      see `docs/disk.md` for the exact layout table).
- [ ] Verification target: `LOAD"$",8` / `LOAD"PROGRAM",8,1` /
      `SAVE"PROGRAM",8` round-trip correctly against the real KERNAL/
      BASIC, including wildcard `LOAD` (`*`/`?`) per real documented DOS
      behavior.
- [ ] Known, disclosed limitation: cannot run software using a custom
      fastloader (bypasses the KERNAL's own loader entirely) — this is
      the whole reason Phase 9b exists as a stretch goal.

### Phase 9b — True 1541 drive emulation (stretch goal)

This is the other half of the reason this project exists in C instead
of staying in a slower environment — see `CLAUDE.md`'s opening
rationale. Comparable in scope to Phase 4's VIC-II rewrite; don't
start it until Phases 1-8 are solid.

- [ ] A VIA 6522 chip module (two instances) — similar scope to
      Phase 3's CIA, but a genuinely different chip's register/timer
      semantics; write its own reference notes as part of this phase
      (fold into `docs/disk.md` or split into `docs/via.md` — your
      call, but document it either way per the one-doc-per-chip rule).
- [ ] The 1541's own memory map (much simpler than the C64's — no
      bank-switching, just RAM + ROM + I/O).
- [ ] A second, independent 6502 CPU instance (the Phase 1 core is
      directly reusable as-is — it's chip-agnostic).
- [ ] The IEC serial bus protocol itself (ATN/CLK/DATA lines, bit-banged
      timing) — this is the fiddly, timing-sensitive part. Both CPUs
      (C64 and drive) must be cross-stepped cycle-by-cycle for this to
      work at all, which is exactly the interleaving Phase 6 already
      built the main loop around.
- [ ] A real 1541 DOS ROM, user-supplied via the same `stage_roms.sh`
      discipline as the C64 ROMs — never fetched/vendored by this
      project. (For the record: the real ROM's historical part numbers
      are 325302-01 + 901229-05, DOS 2.6, 16KB — useful for the user to
      know what to go looking for; that's a historical fact, not a
      license to redistribute it.)
- [ ] Verification target: a real fastloader-using disk game that hangs
      under Phase 9a now loads and runs correctly.

## Phase 10 — Undocumented/"illegal" 6502 opcodes

Deferred from Phase 1. Revisit once real commercial software you're
testing against actually needs one (copy-protected games and demoscene
code are the realistic trigger, per `docs/6502-reference.md`).

- [ ] Implement the reliable combined-operation illegal opcodes
      (`LAX`, `SAX`, `DCP`, `ISC`, `SLO`, `RLA`, `SRE`, `RRA`, `ANC`,
      `ALR`, `ARR`, `SBX`) and multi-byte NOPs — these have consistent,
      well-documented behavior across real chips.
- [ ] `JAM`/`KIL` opcodes: real hardware halts the CPU entirely — model
      this as a distinct "processor jammed" condition, not a crash and
      not a silent no-op.
- [ ] The chip-unstable opcodes (`ANE`/`XAA`, `LXA`, `LAS`, `SHA`, `SHX`,
      `SHY`, `TAS`) are a **permanent, disclosed gap** — their real
      behavior genuinely varies by chip revision/analog bus conditions,
      so there is no single "correct" answer to implement. Don't fake
      one; document the gap.
- [ ] Verification: hand-computed test vectors (the same style as
      Phase 1's own instruction tests) — no verified-license NMOS
      illegal-opcode test ROM is known to exist publicly; don't vendor
      an unverified one on the assumption it's "probably fine."

## Beyond here

No further phases are pre-planned past Phase 10 — decide the next
priority based on what real software you're actually trying to run once
the above is solid (candidates: REU/expansion-RAM support, more
cartridge hardware types, a second SID for stereo demos, savestate
support). Add a new phase section here with the same shape (owed docs,
concrete checklist, a verification target) rather than starting
untracked work.
