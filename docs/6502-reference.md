# 6502 CPU reference (Phase 1)

The C64 uses a **6510**, not a plain 6502 — the only real difference is
a 2-bit-plus-control-lines I/O port memory-mapped at `$00`/`$01` (used
for ROM bank-switching and the datasette). **Model that as a bus-level
device in Phase 2 (`docs/memory-map.md`), not a CPU core feature.** The
CPU core built in this phase should be a plain, chip-agnostic 6502 that
knows nothing about banking, memory maps, or the C64 at all — it only
ever calls out to `bus_read8(addr)` / `bus_write8(addr, value)`. This
separation is what makes the core directly reusable, unmodified, for the
1541's own 6502 in Phase 9b (a completely different memory map).

## Registers

- `A` (accumulator), `X`, `Y` (index registers) — 8-bit.
- `PC` — 16-bit program counter.
- `S` — 8-bit stack pointer (stack lives at `$0100`-`$01FF`, grows down).
- `P` — status flags: `N V - B D I Z C` (bit 7 to bit 0). Bit 5 is
  always read as 1; there is no real bit-5 flag. `B` is not a real
  stored flag either — it only ever appears as the *pushed* value of
  byte on the stack during `BRK`/IRQ/NMI (set for `BRK`, clear for a
  real interrupt), never as a bit the CPU reads back out of `P` during
  normal execution. Get this right — it's a classic source of subtle
  bugs in from-scratch 6502 cores.

## Addressing modes

Implicit, accumulator, immediate, zero page, zero page X, zero page Y,
absolute, absolute X, absolute Y, indirect (only used by `JMP`, and only
`JMP`, with its famous page-boundary bug — see below), indexed indirect
`(zp,X)`, indirect indexed `(zp),Y`, relative (branches).

**The `JMP ($xxFF)` page-boundary bug is real 6502 hardware behavior,
not a bug to "fix"**: if the low byte of the indirect address is `$FF`,
the CPU fetches the high byte of the target from `$xx00` on the *same*
page instead of correctly crossing into the next page. Real software
occasionally depends on this (rarely deliberately, but it must still be
reproduced faithfully) — implement it exactly as documented, don't
"correct" it.

## Cycle counts

Every instruction's cycle count is well-documented and fixed except for
a few real, documented variable cases your core must handle correctly:
- Absolute-indexed and indirect-indexed addressing modes take one extra
  cycle when the indexed access crosses a page boundary (add, don't
  average).
- Branch instructions take one extra cycle when the branch is taken, and
  a further extra cycle when the branch target crosses a page boundary.

These aren't optional detail — Phase 4's scanline-accurate VIC-II and
Phase 6's cycle-interleaved main loop depend on the CPU core reporting
*exactly* the real cycle count for every instruction, not an average or
an approximation.

## Decimal mode

`ADC`/`SBC` in decimal mode (`D` flag set) perform BCD arithmetic with
several well-documented NMOS quirks (invalid BCD digit inputs produce
specific, documented — if "weird" — outputs; flags are set based on the
*binary* result in some cases even in decimal mode on NMOS parts).
Get the documented behavior right rather than "sensible" BCD math; the
Dormann suite below specifically exercises this.

## Undocumented ("illegal") opcodes — deferred to Phase 10

Real 6502/6510 silicon executes defined behavior for many of the
"undocumented" opcodes (a side effect of how the instruction decode ROM
is laid out, not intentional design) — commercial C64 software
(especially copy-protected games and demoscene code) genuinely relies on
some of these, both for code density and as a deliberate
anti-emulation trick (an emulator that treats them as a NOP or a crash
will misbehave in a way real hardware doesn't). This project **defers**
implementing them until Phase 10, once the stock KERNAL/BASIC boot path
(which never executes one) is solid — see `docs/roadmap.md`'s Phase 10
for the specifics of what's implementable reliably vs. permanently
chip-unstable/undefined.

## Correctness gate: the Dormann functional test suite

Klaus Dormann's 6502 functional test suite is the de facto standard
correctness gate used across the 6502-emulator community — it exercises
every legal opcode/addressing-mode/flag combination exhaustively,
including the decimal-mode edge cases above, and traps (jumps to itself)
at a specific, documented address on success.

- Fetch it on demand via `scripts/fetch_dormann_tests.sh` — **do not
  vendor it into this repo.** It's GPLv3, which is a real, different
  license situation from the Commodore ROMs (this one genuinely is open
  and redistributable), but the project's own convention is still
  fetch-on-demand for anything not originally authored here.
- Assemble it with an external assembler (`ca65`, `vasm`, or similar —
  install via the system package manager; this project does not need to
  write its own 6502 assembler to do this).
- Load the assembled binary into a flat 64KB RAM test harness (no C64
  memory map needed for this — that's the whole point of keeping the
  CPU core bus-agnostic) and run until the PC stops advancing (a trap).
  **Verify the actual success address from the fetched suite's own
  source/listing** rather than trusting a number written down secondhand
  — different builds/versions of the suite can trap at different
  addresses depending on assembly options.
- A run that traps anywhere else, or never traps, means a real bug —
  the suite's own comments around each test block usually pinpoint which
  opcode/mode was being exercised near the trap address, which is why
  the hand-written per-instruction unit tests (Phase 1's other
  checklist item) matter too: they narrow down a failure far faster than
  re-reading the whole suite from scratch.
- `docs/testing-strategy.md` has the full test-tier writeup (unit tests
  → Dormann suite → integration boot tests) that this suite is the
  middle tier of.
