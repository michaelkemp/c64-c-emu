/* MOS 6567/6569 VIC-II (PAL timing). See vic_ii.h for the granularity
 * decision this file implements, and docs/vic-ii.md / docs/sources.md
 * for the primary-source facts behind every rule below (rule numbers
 * in comments refer to Christian Bauer's article's own numbering). */

#include "vic_ii.h"

#include <string.h>

/* ---------------------------------------------------------------- */
/* Border comparator values (article section 3.9)                    */
/* ---------------------------------------------------------------- */

static uint16_t border_left(const VicII *vic) { return (vic->ctrl2 & VIC_D016_CSEL) ? 24u : 31u; }
static uint16_t border_right(const VicII *vic) { return (vic->ctrl2 & VIC_D016_CSEL) ? 344u : 335u; }
static uint16_t border_top(const VicII *vic) { return (vic->ctrl1 & VIC_D011_RSEL) ? 51u : 55u; }
static uint16_t border_bottom(const VicII *vic) { return (vic->ctrl1 & VIC_D011_RSEL) ? 251u : 247u; }

/* ---------------------------------------------------------------- */
/* Memory access                                                     */
/* ---------------------------------------------------------------- */

uint8_t vic_ii_read(const VicII *vic, uint16_t vic_addr14) {
    uint16_t full = (uint16_t)(vic->bank_base + (vic_addr14 & 0x3FFFu));
    /* Character ROM is permanently wired into two specific 4KB windows
     * of the full 64KB address space -- $1000-$1FFF and $9000-$9FFF --
     * regardless of what's in RAM there, for whichever VIC bank(s)
     * happen to overlap them (banks 0 and 2). This is a C64-board
     * wiring fact, not a VIC-II chip fact -- see docs/vic-ii.md's
     * Known Gaps for its verification status. */
    if ((full >= 0x1000u && full <= 0x1FFFu) || (full >= 0x9000u && full <= 0x9FFFu)) {
        return vic->char_rom[full & 0x0FFFu];
    }
    return vic->ram[full];
}

void vic_ii_set_bank(VicII *vic, uint8_t bank) {
    vic->bank_base = (uint16_t)((bank & 0x03u) * 0x4000u);
}

/* ---------------------------------------------------------------- */
/* Init                                                               */
/* ---------------------------------------------------------------- */

void vic_ii_init(VicII *vic, const uint8_t *ram, const uint8_t *char_rom, const uint8_t *color_ram) {
    memset(vic, 0, sizeof(*vic));
    vic->ram = ram;
    vic->char_rom = char_rom;
    vic->color_ram = color_ram;
    vic->cycle = 1;
    vic->ref_counter = 0xFFu;
    /* The article doesn't state a power-on default for the two border
     * flip-flops (they're internal, not a numbered register); real
     * hardware visibly shows a full-screen border before the KERNAL
     * configures the display, so both start set -- a reasonable,
     * disclosed initialization choice rather than a primary-sourced
     * fact. See docs/vic-ii.md's Known Gaps. */
    vic->main_border = true;
    vic->vertical_border = true;
}

/* ---------------------------------------------------------------- */
/* Register read/write. Mirrored every 64 bytes in $D000-$D3FF -- the  */
/* caller (C64Memory, Phase 6) is responsible for that address         */
/* folding, same convention as the CIA; `reg` here is already 0-0x2E   */
/* or an out-of-range value for the unused $D02F-$D03F hole.           */
/* ---------------------------------------------------------------- */

