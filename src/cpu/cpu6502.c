/* NMOS 6502/6510 core, cycle-stepped. See cpu6502.h and
 * docs/6502-reference.md for the interface contract and the specific
 * documented-hardware facts (JMP indirect page bug, decimal-mode
 * quirks, the B flag not being a real stored flag, etc.) this file
 * implements. Undocumented/"illegal" opcodes are deferred to Phase 10
 * -- see the OP_ILLEGAL handling at the bottom of cpu6502_cycle(). */

#include "cpu6502.h"

#include <string.h>

/* ---------------------------------------------------------------- */
/* Flag helpers                                                      */
/* ---------------------------------------------------------------- */

static inline void set_flag(Cpu6502 *cpu, uint8_t flag, bool value) {
    if (value) {
        cpu->p |= flag;
    } else {
        cpu->p &= (uint8_t)~flag;
    }
}

static inline bool get_flag(const Cpu6502 *cpu, uint8_t flag) {
    return (cpu->p & flag) != 0;
}

static inline void set_nz(Cpu6502 *cpu, uint8_t value) {
    set_flag(cpu, CPU6502_FLAG_Z, value == 0);
    set_flag(cpu, CPU6502_FLAG_N, (value & 0x80u) != 0);
}

static inline uint16_t stack_addr(const Cpu6502 *cpu) {
    return (uint16_t)(0x0100u + cpu->s);
}

static inline uint8_t fetch_pc(Cpu6502 *cpu) {
    return bus_read8(cpu->bus, cpu->pc++);
}

static inline uint8_t peek(Cpu6502 *cpu, uint16_t addr) {
    return bus_read8(cpu->bus, addr);
}

static inline void poke(Cpu6502 *cpu, uint16_t addr, uint8_t value) {
    bus_write8(cpu->bus, addr, value);
}

/* ---------------------------------------------------------------- */
/* Opcode function signatures                                        */
/* ---------------------------------------------------------------- */

typedef void (*ReadFn)(Cpu6502 *cpu, uint8_t value);
typedef uint8_t (*RmwFn)(Cpu6502 *cpu, uint8_t value);
typedef uint8_t (*WriteFn)(Cpu6502 *cpu);
typedef void (*ImpliedFn)(Cpu6502 *cpu);
typedef bool (*BranchCondFn)(const Cpu6502 *cpu);
typedef uint8_t (*PushFn)(const Cpu6502 *cpu);
typedef void (*PullFn)(Cpu6502 *cpu, uint8_t value);

typedef enum {
    MODE_IMPLIED = 0,
    MODE_ACCUMULATOR,
    MODE_IMMEDIATE,
    MODE_ZP,
    MODE_ZPX,
    MODE_ZPY,
    MODE_ABS,
    MODE_ABSX,
    MODE_ABSY,
    MODE_INDIRECT, /* JMP only */
    MODE_INDX,
    MODE_INDY,
    MODE_RELATIVE,
    MODE_NONE
} AddrMode;

typedef enum {
    OP_ILLEGAL = 0, /* deliberate: the default zero-initialized table entry */
    OP_READ,
    OP_WRITE,
    OP_RMW,
    OP_IMPLIED,
    OP_BRANCH,
    OP_JUMP_ABS,
    OP_JUMP_IND,
    OP_JSR,
    OP_RTS,
    OP_RTI,
    OP_BRK,
    OP_PUSH,
    OP_PULL
} OpKind;

typedef struct {
    const char *mnemonic;
    AddrMode mode;
    OpKind kind;
    union {
        ReadFn read_fn;
        RmwFn rmw_fn;
        WriteFn write_fn;
        ImpliedFn implied_fn;
        BranchCondFn branch_cond_fn;
        PushFn push_fn;
        PullFn pull_fn;
    };
} OpcodeDef;

/* ---------------------------------------------------------------- */
/* Read-instruction operations (ADC, AND, LDA, ... including BIT/CMP) */
/* ---------------------------------------------------------------- */

static void op_lda(Cpu6502 *cpu, uint8_t v) { cpu->a = v; set_nz(cpu, cpu->a); }
static void op_ldx(Cpu6502 *cpu, uint8_t v) { cpu->x = v; set_nz(cpu, cpu->x); }
static void op_ldy(Cpu6502 *cpu, uint8_t v) { cpu->y = v; set_nz(cpu, cpu->y); }
static void op_and(Cpu6502 *cpu, uint8_t v) { cpu->a &= v; set_nz(cpu, cpu->a); }
static void op_ora(Cpu6502 *cpu, uint8_t v) { cpu->a |= v; set_nz(cpu, cpu->a); }
static void op_eor(Cpu6502 *cpu, uint8_t v) { cpu->a ^= v; set_nz(cpu, cpu->a); }

static void op_bit(Cpu6502 *cpu, uint8_t v) {
    set_flag(cpu, CPU6502_FLAG_Z, (cpu->a & v) == 0);
    set_flag(cpu, CPU6502_FLAG_N, (v & 0x80u) != 0);
    set_flag(cpu, CPU6502_FLAG_V, (v & 0x40u) != 0);
}

static void do_compare(Cpu6502 *cpu, uint8_t reg, uint8_t v) {
    set_flag(cpu, CPU6502_FLAG_C, reg >= v);
    set_nz(cpu, (uint8_t)(reg - v));
}
static void op_cmp(Cpu6502 *cpu, uint8_t v) { do_compare(cpu, cpu->a, v); }
static void op_cpx(Cpu6502 *cpu, uint8_t v) { do_compare(cpu, cpu->x, v); }
static void op_cpy(Cpu6502 *cpu, uint8_t v) { do_compare(cpu, cpu->y, v); }

/* ADC/SBC decimal-mode algorithm per the documented NMOS behavior (see
 * docs/6502-reference.md's decimal-mode section, and Bruce Clark's
 * widely-cited "Decimal Mode" writeup of the real chip's documented
 * behavior -- a factual description of hardware, not vendored code):
 * ADC's Z flag comes from the *binary* sum, N/V come from the BCD
 * partial result *before* the final >=0xA0 high-nibble correction, and
 * C comes from *after* that correction. SBC's N/V/Z/C all come from the
 * plain binary subtraction regardless of decimal mode -- only the
 * accumulator's own result value gets BCD-corrected. */
static void op_adc(Cpu6502 *cpu, uint8_t v) {
    uint8_t a = cpu->a;
    uint16_t c = get_flag(cpu, CPU6502_FLAG_C) ? 1u : 0u;
    uint16_t bin = (uint16_t)((uint16_t)a + v + c);
    bool z = (uint8_t)bin == 0;

    if (get_flag(cpu, CPU6502_FLAG_D)) {
        uint16_t al = (uint16_t)((a & 0x0Fu) + (v & 0x0Fu) + c);
        if (al >= 0x0Au) {
            al = (uint16_t)(((al + 0x06u) & 0x0Fu) + 0x10u);
        }
        uint16_t sum = (uint16_t)((a & 0xF0u) + (v & 0xF0u) + al);
        bool n = (sum & 0x80u) != 0;
        bool ovf = ((~(a ^ v)) & (a ^ (uint8_t)sum) & 0x80u) != 0;
        if (sum >= 0xA0u) {
            sum = (uint16_t)(sum + 0x60u);
        }
        bool carry = sum >= 0x100u;
        cpu->a = (uint8_t)sum;
        set_flag(cpu, CPU6502_FLAG_Z, z);
        set_flag(cpu, CPU6502_FLAG_N, n);
        set_flag(cpu, CPU6502_FLAG_V, ovf);
        set_flag(cpu, CPU6502_FLAG_C, carry);
    } else {
        bool n = (bin & 0x80u) != 0;
        bool ovf = ((~(a ^ v)) & (a ^ (uint8_t)bin) & 0x80u) != 0;
        bool carry = bin > 0xFFu;
        cpu->a = (uint8_t)bin;
        set_flag(cpu, CPU6502_FLAG_Z, z);
        set_flag(cpu, CPU6502_FLAG_N, n);
        set_flag(cpu, CPU6502_FLAG_V, ovf);
        set_flag(cpu, CPU6502_FLAG_C, carry);
    }
}

