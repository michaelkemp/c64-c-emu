#ifndef C64EMU_C64_VIC_II_H
#define C64EMU_C64_VIC_II_H

#include <stdbool.h>
#include <stdint.h>

/* MOS 6567/6569 VIC-II, PAL (6569) timing only -- see docs/vic-ii.md
 * and docs/sources.md. Every behavior fact here (bad-line condition,
 * VC/RC/VCBASE/VMLI rules, sprite DMA rules, border flip-flop rules,
 * register map, memory-access address formulas) is sourced directly
 * from Christian Bauer's cycle-by-cycle VIC-II article, read as raw
 * text (see docs/sources.md) rather than trusted from memory or an AI
 * summary of it, per this project's own methodology.
 *
 * GRANULARITY -- a real, disclosed architectural decision (see
 * docs/vic-ii.md's "Known gaps"): the *timing/bus-access* state
 * machine (bad lines, VC/RC, c/g/p/s-access scheduling, the BA/steal
 * signal, sprite DMA on/off, raster IRQ comparison, the two border
 * flip-flops' Y-checks) runs at true WHOLE-PHI2-CYCLE granularity,
 * driven by vic_ii_cycle() once per cycle exactly like the CPU core --
 * this is what makes badline cycle-stealing and raster-IRQ timing
 * real. *Pixel compositing* (border X-check, graphics color, sprite
 * overlay/expansion/priority/collision) is computed per PIXEL (8 per
 * cycle) as each cycle's data becomes available, and written into a
 * per-scanline buffer that's final once the line's 63 cycles complete.
 * This is coarser than literally simulating the real 24-bit sprite
 * shift registers and 8-bit graphics shift register cycle-by-cycle in
 * real time -- it will NOT reproduce sub-character-cell effects like
 * FLI, hyperscreen, linecrunch, or sprite stretching (docs/vic-ii.md
 * section 3.14-equivalent "Effects and applications" territory) -- but
 * it reproduces every one of this phase's own stated verification
 * targets: raster-timed register writes taking effect at the right
 * scanline, real badline CPU-cycle stealing, and correct sprite
 * priority/collision/movement. */

#define VIC_CYCLES_PER_LINE 63u
#define VIC_LINES_PER_FRAME 312u
#define VIC_FIRST_LINE_X 404u /* the X coordinate at the start of cycle 1, per the article's own reference point */
#define VIC_X_MODULUS (VIC_CYCLES_PER_LINE * 8u) /* 504 -- X coordinates run 0-503 on the 6569 */

/* $D019/$D01A interrupt bits */
#define VIC_IRQ_RST 0x01u
#define VIC_IRQ_MBC 0x02u
#define VIC_IRQ_MMC 0x04u
#define VIC_IRQ_LP 0x08u
#define VIC_IRQ_IRQ 0x80u /* read-only, computed: inverted state of the physical IRQ output */

/* $D011 bits */
#define VIC_D011_YSCROLL 0x07u
#define VIC_D011_RSEL 0x08u
#define VIC_D011_DEN 0x10u
#define VIC_D011_BMM 0x20u
#define VIC_D011_ECM 0x40u
#define VIC_D011_RST8 0x80u

/* $D016 bits */
#define VIC_D016_XSCROLL 0x07u
#define VIC_D016_CSEL 0x08u
#define VIC_D016_MCM 0x10u

