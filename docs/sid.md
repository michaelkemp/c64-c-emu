# MOS 6581/8580 SID (Phase 5)

Primary reference for reliable, documented behavior: the SID datasheet
and general technical literature. **reSID** (the standard reference
implementation used by VICE and others) is the real state of the art for
transistor-level filter/combined-waveform fidelity — it is GPL, so per
`CLAUDE.md`'s license discipline, **read it for understanding, never
copy its code or vendor its data tables**. Where this project's simpler
model diverges from reSID's fidelity, disclose it here rather than
quietly shipping a plausible-sounding approximation.

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

**Hard sync** (each voice's oscillator can be reset by voice 3's — or in
some real wiring, the "previous" voice's — oscillator crossing zero) and
**ring modulation** (XORing two voices' triangle outputs) are both real,
well-documented, implementable-without-reSID behaviors — build these for
real, not as a stub.

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

## Filter

A resonant multi-mode (low/band/high-pass, independently selectable and
combinable) filter is real SID behavior worth implementing, but
reSID's transistor-level model is the actual state of the art and is
off-limits to copy (GPL). A simpler documented digital-filter
approximation (e.g. a standard resonant IIR design tuned to the SID's
documented cutoff-frequency register range) is an acceptable, disclosed
starting point — **write down explicitly that it's an approximation, not
the real analog behavior**, in this doc's Known Gaps section once built.

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

## Known gaps to disclose as you build

- Combined-waveform fidelity vs. reSID's transistor-level model.
- Filter fidelity vs. reSID's transistor-level model.
- Paddle/analog input — likely fine to stub unless something needs it.
- Digi-playback via pure DC-offset riding (no oscillator) — a permanent
  gap under a linear volume model, as described above.