static void op_sbc(Cpu6502 *cpu, uint8_t v) {
    uint8_t a = cpu->a;
    int c = get_flag(cpu, CPU6502_FLAG_C) ? 1 : 0;
    int bin = (int)a - (int)v - (1 - c);
    uint8_t bin8 = (uint8_t)(bin & 0xFF);
    bool carry = bin >= 0; /* carry clear means a borrow occurred */
    bool z = bin8 == 0;
    bool n = (bin8 & 0x80u) != 0;
    bool ovf = (((a ^ v) & (a ^ bin8)) & 0x80u) != 0;

    set_flag(cpu, CPU6502_FLAG_C, carry);
    set_flag(cpu, CPU6502_FLAG_Z, z);
    set_flag(cpu, CPU6502_FLAG_N, n);
    set_flag(cpu, CPU6502_FLAG_V, ovf);

    if (get_flag(cpu, CPU6502_FLAG_D)) {
        int al = (int)(a & 0x0Fu) - (int)(v & 0x0Fu) - (1 - c);
        if (al < 0) {
            al = ((al - 0x06) & 0x0F) - 0x10;
        }
        int result = (int)(a & 0xF0u) - (int)(v & 0xF0u) + al;
        if (result < 0) {
            result -= 0x60;
        }
        cpu->a = (uint8_t)(result & 0xFF);
    } else {
        cpu->a = bin8;
    }
}

/* ---------------------------------------------------------------- */
/* Read-modify-write operations (ASL, LSR, ROL, ROR, INC, DEC)        */
/* ---------------------------------------------------------------- */

static uint8_t op_asl(Cpu6502 *cpu, uint8_t v) {
    set_flag(cpu, CPU6502_FLAG_C, (v & 0x80u) != 0);
    uint8_t r = (uint8_t)(v << 1);
    set_nz(cpu, r);
    return r;
}
static uint8_t op_lsr(Cpu6502 *cpu, uint8_t v) {
    set_flag(cpu, CPU6502_FLAG_C, (v & 0x01u) != 0);
    uint8_t r = (uint8_t)(v >> 1);
    set_nz(cpu, r);
    return r;
}
static uint8_t op_rol(Cpu6502 *cpu, uint8_t v) {
    bool old_c = get_flag(cpu, CPU6502_FLAG_C);
    set_flag(cpu, CPU6502_FLAG_C, (v & 0x80u) != 0);
    uint8_t r = (uint8_t)((uint8_t)(v << 1) | (old_c ? 0x01u : 0u));
    set_nz(cpu, r);
    return r;
}
static uint8_t op_ror(Cpu6502 *cpu, uint8_t v) {
    bool old_c = get_flag(cpu, CPU6502_FLAG_C);
    set_flag(cpu, CPU6502_FLAG_C, (v & 0x01u) != 0);
    uint8_t r = (uint8_t)((uint8_t)(v >> 1) | (old_c ? 0x80u : 0u));
    set_nz(cpu, r);
    return r;
}
static uint8_t op_inc(Cpu6502 *cpu, uint8_t v) { uint8_t r = (uint8_t)(v + 1); set_nz(cpu, r); return r; }
static uint8_t op_dec(Cpu6502 *cpu, uint8_t v) { uint8_t r = (uint8_t)(v - 1); set_nz(cpu, r); return r; }

/* ---------------------------------------------------------------- */
/* Write operations (STA, STX, STY)                                  */
/* ---------------------------------------------------------------- */

static uint8_t op_sta(Cpu6502 *cpu) { return cpu->a; }
static uint8_t op_stx(Cpu6502 *cpu) { return cpu->x; }
static uint8_t op_sty(Cpu6502 *cpu) { return cpu->y; }

/* ---------------------------------------------------------------- */
/* Implied-mode operations                                           */
/* ---------------------------------------------------------------- */

static void op_clc(Cpu6502 *c) { set_flag(c, CPU6502_FLAG_C, false); }
static void op_sec(Cpu6502 *c) { set_flag(c, CPU6502_FLAG_C, true); }
static void op_cli(Cpu6502 *c) { set_flag(c, CPU6502_FLAG_I, false); }
static void op_sei(Cpu6502 *c) { set_flag(c, CPU6502_FLAG_I, true); }
static void op_clv(Cpu6502 *c) { set_flag(c, CPU6502_FLAG_V, false); }
static void op_cld(Cpu6502 *c) { set_flag(c, CPU6502_FLAG_D, false); }
static void op_sed(Cpu6502 *c) { set_flag(c, CPU6502_FLAG_D, true); }
static void op_nop(Cpu6502 *c) { (void)c; }
static void op_tax(Cpu6502 *c) { c->x = c->a; set_nz(c, c->x); }
static void op_tay(Cpu6502 *c) { c->y = c->a; set_nz(c, c->y); }
static void op_txa(Cpu6502 *c) { c->a = c->x; set_nz(c, c->a); }
static void op_tya(Cpu6502 *c) { c->a = c->y; set_nz(c, c->a); }
static void op_tsx(Cpu6502 *c) { c->x = c->s; set_nz(c, c->x); }
static void op_txs(Cpu6502 *c) { c->s = c->x; /* no flags affected */ }
static void op_inx(Cpu6502 *c) { c->x = (uint8_t)(c->x + 1); set_nz(c, c->x); }
static void op_iny(Cpu6502 *c) { c->y = (uint8_t)(c->y + 1); set_nz(c, c->y); }
static void op_dex(Cpu6502 *c) { c->x = (uint8_t)(c->x - 1); set_nz(c, c->x); }
static void op_dey(Cpu6502 *c) { c->y = (uint8_t)(c->y - 1); set_nz(c, c->y); }

/* Accumulator-mode ASL/LSR/ROL/ROR reuse the RMW operations above,
 * applied directly to A instead of a memory operand -- see
 * step_rmw_mode()'s MODE_ACCUMULATOR case. */

/* ---------------------------------------------------------------- */
/* Branch conditions                                                  */
/* ---------------------------------------------------------------- */

static bool cond_bcc(const Cpu6502 *c) { return !get_flag(c, CPU6502_FLAG_C); }
static bool cond_bcs(const Cpu6502 *c) { return get_flag(c, CPU6502_FLAG_C); }
static bool cond_beq(const Cpu6502 *c) { return get_flag(c, CPU6502_FLAG_Z); }
static bool cond_bne(const Cpu6502 *c) { return !get_flag(c, CPU6502_FLAG_Z); }
static bool cond_bpl(const Cpu6502 *c) { return !get_flag(c, CPU6502_FLAG_N); }
static bool cond_bmi(const Cpu6502 *c) { return get_flag(c, CPU6502_FLAG_N); }
static bool cond_bvc(const Cpu6502 *c) { return !get_flag(c, CPU6502_FLAG_V); }
static bool cond_bvs(const Cpu6502 *c) { return get_flag(c, CPU6502_FLAG_V); }

