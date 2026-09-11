/* MOS 6581 SID. See sid.h and docs/sid.md/docs/sources.md for exactly
 * which facts come from the official datasheet vs. a secondary,
 * disclosed source, and which behaviors (combined-waveform fidelity,
 * filter response) are documented, deliberate approximations rather
 * than reSID-level transistor modeling (off-limits to copy -- see
 * CLAUDE.md's license discipline). */

#include "sid.h"

#include <math.h>
#include <string.h>

/* Voice `i`'s hard-sync/ring-mod partner: "generator 1 is synched to
 * generator 3, 2 to 1, 3 to 2" (datasheet) -- 0-indexed here. */
static const int SYNC_SOURCE[3] = {2, 0, 1};

/* Forward declaration: sid_voice_waveform_output() is defined further
 * down but needed here for the OSC3 register readback. */
uint16_t sid_voice_waveform_output(const Sid *sid, int voice_index);

/* Envelope rate table (Table 2 of the datasheet): cycles for a full
 * 0-255 (or 255-0) traversal at a 1MHz PHI2 clock -- i.e. milliseconds
 * * 1000. Real hardware's actual per-step timing also depends on a
 * separate, non-linear "exponential counter" lookup that isn't given
 * in the official datasheet (only "Exponential response" is listed as
 * a feature) -- this project uses a LINEAR ramp across the same total
 * times instead, a disclosed simplification (see docs/sid.md). */
static const uint32_t ATTACK_CYCLES_1MHZ[16] = {
    2000, 8000, 16000, 24000, 38000, 56000, 68000, 80000,
    100000, 250000, 500000, 800000, 1000000, 3000000, 5000000, 8000000};
static const uint32_t DECAY_RELEASE_CYCLES_1MHZ[16] = {
    6000, 24000, 48000, 72000, 114000, 168000, 204000, 240000,
    300000, 750000, 1500000, 2400000, 3000000, 9000000, 15000000, 24000000};

static uint32_t cycles_per_step(uint32_t full_range_cycles) {
    uint32_t v = full_range_cycles / 255u;
    return v == 0 ? 1u : v;
}

void sid_init(Sid *sid) {
    memset(sid, 0, sizeof(*sid));
    for (int i = 0; i < 3; i++) {
        sid->voice[i].env_state = SID_ENV_RELEASE;
    }
}

/* ---------------------------------------------------------------- */
/* Registers                                                          */
/* ---------------------------------------------------------------- */

uint8_t sid_read(Sid *sid, uint8_t reg) {
    switch (reg) {
    case 0x19: return 0; /* POTX -- stub, no paddle input modeled */
    case 0x1A: return 0; /* POTY -- stub */
    case 0x1B: /* OSC3/RANDOM: upper 8 bits of voice 3's own waveform output */
        return (uint8_t)(sid_voice_waveform_output(sid, 2) >> 4);
    case 0x1C: return sid->voice[2].envelope; /* ENV3 */
    default: return 0; /* every other register is write-only */
    }
}

void sid_write(Sid *sid, uint8_t reg, uint8_t value) {
    if (reg <= 0x14) {
        int voice_index = reg / 7;
        int sub = reg % 7;
        SidVoice *v = &sid->voice[voice_index];
        switch (sub) {
        case 0: v->freq = (uint16_t)((v->freq & 0xFF00u) | value); break;
        case 1: v->freq = (uint16_t)((v->freq & 0x00FFu) | ((uint16_t)value << 8)); break;
        case 2: v->pw = (uint16_t)((v->pw & 0x0F00u) | value); break;
        case 3: v->pw = (uint16_t)((v->pw & 0x00FFu) | ((uint16_t)(value & 0x0Fu) << 8)); break;
        case 4: {
            bool new_gate = (value & SID_CTRL_GATE) != 0;
            if (new_gate != v->gate_prev) {
                v->gate_prev = new_gate;
                v->env_state = new_gate ? SID_ENV_ATTACK : SID_ENV_RELEASE;
                v->env_rate_counter = 0;
            }
            v->control = value;
            break;
        }
        case 5: v->attack_decay = value; break;
        case 6: v->sustain_release = value; break;
        default: break;
        }
        return;
    }

    switch (reg) {
    case 0x15: sid->filter_cutoff = (uint16_t)((sid->filter_cutoff & 0x07F8u) | (value & 0x07u)); break;
    case 0x16: sid->filter_cutoff = (uint16_t)((sid->filter_cutoff & 0x0007u) | ((uint16_t)value << 3)); break;
    case 0x17:
        sid->filter_resonance = (uint8_t)((value >> 4) & 0x0Fu);
        sid->filter_route = (uint8_t)(value & 0x0Fu);
        break;
    case 0x18:
        sid->filter_mode = (uint8_t)(value & 0xF0u);
        sid->volume = (uint8_t)(value & 0x0Fu);
        break;
    default: break; /* $19-$1C are read-only */
    }
}

/* ---------------------------------------------------------------- */
/* Oscillators -- see sid.h's header comment on sourcing              */
/* ---------------------------------------------------------------- */

