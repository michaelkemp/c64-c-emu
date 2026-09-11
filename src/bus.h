#ifndef C64EMU_BUS_H
#define C64EMU_BUS_H

#include <stdint.h>

/* Generic bus interface. The CPU core (src/cpu/cpu6502.h) only ever
 * calls these two callbacks -- it has no idea what's behind them. A
 * concrete implementation can be a flat 64KB RAM test harness (see
 * tests/), and later the real C64 PLA-driven memory map (Phase 2). */
typedef struct Bus {
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
} Bus;

static inline uint8_t bus_read8(Bus *bus, uint16_t addr) {
    return bus->read(bus->ctx, addr);
}

static inline void bus_write8(Bus *bus, uint16_t addr, uint8_t value) {
    bus->write(bus->ctx, addr, value);
}

#endif /* C64EMU_BUS_H */