/* ---------------------------------------------------------------- */
/* Push/pull operations (PHA, PHP, PLA, PLP)                          */
/* ---------------------------------------------------------------- */

static uint8_t op_pha_val(const Cpu6502 *c) { return c->a; }
/* PHP always pushes with bits 4 (B) and 5 (unused) set to 1 -- this is
 * real, documented hardware behavior, not a bug. */
static uint8_t op_php_val(const Cpu6502 *c) { return (uint8_t)(c->p | CPU6502_FLAG_B | CPU6502_FLAG_UNUSED); }
static void op_pla_val(Cpu6502 *c, uint8_t v) { c->a = v; set_nz(c, v); }
/* Bit 5 always reads back as 1; bit 4 (B) isn't a real stored flag at
 * all, so whatever bit4 value gets pulled in here is harmless -- it's
 * only ever examined again via a later PHP/BRK push, which always
 * forces it explicitly regardless of what's currently stored. */
static void op_plp_val(Cpu6502 *c, uint8_t v) { c->p = (uint8_t)(v | CPU6502_FLAG_UNUSED); }

/* ---------------------------------------------------------------- */
/* Opcode table                                                      */
/* ---------------------------------------------------------------- */

static const OpcodeDef opcode_table[256] = {
    /* ADC */
    [0x69] = {"ADC", MODE_IMMEDIATE, OP_READ, {.read_fn = op_adc}},
    [0x65] = {"ADC", MODE_ZP, OP_READ, {.read_fn = op_adc}},
    [0x75] = {"ADC", MODE_ZPX, OP_READ, {.read_fn = op_adc}},
    [0x6D] = {"ADC", MODE_ABS, OP_READ, {.read_fn = op_adc}},
    [0x7D] = {"ADC", MODE_ABSX, OP_READ, {.read_fn = op_adc}},
    [0x79] = {"ADC", MODE_ABSY, OP_READ, {.read_fn = op_adc}},
    [0x61] = {"ADC", MODE_INDX, OP_READ, {.read_fn = op_adc}},
    [0x71] = {"ADC", MODE_INDY, OP_READ, {.read_fn = op_adc}},

    /* AND */
    [0x29] = {"AND", MODE_IMMEDIATE, OP_READ, {.read_fn = op_and}},
    [0x25] = {"AND", MODE_ZP, OP_READ, {.read_fn = op_and}},
    [0x35] = {"AND", MODE_ZPX, OP_READ, {.read_fn = op_and}},
    [0x2D] = {"AND", MODE_ABS, OP_READ, {.read_fn = op_and}},
    [0x3D] = {"AND", MODE_ABSX, OP_READ, {.read_fn = op_and}},
    [0x39] = {"AND", MODE_ABSY, OP_READ, {.read_fn = op_and}},
    [0x21] = {"AND", MODE_INDX, OP_READ, {.read_fn = op_and}},
    [0x31] = {"AND", MODE_INDY, OP_READ, {.read_fn = op_and}},

    /* ASL */
    [0x0A] = {"ASL", MODE_ACCUMULATOR, OP_RMW, {.rmw_fn = op_asl}},
    [0x06] = {"ASL", MODE_ZP, OP_RMW, {.rmw_fn = op_asl}},
    [0x16] = {"ASL", MODE_ZPX, OP_RMW, {.rmw_fn = op_asl}},
    [0x0E] = {"ASL", MODE_ABS, OP_RMW, {.rmw_fn = op_asl}},
    [0x1E] = {"ASL", MODE_ABSX, OP_RMW, {.rmw_fn = op_asl}},

    /* Branches */
    [0x90] = {"BCC", MODE_RELATIVE, OP_BRANCH, {.branch_cond_fn = cond_bcc}},
    [0xB0] = {"BCS", MODE_RELATIVE, OP_BRANCH, {.branch_cond_fn = cond_bcs}},
    [0xF0] = {"BEQ", MODE_RELATIVE, OP_BRANCH, {.branch_cond_fn = cond_beq}},
    [0x30] = {"BMI", MODE_RELATIVE, OP_BRANCH, {.branch_cond_fn = cond_bmi}},
    [0xD0] = {"BNE", MODE_RELATIVE, OP_BRANCH, {.branch_cond_fn = cond_bne}},
    [0x10] = {"BPL", MODE_RELATIVE, OP_BRANCH, {.branch_cond_fn = cond_bpl}},
    [0x50] = {"BVC", MODE_RELATIVE, OP_BRANCH, {.branch_cond_fn = cond_bvc}},
    [0x70] = {"BVS", MODE_RELATIVE, OP_BRANCH, {.branch_cond_fn = cond_bvs}},

    /* BIT */
    [0x24] = {"BIT", MODE_ZP, OP_READ, {.read_fn = op_bit}},
    [0x2C] = {"BIT", MODE_ABS, OP_READ, {.read_fn = op_bit}},

    /* BRK */
    [0x00] = {"BRK", MODE_NONE, OP_BRK, {0}},

    /* Flag clear/set */
    [0x18] = {"CLC", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_clc}},
    [0xD8] = {"CLD", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_cld}},
    [0x58] = {"CLI", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_cli}},
    [0xB8] = {"CLV", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_clv}},
    [0x38] = {"SEC", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_sec}},
    [0xF8] = {"SED", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_sed}},
    [0x78] = {"SEI", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_sei}},

    /* CMP/CPX/CPY */
    [0xC9] = {"CMP", MODE_IMMEDIATE, OP_READ, {.read_fn = op_cmp}},
    [0xC5] = {"CMP", MODE_ZP, OP_READ, {.read_fn = op_cmp}},
    [0xD5] = {"CMP", MODE_ZPX, OP_READ, {.read_fn = op_cmp}},
    [0xCD] = {"CMP", MODE_ABS, OP_READ, {.read_fn = op_cmp}},
    [0xDD] = {"CMP", MODE_ABSX, OP_READ, {.read_fn = op_cmp}},
    [0xD9] = {"CMP", MODE_ABSY, OP_READ, {.read_fn = op_cmp}},
    [0xC1] = {"CMP", MODE_INDX, OP_READ, {.read_fn = op_cmp}},
    [0xD1] = {"CMP", MODE_INDY, OP_READ, {.read_fn = op_cmp}},
    [0xE0] = {"CPX", MODE_IMMEDIATE, OP_READ, {.read_fn = op_cpx}},
    [0xE4] = {"CPX", MODE_ZP, OP_READ, {.read_fn = op_cpx}},
    [0xEC] = {"CPX", MODE_ABS, OP_READ, {.read_fn = op_cpx}},
    [0xC0] = {"CPY", MODE_IMMEDIATE, OP_READ, {.read_fn = op_cpy}},
    [0xC4] = {"CPY", MODE_ZP, OP_READ, {.read_fn = op_cpy}},
    [0xCC] = {"CPY", MODE_ABS, OP_READ, {.read_fn = op_cpy}},

    /* DEC/INC (memory) */
    [0xC6] = {"DEC", MODE_ZP, OP_RMW, {.rmw_fn = op_dec}},
    [0xD6] = {"DEC", MODE_ZPX, OP_RMW, {.rmw_fn = op_dec}},
    [0xCE] = {"DEC", MODE_ABS, OP_RMW, {.rmw_fn = op_dec}},
    [0xDE] = {"DEC", MODE_ABSX, OP_RMW, {.rmw_fn = op_dec}},
    [0xE6] = {"INC", MODE_ZP, OP_RMW, {.rmw_fn = op_inc}},
    [0xF6] = {"INC", MODE_ZPX, OP_RMW, {.rmw_fn = op_inc}},
    [0xEE] = {"INC", MODE_ABS, OP_RMW, {.rmw_fn = op_inc}},
    [0xFE] = {"INC", MODE_ABSX, OP_RMW, {.rmw_fn = op_inc}},

    /* DEX/DEY/INX/INY */
    [0xCA] = {"DEX", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_dex}},
    [0x88] = {"DEY", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_dey}},
    [0xE8] = {"INX", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_inx}},
    [0xC8] = {"INY", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_iny}},

    /* EOR */
    [0x49] = {"EOR", MODE_IMMEDIATE, OP_READ, {.read_fn = op_eor}},
    [0x45] = {"EOR", MODE_ZP, OP_READ, {.read_fn = op_eor}},
    [0x55] = {"EOR", MODE_ZPX, OP_READ, {.read_fn = op_eor}},
    [0x4D] = {"EOR", MODE_ABS, OP_READ, {.read_fn = op_eor}},
    [0x5D] = {"EOR", MODE_ABSX, OP_READ, {.read_fn = op_eor}},
    [0x59] = {"EOR", MODE_ABSY, OP_READ, {.read_fn = op_eor}},
    [0x41] = {"EOR", MODE_INDX, OP_READ, {.read_fn = op_eor}},
    [0x51] = {"EOR", MODE_INDY, OP_READ, {.read_fn = op_eor}},

    /* JMP/JSR */
    [0x4C] = {"JMP", MODE_ABS, OP_JUMP_ABS, {0}},
    [0x6C] = {"JMP", MODE_INDIRECT, OP_JUMP_IND, {0}},
    [0x20] = {"JSR", MODE_ABS, OP_JSR, {0}},

    /* LDA/LDX/LDY */
    [0xA9] = {"LDA", MODE_IMMEDIATE, OP_READ, {.read_fn = op_lda}},
    [0xA5] = {"LDA", MODE_ZP, OP_READ, {.read_fn = op_lda}},
    [0xB5] = {"LDA", MODE_ZPX, OP_READ, {.read_fn = op_lda}},
    [0xAD] = {"LDA", MODE_ABS, OP_READ, {.read_fn = op_lda}},
    [0xBD] = {"LDA", MODE_ABSX, OP_READ, {.read_fn = op_lda}},
    [0xB9] = {"LDA", MODE_ABSY, OP_READ, {.read_fn = op_lda}},
    [0xA1] = {"LDA", MODE_INDX, OP_READ, {.read_fn = op_lda}},
    [0xB1] = {"LDA", MODE_INDY, OP_READ, {.read_fn = op_lda}},
    [0xA2] = {"LDX", MODE_IMMEDIATE, OP_READ, {.read_fn = op_ldx}},
    [0xA6] = {"LDX", MODE_ZP, OP_READ, {.read_fn = op_ldx}},
    [0xB6] = {"LDX", MODE_ZPY, OP_READ, {.read_fn = op_ldx}},
    [0xAE] = {"LDX", MODE_ABS, OP_READ, {.read_fn = op_ldx}},
    [0xBE] = {"LDX", MODE_ABSY, OP_READ, {.read_fn = op_ldx}},
    [0xA0] = {"LDY", MODE_IMMEDIATE, OP_READ, {.read_fn = op_ldy}},
    [0xA4] = {"LDY", MODE_ZP, OP_READ, {.read_fn = op_ldy}},
    [0xB4] = {"LDY", MODE_ZPX, OP_READ, {.read_fn = op_ldy}},
    [0xAC] = {"LDY", MODE_ABS, OP_READ, {.read_fn = op_ldy}},
    [0xBC] = {"LDY", MODE_ABSX, OP_READ, {.read_fn = op_ldy}},

    /* LSR */
    [0x4A] = {"LSR", MODE_ACCUMULATOR, OP_RMW, {.rmw_fn = op_lsr}},
    [0x46] = {"LSR", MODE_ZP, OP_RMW, {.rmw_fn = op_lsr}},
    [0x56] = {"LSR", MODE_ZPX, OP_RMW, {.rmw_fn = op_lsr}},
    [0x4E] = {"LSR", MODE_ABS, OP_RMW, {.rmw_fn = op_lsr}},
    [0x5E] = {"LSR", MODE_ABSX, OP_RMW, {.rmw_fn = op_lsr}},

    /* NOP */
    [0xEA] = {"NOP", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_nop}},

    /* ORA */
    [0x09] = {"ORA", MODE_IMMEDIATE, OP_READ, {.read_fn = op_ora}},
    [0x05] = {"ORA", MODE_ZP, OP_READ, {.read_fn = op_ora}},
    [0x15] = {"ORA", MODE_ZPX, OP_READ, {.read_fn = op_ora}},
    [0x0D] = {"ORA", MODE_ABS, OP_READ, {.read_fn = op_ora}},
    [0x1D] = {"ORA", MODE_ABSX, OP_READ, {.read_fn = op_ora}},
    [0x19] = {"ORA", MODE_ABSY, OP_READ, {.read_fn = op_ora}},
    [0x01] = {"ORA", MODE_INDX, OP_READ, {.read_fn = op_ora}},
    [0x11] = {"ORA", MODE_INDY, OP_READ, {.read_fn = op_ora}},

    /* Stack: push/pull */
    [0x48] = {"PHA", MODE_NONE, OP_PUSH, {.push_fn = op_pha_val}},
    [0x08] = {"PHP", MODE_NONE, OP_PUSH, {.push_fn = op_php_val}},
    [0x68] = {"PLA", MODE_NONE, OP_PULL, {.pull_fn = op_pla_val}},
    [0x28] = {"PLP", MODE_NONE, OP_PULL, {.pull_fn = op_plp_val}},

    /* ROL/ROR */
    [0x2A] = {"ROL", MODE_ACCUMULATOR, OP_RMW, {.rmw_fn = op_rol}},
    [0x26] = {"ROL", MODE_ZP, OP_RMW, {.rmw_fn = op_rol}},
    [0x36] = {"ROL", MODE_ZPX, OP_RMW, {.rmw_fn = op_rol}},
    [0x2E] = {"ROL", MODE_ABS, OP_RMW, {.rmw_fn = op_rol}},
    [0x3E] = {"ROL", MODE_ABSX, OP_RMW, {.rmw_fn = op_rol}},
    [0x6A] = {"ROR", MODE_ACCUMULATOR, OP_RMW, {.rmw_fn = op_ror}},
    [0x66] = {"ROR", MODE_ZP, OP_RMW, {.rmw_fn = op_ror}},
    [0x76] = {"ROR", MODE_ZPX, OP_RMW, {.rmw_fn = op_ror}},
    [0x6E] = {"ROR", MODE_ABS, OP_RMW, {.rmw_fn = op_ror}},
    [0x7E] = {"ROR", MODE_ABSX, OP_RMW, {.rmw_fn = op_ror}},

    /* RTI/RTS */
    [0x40] = {"RTI", MODE_NONE, OP_RTI, {0}},
    [0x60] = {"RTS", MODE_NONE, OP_RTS, {0}},

    /* SBC */
    [0xE9] = {"SBC", MODE_IMMEDIATE, OP_READ, {.read_fn = op_sbc}},
    [0xE5] = {"SBC", MODE_ZP, OP_READ, {.read_fn = op_sbc}},
    [0xF5] = {"SBC", MODE_ZPX, OP_READ, {.read_fn = op_sbc}},
    [0xED] = {"SBC", MODE_ABS, OP_READ, {.read_fn = op_sbc}},
    [0xFD] = {"SBC", MODE_ABSX, OP_READ, {.read_fn = op_sbc}},
    [0xF9] = {"SBC", MODE_ABSY, OP_READ, {.read_fn = op_sbc}},
    [0xE1] = {"SBC", MODE_INDX, OP_READ, {.read_fn = op_sbc}},
    [0xF1] = {"SBC", MODE_INDY, OP_READ, {.read_fn = op_sbc}},

    /* STA/STX/STY */
    [0x85] = {"STA", MODE_ZP, OP_WRITE, {.write_fn = op_sta}},
    [0x95] = {"STA", MODE_ZPX, OP_WRITE, {.write_fn = op_sta}},
    [0x8D] = {"STA", MODE_ABS, OP_WRITE, {.write_fn = op_sta}},
    [0x9D] = {"STA", MODE_ABSX, OP_WRITE, {.write_fn = op_sta}},
    [0x99] = {"STA", MODE_ABSY, OP_WRITE, {.write_fn = op_sta}},
    [0x81] = {"STA", MODE_INDX, OP_WRITE, {.write_fn = op_sta}},
    [0x91] = {"STA", MODE_INDY, OP_WRITE, {.write_fn = op_sta}},
    [0x86] = {"STX", MODE_ZP, OP_WRITE, {.write_fn = op_stx}},
    [0x96] = {"STX", MODE_ZPY, OP_WRITE, {.write_fn = op_stx}},
    [0x8E] = {"STX", MODE_ABS, OP_WRITE, {.write_fn = op_stx}},
    [0x84] = {"STY", MODE_ZP, OP_WRITE, {.write_fn = op_sty}},
    [0x94] = {"STY", MODE_ZPX, OP_WRITE, {.write_fn = op_sty}},
    [0x8C] = {"STY", MODE_ABS, OP_WRITE, {.write_fn = op_sty}},

    /* Register transfers */
    [0xAA] = {"TAX", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_tax}},
    [0xA8] = {"TAY", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_tay}},
    [0xBA] = {"TSX", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_tsx}},
    [0x8A] = {"TXA", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_txa}},
    [0x9A] = {"TXS", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_txs}},
    [0x98] = {"TYA", MODE_IMPLIED, OP_IMPLIED, {.implied_fn = op_tya}},
};