uint8_t vic_ii_reg_read(VicII *vic, uint8_t reg) {
    switch (reg) {
    case 0x00: case 0x02: case 0x04: case 0x06: case 0x08: case 0x0A: case 0x0C: case 0x0E:
        return vic->sprite_x_lo[reg / 2];
    case 0x01: case 0x03: case 0x05: case 0x07: case 0x09: case 0x0B: case 0x0D: case 0x0F:
        return vic->sprite_y[reg / 2];
    case 0x10: return vic->sprite_x_msb;
    case 0x11: return vic->ctrl1;
    case 0x12: return (uint8_t)(vic->raster_line & 0xFFu);
    case 0x13: return vic->lpx;
    case 0x14: return vic->lpy;
    case 0x15: return vic->sprite_enable;
    case 0x16: return (uint8_t)(vic->ctrl2 | 0xC0u); /* bits 6,7 unconnected, read as 1 */
    case 0x17: return vic->sprite_y_expansion;
    case 0x18: return (uint8_t)(vic->mem_ptrs | 0x01u); /* bit 0 unconnected */
    case 0x19: return (uint8_t)(vic->irq_latch | 0x70u | (vic_ii_irq_asserted(vic) ? VIC_IRQ_IRQ : 0u));
    case 0x1A: return (uint8_t)(vic->irq_enable | 0xF0u);
    case 0x1B: return vic->sprite_priority;
    case 0x1C: return vic->sprite_mc_select;
    case 0x1D: return vic->sprite_x_expansion;
    case 0x1E: { /* auto-clears on read */
        uint8_t v = vic->sprite_sprite_collision;
        vic->sprite_sprite_collision = 0;
        return v;
    }
    case 0x1F: {
        uint8_t v = vic->sprite_bg_collision;
        vic->sprite_bg_collision = 0;
        return v;
    }
    case 0x20: return (uint8_t)(vic->border_color | 0xF0u);
    case 0x21: case 0x22: case 0x23: case 0x24: return (uint8_t)(vic->bg_color[reg - 0x21] | 0xF0u);
    case 0x25: case 0x26: return (uint8_t)(vic->sprite_mc[reg - 0x25] | 0xF0u);
    case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E:
        return (uint8_t)(vic->sprite_color[reg - 0x27] | 0xF0u);
    default:
        return 0xFFu; /* $D02F-$D03F: unused, reads as $FF */
    }
}

void vic_ii_reg_write(VicII *vic, uint8_t reg, uint8_t value) {
    switch (reg) {
    case 0x00: case 0x02: case 0x04: case 0x06: case 0x08: case 0x0A: case 0x0C: case 0x0E:
        vic->sprite_x_lo[reg / 2] = value; break;
    case 0x01: case 0x03: case 0x05: case 0x07: case 0x09: case 0x0B: case 0x0D: case 0x0F:
        vic->sprite_y[reg / 2] = value; break;
    case 0x10: vic->sprite_x_msb = value; break;
    case 0x11:
        vic->ctrl1 = value;
        if (value & VIC_D011_DEN) {
            vic->den_seen_this_frame_at_line_30 = true; /* approximation: "seen at some point" rather than exactly "during line $30", see Known Gaps */
        }
        break;
    case 0x12: vic->raster_compare = value; break;
    case 0x13: vic->lpx = value; break; /* stub, real hardware is read-only from lightpen latching */
    case 0x14: vic->lpy = value; break;
    case 0x15: vic->sprite_enable = value; break;
    case 0x16: vic->ctrl2 = value; break;
    case 0x17: vic->sprite_y_expansion = value; break;
    case 0x18: vic->mem_ptrs = value; break;
    case 0x19: /* write-1-to-clear -- NOT read-clears, unlike the CIA's ICR */
        vic->irq_latch = (uint8_t)(vic->irq_latch & (uint8_t)~(value & 0x0Fu));
        break;
    case 0x1A: vic->irq_enable = (uint8_t)(value & 0x0Fu); break;
    case 0x1B: vic->sprite_priority = value; break;
    case 0x1C: vic->sprite_mc_select = value; break;
    case 0x1D: vic->sprite_x_expansion = value; break;
    case 0x1E: case 0x1F: break; /* read-only, cannot be written */
    case 0x20: vic->border_color = value; break;
    case 0x21: case 0x22: case 0x23: case 0x24: vic->bg_color[reg - 0x21] = value; break;
    case 0x25: case 0x26: vic->sprite_mc[reg - 0x25] = value; break;
    case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E:
        vic->sprite_color[reg - 0x27] = value; break;
    default: break; /* $D02F-$D03F: ignored */
    }
}

bool vic_ii_irq_asserted(const VicII *vic) {
    return (vic->irq_latch & vic->irq_enable & 0x0Fu) != 0;
}

static void raise_irq(VicII *vic, uint8_t source) {
    vic->irq_latch = (uint8_t)(vic->irq_latch | source);
}

/* ---------------------------------------------------------------- */
/* Bad Line Condition -- article section 3.5, quoted in docs/vic-ii.md */
/* ---------------------------------------------------------------- */

static bool is_bad_line(const VicII *vic) {
    uint8_t yscroll = (uint8_t)(vic->ctrl1 & VIC_D011_YSCROLL);
    return vic->raster_line >= 0x30u && vic->raster_line <= 0xF7u &&
           (uint8_t)(vic->raster_line & 0x07u) == yscroll && vic->den_seen_this_frame_at_line_30;
}

