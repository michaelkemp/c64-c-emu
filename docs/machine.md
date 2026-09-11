# The main loop: cycle interleaving, real-time pacing, interrupts (Phase 6)

This is where Phases 1-5's separately-built pieces get driven together
as one real machine. Two responsibilities live here that don't belong in
any single chip module: **cycle interleaving** (who runs when, at what
granularity) and **real-time pacing** (making emulated time track real
wall-clock time, without drifting and without needing the host CPU to
spin at 100%).

**Done.** `src/c64/machine.h`/`.c` implements exactly this: a `Machine`
struct owning the CPU, `C64Memory`, both CIAs, the VIC-II, and the SID,
with `machine_cycle()` doing the interleaving below plus IRQ/NMI
delivery, VIC-II bank tracking (from CIA2 Port A), and keyboard/
joystick-to-CIA1 wiring, every single cycle. `tools/demos/
framebuffer_dump.c` (the earlier, deliberately partial CPU+VIC-II-only
prototype predating this phase) is superseded by `tools/demos/
real_rom_boot_dump.c`, which now runs through the real `Machine`.

## Cycle interleaving

The C64's CPU, VIC-II, and (once Phase 9b exists) the 1541's own CPU all
genuinely share/contend for bus cycles or must react to each other
within a single PHI2 cycle's granularity — not "after N cycles have
passed." The concrete shape:

```
for each PHI2 cycle:
    vic_ii_cycle(&vic, bus)       // may claim the bus this cycle (Phase 4)
    if bus not claimed by VIC-II this cycle:
        cpu_cycle(&cpu, bus)      // CPU executes exactly one cycle's worth
    cia1_tick(&cia1, 1); cia2_tick(&cia2, 1)   // timers/TOD advance
    sid_tick(&sid, 1)             // oscillators/envelopes advance
    // once Phase 9b exists: iec_bus_cycle(...), drive_cpu_cycle(...)
```

This requires the Phase 1 CPU core to expose a genuine "execute exactly
one cycle of the current instruction, then return control" interface —
internally this usually means the core is a state machine over each
instruction's micro-steps (fetch opcode, fetch operand byte(s), compute,
write-back), not a function that runs a whole instruction to completion
and reports a cycle count after the fact. **Decide this in Phase 1**,
not here — retrofitting it later means rewriting the instruction
dispatch loop, not just adding a wrapper.