/* ---------------------------------------------------------------- */
/* Per-(kind,mode) cycle engines                                     */
/*                                                                    */
/* Each of these is called once per PHI2 cycle while an instruction   */
/* of that kind is in progress; cpu->step (0-indexed, starting right  */
/* after the opcode fetch cycle) says which cycle within the          */
/* instruction this is. Every case ends by either falling through to  */
/* let cpu6502_cycle() advance cpu->step, or by clearing               */
/* mid_instruction once the instruction is complete. The exact cycle   */
/* sequence for each (kind, mode) pair below is standard, well-        */
/* documented NMOS 6502 bus behavior -- see docs/6502-reference.md    */
/* and docs/references-and-gotchas.md.                                */
/* ---------------------------------------------------------------- */

static void step_read_mode(Cpu6502 *cpu, const OpcodeDef *op) {
    switch (op->mode) {
    case MODE_IMMEDIATE:
        if (cpu->step == 0) {
            cpu->val = fetch_pc(cpu);
            op->read_fn(cpu, cpu->val);
            cpu->mid_instruction = false;
        }
        break;

    case MODE_ZP:
        switch (cpu->step) {
        case 0: cpu->addr = fetch_pc(cpu); break;
        case 1:
            cpu->val = peek(cpu, cpu->addr);
            op->read_fn(cpu, cpu->val);
            cpu->mid_instruction = false;
            break;
        default: break;
        }
        break;

    case MODE_ZPX:
    case MODE_ZPY:
        switch (cpu->step) {
        case 0: cpu->addr_base = fetch_pc(cpu); break;
        case 1: {
            peek(cpu, cpu->addr_base);
            uint8_t idx = (op->mode == MODE_ZPX) ? cpu->x : cpu->y;
            cpu->addr = (uint16_t)((cpu->addr_base + idx) & 0xFFu);
            break;
        }
        case 2:
            cpu->val = peek(cpu, cpu->addr);
            op->read_fn(cpu, cpu->val);
            cpu->mid_instruction = false;
            break;
        default: break;
        }
        break;

    case MODE_ABS:
        switch (cpu->step) {
        case 0: cpu->addr = fetch_pc(cpu); break;
        case 1: cpu->addr = (uint16_t)(cpu->addr | ((uint16_t)fetch_pc(cpu) << 8)); break;
        case 2:
            cpu->val = peek(cpu, cpu->addr);
            op->read_fn(cpu, cpu->val);
            cpu->mid_instruction = false;
            break;
        default: break;
        }
        break;

    case MODE_ABSX:
    case MODE_ABSY:
        switch (cpu->step) {
        case 0: cpu->addr_base = fetch_pc(cpu); break; /* low byte, temporarily */
        case 1: {
            uint16_t hi = (uint16_t)((uint16_t)fetch_pc(cpu) << 8);
            uint8_t lo = (uint8_t)cpu->addr_base;
            uint8_t idx = (op->mode == MODE_ABSX) ? cpu->x : cpu->y;
            uint16_t base = (uint16_t)(hi | lo);
            cpu->addr_base = base;
            cpu->page_crossed = ((uint16_t)lo + idx) > 0xFFu;
            cpu->addr = (uint16_t)((base & 0xFF00u) | (uint16_t)((lo + idx) & 0xFFu));
            break;
        }
        case 2: {
            uint8_t v = peek(cpu, cpu->addr); /* dummy if wrong page */
            if (cpu->page_crossed) {
                uint8_t idx = (op->mode == MODE_ABSX) ? cpu->x : cpu->y;
                cpu->addr = (uint16_t)(cpu->addr_base + idx);
                break;
            }
            op->read_fn(cpu, v);
            cpu->mid_instruction = false;
            break;
        }
        case 3:
            cpu->val = peek(cpu, cpu->addr);
            op->read_fn(cpu, cpu->val);
            cpu->mid_instruction = false;
            break;
        default: break;
        }
        break;

    case MODE_INDX:
        switch (cpu->step) {
        case 0: cpu->addr_base = fetch_pc(cpu); break;
        case 1:
            peek(cpu, cpu->addr_base);
            cpu->addr_base = (uint16_t)((cpu->addr_base + cpu->x) & 0xFFu);
            break;
        case 2: cpu->ptr = peek(cpu, cpu->addr_base); break;
        case 3:
            cpu->ptr = (uint16_t)(cpu->ptr | ((uint16_t)peek(cpu, (uint16_t)((cpu->addr_base + 1) & 0xFFu)) << 8));
            cpu->addr = cpu->ptr;
            break;
        case 4:
            cpu->val = peek(cpu, cpu->addr);
            op->read_fn(cpu, cpu->val);
            cpu->mid_instruction = false;
            break;
        default: break;
        }
        break;

    case MODE_INDY:
        switch (cpu->step) {
        case 0: cpu->addr_base = fetch_pc(cpu); break; /* zp pointer */
        case 1: cpu->ptr = peek(cpu, cpu->addr_base); break; /* low byte of base addr */
        case 2: {
            uint16_t hi = (uint16_t)((uint16_t)peek(cpu, (uint16_t)((cpu->addr_base + 1) & 0xFFu)) << 8);
            uint8_t lo = (uint8_t)cpu->ptr;
            uint16_t base = (uint16_t)(hi | lo);
            cpu->addr_base = base;
            cpu->page_crossed = ((uint16_t)lo + cpu->y) > 0xFFu;
            cpu->addr = (uint16_t)((base & 0xFF00u) | (uint16_t)((lo + cpu->y) & 0xFFu));
            break;
        }
        case 3: {
            uint8_t v = peek(cpu, cpu->addr);
            if (cpu->page_crossed) {
                cpu->addr = (uint16_t)(cpu->addr_base + cpu->y);
                break;
            }
            op->read_fn(cpu, v);
            cpu->mid_instruction = false;
            break;
        }
        case 4:
            cpu->val = peek(cpu, cpu->addr);
            op->read_fn(cpu, cpu->val);
            cpu->mid_instruction = false;
            break;
        default: break;
        }
        break;

    default:
        break;
    }
}