/* ---------------------------------------------------------------- */
/* Graphics mode g-access: produces 8 (standard) or 4 (multicolor,     */
/* each pixel twice as wide) pixel colors for the current character/   */
/* bitmap column. Only the four non-ECM, non-invalid modes are         */
/* implemented -- ECM and the three invalid combinations are a         */
/* disclosed, deferred gap (see docs/vic-ii.md and the Known Gaps      */
/* section there); ECM/invalid modes render as plain black instead.    */
/* ---------------------------------------------------------------- */

static void render_pixels_from_byte(VicII *vic, uint16_t x_base, uint8_t pixel_byte, uint16_t c_data,
                                     bool multicolor, bool is_bitmap) {
    bool ecm = (vic->ctrl1 & VIC_D011_ECM) != 0;
    if (ecm) {
        /* Deferred -- see docs/vic-ii.md. Render as black, matching
         * the article's own statement that invalid/ECM combinations
         * this project hasn't implemented all produce solid black. */
        for (int p = 0; p < 8; p++) {
            uint16_t x = (uint16_t)((x_base + p) % VIC_X_MODULUS);
            vic->line_color[x] = 0;
            vic->line_is_foreground[x] = false;
        }
        return;
    }

    if (!multicolor) {
        uint8_t fg_color = is_bitmap ? (uint8_t)((c_data >> 4) & 0x0Fu) : (uint8_t)((c_data >> 8) & 0x0Fu);
        uint8_t bg_color = is_bitmap ? (uint8_t)(c_data & 0x0Fu) : vic->bg_color[0];
        for (int p = 0; p < 8; p++) {
            bool set = (pixel_byte & (0x80u >> p)) != 0;
            uint16_t x = (uint16_t)((x_base + p) % VIC_X_MODULUS);
            vic->line_color[x] = set ? fg_color : bg_color;
            vic->line_is_foreground[x] = set;
        }
        return;
    }

    /* Multicolor: two adjacent bits form one (double-wide) pixel. */
    uint8_t colors[4];
    if (is_bitmap) {
        colors[0] = vic->bg_color[0];
        colors[1] = (uint8_t)((c_data >> 4) & 0x0Fu);
        colors[2] = (uint8_t)(c_data & 0x0Fu);
        colors[3] = (uint8_t)((c_data >> 8) & 0x0Fu);
    } else {
        bool mc_flag = (c_data & 0x0800u) != 0;
        if (!mc_flag) {
            /* Falls back to standard-mode rendering per the article. */
            render_pixels_from_byte(vic, x_base, pixel_byte, c_data, false, false);
            return;
        }
        colors[0] = vic->bg_color[0];
        colors[1] = vic->bg_color[1];
        colors[2] = vic->bg_color[2];
        colors[3] = (uint8_t)((c_data >> 8) & 0x0Fu);
    }
    for (int group = 0; group < 4; group++) {
        uint8_t bits = (uint8_t)((pixel_byte >> (6 - group * 2)) & 0x03u);
        bool foreground = bits >= 2; /* "00","01" = background; "10","11" = foreground -- article section 3.8.2 */
        for (int sub = 0; sub < 2; sub++) {
            uint16_t x = (uint16_t)((x_base + group * 2 + sub) % VIC_X_MODULUS);
            vic->line_color[x] = colors[bits];
            vic->line_is_foreground[x] = foreground;
        }
    }
}

static void do_g_access(VicII *vic, uint16_t x_base) {
    bool bmm = (vic->ctrl1 & VIC_D011_BMM) != 0;
    bool mcm = (vic->ctrl2 & VIC_D016_MCM) != 0;
    uint8_t cb = (uint8_t)((vic->mem_ptrs >> 1) & 0x07u); /* CB13-11 */

    uint16_t c_data = vic->vm_color_line[vic->vmli];
    uint8_t char_code = (uint8_t)(c_data & 0xFFu);
    uint8_t pixel_byte;

    if (!vic->display_state) {
        /* Idle state: g-access always at $3FFF (ECM: $39FF), video
         * matrix data treated as "0" bits -- article section 3.7.1. */
        uint16_t addr = (vic->ctrl1 & VIC_D011_ECM) ? 0x39FFu : 0x3FFFu;
        pixel_byte = vic_ii_read(vic, addr);
        render_pixels_from_byte(vic, x_base, pixel_byte, 0, mcm, false);
        return;
    }

    if (bmm) {
        uint16_t addr = (uint16_t)(((cb & 0x04u) << 11) | (vic->vc << 3) | vic->rc);
        pixel_byte = vic_ii_read(vic, addr);
        render_pixels_from_byte(vic, x_base, pixel_byte, c_data, mcm, true);
    } else {
        uint16_t addr = (uint16_t)((cb << 11) | ((uint16_t)char_code << 3) | vic->rc);
        pixel_byte = vic_ii_read(vic, addr);
        render_pixels_from_byte(vic, x_base, pixel_byte, c_data, mcm, false);
    }
}