If Phase 4 took the coarser per-scanline VIC-II shortcut (see
`docs/vic-ii.md`'s note on this), the interleaving here can correspondingly
be "run the CPU for up to 63 cycles or until a VIC-II-relevant event,
whichever comes first, then let the VIC-II process one scanline" — still
real interleaving, just coarser-grained. Document which granularity you
actually built.

**Decided: true per-cycle**, exactly matching the pseudocode above --
`machine_cycle()` in `src/c64/machine.c` is a literal, direct
implementation of it (VIC-II first, CPU only if the bus wasn't stolen,
then both CIAs and the SID, every single PHI2 cycle). No 1541/IEC bus
interleaving yet (Phase 9b).

## IRQ / NMI delivery

- **IRQ** is level-triggered and shared: both CIAs and the VIC-II raster
  interrupt all assert onto the same physical IRQ line, ORed together.
  The CPU checks the line's current state at the correct point in its
  instruction cycle (after fetching the *next* opcode, per real 6502
  behavior — an instruction in progress finishes before an IRQ is taken,
  and the flag-check timing relative to `CLI`/`SEI`/`PLP` has real,
  documented one-instruction-delay edge cases worth getting right, since
  real software occasionally depends on them for atomic critical
  sections).
- **NMI** is edge-triggered, sourced from CIA2 and the RESTORE key.
  Edge-triggered means it fires once on the transition, not continuously
  while asserted — a common bug is treating it like IRQ and firing
  repeatedly while the line stays high.
- Verify both against real KERNAL behavior once Phases 2/3/4 are staged:
  the jiffy-clock IRQ (from CIA1) actually firing and advancing `$A0`-
  `$A2`, and (once RESTORE-key handling matters) NMI actually breaking
  into a hung BASIC program.

**Done, and this phase is exactly what surfaced a real, latent Phase 1
CPU bug**: `cpu6502_cycle()`'s interrupt-entry sequence (shared by
IRQ/NMI/BRK) set the real `I` flag to 1 as part of vectoring, but never
updated the *separate* `i_flag_before_instruction` snapshot the CLI/
SEI/PLP one-instruction-delay mechanism uses to decide the *next* poll.
That snapshot was left stale at whatever it was before the interrupt
was taken -- harmless for a handler that immediately acknowledges its
interrupt source, but if the source is still asserted when the poll
happens again (nothing has read/cleared the CIA's ICR or the VIC's
`$D019` yet), the CPU would re-enter interrupt service on the very next
cycle, forever, without ever executing the handler's own first
instruction. This never showed up in Phase 1's own unit tests (which
either always acknowledged, or only ever triggered one interrupt) --
only became visible once Phase 6 actually drove a real, repeating
interrupt through a full handler. Fixed in `src/cpu/cpu6502.c`'s
`step_brk()`: it now sets `i_flag_before_instruction = true` the moment
interrupt entry completes, since only CLI/SEI/PLP specifically get the
one-instruction delay -- entering interrupt service is not one of
those three opcodes, so its own effect on `I` must be visible
immediately. `tests/unit/test_machine.c` has both a test that
deliberately doesn't acknowledge the source (confirming the resulting
"interrupt storm" is real, expected behavior, not a bug) and one that
does acknowledge it (confirming a clean, correctly-paced interrupt
period).

Verification against real KERNAL behavior: **done**, and it surfaced
two more real, worth-recording facts along the way (see
`docs/cia.md` and `docs/sources.md`): the staged KERNAL's real jiffy-
clock CIA1 Timer A reload is empirically ~16421 cycles (~60Hz, *not*
tied to the PAL video rate -- the KERNAL ROM content, and so this
constant, is shared between PAL and NTSC hardware), and the `$A0`-`$A2`
jiffy counter is big-endian (`$A0` is the high byte, `$A2` the low byte
-- the opposite of an untested guess). `tests/integration/
test_jiffy_clock.c` confirms the real rate empirically rather than
assuming either fact in advance.

## Real-time pacing

The emulated machine must track real wall-clock time at the correct
rate — neither running faster than real hardware (audio pitch/tempo
would be wrong, and some software has real timing assumptions) nor
falling behind (audio underruns, visibly slow-motion behavior). Two
reasonable strategies, pick one and document which:

1. **Host high-resolution timer as master clock** (simplest, works
   before audio exists): compute how many real PHI2 cycles *should* have
   elapsed since start based on a monotonic host clock, run the
   interleaved loop up to that many cycles, sleep briefly if you're
   ahead of schedule. Simple, but can drift/jitter under host scheduling
   pressure and needs its own care to avoid busy-waiting a whole core.
2. **Audio output device as master clock** (once Phase 7 exists,
   generally the better long-term choice): let the sound card's own
   real playback rate be what paces emulation — the audio callback pulls
   samples from a ring buffer on its own real schedule, and the main
   loop's job becomes "keep that ring buffer topped up, running ahead of
   real time by a small, bounded margin, never behind." This
   automatically gives audio-glitch-free pacing without a separate
   timer, since audio hardware plays at true real-time rate by
   construction. Decide the target buffer margin (a few tens of
   milliseconds is a reasonable starting point) and measure whether it
   holds up once real screen rendering (Phase 7) is also happening every
   frame — rendering has real cost too, and a naive design that draws
   every single frame at full detail while also running audio can
   starve the audio buffer if the combined per-frame cost exceeds the
   frame budget (measure this directly rather than assuming headroom;
   if it's tight, throttling actual screen redraws to something like
   1-in-N frames while still stepping CPU/audio every frame is a
   reasonable, disclosed trade-off — document the chosen N and the
   measured budget that justified it).

