# The main loop: cycle interleaving, real-time pacing, interrupts (Phase 6)

This is where Phases 1-5's separately-built pieces get driven together
as one real machine. Two responsibilities live here that don't belong in
any single chip module: **cycle interleaving** (who runs when, at what
granularity) and **real-time pacing** (making emulated time track real
wall-clock time, without drifting and without needing the host CPU to
spin at 100%).

**Note**: `tools/demos/framebuffer_dump.c` already does a tiny,
deliberately partial slice of "who runs when" (CPU + VIC-II only, no
CIAs/SID/interrupts, no real-time pacing at all — it just runs a fixed
number of cycles as fast as possible and dumps a screenshot). It exists
purely as an early visual smoke test for Phase 4's VIC-II (see
`tools/demos/README.md`) and is not a starting point to build this
phase's real main loop from — this phase still needs to add both CIAs,
SID, real IRQ/NMI delivery, and actual real-time pacing, none of which
that tool has.

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

## Verification target

Boot the real KERNAL+BASIC through the full interleaved loop and confirm
the jiffy clock (`$A0`-`$A2`) advances at the correct real-world rate
once BASIC reaches its keyboard-wait loop — this exercises CPU+CIA
interleaving, IRQ delivery, and pacing all at once, the same way it did
in each earlier phase's own narrower verification target.

## Known gaps to disclose as you build

- Which cycle-interleaving granularity you actually built (true
  per-cycle vs. the coarser per-scanline shortcut).
- Which pacing strategy you built, and any measured headroom/deficit
  numbers from testing it with real rendering + audio both active.