static void do_c_access(VicII *vic) {
    uint16_t vm = (uint16_t)((vic->mem_ptrs >> 4) & 0x0Fu); /* VM13-10 */
    uint16_t addr = (uint16_t)((vm << 10) | vic->vc);
    uint8_t data = vic_ii_read(vic, addr);
    uint8_t color = (uint8_t)(vic->color_ram[vic->vc & 0x03FFu] & 0x0Fu); /* Color RAM is wired directly to the c-access, not through the bank/char-ROM path */

    /* A real, previously-misapplied bug lived here: this unconditionally
     * forced $FF for cycles 15-17 on EVERY bad line, citing article
     * section 3.14.3 (FLI). Re-reading that section directly (raw
     * .txt, docs/sources.md) shows the $ff-garbage is NOT a property of
     * ordinary bad lines at all -- it only happens when a Bad Line
     * Condition is created artificially LATE, specifically by writing
     * $d011 at cycle 14 instead of the condition already being true
     * before cycle 12 (the FLI trick). The article's own reasoning:
     * the first c-access needs BA low since cycle 12 for AEC to have
     * settled (3 cycles) by cycle 15; on a normal bad line BA already
     * goes low at cycle 12 (see is_bad_line()/the bus-steal window
     * below), so AEC is settled in time and c-access data is valid
     * from cycle 15 onward -- only the FLI trick's artificially late
     * (cycle-14) BA transition leaves AEC unsettled at cycle 15/16.
     * Unconditionally forcing $ff corrupted every single normal text
     * display's first two-plus columns -- caught when a live, readable
     * SDL2 rendering (Phase 7) made that implausible on sight ("Commodore
     * would not have shipped a machine with unreadable text"), which a
     * purely-behavioral automated check comparing screen memory content
     * never would have caught. Reproducing the real, narrow FLI-only
     * effect (which needs tracking exactly when BA went low relative to
     * cycle 12 for the CURRENT line, not a fixed cycle range) is out of
     * scope per this project's own already-disclosed granularity
     * decision that FLI is not reproducible under this model (see
     * vic_ii.h) -- so this is simply not modeled, not even
     * approximately. */

    vic->vm_color_line[vic->vmli] = (uint16_t)(((uint16_t)color << 8) | data);
}

/* ---------------------------------------------------------------- */
/* VC/RC/VCBASE/VMLI -- article section 3.7.2, rules 1-5              */
/* ---------------------------------------------------------------- */

static void update_vc_rc(VicII *vic) {
    /* Rule 1: outside the bad-line raster range, VCBASE resets to 0.
     * Approximated as "at the top of the frame" (raster line 0) rather
     * than "somewhere outside $30-$f7" per the article's own "the exact
     * moment cannot be determined and is irrelevant" note. */
    if (vic->raster_line == 0 && vic->cycle == 1) {
        vic->vcbase = 0;
    }

    if (vic->cycle == 14) {
        vic->vc = vic->vcbase;
        vic->vmli = 0;
        if (is_bad_line(vic)) {
            vic->rc = 0;
        }
    }

    if (is_bad_line(vic)) {
        /* "The transition from idle to display state occurs as soon as
         * there is a Bad Line Condition" -- checked every cycle (not
         * gated to one fixed cycle) so a mid-line YSCROLL/DEN change
         * that creates a Bad Line Condition partway through a line is
         * still honored, matching real hardware. */
        vic->display_state = true;
    }

    if (vic->cycle == 58) {
        if (vic->rc == 7) {
            vic->display_state = is_bad_line(vic); /* "goes to idle state... unless there is a Bad Line Condition" */
            vic->vcbase = vic->vc;
        }
        if (vic->display_state) {
            vic->rc = (uint8_t)((vic->rc + 1) & 0x07u);
        }
    }
}

