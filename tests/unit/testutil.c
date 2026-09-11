#include "testutil.h"

#include <string.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static uint8_t flat_bus_read(void *ctx, uint16_t addr) {
    FlatBus *fb = (FlatBus *)ctx;
    return fb->mem[addr];
}

static void flat_bus_write(void *ctx, uint16_t addr, uint8_t value) {
    FlatBus *fb = (FlatBus *)ctx;
    fb->mem[addr] = value;
}

void flat_bus_init(FlatBus *fb) {
    memset(fb->mem, 0, sizeof(fb->mem));
}

Bus flat_bus_as_bus(FlatBus *fb) {
    Bus bus;
    bus.ctx = fb;
    bus.read = flat_bus_read;
    bus.write = flat_bus_write;
    return bus;
}

void run_one_instruction(Cpu6502 *cpu) {
    cpu6502_cycle(cpu); /* opcode fetch */
    while (cpu->mid_instruction) {
        cpu6502_cycle(cpu);
    }
}