typedef struct VicII {
    /* --- registers, $D000-$D02E --- */
    uint8_t sprite_x_lo[8]; /* $D000,$D002,...: low 8 bits of each sprite's X */
    uint8_t sprite_y[8];    /* $D001,$D003,...: Y coordinate (8 bits, full range) */
    uint8_t sprite_x_msb;   /* $D010: bit N = MSB of sprite N's 9-bit X */
    uint8_t ctrl1;          /* $D011 */
    uint8_t raster_compare; /* value written to $D012 (low 8 bits of the 9-bit raster compare) */
    uint8_t lpx, lpy;       /* $D013/$D014 -- lightpen, stubbed (see Known Gaps) */
    uint8_t sprite_enable;  /* $D015 */
    uint8_t ctrl2;          /* $D016 */
    uint8_t sprite_y_expansion; /* $D017 */
    uint8_t mem_ptrs;       /* $D018 */
    uint8_t irq_latch;      /* $D019, low 4 bits meaningful; write-1-to-clear (NOT read-clears, unlike the CIA's ICR) */
    uint8_t irq_enable;     /* $D01A, low 4 bits meaningful */
    uint8_t sprite_priority; /* $D01B: MxDP */
    uint8_t sprite_mc_select; /* $D01C: MxMC */
    uint8_t sprite_x_expansion; /* $D01D */
    uint8_t sprite_sprite_collision; /* $D01E, auto-clears on read */
    uint8_t sprite_bg_collision;     /* $D01F, auto-clears on read */
    uint8_t border_color;   /* $D020 */
    uint8_t bg_color[4];    /* $D021-$D024 */
    uint8_t sprite_mc[2];   /* $D025/$D026 */
    uint8_t sprite_color[8]; /* $D027-$D02E */

    /* --- VIC's own memory view --- */
    const uint8_t *ram;      /* shared with C64Memory's own backing RAM -- see docs/memory-map.md */
    const uint8_t *char_rom; /* shared with C64Memory's own char_rom -- used only in the two hardwired windows, see vic_ii_read() */
    const uint8_t *color_ram; /* shared with C64Memory's own color_ram -- Color RAM is wired directly to the c-access, NOT part of the bank-switched 16KB space */
    uint16_t bank_base;      /* 0/0x4000/0x8000/0xC000 -- set via vic_ii_set_bank(), driven by CIA2 Port A in real hardware (Phase 6's job to wire) */

    /* --- raster position --- */
    uint16_t raster_line; /* 0-311 */
    uint16_t cycle;        /* 1-63 */

    /* --- bad-line / DEN latch --- */
    bool den_seen_this_frame_at_line_30; /* see is_bad_line(): DEN must have been set at some point during line $30 */

    /* --- VC/RC/VCBASE/VMLI state machine, article section 3.7.2 --- */
    uint16_t vc, vcbase; /* 10-bit */
    uint8_t rc;          /* 3-bit */
    uint8_t vmli;        /* video matrix line index, 0-39 */
    bool display_state;  /* true = display state, false = idle state */

    /* video matrix/color line buffer: 40 entries of (8 data bits | 4 color bits) from this row's c-accesses */
    uint16_t vm_color_line[40];

    /* --- border flip-flops, article section 3.9 --- */
    bool main_border;
    bool vertical_border;

    /* --- DRAM refresh counter, article section 3.13 --- */
    uint8_t ref_counter;

    /* --- per-sprite internal state, article section 3.8.1 --- */
    uint8_t sprite_mdc[8];       /* MC: 6-bit MOB data counter (named sprite_mdc to avoid colliding with the sprite_mc[] multicolor-register field below) */
    uint8_t sprite_mcbase[8];    /* MCBASE */
    bool sprite_dma[8];
    bool sprite_display[8];
    bool sprite_advance_line[8];
    uint8_t sprite_pointer[8];   /* fetched MP byte (p-access) */
    uint8_t sprite_data[8][3];   /* fetched sprite line data (s-accesses) */

    /* --- per-scanline pixel/compositing buffers, indexed by X 0-503 --- */
    uint8_t line_color[VIC_X_MODULUS];
    bool line_is_foreground[VIC_X_MODULUS]; /* background/bitmap classification, for sprite-background collision */
    bool line_vertical_border[VIC_X_MODULUS]; /* suppresses sprite/graphics collisions where set */
    bool line_sprite_drawn[VIC_X_MODULUS];    /* a higher-priority sprite already drew here this line */
    uint8_t line_sprite_opaque_mask[VIC_X_MODULUS]; /* bit N = sprite N drew an opaque pixel here this line (sprite-sprite collision) */

    /* Output framebuffer: one entry per (raster_line, x), a 4-bit VIC
     * color index (0-15) -- Phase 7 maps this through the real C64
     * palette when blitting to the screen. */
    uint8_t framebuffer[VIC_LINES_PER_FRAME][VIC_X_MODULUS];
} VicII;

void vic_ii_init(VicII *vic, const uint8_t *ram, const uint8_t *char_rom, const uint8_t *color_ram);

/* bank: 0-3, matching the real (inverted) CIA2 Port A encoding already
 * resolved to a plain 0-3 bank number by the caller -- see
 * docs/vic-ii.md's "VIC-II's own memory view" section. */
void vic_ii_set_bank(VicII *vic, uint8_t bank);

/* Reads through the VIC's own 14-bit address space (0-16383, already
 * relative to the current bank), honoring the two hardwired windows
 * ($1000-$1FFF and $9000-$9FFF of the full 64KB space) that always
 * read Character ROM regardless of what's in RAM there -- see
 * docs/vic-ii.md. Exposed for tests/diagnostics; vic_ii_cycle() uses
 * this internally for all c/g/p/s-accesses. */
uint8_t vic_ii_read(const VicII *vic, uint16_t vic_addr14);

uint8_t vic_ii_reg_read(VicII *vic, uint8_t reg);
void vic_ii_reg_write(VicII *vic, uint8_t reg, uint8_t value);

/* Advances exactly one PHI2 cycle. Returns true if the VIC claims the
 * bus this cycle (the CPU must not access the bus -- see
 * docs/machine.md's cycle-interleaving section); this is a
 * simplification of the real BA/RDY/AEC mechanism (which lets a
 * write-in-progress finish before actually stalling reads) down to a
 * plain "CPU stalls or not" signal, matching the shape
 * docs/vic-ii.md's own sketch calls for. */
bool vic_ii_cycle(VicII *vic);

/* True on the exact cycle a raster-line IRQ condition is newly
 * detected (i.e. the moment $D019's RST bit gets set) -- Phase 6 ORs
 * this (and the other three $D019 sources' enabled state) into the
 * CPU's IRQ line. cia_irq_asserted()-style "is anything currently
 * pending and enabled" is vic_ii_irq_asserted() below. */
bool vic_ii_irq_asserted(const VicII *vic);

#endif /* C64EMU_C64_VIC_II_H */