/* ---------------------------------------------------------------- */
/* Sprites -- article section 3.8.1, rules 1-7 (7a deliberately        */
/* omitted, see docs/vic-ii.md's Known Gaps)                          */
/* ---------------------------------------------------------------- */

static const uint8_t SPRITE_P_CYCLE[8] = {58, 60, 62, 1, 3, 5, 7, 9};

static int sprite_for_p_cycle(uint8_t cycle) {
    for (int n = 0; n < 8; n++) {
        if (SPRITE_P_CYCLE[n] == cycle) return n;
    }
    return -1;
}
static int sprite_for_s_cycle(uint8_t cycle) {
    for (int n = 0; n < 8; n++) {
        uint8_t s = (uint8_t)(SPRITE_P_CYCLE[n] % VIC_CYCLES_PER_LINE + 1);
        if (s == cycle) return n;
    }
    return -1;
}

static void update_sprite_dma(VicII *vic) {
    /* Rule 1 (continuous condition): the advance-line flip-flop is
     * held set whenever Y-expansion is disabled for that sprite. */
    for (int n = 0; n < 8; n++) {
        if (!(vic->sprite_y_expansion & (1u << n))) {
            vic->sprite_advance_line[n] = true;
        }
    }

    if (vic->cycle == 55 || vic->cycle == 56) {
        for (int n = 0; n < 8; n++) {
            bool enabled = (vic->sprite_enable & (1u << n)) != 0;
            bool y_match = vic->sprite_y[n] == (uint8_t)(vic->raster_line & 0xFFu);
            if (enabled && y_match && !vic->sprite_dma[n]) {
                vic->sprite_dma[n] = true;
                vic->sprite_mcbase[n] = 0;
                vic->sprite_advance_line[n] = true;
            }
        }
    }

    if (vic->cycle == 56) {
        for (int n = 0; n < 8; n++) {
            if ((vic->sprite_y_expansion & (1u << n)) && vic->sprite_dma[n]) {
                vic->sprite_advance_line[n] = !vic->sprite_advance_line[n];
            }
        }
    }

    if (vic->cycle == 58) {
        for (int n = 0; n < 8; n++) {
            vic->sprite_mdc[n] = vic->sprite_mcbase[n];
            bool y_match = vic->sprite_y[n] == (uint8_t)(vic->raster_line & 0xFFu);
            if (vic->sprite_dma[n] && y_match) {
                vic->sprite_display[n] = true;
            } else if (!vic->sprite_dma[n]) {
                vic->sprite_display[n] = false;
            }
        }
    }

    if (vic->cycle == 16) {
        for (int n = 0; n < 8; n++) {
            if (vic->sprite_advance_line[n]) {
                vic->sprite_mcbase[n] = vic->sprite_mdc[n];
            }
            if (vic->sprite_mcbase[n] == 63) {
                vic->sprite_dma[n] = false;
            }
        }
    }

    int p_sprite = sprite_for_p_cycle((uint8_t)vic->cycle);
    if (p_sprite >= 0) {
        /* p-access always happens, even for a sprite whose DMA is off. */
        uint16_t vm = (uint16_t)((vic->mem_ptrs >> 4) & 0x0Fu);
        uint16_t p_addr = (uint16_t)((vm << 10) | 0x03F8u | (unsigned)p_sprite);
        vic->sprite_pointer[p_sprite] = vic_ii_read(vic, p_addr);
    }

    int s_sprite = sprite_for_s_cycle((uint8_t)vic->cycle);
    if (s_sprite >= 0 && vic->sprite_dma[s_sprite]) {
        /* Real hardware spreads the 3 s-accesses across "three half-
         * cycles"; this whole-cycle model fetches all 3 bytes in the
         * one cycle reserved for that sprite -- see vic_ii.h's
         * granularity note. */
        uint16_t base = (uint16_t)(((uint16_t)vic->sprite_pointer[s_sprite] << 6) & 0x3FC0u);
        for (int k = 0; k < 3; k++) {
            vic->sprite_data[s_sprite][k] = vic_ii_read(vic, (uint16_t)(base | (uint8_t)(vic->sprite_mdc[s_sprite] + k)));
        }
        vic->sprite_mdc[s_sprite] = (uint8_t)(vic->sprite_mdc[s_sprite] + 3);
    }
}