static uint16_t sawtooth_output(const SidVoice *v) {
    return (uint16_t)((v->accumulator >> 12) & 0x0FFFu);
}

static uint16_t triangle_output(const Sid *sid, int voice_index) {
    const SidVoice *v = &sid->voice[voice_index];
    bool msb = (v->accumulator & 0x800000u) != 0;
    if (v->control & SID_CTRL_RING) {
        bool other_msb = (sid->voice[SYNC_SOURCE[voice_index]].accumulator & 0x800000u) != 0;
        msb = msb != other_msb; /* XOR */
    }
    uint32_t inv = msb ? ((~v->accumulator) & 0xFFFFFFu) : v->accumulator;
    return (uint16_t)((inv >> 11) & 0x0FFFu);
}

static uint16_t pulse_output(const SidVoice *v) {
    if (v->control & SID_CTRL_TEST) {
        return 0x0FFFu; /* datasheet: TEST holds the pulse output at a DC (all-1s) level */
    }
    uint16_t acc_top = (uint16_t)((v->accumulator >> 12) & 0x0FFFu);
    uint16_t pw12 = (uint16_t)(v->pw & 0x0FFFu);
    return (acc_top >= pw12) ? 0x0FFFu : 0x0000u;
}

static uint16_t noise_output(const SidVoice *v) {
    uint32_t l = v->lfsr;
    /* "The eight output bits are the values on any given read of bits
     * 0, 2, 5, 9, 11, 14, 18, and 20 of the shift register" -- mapped
     * to output bits 0-7 respectively; "four zeros are added as low
     * bits to create a 12-bit noise waveform." */
    uint8_t out = (uint8_t)(((l >> 0) & 1u) | (((l >> 2) & 1u) << 1) | (((l >> 5) & 1u) << 2) |
                             (((l >> 9) & 1u) << 3) | (((l >> 11) & 1u) << 4) | (((l >> 14) & 1u) << 5) |
                             (((l >> 18) & 1u) << 6) | (((l >> 20) & 1u) << 7));
    return (uint16_t)((uint16_t)out << 4);
}

/* Combining multiple waveforms: "the oscillator output waveforms are
 * NOT additive... the result will be a logical ANDing of the
 * waveforms" -- and combining Noise with any other waveform is
 * documented to risk the Noise output "locking up". Both facts are
 * taken directly from the official datasheet (not reSID's own more
 * elaborate, measured-table model of this same behavior). */
uint16_t sid_voice_waveform_output(const Sid *sid, int voice_index) {
    const SidVoice *v = &sid->voice[voice_index];
    uint8_t sel = (uint8_t)(v->control & 0xF0u);
    if (sel == 0) {
        return 0;
    }

    bool noise_sel = (sel & SID_CTRL_NOISE) != 0;
    bool pulse_sel = (sel & SID_CTRL_PULSE) != 0;
    bool saw_sel = (sel & SID_CTRL_SAWTOOTH) != 0;
    bool tri_sel = (sel & SID_CTRL_TRIANGLE) != 0;
    int count = (noise_sel ? 1 : 0) + (pulse_sel ? 1 : 0) + (saw_sel ? 1 : 0) + (tri_sel ? 1 : 0);

    if (noise_sel) {
        return (count > 1) ? 0 : noise_output(v);
    }

    uint16_t result = 0x0FFFu;
    if (tri_sel) result &= triangle_output(sid, voice_index);
    if (saw_sel) result &= sawtooth_output(v);
    if (pulse_sel) result &= pulse_output(v);
    return result;
}

/* ---------------------------------------------------------------- */
/* Envelope generator                                                 */
/* ---------------------------------------------------------------- */

static void envelope_tick_one_cycle(SidVoice *v) {
    switch (v->env_state) {
    case SID_ENV_ATTACK: {
        uint32_t rate = (uint32_t)((v->attack_decay >> 4) & 0x0Fu);
        uint32_t cps = cycles_per_step(ATTACK_CYCLES_1MHZ[rate]);
        v->env_rate_counter++;
        if (v->env_rate_counter >= cps) {
            v->env_rate_counter = 0;
            if (v->envelope < 255u) {
                v->envelope++;
            }
            if (v->envelope == 255u) {
                v->env_state = SID_ENV_DECAY_SUSTAIN;
            }
        }
        break;
    }
    case SID_ENV_DECAY_SUSTAIN: {
        uint8_t sustain_level = (uint8_t)(((v->sustain_release >> 4) & 0x0Fu) * 17u);
        if (v->envelope > sustain_level) {
            uint32_t rate = (uint32_t)(v->attack_decay & 0x0Fu);
            uint32_t cps = cycles_per_step(DECAY_RELEASE_CYCLES_1MHZ[rate]);
            v->env_rate_counter++;
            if (v->env_rate_counter >= cps) {
                v->env_rate_counter = 0;
                v->envelope--;
            }
        }
        break;
    }
    case SID_ENV_RELEASE: {
        if (v->envelope > 0u) {
            uint32_t rate = (uint32_t)(v->sustain_release & 0x0Fu);
            uint32_t cps = cycles_per_step(DECAY_RELEASE_CYCLES_1MHZ[rate]);
            v->env_rate_counter++;
            if (v->env_rate_counter >= cps) {
                v->env_rate_counter = 0;
                v->envelope--;
            }
        }
        break;
    }
    default: break;
    }
}