**Whichever you pick, use the exact computed PAL frame rate** (dot clock
÷ cycles-per-frame ≈ 50.125Hz, from `docs/vic-ii.md`) for anything
timing-sensitive, not a hardcoded `50` — a small, persistent rate
mismatch (not random jitter) will slowly starve a buffer or drift a
clock over a sustained run even though it looks fine over a short test.

**Decided: strategy 1 (host high-resolution timer)**, since Phase 7's
audio device doesn't exist yet. `machine_run_realtime()` in
`src/c64/machine.c` computes the target cycle count from a monotonic
host clock (`clock_gettime(CLOCK_MONOTONIC)`) and a fixed real PAL
clock constant (`C64_PAL_MASTER_CLOCK_HZ` in `src/c64/machine.h`,
derived from the same 17.734472MHz crystal / 18 real hardware uses --
cross-checked against `docs/vic-ii.md`'s independently-stated
~50.1245Hz frame rate and matching exactly), runs cycles up to that
target, and sleeps a fixed 1ms when caught up rather than busy-waiting
a host core. The cycle-budget arithmetic itself
(`machine_target_cycles_for_elapsed()`) is a pure function, unit-tested
directly; the sleeping wrapper is deliberately not tested for precise
real-time accuracy (host scheduling jitter would make that a flaky
test), only smoke-tested that it runs and makes roughly the right
amount of progress. Revisit this once Phase 7 exists, per strategy 2's
own note above about measuring combined render+audio cost.

## Verification target

Boot the real KERNAL+BASIC through the full interleaved loop and confirm
the jiffy clock (`$A0`-`$A2`) advances at the correct real-world rate
once BASIC reaches its keyboard-wait loop — this exercises CPU+CIA
interleaving, IRQ delivery, and pacing all at once, the same way it did
in each earlier phase's own narrower verification target.

**Done** — see the IRQ/NMI section above for what this uncovered.
`tests/integration/test_jiffy_clock.c` is the concrete check: it
watches `$A0`-`$A2` directly (rather than assuming which CIA1 Timer A
configuration is the KERNAL's final, settled one -- the staged KERNAL
here briefly self-tests the timer/interrupt path very early in boot,
before the CPU's own `I` flag is even clear, so an internal-register-
based detection heuristic isn't reliable) and confirms the observed
rate matches whatever reload value is actually driving real ticks, to
within a few cycles of interrupt-service latency.

## Known gaps to disclose as you build

- Which cycle-interleaving granularity you actually built (true
  per-cycle vs. the coarser per-scanline shortcut). **True per-cycle.**
- Which pacing strategy you built, and any measured headroom/deficit
  numbers from testing it with real rendering + audio both active.
  **Host-timer strategy (1)** here; Phase 7's real `sdl_frontend.c`
  actually uses strategy 2 (audio as master clock) instead once a real
  audio device exists — see `docs/peripherals.md`'s "Real-time pacing"
  discussion there. Rendering measured well under the audio pacing
  budget in Phase 7 (~20% CPU, not busy-waiting a whole core).
- ~~The RESTORE key isn't modeled~~ **Done in this phase**:
  `machine_set_restore_key()` (added once Phase 7's real keyboard
  pipeline made it matter) ORs a direct, edge-triggered NMI source into
  the same line as CIA2's own, matching real hardware's own wiring
  (RESTORE isn't a matrix position -- see `src/c64/keyboard.h`).
  `tests/unit/test_machine.c`'s `test_restore_key_edge_triggers_nmi`
  confirms it fires exactly once per press edge, independent of CIA2.
- 1541/IEC bus cross-stepping (Phase 9b) isn't part of the
  interleaving loop yet, as expected at this stage.