static bool sprite_dma_active_this_cycle(const VicII *vic, uint8_t cycle) {
    int p = sprite_for_p_cycle(cycle);
    if (p >= 0 && vic->sprite_dma[p]) return true;
    int s = sprite_for_s_cycle(cycle);
    if (s >= 0 && vic->sprite_dma[s]) return true;
    return false;
}

/* Renders all currently-displayed sprites into the just-finished
 * scanline's line_color[]/line_is_foreground[] buffers, respecting
 * priority (sprite 0 highest; MxDP front/behind foreground) and
 * updating the collision registers -- article section 3.8.2. Called
 * once per line, after all of that line's background pixels are in
 * place. */
static void composite_sprites_for_line(VicII *vic) {
    /* "Only the first collision will trigger an interrupt (i.e. if the
     * collision register contained the value zero before the
     * collision)" -- article section 3.8.2. The latch bits in $D019
     * themselves are set unconditionally on every collision (matching
     * "each source... sets the corresponding bit... regardless of the
     * mask" behavior shared across this chip family); only the *first*
     * transition from all-clear actually raises the IRQ-eligible
     * latch bit for this line's compositing pass. */
    bool ss_was_clear = vic->sprite_sprite_collision == 0;
    bool bg_was_clear = vic->sprite_bg_collision == 0;

    for (int n = 0; n < 8; n++) {
        if (!vic->sprite_display[n]) continue;

        bool multicolor = (vic->sprite_mc_select & (1u << n)) != 0;
        bool x_expand = (vic->sprite_x_expansion & (1u << n)) != 0;
        bool behind_foreground = (vic->sprite_priority & (1u << n)) != 0;
        uint16_t sprite_x = (uint16_t)(vic->sprite_x_lo[n] | (((vic->sprite_x_msb >> n) & 1u) << 8));
        int expand = x_expand ? 2 : 1;
        int width = 24 * expand;

        for (int off = 0; off < width; off++) {
            uint16_t x = (uint16_t)((sprite_x + off) % VIC_X_MODULUS);
            uint8_t color;
            bool opaque;

            if (multicolor) {
                int group = (off / expand) / 2;
                uint8_t byte = vic->sprite_data[n][group / 4];
                int bit_pos = 6 - (group % 4) * 2;
                uint8_t bits = (uint8_t)((byte >> bit_pos) & 0x03u);
                opaque = bits != 0;
                color = (bits == 1) ? vic->sprite_mc[0] : (bits == 2) ? vic->sprite_color[n] : vic->sprite_mc[1];
            } else {
                int bit_index = off / expand;
                uint8_t byte = vic->sprite_data[n][bit_index / 8];
                opaque = (byte & (0x80u >> (bit_index % 8))) != 0;
                color = vic->sprite_color[n];
            }

            if (!opaque) continue;

            /* Sprite-sprite collision: any other displayed, opaque
             * sprite pixel already recorded at this x this line. */
            if (vic->line_sprite_opaque_mask[x] != 0) {
                vic->sprite_sprite_collision |= (uint8_t)(vic->line_sprite_opaque_mask[x] | (1u << n));
            }
            vic->line_sprite_opaque_mask[x] = (uint8_t)(vic->line_sprite_opaque_mask[x] | (1u << n));

            /* Sprite-background collision: suppressed within the
             * vertical border, per article section 3.8.2. */
            if (!vic->line_vertical_border[x] && vic->line_is_foreground[x]) {
                vic->sprite_bg_collision |= (uint8_t)(1u << n);
            }

            /* Priority: sprite 0 highest. A higher-priority sprite that
             * already drew an opaque pixel here wins outright. */
            if (vic->line_sprite_drawn[x]) continue;

            bool draw_over_foreground = !(behind_foreground && vic->line_is_foreground[x]);
            if (draw_over_foreground) {
                vic->line_color[x] = color;
            }
            vic->line_sprite_drawn[x] = true;
        }
    }

    if (ss_was_clear && vic->sprite_sprite_collision != 0) {
        raise_irq(vic, VIC_IRQ_MMC);
    }
    if (bg_was_clear && vic->sprite_bg_collision != 0) {
        raise_irq(vic, VIC_IRQ_MBC);
    }
}

/* ---------------------------------------------------------------- */
/* Border compositing -- article section 3.9, rules 1-6               */
/* ---------------------------------------------------------------- */

