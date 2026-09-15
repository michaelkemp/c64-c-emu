# Sound / SID (real hardware facts, for writing programs)

See `reference/README.md` for how this differs from `docs/sid.md`
(this emulator's own SID implementation and its disclosed gaps). This
file is about the real chip's sound hardware as a programmer sees it.

## Voices

Only **3 real voices** exist, each an identical 7-register block:

| Voice | Base address (decimal) |
|---|---|
| 1 | `$D400` (54272) |
| 2 | `$D407` (54279) |
| 3 | `$D40E` (54286) |

Per-voice registers (offset from that voice's base): `+0`/`+1` freq
lo/hi, `+2`/`+3` pulse-width lo/hi (12-bit, pulse waveform only),
`+4` control (waveform select + gate + sync + ring + test), `+5`
attack/decay, `+6` sustain/release.

Shared (not per-voice) registers: `$D415`/`$D416` (54293/54294) filter
cutoff lo/hi, `$D417` (54295) filter resonance (high nibble) + which
voices are routed through the filter (low nibble, one bit per voice),
`$D418` (54296) filter mode (high nibble: bit4 low-pass, bit5
band-pass, bit6 high-pass; bit7 disconnects voice 3 from the direct
output when set and voice 3 isn't routed through the filter) + master
volume (low nibble, 0-15).

If a program needs more than 3 simultaneous "independent" sounds (e.g.
one tone per sprite in an 8-sprite demo), it has to multiplex physical
voices -- there's no way around this on real hardware. A simple, honest
approach: `voice = id MOD 3`, disclosing that two ids sharing a voice
can cut each other off if triggered at the same moment.

## Control register (`+4`) bits

| Bit | Value | Meaning |
|---|---|---|
| 0 | 1 | Gate -- 0->1 triggers Attack, 1->0 triggers Release |
| 1 | 2 | Sync |
| 2 | 4 | Ring modulation |
| 3 | 8 | Test -- holds the oscillator at 0 and continuously shifts 1s into the noise LFSR while set (see "Noise" below) |
| 4 | 16 | Triangle waveform select |
| 5 | 32 | Sawtooth waveform select |
| 6 | 64 | Pulse waveform select |
| 7 | 128 | Noise waveform select |

Multiple waveform-select bits can be set together; the real chip's
result is a real, messier analog combination -- the datasheet's own
documented simplification is "logical AND of the waveforms" (and Noise
combined with anything else is documented to force output to 0, "lock
up", until the shift register is disturbed again).

**Re-triggering a note on a voice that's already gated on** requires an
explicit 1->0->1 transition (e.g. POKE the control byte with gate
cleared, then POKE it again with gate set) -- writing the same
already-set gate bit again is not a rising edge and won't restart the
envelope.

## ADSR (`+5`/`+6`)

`+5`: high nibble = attack rate index (0-15), low nibble = decay rate
index (0-15). `+6`: high nibble = sustain level (0-15, effectively
scaled to 0-255), low nibble = release rate index (0-15). Attack index
0 and a decay index around 6-9 with sustain 0 gives a short, self-
fading "pluck" that needs no explicit gate-off -- convenient for a
percussive/collision-sound effect that shouldn't block program flow
with a delay loop. The full 16-attack/16-decay-release rate table (2ms
to 8s attack, 6ms to 24s decay/release) is cross-verified, value for
value, against this project's own `ATTACK_CYCLES_1MHZ`/
`DECAY_RELEASE_CYCLES_1MHZ` tables in `src/c64/sid.c` -- exact match on
every entry (Commodore 64 Programmer's Reference Guide, Appendix O,
Table 2).

## Frequency

`Fout = Fn * Fclk / 16777216` (24-bit phase accumulator -- some
secondary sources claim 23 bits, which is actually the *separate* noise
shift register's width, not the accumulator's). A reusable, verified
8-note C-major scale (already used across `programs/basic/`'s test
programs, sourced from the datasheet's own Appendix A table): `4455,
5001, 5613, 5947, 6675, 7493, 8410, 8910`.

## Noise: a real lock-up hazard

The noise waveform comes from a 23-bit Fibonacci LFSR (taps at bits
17/22 feeding back into bit 0, clocked by the accumulator's own bit 19
going high). **All-zero is a genuine fixed point** -- if the shift
register ever reaches all zeros, both feedback taps read 0, so it
shifts in 0 forever and noise stays silent no matter what plays.
Real, documented recovery: strobe the TEST bit (shifts 1s in until the
register is charged with real entropy again) -- **or a real chip
RESET**. The Commodore 64 Programmer's Reference Guide's own SID
appendix (Appendix O, p.464) states it directly: "the Noise output
will remain silent until reset by the TEST bit **or by bringing RES
(pin 5) low**." So real hardware's own reset does *not* leave a locked
generator stuck forever -- it's an active recovery path, same as TEST
-- which was exactly the gap in this project's own emulator (its
`sid_init()` used to leave the shift register at the one value
guaranteed to be silent forever, with nothing to unstick it) -- fixed
by seeding the register non-zero at init. See `docs/sid.md`'s Known
Gaps and `programs/basic/sid_diagnostic.bas`'s own notes for the full
story.

## Digi-playback (worth knowing, rarely needed)

Some real music/software gets extra "voices" by rapidly rewriting the
master volume nibble (`$D418`'s low 4 bits) with no oscillator playing
a tone at all, riding the chip's own analog DC offset. Real, documented
technique; not something a simple digital-volume model can reproduce
faithfully (see `docs/sid.md`'s "Digi-playback" section).