static void step_write_mode(Cpu6502 *cpu, const OpcodeDef *op) {
    switch (op->mode) {
    case MODE_ZP:
        switch (cpu->step) {
        case 0: cpu->addr = fetch_pc(cpu); break;
        case 1: poke(cpu, cpu->addr, op->write_fn(cpu)); cpu->mid_instruction = false; break;
        default: break;
        }
        break;

    case MODE_ZPX:
    case MODE_ZPY:
        switch (cpu->step) {
        case 0: cpu->addr_base = fetch_pc(cpu); break;
        case 1: {
            peek(cpu, cpu->addr_base);
            uint8_t idx = (op->mode == MODE_ZPX) ? cpu->x : cpu->y;
            cpu->addr = (uint16_t)((cpu->addr_base + idx) & 0xFFu);
            break;
        }
        case 2: poke(cpu, cpu->addr, op->write_fn(cpu)); cpu->mid_instruction = false; break;
        default: break;
        }
        break;

    case MODE_ABS:
        switch (cpu->step) {
        case 0: cpu->addr = fetch_pc(cpu); break;
        case 1: cpu->addr = (uint16_t)(cpu->addr | ((uint16_t)fetch_pc(cpu) << 8)); break;
        case 2: poke(cpu, cpu->addr, op->write_fn(cpu)); cpu->mid_instruction = false; break;
        default: break;
        }
        break;

    case MODE_ABSX:
    case MODE_ABSY:
        switch (cpu->step) {
        case 0: cpu->addr_base = fetch_pc(cpu); break;
        case 1: {
            uint16_t hi = (uint16_t)((uint16_t)fetch_pc(cpu) << 8);
            uint8_t lo = (uint8_t)cpu->addr_base;
            uint8_t idx = (op->mode == MODE_ABSX) ? cpu->x : cpu->y;
            uint16_t base = (uint16_t)(hi | lo);
            cpu->addr_base = base;
            cpu->addr = (uint16_t)((base & 0xFF00u) | (uint16_t)((lo + idx) & 0xFFu));
            break;
        }
        case 2: {
            peek(cpu, cpu->addr); /* dummy -- writes always take the worst-case cycle count */
            uint8_t idx = (op->mode == MODE_ABSX) ? cpu->x : cpu->y;
            cpu->addr = (uint16_t)(cpu->addr_base + idx);
            break;
        }
        case 3: poke(cpu, cpu->addr, op->write_fn(cpu)); cpu->mid_instruction = false; break;
        default: break;
        }
        break;

    case MODE_INDX:
        switch (cpu->step) {
        case 0: cpu->addr_base = fetch_pc(cpu); break;
        case 1:
            peek(cpu, cpu->addr_base);
            cpu->addr_base = (uint16_t)((cpu->addr_base + cpu->x) & 0xFFu);
            break;
        case 2: cpu->ptr = peek(cpu, cpu->addr_base); break;
        case 3:
            cpu->ptr = (uint16_t)(cpu->ptr | ((uint16_t)peek(cpu, (uint16_t)((cpu->addr_base + 1) & 0xFFu)) << 8));
            cpu->addr = cpu->ptr;
            break;
        case 4: poke(cpu, cpu->addr, op->write_fn(cpu)); cpu->mid_instruction = false; break;
        default: break;
        }
        break;

    case MODE_INDY:
        switch (cpu->step) {
        case 0: cpu->addr_base = fetch_pc(cpu); break;
        case 1: cpu->ptr = peek(cpu, cpu->addr_base); break;
        case 2: {
            uint16_t hi = (uint16_t)((uint16_t)peek(cpu, (uint16_t)((cpu->addr_base + 1) & 0xFFu)) << 8);
            uint8_t lo = (uint8_t)cpu->ptr;
            uint16_t base = (uint16_t)(hi | lo);
            cpu->addr_base = base;
            cpu->addr = (uint16_t)((base & 0xFF00u) | (uint16_t)((lo + cpu->y) & 0xFFu));
            break;
        }
        case 3:
            peek(cpu, cpu->addr); /* dummy -- always happens for writes */
            cpu->addr = (uint16_t)(cpu->addr_base + cpu->y);
            break;
        case 4: poke(cpu, cpu->addr, op->write_fn(cpu)); cpu->mid_instruction = false; break;
        default: break;
        }
        break;

    default:
        break;
    }
}