static void update_border_and_render(VicII *vic, uint16_t x_base) {
    uint16_t left = border_left(vic);
    uint16_t right = border_right(vic);
    uint16_t top = border_top(vic);
    uint16_t bottom = border_bottom(vic);
    bool den = (vic->ctrl1 & VIC_D011_DEN) != 0;

    for (int p = 0; p < 8; p++) {
        uint16_t x = (uint16_t)((x_base + p) % VIC_X_MODULUS);

        if (x == left) {
            if (vic->raster_line == bottom) vic->vertical_border = true;
            if (vic->raster_line == top && den) vic->vertical_border = false;
        }
        if (x == right) {
            vic->main_border = true;
        }
        if (x == left && !vic->vertical_border) {
            vic->main_border = false;
        }

        vic->line_vertical_border[x] = vic->vertical_border;
        if (vic->main_border) {
            vic->line_color[x] = vic->border_color;
            vic->line_is_foreground[x] = false;
        } else if (vic->vertical_border) {
            /* Sequencer output is suppressed; background color shows through. */
            vic->line_color[x] = vic->bg_color[0];
            vic->line_is_foreground[x] = false;
        }
        /* else: already populated by do_g_access()'s render_pixels_from_byte(). */
    }
}

/* ---------------------------------------------------------------- */
/* Public per-cycle step                                             */
/* ---------------------------------------------------------------- */

