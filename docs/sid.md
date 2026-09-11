# MOS 6581/8580 SID (Phase 5)

Primary reference: the official preliminary MOS 6581 SID datasheet —
https://6502.org/documents/datasheets/mos/mos_6581_sid.pdf, read in
full (12 pages, as page images — another scanned PDF, see
docs/sources.md). This gives the complete register map, the exact ADSR
rate table (Table 2), gate/sync/ring-mod/test bit semantics, the filter
register layout, and the datasheet's own documented (simplified)
combined-waveform behavior. It does **not** give the bit-level
oscillator/noise algorithm (how a 12-bit waveform value is actually
derived from the internal phase accumulator) — for that, docs/sources.md
records a second, disclosed community source, used only for that
structural detail, not for anything reSID-proprietary.

**reSID** (the standard reference implementation used by VICE and
others) is the real state of the art for transistor-level filter/
combined-waveform fidelity — it is GPL, so per `CLAUDE.md`'s license
discipline, **read it for understanding, never copy its code or vendor
its data tables**. Where this project's simpler model diverges from
reSID's fidelity, disclose it here rather than quietly shipping a
plausible-sounding approximation.

## Register map (`$D400-$D41C`, mirrored through `$D7FF`)

Three voices, each with an identical 7-register block, plus shared
filter/volume registers:

| Offset (per voice, ×3 blocks starting `$D400`/`$D407`/`$D40E`) | Register |
|---|---|
| `+0`/`+1` | Frequency low/high |
| `+2`/`+3` | Pulse width low/high (low 12 bits used) |
| `+4` | Control: waveform select (triangle/sawtooth/pulse/noise bits), test, ring mod, sync, gate |
| `+5` | Attack/Decay |
| `+6` | Sustain/Release |

Shared, starting `$D415`:

| Offset | Register |
|---|---|
| `$D415`/`$D416` | Filter cutoff frequency low/high |
| `$D417` | Filter resonance + which voices are routed through the filter |
| `$D418` | Filter mode (low/band/high pass, each independently) + master volume (low nibble) |
| `$D419`/`$D41A` | Paddle X/Y (analog input, low priority — stub) |
| `$D41B`/`$D41C` | Oscillator 3 / envelope 3 read-back (used by some real software for pseudo-random numbers / modulation — worth implementing since it's cheap and some real programs read it) |

## Oscillators

Four waveforms, selectable and combinable via the control register's
high nibble: triangle, sawtooth, pulse (with variable width via the
pulse-width registers), and noise (a real, documented LFSR — implement
its actual documented tap/shift structure, not a generic PRNG, since
real software's noise character depends on the specific real shift
pattern). **Combined waveforms** (e.g. triangle+sawtooth selected
together) are a case where real hardware's analog behavior is
genuinely more complex than a simple logical AND of the two waveforms'
digital values — reSID models this with vendored lookup tables derived
from real chip measurement. This project's simpler model is expected to
diverge here; document it as a known gap rather than claim it's exact.

**Done** — `src/c64/sid.c`. Each voice runs a 24-bit phase accumulator
(confirmed from the datasheet's own frequency equation, `Fout = Fn *
Fclk / 16777216` — 16777216 = 2^24 — which directly settled a real
discrepancy: the community source used for the bit-level algorithm
claims a 23-bit accumulator, which is wrong for this register; 23 bits
is correct only for the *separate* noise LFSR). Sawtooth is the top 12
accumulator bits; triangle XORs the top 11 bits with the MSB (inverting
them when the MSB is set, producing the up/down ramp) then shifts;
pulse compares the top 12 accumulator bits against the 12-bit pulse-
width register; noise is a 23-bit Fibonacci LFSR (taps at bits 17/22
feeding back into bit 0, clocked by the accumulator's own bit 19
transitioning 0→1, with 8 specific output bits — 0,2,5,9,11,14,18,20 —
forming the 12-bit noise value). Combining multiple waveforms uses the
datasheet's **own** documented (simplified) behavior — a plain bitwise
AND, and Noise combined with anything else forced to 0 (the documented
"lock up" hazard) — not reSID's more elaborate measured-table model of
the same real, messier chip behavior.

**Hard sync** (each voice's oscillator can be reset by voice 3's — or in
some real wiring, the "previous" voice's — oscillator crossing zero) and
**ring modulation** (XORing two voices' triangle outputs) are both real,
well-documented, implementable-without-reSID behaviors — build these for
real, not as a stub. **Done**: hard sync resets a voice's accumulator to
0 exactly when its sync-source voice's accumulator MSB transitions 0→1
(voice pairing is fixed: 1←3, 2←1, 3←2); ring modulation XORs the two
voices' MSBs before the triangle formula's invert-or-not decision.

## ADSR envelope generator

Attack/Decay/Sustain/Release, each parameter selecting a rate from the
datasheet's documented rate tables (16 discrete rate settings each for
A/D/R, mapped to real, documented cycle counts per envelope step — get
these exact values from the datasheet, they are a well-established,
safe-to-implement-directly fact, not something that requires reSID).
Sustain is a direct 4-bit level, not a rate. Get the **gate bit**
semantics right: setting it starts attack→decay→sustain; clearing it
starts release from wherever the envelope currently is (not from
sustain level) — a common and easy mistake is releasing from the wrong
starting level.

**Done, with one disclosed simplification.** The exact attack/decay/
release cycle counts (Table 2 of the datasheet) are implemented exactly
as given, and the gate-triggered attack/decay/sustain vs. release-from-
current-level behavior is correct (verified: re-gating mid-release
resumes attack from wherever the envelope was, not from 0). The
simplification: real hardware's decay/release is **not** a linear ramp
— it uses an internal, non-linear "exponential counter" table to make
the fall-off resemble a real capacitor discharge curve, and that table
isn't given in the official datasheet (only "Exponential response" is
listed as a chip *feature*). `src/c64/sid.c` uses a **linear** ramp
across the same total datasheet-given times instead — the total time
for a full 0-255 (or 255-0) traversal matches the datasheet, but the
real curve's shape (fast-then-slow) doesn't. Sustain-level stop point
is `sustain_nibble * 17` (0-255), a reasonable direct scaling matching
the datasheet's own "SUSTAIN of 8 ≈ one-half peak amplitude" example
(8×17=136 ≈ 53%).

## Filter

A resonant multi-mode (low/band/high-pass, independently selectable and
combinable) filter is real SID behavior worth implementing, but
reSID's transistor-level model is the actual state of the art and is
off-limits to copy (GPL). A simpler documented digital-filter
approximation (e.g. a standard resonant IIR design tuned to the SID's
documented cutoff-frequency register range) is an acceptable, disclosed
starting point — **write down explicitly that it's an approximation, not
the real analog behavior**, in this doc's Known Gaps section once built.

**Done, as a disclosed approximation.** `apply_filter()` in
`src/c64/sid.c` is a simple Chamberlin state-variable filter (a common,
well-understood generic digital-filter design, not anything reSID-
specific), with cutoff linearly mapped across the datasheet's own
documented 30Hz-12kHz range and resonance linearly mapped from the
4-bit register. It assumes a **nominal internal sample rate of
44100Hz** for its own recurrence, since Phase 7's real audio pipeline
(which will call `sid_output()` at an explicit, known rate) doesn't
exist yet — calling `sid_output()` faster or slower than that will
shift the *effective* cutoff proportionally until Phase 7 wires this up
properly. Frequency response is not calibrated against real hardware
measurements at all — this is a documented approximation, not a
reSID-fidelity model, exactly as this section originally called for.

## Digi-playback (a real technique worth knowing about, not required to nail on the first pass)

Some real C64 music/software plays back sample data by rapidly rewriting
the master volume register (`$D418`'s low nibble) with no oscillator
producing a "musical" tone at all — riding the SID's own analog DC
offset/leakage. A **linear** volume-multiply model (volume nibble as a
clean multiplier on the mixed digital signal) reproduces the
"amplitude-modulate an already-running tone via rapid volume writes"
variant of this technique correctly, but **cannot** reproduce the
"volume writes alone, no oscillator running" variant — that one
fundamentally depends on real analog non-linearity a linear model can't
represent. This is a real, disclosed, permanent gap under this
project's declined-to-vendor-reSID model, not a bug to chase.

## Verification target

A hand-assembled test program poking a known frequency (e.g. voice 1 at
a register value corresponding to 440Hz) should produce rendered output
measurably at that frequency — count zero-crossings or take an FFT peak
of the rendered samples and check it lands at 440.0Hz (or very close),
a concrete falsifiable check rather than "it produces some sound."

**Done** — `test_sawtooth_frequency_matches_440hz_appendix_a` in
`tests/unit/test_sid.c` uses `Fn=7382` for A4 (440Hz) at a 1MHz PHI2
clock, taken directly from the datasheet's own Appendix A frequency
table (not computed independently), ticks the SID for exactly one
simulated second (1,000,000 cycles), counts accumulator overflows
("wraps," each one a full oscillator cycle), and asserts the count
lands within ±1 of 440 — landed exactly on 440. This exercises the
oscillator/accumulator path directly rather than going through
`sid_output()`'s mixing/filter/volume stages, since those don't affect
the oscillator's own frequency.

## Known gaps to disclose as you build

- Combined-waveform fidelity vs. reSID's transistor-level model — this
  project uses the datasheet's own simpler documented AND-combination
  rule instead (see "Oscillators" above).
- Filter fidelity vs. reSID's transistor-level model — a generic
  Chamberlin state-variable filter at an assumed nominal 44100Hz
  internal rate (see "Filter" above), not calibrated against real
  hardware measurements.
- ADSR decay/release uses a **linear** ramp, not real hardware's
  non-linear "exponential counter" shape — the official datasheet gives
  the correct total times but not that internal table (see "ADSR
  envelope generator" above).
- Paddle/analog input (`$D419`/`$D41A`, POTX/POTY) — stubbed, always
  reads 0; no paddle input exists in this project.
- Digi-playback via pure DC-offset riding (no oscillator) — a permanent
  gap under a linear volume model, as described above.
- **Bus wiring done in Phase 6** (`c64memory_attach_sid()`, ticked
  every PHI2 cycle from `src/c64/machine.c`'s `machine_cycle()`) — no
  actual audio output device yet, still deferred to Phase 7 (SDL2
  audio callback).
- External audio input (`EXT IN`, the `FILTEX`/"3 OFF" mixing path) is
  not modeled — there's no external audio source to mix in this
  project.