static void step_rmw_mode(Cpu6502 *cpu, const OpcodeDef *op) {
    switch (op->mode) {
    case MODE_ACCUMULATOR:
        if (cpu->step == 0) {
            peek(cpu, cpu->pc); /* dummy */
            cpu->a = op->rmw_fn(cpu, cpu->a);
            cpu->mid_instruction = false;
        }
        break;

    case MODE_ZP:
        switch (cpu->step) {
        case 0: cpu->addr = fetch_pc(cpu); break;
        case 1: cpu->val = peek(cpu, cpu->addr); break;
        case 2: poke(cpu, cpu->addr, cpu->val); break; /* dummy write-back */
        case 3: poke(cpu, cpu->addr, op->rmw_fn(cpu, cpu->val)); cpu->mid_instruction = false; break;
        default: break;
        }
        break;

    case MODE_ZPX:
        switch (cpu->step) {
        case 0: cpu->addr_base = fetch_pc(cpu); break;
        case 1:
            peek(cpu, cpu->addr_base);
            cpu->addr = (uint16_t)((cpu->addr_base + cpu->x) & 0xFFu);
            break;
        case 2: cpu->val = peek(cpu, cpu->addr); break;
        case 3: poke(cpu, cpu->addr, cpu->val); break;
        case 4: poke(cpu, cpu->addr, op->rmw_fn(cpu, cpu->val)); cpu->mid_instruction = false; break;
        default: break;
        }
        break;

    case MODE_ABS:
        switch (cpu->step) {
        case 0: cpu->addr = fetch_pc(cpu); break;
        case 1: cpu->addr = (uint16_t)(cpu->addr | ((uint16_t)fetch_pc(cpu) << 8)); break;
        case 2: cpu->val = peek(cpu, cpu->addr); break;
        case 3: poke(cpu, cpu->addr, cpu->val); break;
        case 4: poke(cpu, cpu->addr, op->rmw_fn(cpu, cpu->val)); cpu->mid_instruction = false; break;
        default: break;
        }
        break;

    case MODE_ABSX:
        switch (cpu->step) {
        case 0: cpu->addr_base = fetch_pc(cpu); break;
        case 1: {
            uint16_t hi = (uint16_t)((uint16_t)fetch_pc(cpu) << 8);
            uint8_t lo = (uint8_t)cpu->addr_base;
            uint16_t base = (uint16_t)(hi | lo);
            cpu->addr_base = base;
            cpu->addr = (uint16_t)((base & 0xFF00u) | (uint16_t)((lo + cpu->x) & 0xFFu));
            break;
        }
        case 2:
            peek(cpu, cpu->addr); /* dummy -- RMW absolute,X always takes 7 cycles */
            cpu->addr = (uint16_t)(cpu->addr_base + cpu->x);
            break;
        case 3: cpu->val = peek(cpu, cpu->addr); break;
        case 4: poke(cpu, cpu->addr, cpu->val); break;
        case 5: poke(cpu, cpu->addr, op->rmw_fn(cpu, cpu->val)); cpu->mid_instruction = false; break;
        default: break;
        }
        break;

    default:
        break;
    }
}

