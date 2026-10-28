/* Include in comm_main.c when TIDESENSE_C906_RELOAD is enabled. */
#ifndef TIDESENSE_C906_RELOAD_FIRMWARE_H
#define TIDESENSE_C906_RELOAD_FIRMWARE_H
#include <stdint.h>
#include <string.h>
#include "arch_helpers.h"
#include "trampoline.inc"

#define C906_RELOAD_CAP_CMD 0x4c
#define C906_RELOAD_PARK_CMD 0x4d
#define C906_RELOAD_MAGIC 0x52393036u

struct c906_firmware_reload_gate {
    uint32_t magic, phase, entry, reserved;
};

static void c906_reload_ready(void)
{
    uintptr_t addr;
    __asm__ volatile("csrr %0, mscratch" : "=r"(addr));
    if (addr >= 0x80000000u && addr <= 0x8ff3f000u && !(addr & 4095u)) {
        volatile struct c906_firmware_reload_gate *g = (void *)addr;
        if (g->magic == C906_RELOAD_MAGIC && g->phase == 2 &&
            g->entry == 0x8ff40000u) {
            g->phase = 3;
            clean_dcache_range(addr, 64);
        }
    }
    __asm__ volatile("csrw mscratch, zero" ::: "memory");
}

static int c906_firmware_reload_enter(uintptr_t addr)
{
    struct c906_firmware_reload_gate *gate = (void *)addr;
    void (*enter)(uintptr_t) = (void *)(addr + 256);
    /* Referenced signature lets Linux reject incompatible flat firmware. */
    static const volatile char signature[] = "TS_C906_RELOAD_V1";
    if (addr < 0x80000000u || addr > 0x8ff3f000u || (addr & 4095u))
        return -1;
    inv_dcache_range(addr, 4096);
    if (gate->magic != C906_RELOAD_MAGIC || gate->phase ||
        gate->entry != 0x8ff40000u || signature[0] != 'T')
        return -1;
    memcpy((void *)(addr + 256), c906_reload_trampoline_code,
           sizeof(c906_reload_trampoline_code));
    clean_dcache_range(addr, 4096);
    __asm__ volatile("fence.i" ::: "memory");
    enter(addr);
    __builtin_unreachable();
}
#endif
