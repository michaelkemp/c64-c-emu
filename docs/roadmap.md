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

- [x] Pick and document a build system as the *first* thing this phase
      does — **decided: a plain Makefile**, not CMake, since CMake
      wasn't installed on the machine this was first built on and the
      project only needs gcc/clang; see the README and
      `docs/testing-strategy.md` for the exact commands.
- [x] Implement the legal 6502 instruction set: all official opcodes,
      all addressing modes, correct cycle counts, correct flag
      behavior (including the documented decimal-mode quirks). See
      `src/cpu/cpu6502.c`.
- [x] Hand-written unit tests for a representative sample of
      instructions/addressing modes/flag edge cases *before* running the
      full Dormann suite. See `tests/unit/test_cpu.c` (70 assertions,
      covering load/store, indexed-addressing page-crossing cycle
      counts, RMW, binary and decimal ADC/SBC, branches, `JMP`
      indirect's page-boundary bug, `JSR`/`RTS`, `PHA`/`PHP`/`PLA`/`PLP`,
      `BRK`/`RTI`, and the IRQ/NMI polling/delay behavior below).
- [x] `scripts/fetch_dormann_tests.sh` fetches Klaus Dormann's suite
      on demand (never vendored — see `CLAUDE.md`'s license discipline).
      In practice the fetched copy already ships a prebuilt flat-64KB
      `bin_files/6502_functional_test.bin` (built by the suite's own
      author) — used directly rather than reassembling with `ca65`/
      `vasm`, since it's equally "fetched, not vendored." Assembling by
      hand is still the fallback if a future fetch doesn't include one.
- [x] The core passes the Dormann suite: traps at its documented success
      address — confirmed as `$3469` by reading the fetched copy's own
      `bin_files/6502_functional_test.lst` listing directly (its
      `success` macro expands to `jmp *` at that exact address), not
      trusted from memory. `make dormann` runs it; last run trapped
      there after 96,241,367 cycles.
- [x] Decide NOW whether the CPU core exposes a "step exactly one cycle"
      interface or a "step exactly one instruction" interface — Phase 4
      (scanline-accurate VIC-II) needs the former. **Decided: per-cycle**
      — `cpu6502_cycle()` executes exactly one PHI2 cycle via an
      explicit micro-op state machine (`cpu->step` within the current
      opcode), never a whole instruction at once. See
      `docs/6502-reference.md` and `docs/machine.md`.
- [x] Undocumented/"illegal" opcodes: explicitly **deferred**, tracked
      as Phase 10. The stock KERNAL/BASIC ROM never executes one; only
      some commercial software (often deliberately, as copy protection)
      does. The core doesn't crash or silently treat one as a NOP —
      `cpu6502_cycle()` sets `illegal_opcode_hit`/`last_illegal_opcode`
      so a caller can detect and report it (see `docs/6502-reference.md`).

## Phase 2 — Memory map, PLA bank-switching, 6510 I/O port

Owes: `docs/memory-map.md` (already written this pass).

- [x] Implement the real C64 address space: RAM, the switchable
      BASIC/KERNAL/Character ROM views, and I/O space, gated by the
      6510's `$00`/`$01` port (`LORAM`/`HIRAM`/`CHAREN`) exactly per the
      truth table in `docs/memory-map.md`. See `src/c64/memory.c`.
- [x] Model the 6510 I/O port as a memory-mapped device the bus
      dispatches to at `$00`/`$01` — **do not modify the CPU core** to
      know about banking. Confirmed: `src/cpu/cpu6502.c` is untouched;
      `C64Memory` is just another `Bus` implementation.
- [x] `scripts/stage_roms.sh` stages the user's own legally-acquired
      KERNAL/BASIC/Character ROM dumps into gitignored `roms/c64/`
      (already existed from Phase 0; `c64memory_load_kernal/basic/
      chargen()` now consume what it stages).
- [x] Verification target: with real staged ROMs, the CPU reaches the
      genuine KERNAL reset vector and starts executing real ROM code.
      **Done**: `tests/integration/test_boot.c` (`make integration`)
      loads real ROMs, resets the CPU through the genuine vector
      (observed: `$FCE2`), runs 3,000,000 real cycles, and asserts the
      real "READY." screen-code sequence actually appears in screen
      memory — confirming the genuine KERNAL/BASIC cold-start completed,
      not just "didn't crash." `tools/demos/real_rom_boot_dump.c`
      (`make demo-real-rom`) independently confirms this visually,
      rendering the real boot screen. The exhaustive bank-switching
      truth table itself (all 8 LORAM/HIRAM/CHAREN combinations) is
      separately, fully verified with synthetic ROM content in
      `tests/unit/test_memory.c`, needing no real ROMs.

## Phase 3 — CIA 6526 (×2)

Owes: `docs/cia.md`.

- [x] Ports A/B, both 16-bit timers (all four run modes: one-shot,
      continuous, count-CNT-pulses, count-underflow-of-the-other-timer),
      the TOD (time-of-day) clock, and the ICR (interrupt control
      register). See `src/c64/cia.c`, sourced directly from the primary
      datasheet (`docs/sources.md`) — which also corrected this
      checklist's own "count-CNT-pulses" framing: that mode exists for
      **both** timers, not Timer B only (see `docs/cia.md`). The
      IRQ-line-OR-of-both-CIAs behavior itself is Phase 6's job
      (`cia_irq_asserted()` exists; nothing wires it to the CPU's IRQ
      line yet).
- [x] Keyboard matrix (8×8) coupled to CIA1's ports, and digital
      joystick coupled the same way — **the matrix layout is a real,
      disputed-in-the-community fact**: at least two commonly-cited
      layouts disagree on some key positions. See `src/c64/keyboard.c`
      and `docs/cia.md`: this project's table is sourced from a specific
      citable reference (`docs/sources.md`), **not yet cross-checked
      empirically** (see below).
- [~] Verification target: booting the real KERNAL, both CIAs initialize
      exactly as their datasheet describes, the keyboard matrix verified
      empirically against the real KERNAL's own character-input
      routine. **Blocked**: no real ROMs available in the session that
      built this (same blocker as Phase 2's `tests/integration/
      test_boot.c`) — the `Cia`/`KeyboardMatrix`/`Joystick` modules
      themselves are done and unit-tested (74 assertions total,
      synthetic), but neither wired into `src/c64/memory.c`'s I/O
      dispatch (deliberately deferred to Phase 6, see `docs/cia.md`'s
      status section) nor verified against real hardware/ROM behavior.

## Phase 4 — VIC-II, scanline-accurate from the start

Owes: `docs/vic-ii.md` (already written this pass — it lays out the
scanline architecture in detail; read it fully before starting).

This is the phase that most needs to not take a shortcut. Build the
renderer as a per-cycle (or, at coarsest, per-scanline) state machine
driven by the main loop's cycle interleaving from Phase 1/6's design,
**not** a "run N cycles, then synthesize a whole frame" function. See
`docs/vic-ii.md` for the concrete cycle budget (63 PAL cycles/line ×
312 lines/frame) and how badlines/sprite DMA steal cycles from the CPU.

- [x] Standard character-mode text rendering first — verify against the
      real boot screen (`**** COMMODORE 64 BASIC V2 ****` etc.) from
      genuine staged ROM content. **Done**: `make demo-real-rom`
      (`tools/demos/real_rom_boot_dump.c`) genuinely renders the real
      boot screen from real KERNAL/BASIC/Character ROM content,
      including visibly exhibiting the documented "first three
      c-accesses read forced `$FF`" DMA-delay quirk as a real, expected
      artifact — see `docs/vic-ii.md`'s Verification targets section.
      Synthetic screen/charset content in `tests/unit/test_vic_ii.c`
      remains the base, ROM-free test coverage.
- [x] Multicolor and bitmap modes. See `src/c64/vic_ii.c`'s
      `render_pixels_from_byte()`, sourced directly from the primary
      article (`docs/sources.md`).
- [x] Sprites: fetch/render/expansion/multicolor/priority, sprite-sprite
      and sprite-background collision, correctly clipped by the border
      (the border has strictly higher display priority than every
      sprite on real hardware — verify this specific fact against a
      primary source, e.g. Christian Bauer's VIC-II article, section on
      display priority, not a secondhand summary; this is a genuinely
      easy fact to get backwards from a paraphrase). Confirmed directly
      from the article and enforced structurally (sprite compositing
      runs against a background/border buffer that already has the
      border color written in, and never overwrites it) — see
      `docs/vic-ii.md`'s Sprites section for what's deliberately not
      modeled (rule 7a; the rare mixed-`MxDP` multi-sprite interaction).
- [x] Raster IRQ (`$D012`/`$D011` bit 7) firing at the exact real
      scanline, and mid-frame register changes (e.g. `$D018` character
      set swaps partway down the screen) actually taking effect at the
      right scanline instead of only at frame-render time — this is the
      concrete capability a frame-snapshot design cannot provide, and
      the reason this phase exists in this shape. Includes the real,
      easy-to-miss `$D019` write-1-to-clear semantics (genuinely
      different from the CIA's read-clears ICR — see `docs/vic-ii.md`).
- [x] Badlines modeled as a real, cycle-stealing condition (not just a
      queryable flag) — they must actually cost the CPU cycles when they
      happen, since real programs' timing-sensitive code depends on it.
      Confirmed the real number is 43 cycles (12-54), not 40 — see
      `docs/vic-ii.md`'s Badlines section.
- [x] Verification target: a real 6502 test program (hand-assembled)
      that changes a register mid-frame (e.g. border color) and produces
      a visibly split-color frame when rendered scanline-by-scanline —
      the concrete test that a frame-snapshot design would fail. **Done
      as a direct register-write equivalent**, not yet as an actual
      hand-assembled 6502 program (Phase 6's machine loop doesn't exist
      yet to run one against) — see `docs/vic-ii.md`'s Verification
      targets section for the precise scope of what's tested.

## Phase 5 — SID

Owes: `docs/sid.md`.

- [x] Three oscillators (triangle/sawtooth/pulse/noise), ADSR envelope
      generators (per the datasheet's documented rate tables), hard
      sync, ring modulation, and a filter. See `src/c64/sid.c`, sourced
      directly from the primary datasheet (`docs/sources.md`) —
      including catching a real accumulator-width error (23 vs. 24 bit)
      in a secondary source used only for the bit-level waveform
      algorithm, resolved via the datasheet's own frequency equation.
- [x] **Filter and combined-waveform fidelity**: reSID's own
      transistor-level modeling is the real state of the art here, but
      it's GPL — per `CLAUDE.md`'s license discipline, don't vendor its
      tables/logic. A simpler documented-approximation filter/combined-
      waveform model is an acceptable, disclosed gap; write it up as one
      in `docs/sid.md` rather than silently shipping something that
      merely sounds plausible. **Done**: combined waveforms use the
      datasheet's own documented AND-combination rule; the filter is a
      generic Chamberlin state-variable design at an assumed nominal
      sample rate — see `docs/sid.md`'s Oscillators/Filter sections for
      the full disclosure, including that ADSR decay/release uses a
      linear (not real hardware's non-linear) ramp shape.
- [x] Verification target: a hand-assembled test program poking a known
      frequency (e.g. 440Hz) produces output measurably at that
      frequency (zero-crossing count or FFT peak against the rendered
      samples) — a concrete, falsifiable check, not "it makes a sound."
      **Done** as a direct register-write equivalent (Phase 6's machine
      loop doesn't exist yet to run a real hand-assembled 6502 program
      against): `Fn=7382` for 440Hz taken directly from the datasheet's
      own Appendix A table, ticked for one simulated second, counting
      accumulator-overflow "wraps" — landed exactly on 440.

## Phase 6 — The real machine: cycle interleaving + IRQ/NMI (done)

Owes: `docs/machine.md` (already written this pass).

- [x] Wire CPU + Bus + both CIAs + VIC-II + SID together, driven by real
      elapsed PHI2 cycles, with correct IRQ (level-triggered, OR of both
      CIAs and the VIC-II raster interrupt) and NMI (edge-triggered)
      delivery. `src/c64/machine.h`/`.c`; 20 unit tests in
      `tests/unit/test_machine.c`. This surfaced and fixed a genuine,
      latent Phase 1 CPU bug in interrupt entry — see `docs/machine.md`.
- [x] Real-time pacing strategy decided and implemented (see
      `docs/machine.md`'s "Real-time pacing" section — the recommended
      approach is letting the audio output device's own real playback
      rate be the master clock once Phase 7 exists, with a host
      high-resolution timer as the pre-audio fallback). **Decided:
      host high-resolution timer** (`machine_run_realtime()`), since
      Phase 7's audio device doesn't exist yet; revisit once it does.
- [x] Verification target: booting the real KERNAL+BASIC and watching
      its own real jiffy-clock counter (`$A0`-`$A2`) actually increment
      at the correct real-world rate once BASIC reaches its
      keyboard-wait loop. `tests/integration/test_jiffy_clock.c`,
      confirmed against the user's own staged ROMs: PASS, and it
      empirically settled two real facts along the way (big-endian
      `$A0`-`$A2` byte order, and the real ~60Hz/16421-cycle Timer A
      reload) — see `docs/cia.md` and `docs/sources.md`.

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
- [ ] Ultimax mode (`GAME=0`, `EXROM=1`) — implement it in this same
      pass rather than deferring it as a corner case. It's not rare in
      practice (real diagnostic cartridges use it) and the realistic
      way it gets found is by mis-mapping a real cartridge under the
      wrong assumed configuration first, which produces a plausible-
      looking but silently wrong result rather than an obvious error —
      see `docs/cartridge.md`'s Ultimax section for the specific trap
      (never infer the mode from ROM size/shape; always read the
      header's actual `EXROM`/`GAME` bits).
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