static void step_implied(Cpu6502 *cpu, const OpcodeDef *op) {
    if (cpu->step == 0) {
        peek(cpu, cpu->pc); /* dummy fetch of the next opcode byte, PC unmodified */
        op->implied_fn(cpu);
        cpu->mid_instruction = false;
    }
}

static void step_push(Cpu6502 *cpu, const OpcodeDef *op) {
    switch (cpu->step) {
    case 0: peek(cpu, cpu->pc); break;
    case 1:
        poke(cpu, stack_addr(cpu), op->push_fn(cpu));
        cpu->s = (uint8_t)(cpu->s - 1);
        cpu->mid_instruction = false;
        break;
    default: break;
    }
}

static void step_pull(Cpu6502 *cpu, const OpcodeDef *op) {
    switch (cpu->step) {
    case 0: peek(cpu, cpu->pc); break;
    case 1: peek(cpu, stack_addr(cpu)); break;
    case 2: {
        cpu->s = (uint8_t)(cpu->s + 1);
        uint8_t v = peek(cpu, stack_addr(cpu));
        op->pull_fn(cpu, v);
        cpu->mid_instruction = false;
        break;
    }
    default: break;
    }
}

static void step_branch(Cpu6502 *cpu, const OpcodeDef *op) {
    switch (cpu->step) {
    case 0: {
        uint8_t offset_byte = fetch_pc(cpu);
        cpu->val = offset_byte;
        if (!op->branch_cond_fn(cpu)) {
            cpu->mid_instruction = false;
        }
        break;
    }
    case 1: {
        peek(cpu, cpu->pc); /* dummy read of the next opcode byte */
        int8_t offset = (int8_t)cpu->val;
        uint16_t pc_before = cpu->pc;
        uint8_t new_lo = (uint8_t)((uint8_t)pc_before + offset);
        uint16_t candidate = (uint16_t)((pc_before & 0xFF00u) | new_lo);
        uint16_t correct = (uint16_t)(pc_before + offset);
        cpu->page_crossed = (candidate & 0xFF00u) != (correct & 0xFF00u);
        cpu->addr = candidate;
        cpu->ptr = correct;
        if (!cpu->page_crossed) {
            cpu->pc = correct;
            cpu->mid_instruction = false;
        }
        break;
    }
    case 2:
        peek(cpu, cpu->addr); /* dummy read at the not-yet-fixed-up page */
        cpu->pc = cpu->ptr;
        cpu->mid_instruction = false;
        break;
    default:
        break;
    }
}

static void step_jump_abs(Cpu6502 *cpu) {
    switch (cpu->step) {
    case 0: cpu->addr = fetch_pc(cpu); break;
    case 1:
        cpu->addr = (uint16_t)(cpu->addr | ((uint16_t)fetch_pc(cpu) << 8));
        cpu->pc = cpu->addr;
        cpu->mid_instruction = false;
        break;
    default: break;
    }
}

/* JMP ($xxFF): the famous page-boundary bug is real NMOS hardware
 * behavior, not a bug to fix -- see docs/6502-reference.md. */
static void step_jump_ind(Cpu6502 *cpu) {
    switch (cpu->step) {
    case 0: cpu->ptr = fetch_pc(cpu); break;
    case 1: cpu->ptr = (uint16_t)(cpu->ptr | ((uint16_t)fetch_pc(cpu) << 8)); break;
    case 2: cpu->addr = peek(cpu, cpu->ptr); break;
    case 3: {
        uint16_t hi_addr = (uint16_t)((cpu->ptr & 0xFF00u) | ((cpu->ptr + 1) & 0x00FFu));
        cpu->addr = (uint16_t)(cpu->addr | ((uint16_t)peek(cpu, hi_addr) << 8));
        cpu->pc = cpu->addr;
        cpu->mid_instruction = false;
        break;
    }
    default: break;
    }
}

static void step_jsr(Cpu6502 *cpu) {
    switch (cpu->step) {
    case 0: cpu->addr = fetch_pc(cpu); break; /* low byte of target */
    case 1: peek(cpu, stack_addr(cpu)); break; /* internal operation */
    case 2:
        poke(cpu, stack_addr(cpu), (uint8_t)(cpu->pc >> 8));
        cpu->s = (uint8_t)(cpu->s - 1);
        break;
    case 3:
        poke(cpu, stack_addr(cpu), (uint8_t)(cpu->pc & 0xFFu));
        cpu->s = (uint8_t)(cpu->s - 1);
        break;
    case 4: {
        uint16_t hi = (uint16_t)((uint16_t)fetch_pc(cpu) << 8);
        cpu->pc = (uint16_t)(hi | cpu->addr);
        cpu->mid_instruction = false;
        break;
    }
    default: break;
    }
}

static void step_rts(Cpu6502 *cpu) {
    switch (cpu->step) {
    case 0: peek(cpu, cpu->pc); break;
    case 1: peek(cpu, stack_addr(cpu)); break;
    case 2: cpu->s = (uint8_t)(cpu->s + 1); cpu->addr = peek(cpu, stack_addr(cpu)); break;
    case 3:
        cpu->s = (uint8_t)(cpu->s + 1);
        cpu->addr = (uint16_t)(cpu->addr | ((uint16_t)peek(cpu, stack_addr(cpu)) << 8));
        break;
    case 4:
        cpu->pc = cpu->addr;
        peek(cpu, cpu->pc);
        cpu->pc = (uint16_t)(cpu->pc + 1);
        cpu->mid_instruction = false;
        break;
    default: break;
    }
}

static void step_rti(Cpu6502 *cpu) {
    switch (cpu->step) {
    case 0: peek(cpu, cpu->pc); break;
    case 1: peek(cpu, stack_addr(cpu)); break;
    case 2:
        cpu->s = (uint8_t)(cpu->s + 1);
        cpu->p = (uint8_t)(peek(cpu, stack_addr(cpu)) | CPU6502_FLAG_UNUSED);
        break;
    case 3:
        cpu->s = (uint8_t)(cpu->s + 1);
        cpu->addr = peek(cpu, stack_addr(cpu));
        break;
    case 4:
        cpu->s = (uint8_t)(cpu->s + 1);
        cpu->addr = (uint16_t)(cpu->addr | ((uint16_t)peek(cpu, stack_addr(cpu)) << 8));
        cpu->pc = cpu->addr;
        cpu->mid_instruction = false;
        break;
    default: break;
    }
}