bool vic_ii_cycle(VicII *vic) {
    /* Rules 2/3 (article 3.9): the Y-coordinate border checks tied to
     * "cycle 63" use the raster line that is about to finish. */
    if (vic->cycle == 63) {
        uint16_t bottom = border_bottom(vic);
        uint16_t top = border_top(vic);
        if (vic->raster_line == bottom) vic->vertical_border = true;
        if (vic->raster_line == top && (vic->ctrl1 & VIC_D011_DEN)) vic->vertical_border = false;
    }

    if (vic->raster_line == 0x30u && (vic->ctrl1 & VIC_D011_DEN)) {
        vic->den_seen_this_frame_at_line_30 = true;
    }

    update_vc_rc(vic);
    update_sprite_dma(vic);

    /* A second real, confirmed bug lived here (found alongside the
     * do_c_access() bug above, both surfaced by the same live-display
     * investigation): c-access and g-access were both run in the SAME
     * cycle, using the SAME vmli, with zero pipeline delay between
     * "fetch the character pointer" and "use it to render pixels."
     * Real hardware has a genuine ONE-CYCLE pipeline: rule 3 (article
     * 3.7.2) says a c-access happens "in the SECOND phase" of cycles
     * 15-54; decoding the article's own cycle-by-cycle timing diagram
     * (its embedded X-coordinate hex encoding, independently cross-
     * checked against its "First X coo. of a line: 404" table, and its
     * explicit phi0-phase legend) shows the g-access that CONSUMES that
     * data runs in the FIRST phase of the *next* cycle (16-55), not the
     * same one -- confirmed by a second, independent source (schepers'
     * "memory accesses of the 6569/8566": "the character pointers will
     * be fetched one cycle before the image data..."). Zero-delay
     * rendering shifted every displayed pixel one full character cell
     * (8 pixels) to the left of where it belongs -- on a real 40-column
     * boot screen this pushed genuine text (not just border) out of the
     * visible window on the left, and wrapped stale/wrong data in on
     * the right, which is likely what was actually behind at least part
     * of the visible corruption previously (wrongly) blamed entirely on
     * the fabricated "forced $ff" quirk above. Fixed by running the
     * g-access/render/VC-VMLI-increment step for cycle N+1 relative to
     * the c-access that fed it (cycle N), reordered below so a cycle
     * that does both (16-54) increments VMLI *before* that same cycle's
     * own c-access, exactly matching the real phase1-then-phase2 order. */

    /* A third real, empirically-confirmed offset lives here, on top of
     * the one-cycle c/g pipeline above: even after that fix, a
     * synthetic full-40-column/25-row test screen showed column 0
     * rendering only 4 of its 8 pixels (the other 4 overwritten by the
     * still-active left border) while every other column rendered at
     * full width -- direct pixel measurement against the known border
     * comparator value (24) showed the g-access's actual pixel OUTPUT
     * lands 4 pixels (half a cycle) later than its own cycle's raw
     * VIC_FIRST_LINE_X-derived x_base. This is a distinct fact from
     * VIC_FIRST_LINE_X itself (which is a directly primary-sourced,
     * independently-confirmed constant -- see machine.h/vic_ii.h -- and
     * is left untouched here); it's specifically the display pipeline's
     * own fetch-to-visible-output latency, which the article's own
     * "Graph." line explicitly warns is NOT reliable to derive this
     * from ("doesn't correspond to the signal on the VIC video
     * output") -- so this +4 was determined empirically, by measuring
     * exact pixel boundaries in a rendered synthetic test frame against
     * the known, directly-stated border_left=24 constant, rather than
     * asserted from the article text. Applied uniformly to every
     * cycle's x_base (not just text-rendering cycles) so the per-pixel
     * border comparator loop still tiles all 504 X positions exactly
     * once per line with no gap or overlap. Sprite positioning is
     * unaffected -- it compares directly against the raw X coordinate
     * space via vic->sprite_x_lo/msb, never through this x_base. */
    uint16_t x_base = (uint16_t)((VIC_FIRST_LINE_X + (vic->cycle - 1) * 8u + 4u) % VIC_X_MODULUS);
    if (vic->cycle >= 16 && vic->cycle <= 55) {
        do_g_access(vic, x_base);
        update_border_and_render(vic, x_base);
        if (vic->display_state) {
            vic->vc = (uint16_t)((vic->vc + 1) & 0x03FFu);
            vic->vmli = (uint8_t)((vic->vmli + 1) % 40u);
        }
    } else {
        update_border_and_render(vic, x_base);
    }

    bool badline_window = is_bad_line(vic) && vic->cycle >= 15 && vic->cycle <= 54;
    if (badline_window) {
        do_c_access(vic);
    }

    /* DRAM refresh, cycles 11-15 -- article section 3.13. Result
     * discarded; only the counter's own decrement is observable. */
    if (vic->cycle >= 11 && vic->cycle <= 15) {
        vic->ref_counter--;
    }

    /* Raster IRQ compare -- checked in cycle 1 (article section 3.12;
     * the line-0-specific "cycle 2" exception is a disclosed, minor
     * gap, see docs/vic-ii.md). */
    if (vic->cycle == 1) {
        uint16_t compare = (uint16_t)(vic->raster_compare | ((vic->ctrl1 & VIC_D011_RST8) ? 0x100u : 0u));
        if (vic->raster_line == compare) {
            raise_irq(vic, VIC_IRQ_RST);
        }
    }

    bool bus_stolen = is_bad_line(vic) && vic->cycle >= 12 && vic->cycle <= 54; /* BA low cycles 12-54 on a bad line -- rule 3 */
    bus_stolen = bus_stolen || sprite_dma_active_this_cycle(vic, (uint8_t)vic->cycle);

    /* Advance cycle/raster line, committing the finished scanline's
     * sprite compositing pass and framebuffer copy at the line's end. */
    if (vic->cycle == VIC_CYCLES_PER_LINE) {
        composite_sprites_for_line(vic);

        /* Blanking: the real video signal is forced off (no picture at
         * all, not border color) during horizontal/vertical sync and
         * blanking -- a genuinely separate window from the border
         * flip-flops' own on/off state, which keep painting border
         * color straight through this window (see vic_ii.h's own
         * comment on VIC_FIRST_VISIBLE_X and friends for why these two
         * concepts are different). Applied here, once per line, as an
         * override on top of whatever border/graphics color compositing
         * already computed into line_color[]. */
        bool line_is_vblank = vic->raster_line < VIC_FIRST_VISIBLE_LINE || vic->raster_line > VIC_LAST_VISIBLE_LINE;
        if (line_is_vblank) {
            memset(vic->line_color, 0, sizeof(vic->line_color));
        } else {
            for (uint16_t x = (uint16_t)(VIC_LAST_VISIBLE_X + 1u); x < VIC_FIRST_VISIBLE_X; x++) {
                vic->line_color[x] = 0;
            }
        }

        memcpy(vic->framebuffer[vic->raster_line], vic->line_color, sizeof(vic->line_color));

        vic->cycle = 1;
        vic->raster_line = (uint16_t)((vic->raster_line + 1) % VIC_LINES_PER_FRAME);
        if (vic->raster_line == 0) {
            vic->den_seen_this_frame_at_line_30 = false;
            vic->ref_counter = 0xFFu;
        }
        memset(vic->line_sprite_drawn, 0, sizeof(vic->line_sprite_drawn));
        memset(vic->line_sprite_opaque_mask, 0, sizeof(vic->line_sprite_opaque_mask));
    } else {
        vic->cycle++;
    }

    return bus_stolen;
}