/* ---------------------------------------------------------------- */
/* Per-cycle tick                                                     */
/* ---------------------------------------------------------------- */

static void sid_tick_one_cycle(Sid *sid) {
    uint32_t old_acc[3];
    for (int i = 0; i < 3; i++) {
        old_acc[i] = sid->voice[i].accumulator;
    }

    for (int i = 0; i < 3; i++) {
        SidVoice *v = &sid->voice[i];
        if (v->control & SID_CTRL_TEST) {
            v->accumulator = 0;
        } else {
            v->accumulator = (uint32_t)((v->accumulator + v->freq) & 0xFFFFFFu);
        }
    }

    /* Hard sync: reset this voice's accumulator to 0 exactly when its
     * sync source's accumulator MSB transitions 0->1 this cycle. */
    for (int i = 0; i < 3; i++) {
        SidVoice *v = &sid->voice[i];
        if (v->control & SID_CTRL_SYNC) {
            int src = SYNC_SOURCE[i];
            bool src_old_msb = (old_acc[src] & 0x800000u) != 0;
            bool src_new_msb = (sid->voice[src].accumulator & 0x800000u) != 0;
            if (!src_old_msb && src_new_msb) {
                v->accumulator = 0;
            }
        }
    }

    for (int i = 0; i < 3; i++) {
        SidVoice *v = &sid->voice[i];
        bool test = (v->control & SID_CTRL_TEST) != 0;
        bool bit19 = (v->accumulator & 0x080000u) != 0;
        if (test) {
            v->lfsr = (uint32_t)(((v->lfsr << 1) | 1u) & 0x7FFFFFu);
        } else {
            bool prev_bit19 = (old_acc[i] & 0x080000u) != 0;
            if (bit19 && !prev_bit19) {
                uint32_t fb = (uint32_t)(((v->lfsr >> 22) ^ (v->lfsr >> 17)) & 1u);
                v->lfsr = (uint32_t)(((v->lfsr << 1) | fb) & 0x7FFFFFu);
            }
        }
        envelope_tick_one_cycle(v);
    }
}

void sid_tick(Sid *sid, uint32_t cycles) {
    for (uint32_t i = 0; i < cycles; i++) {
        sid_tick_one_cycle(sid);
    }
}

/* ---------------------------------------------------------------- */
/* Filter -- a simple, disclosed documented-approximation (Chamberlin  */
/* state-variable design), NOT reSID's transistor-level model. Its    */
/* own per-call recurrence assumes a nominal ~44100Hz call rate for    */
/* sid_output() -- see sid.h and docs/sid.md's Known Gaps.             */
/* ---------------------------------------------------------------- */

#define SID_FILTER_NOMINAL_SAMPLE_RATE 44100.0

static double apply_filter(Sid *sid, double input) {
    double cutoff_hz = 30.0 + ((double)sid->filter_cutoff / 2047.0) * (12000.0 - 30.0);
    double f = 2.0 * sin(3.14159265358979323846 * cutoff_hz / SID_FILTER_NOMINAL_SAMPLE_RATE);
    if (f > 1.0) f = 1.0;
    double q = 1.0 - ((double)sid->filter_resonance / 15.0) * 0.9; /* lower q -> sharper resonance */

    double normalized_input = input / (3.0 * 4095.0 * 255.0);

    double highpass = normalized_input - sid->filter_lp - q * sid->filter_bp;
    sid->filter_bp += f * highpass;
    sid->filter_lp += f * sid->filter_bp;

    double out = 0.0;
    if (sid->filter_mode & 0x10u) out += sid->filter_lp;
    if (sid->filter_mode & 0x20u) out += sid->filter_bp;
    if (sid->filter_mode & 0x40u) out += highpass;
    return out * (3.0 * 4095.0 * 255.0);
}

double sid_output(Sid *sid) {
    double direct_sum = 0.0;
    double filtered_sum = 0.0;

    for (int i = 0; i < 3; i++) {
        const SidVoice *v = &sid->voice[i];
        uint16_t wave = sid_voice_waveform_output(sid, i);
        double amp = (double)wave * (double)v->envelope;

        bool routed_to_filter = (sid->filter_route & (1u << i)) != 0;
        bool voice3_disconnected_direct = (i == 2) && (sid->filter_mode & 0x80u) != 0 && !routed_to_filter;

        if (routed_to_filter) {
            filtered_sum += amp;
        } else if (!voice3_disconnected_direct) {
            direct_sum += amp;
        }
    }

    double filtered = (sid->filter_mode & 0x70u) ? apply_filter(sid, filtered_sum) : filtered_sum;
    double mixed = direct_sum + filtered;

    double max_possible = 3.0 * 4095.0 * 255.0;
    double normalized = mixed / max_possible; /* 0..1 nominally */
    double centered = (normalized - 0.5) * 2.0; /* -1..1 */
    double vol = (double)sid->volume / 15.0;
    return centered * vol;
}