/* Shared by the real BRK opcode and by hardware IRQ/NMI service, which
 * reuses this exact 7-cycle sequence (see docs/machine.md's IRQ/NMI
 * delivery section) -- the only differences are whether the PC-
 * incrementing "signature byte" fetch happens (real BRK only) and
 * which vector and B-flag value get used. */
static void step_brk(Cpu6502 *cpu) {
    switch (cpu->step) {
    case 0:
        if (cpu->servicing_interrupt) {
            peek(cpu, cpu->pc); /* hardware interrupt: no opcode byte was really fetched, PC untouched */
        } else {
            fetch_pc(cpu); /* real BRK: padding/signature byte, discarded */
        }
        break;
    case 1:
        poke(cpu, stack_addr(cpu), (uint8_t)(cpu->pc >> 8));
        cpu->s = (uint8_t)(cpu->s - 1);
        break;
    case 2:
        poke(cpu, stack_addr(cpu), (uint8_t)(cpu->pc & 0xFFu));
        cpu->s = (uint8_t)(cpu->s - 1);
        break;
    case 3: {
        uint8_t pushed_p = (uint8_t)(cpu->p | CPU6502_FLAG_UNUSED);
        pushed_p = cpu->servicing_interrupt ? (uint8_t)(pushed_p & (uint8_t)~CPU6502_FLAG_B)
                                             : (uint8_t)(pushed_p | CPU6502_FLAG_B);
        poke(cpu, stack_addr(cpu), pushed_p);
        cpu->s = (uint8_t)(cpu->s - 1);
        break;
    }
    case 4: {
        set_flag(cpu, CPU6502_FLAG_I, true);
        uint16_t vector = (cpu->servicing_interrupt && cpu->servicing_nmi) ? 0xFFFAu : 0xFFFEu;
        cpu->ptr = vector;
        cpu->addr = peek(cpu, vector);
        break;
    }
    case 5:
        cpu->addr = (uint16_t)(cpu->addr | ((uint16_t)peek(cpu, (uint16_t)(cpu->ptr + 1)) << 8));
        cpu->pc = cpu->addr;
        cpu->mid_instruction = false;
        cpu->servicing_interrupt = false;
        /* This sequence just forced the real I flag to 1 (case 4). The
         * CLI/SEI/PLP one-instruction-delay mechanism only applies to
         * those three specific opcodes -- entering interrupt service
         * is not one of them, so its effect on I must be visible to
         * the very next poll immediately. Without this, a source that
         * doesn't clear itself in the handler (e.g. nothing has read
         * the CIA/VIC-II interrupt register yet) would make the CPU
         * re-enter service forever without ever executing the
         * handler's own first instruction, since the next poll would
         * otherwise see a stale, pre-interrupt I value and immediately
         * take another "interrupt" instead of fetching real code. */
        cpu->i_flag_before_instruction = true;
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- */
/* Public interface                                                   */
/* ---------------------------------------------------------------- */

void cpu6502_init(Cpu6502 *cpu, Bus *bus) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->bus = bus;
    cpu->p = CPU6502_FLAG_UNUSED | CPU6502_FLAG_I;
    cpu->s = 0xFD;
    cpu->i_flag_before_instruction = true;
}

void cpu6502_reset(Cpu6502 *cpu) {
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->s = 0xFD;
    cpu->p = CPU6502_FLAG_UNUSED | CPU6502_FLAG_I;

    uint8_t lo = peek(cpu, 0xFFFCu);
    uint8_t hi = peek(cpu, 0xFFFDu);
    cpu->pc = (uint16_t)(lo | ((uint16_t)hi << 8));

    cpu->mid_instruction = false;
    cpu->step = 0;
    cpu->irq_line = false;
    cpu->nmi_line = false;
    cpu->nmi_line_prev = false;
    cpu->nmi_pending = false;
    cpu->servicing_interrupt = false;
    cpu->servicing_nmi = false;
    cpu->i_flag_before_instruction = true;
    cpu->illegal_opcode_hit = false;
    cpu->last_illegal_opcode = 0;
    cpu->total_cycles = 0;
}

void cpu6502_set_irq_line(Cpu6502 *cpu, bool asserted) { cpu->irq_line = asserted; }
void cpu6502_set_nmi_line(Cpu6502 *cpu, bool asserted) { cpu->nmi_line = asserted; }

void cpu6502_cycle(Cpu6502 *cpu) {
    cpu->total_cycles++;

    /* NMI edge-detection is armed every cycle, independent of
     * instruction boundaries -- real hardware latches the edge as soon
     * as it happens; only *servicing* it waits for an instruction
     * boundary. */
    if (cpu->nmi_line && !cpu->nmi_line_prev) {
        cpu->nmi_pending = true;
    }
    cpu->nmi_line_prev = cpu->nmi_line;

    if (!cpu->mid_instruction) {
        bool take_nmi = cpu->nmi_pending;
        /* The IRQ decision uses the I flag as it was *before* the
         * instruction that just finished ran -- this is what gives
         * CLI/SEI/PLP their real, documented one-instruction delay
         * before an IRQ-mask change is honored by the interrupt
         * polling logic. See docs/machine.md's IRQ/NMI section. */
        bool take_irq = !take_nmi && cpu->irq_line && !cpu->i_flag_before_instruction;

        if (take_nmi || take_irq) {
            if (take_nmi) {
                cpu->nmi_pending = false;
            }
            cpu->servicing_interrupt = true;
            cpu->servicing_nmi = take_nmi;
            cpu->opcode = 0x00;
            cpu->step = 0;
            cpu->mid_instruction = true;
            /* This cycle plays the same role as a real opcode fetch --
             * it's spent, and the interrupt sequence's own steps start
             * on the next call. */
            return;
        }

        cpu->opcode = fetch_pc(cpu);
        cpu->i_flag_before_instruction = get_flag(cpu, CPU6502_FLAG_I);
        cpu->step = 0;
        cpu->mid_instruction = true;
        return;
    }

    const OpcodeDef *op = &opcode_table[cpu->opcode];
    switch (op->kind) {
    case OP_READ: step_read_mode(cpu, op); break;
    case OP_WRITE: step_write_mode(cpu, op); break;
    case OP_RMW: step_rmw_mode(cpu, op); break;
    case OP_IMPLIED: step_implied(cpu, op); break;
    case OP_BRANCH: step_branch(cpu, op); break;
    case OP_JUMP_ABS: step_jump_abs(cpu); break;
    case OP_JUMP_IND: step_jump_ind(cpu); break;
    case OP_JSR: step_jsr(cpu); break;
    case OP_RTS: step_rts(cpu); break;
    case OP_RTI: step_rti(cpu); break;
    case OP_BRK: step_brk(cpu); break;
    case OP_PUSH: step_push(cpu, op); break;
    case OP_PULL: step_pull(cpu, op); break;
    case OP_ILLEGAL:
    default:
        /* Deferred to Phase 10 -- see docs/6502-reference.md. Consumed
         * as a 1-cycle no-op so a test/harness loop can't hang, but
         * flagged so the caller can detect and report it rather than
         * this silently passing as a real NOP. */
        cpu->illegal_opcode_hit = true;
        cpu->last_illegal_opcode = cpu->opcode;
        cpu->mid_instruction = false;
        break;
    }

    cpu->step++;
}
