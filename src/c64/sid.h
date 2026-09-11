#ifndef C64EMU_C64_SID_H
#define C64EMU_C64_SID_H

#include <stdbool.h>
#include <stdint.h>

/* MOS 6581 SID. See docs/sid.md and docs/sources.md.
 *
 * Register-level facts (map, ADSR rate table, gate/sync/ring/test bit
 * semantics, filter register layout, the "combined waveforms AND
 * together, noise can lock up" warning) are sourced directly from the
 * official preliminary datasheet, read as page images (see
 * docs/sources.md). The datasheet does NOT give the bit-level
 * oscillator/noise algorithm (how a 12-bit waveform value is actually
 * derived from the internal phase accumulator) -- for that,
 * docs/sources.md also records a second, community reverse-engineering
 * source, and a real discrepancy it had that got resolved directly
 * from the datasheet's own frequency equation (accumulator is 24-bit,
 * not the 23-bit that source claimed -- 16777216 = 2^24). */

/* CRA/CRB-equivalent control register bits (per-voice, offset +4) */
#define SID_CTRL_GATE 0x01u
#define SID_CTRL_SYNC 0x02u
#define SID_CTRL_RING 0x04u
#define SID_CTRL_TEST 0x08u
#define SID_CTRL_TRIANGLE 0x10u
#define SID_CTRL_SAWTOOTH 0x20u
#define SID_CTRL_PULSE 0x40u
#define SID_CTRL_NOISE 0x80u

typedef enum {
    SID_ENV_ATTACK,
    SID_ENV_DECAY_SUSTAIN,
    SID_ENV_RELEASE
} SidEnvelopeState;

typedef struct SidVoice {
    /* --- registers --- */
    uint16_t freq;    /* $x0/$x1: 16-bit */
    uint16_t pw;       /* $x2/$x3: 12-bit (low 12 bits used) */
    uint8_t control;  /* $x4 */
    uint8_t attack_decay;   /* $x5: bits 4-7 attack, bits 0-3 decay */
    uint8_t sustain_release; /* $x6: bits 4-7 sustain, bits 0-3 release */

    /* --- oscillator internal state --- */
    uint32_t accumulator; /* 24-bit phase accumulator */
    uint32_t lfsr;         /* 23-bit noise shift register */

    /* --- envelope generator internal state --- */
    SidEnvelopeState env_state;
    uint8_t envelope;         /* 8-bit current envelope level, 0-255 */
    uint32_t env_rate_counter; /* PHI2 cycles accumulated toward the next envelope step */
    bool gate_prev;            /* detects a gate-bit transition on register write */
} SidVoice;

typedef struct Sid {
    SidVoice voice[3];

    uint16_t filter_cutoff;    /* $15/$16: 11-bit */
    uint8_t filter_resonance;  /* $17 bits 4-7 */
    uint8_t filter_route;      /* $17 bits 0-3: which voices (0-2) + EXT IN (bit3) feed the filter */
    uint8_t filter_mode;       /* $18 bits 4-7: LP/BP/HP/3-OFF */
    uint8_t volume;            /* $18 bits 0-3 */

    /* Simple resonant filter's own running state (a disclosed
     * documented-approximation model, not reSID's transistor-level
     * one -- see docs/sid.md). */
    double filter_lp;
    double filter_bp;
} Sid;

void sid_init(Sid *sid);

/* reg is 0-28 ($D400-$D41C); the caller/bus glue handles the real
 * 32-byte mirroring across $D400-$D7FF, same convention as the CIA and
 * VIC-II. */
uint8_t sid_read(Sid *sid, uint8_t reg);
void sid_write(Sid *sid, uint8_t reg, uint8_t value);

/* Exposed for tests/diagnostics (same convention as vic_ii_read()): a
 * voice's current combined 12-bit waveform output, before the envelope
 * multiplier and filter/mixer -- see sid.c's own comment on the
 * datasheet's documented AND-combination and noise-lock-up rules. */
uint16_t sid_voice_waveform_output(const Sid *sid, int voice_index);

/* Advances oscillators/envelopes by exactly `cycles` real PHI2 cycles
 * -- call this from the same cycle-interleaved main loop driving the
 * CPU/CIA/VIC-II (Phase 6), since oscillator frequency and envelope
 * rate are both defined directly in terms of PHI2 cycles, not audio
 * samples. */
void sid_tick(Sid *sid, uint32_t cycles);

/* Returns the current instantaneous mixed/filtered/volume-scaled
 * output, normalized to [-1.0, 1.0]. Call this at whatever rate the
 * audio pipeline wants to pull samples (Phase 7) -- decoupled from
 * sid_tick()'s cycle-accurate advancement, matching how the real
 * chip's internal DSP (PHI2-rate) and its DAC (its own separate
 * concern) are also decoupled. Not `const`: the filter's own simple
 * model advances a small recurrence state on every call -- see
 * sid.c's apply_filter(). */
double sid_output(Sid *sid);

#endif /* C64EMU_C64_SID_H */
