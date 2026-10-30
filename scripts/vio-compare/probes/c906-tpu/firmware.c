/* Included by the temporary FreeRTOS command task. No Linux TPU execution. */
#include "shared.h"
#if defined(FRONT_PROFILE) || defined(FRONT_TPU_BASE_CACHE)
#include "../c906-frontend/front_detail.h"
#endif

#define PROBE_TDMA 0x0c100000ul
#define PROBE_TIU  0x0c101000ul

static inline uint32_t probe_read(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

static inline void probe_write(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static uint64_t probe_clock(void)
{
    return (uint64_t)GetSysTime(); /* 25 MHz platform timer */
}

#ifdef FRONT_TPU_BASE_CACHE
static uint32_t probe_cached_bases[8];
static int probe_bases_valid,probe_bases_checked,probe_bases_allowed;
static void probe_begin_frame(void)
{probe_bases_valid=probe_bases_checked=0;probe_bases_allowed=1;}
#endif

static void probe_snapshot(uint32_t *r)
{
    static const uint16_t off[TPU_PROBE_REGS] = {
        0, 4, 8, 12, 0x70, 0x74, 0xa0, 0xa4, 0xec,
        0x1100, 0x1104, 0x110c
    };
    unsigned i;
    for (i = 0; i < TPU_PROBE_REGS; ++i)
        r[i] = probe_read(PROBE_TDMA + off[i]);
}

static void probe_resync(void)
{
    uint32_t r = probe_read(PROBE_TIU + 0x10c);
    probe_write(PROBE_TIU + 0x10c, r | 1u);
    probe_write(PROBE_TIU + 0x10c, r & ~1u);
    r = probe_read(PROBE_TIU + 0x100);
    r &= ~((1u << 30) | 1u);
    probe_write(PROBE_TIU + 0x100, r);
    probe_write(PROBE_TIU + 0x100, r | 2u);
    probe_write(PROBE_TDMA, 4u);
    probe_write(PROBE_TDMA, 0u);
    probe_write(PROBE_TDMA + 8, 0xffff0000u);
}

static int probe_execute_at(struct tpu_probe_control *ctl, uintptr_t base, unsigned dma_off)
{
    const struct tpu_probe_dma_header *h =
        (const void *)(base + dma_off);
    const struct tpu_probe_cpu_desc *d = (const void *)(h + 1);
    unsigned i, segment;
    if (h->magic_m != 0xb5b5 || !h->cpu_count || h->cpu_count > 16 ||
        sizeof(*h) + h->cpu_count * sizeof(*d) > ctl->dmabuf_bytes ||
        h->pmu_size)
        return -2;
#ifdef FRONT_PROFILE
    struct front_detail *detail=front_detail_at((void*)base);
    int profiling=ctl->operation==4u && detail->magic==FRONT_DETAIL_MAGIC;
    uint64_t setup_begin=probe_clock();
#endif
    for (i = 0; i < 8; ++i) {
        uint32_t low = i < 4 ? h->arraybase[2*i] : h->arraybase_extra[2*(i-4)];
        uint32_t high = i < 4 ? h->arraybase[2*i+1] : h->arraybase_extra[2*(i-4)+1];
        if (high) return -3; /* this probe only uses the board's low DDR */
#ifdef FRONT_TPU_BASE_CACHE
        if(ctl->operation!=4u || !probe_bases_allowed || !probe_bases_valid || probe_cached_bases[i]!=low)
#endif
            probe_write(PROBE_TDMA + 0x70 + 4*i, low);
#ifdef FRONT_TPU_BASE_CACHE
        probe_cached_bases[i]=low;
#endif
    }
#ifdef FRONT_TPU_BASE_CACHE
    if(ctl->operation!=4u || !probe_bases_allowed || !probe_bases_valid)
#endif
    {
    probe_write(PROBE_TDMA + 0x90, 0);
    probe_write(PROBE_TDMA + 0x94, 0);
    }
#ifdef FRONT_TPU_BASE_CACHE
    probe_bases_valid=ctl->operation==4u && probe_bases_allowed;
#endif
    for (segment = 0; segment < h->cpu_count; ++segment, ++d) {
        uint32_t nt = d->num_tiu & 0xffffu, nd = d->num_tdma & 0xffffu;
        uint64_t start;
        uint32_t r;
        uintptr_t dma_start = base + dma_off;
        if (!nd || nd > 512 || nt > 512 ||
            ((uintptr_t)d->offset_tdma << 6) < dma_start ||
            ((uintptr_t)d->offset_tdma << 6) + nd*64u > dma_start + ctl->dmabuf_bytes ||
            (nt && (((uintptr_t)d->offset_tiu << 8) < dma_start ||
              ((uintptr_t)d->offset_tiu << 8) >= dma_start + ctl->dmabuf_bytes)))
            return -4;
        probe_resync();
#ifdef FRONT_TPU_BASE_CACHE
        // Verify persistence across the unchanged TIU/TDMA resync once per frame.
        // A failed readback falls back to the original full configuration path.
        if(ctl->operation==4u && !probe_bases_checked){
            for(i=0;i<8;++i)if(probe_read(PROBE_TDMA+0x70+4*i)!=probe_cached_bases[i])probe_bases_allowed=0;
            if(probe_read(PROBE_TDMA+0x90) || probe_read(PROBE_TDMA+0x94))probe_bases_allowed=0;
            probe_bases_checked=1;probe_bases_valid=probe_bases_allowed;
            front_detail_at((void*)base)->flags|=probe_bases_allowed?64u:128u;
        }
#endif
        start = probe_clock();
        if (nt) {
            uintptr_t desc = (uintptr_t)d->offset_tiu << 8;
            probe_write(PROBE_TIU + 0x104, (uint32_t)desc);
            r = probe_read(PROBE_TIU + 0x108);
            probe_write(PROBE_TIU + 0x108, (r & 0xffffff00u) | (desc >> 32));
            r = probe_read(PROBE_TIU + 0x10c);
            probe_write(PROBE_TIU + 0x10c, r | (1u << 11));
            r = probe_read(PROBE_TIU + 0x100);
            r = (r & ~0x3fc00000u) | (3u << 22);
            /* Completion is polled; TIU interrupt output remains disabled. */
            r &= ~(1u << 31);
            probe_write(PROBE_TIU + 0x100, r | (1u << 30) | 1u);
        }
        probe_write(PROBE_TDMA + 4, d->offset_tdma);
        probe_write(PROBE_TDMA + 0xa0, 0);
        probe_write(PROBE_TDMA + 0xa4, 0);
        /* Mask all IRQ outputs. Status is read and cleared by C906. */
        probe_write(PROBE_TDMA + 8, 0xffffu);
        probe_write(PROBE_TDMA, 3u | (nd << 16) | (3u << 8) |
                    (1u << 5) | (1u << 13) | (1u << 10));
#ifdef FRONT_PROFILE
        uint64_t wait_begin=probe_clock();
        if(profiling)detail->tpu_setup_ticks+=wait_begin-setup_begin;
#endif
        for (;;) {
            uint32_t status = probe_read(PROBE_TDMA + 8) >> 16;
            if (status & ~0x21u) return -5;
            if ((status & 1u) && (probe_read(PROBE_TDMA + 12) >> 16) >= nd)
                break;
            if (probe_clock() - start > (uint64_t)ctl->timeout_us * 25u)
                return -6;
        }
        if (nt) {
            for (;;) {
                r = probe_read(PROBE_TIU + 0x100);
                if (((r >> 6) & 0xffffu) >= nt && (r & 2u)) break;
                if (probe_clock() - start > (uint64_t)ctl->timeout_us * 25u)
                    return -7;
            }
        }
#ifdef FRONT_PROFILE
        if(profiling)detail->tpu_wait_ticks+=probe_clock()-wait_begin;
#endif
        probe_write(PROBE_TDMA + 8, 0xffffffffu);
#ifdef FRONT_PROFILE
        setup_begin=probe_clock();
#endif
    }
    return 0;
}

#ifdef TIDESENSE_C906_TPU_LK
#include "lk_firmware.h"
#endif
#if defined(FRONT_IVE_HIST) || defined(FRONT_IVE_ST)
#include "../c906-ive/ive_firmware.h"
#endif
#ifdef TIDESENSE_C906_FRONTEND
#include "../c906-frontend/front_firmware.h"
#endif

static void c906_tpu_probe(uintptr_t base)
{
    struct tpu_probe_control *ctl = (void *)base;
    unsigned i;
    uint64_t start;
    uint64_t received = probe_clock();
    inv_dcache_range(base, TPU_PROBE_CONTROL_BYTES);
    if (ctl->magic != TPU_PROBE_MAGIC || ctl->version != TPU_PROBE_VERSION)
        return;
    if (ctl->operation == TPU_PROBE_CLOCK) {
        ctl->run_ticks[0] = probe_clock();
        ctl->status = 0;ctl->completed = 1;
        clean_dcache_range(base, TPU_PROBE_CONTROL_BYTES);
        return;
    }
#if defined(FRONT_IVE_HIST) || defined(FRONT_IVE_ST)
    if(ctl->operation==IVE_PROBE_OPERATION){c906_ive_probe(base);return;}
#endif
#ifdef TIDESENSE_C906_FRONTEND
    if(ctl->operation==TPU_PROBE_FRONT){c906_tpu_front(base);return;}
#endif
    inv_dcache_range(base+TPU_PROBE_CONTROL_BYTES, TPU_PROBE_BYTES-TPU_PROBE_CONTROL_BYTES);
#ifdef TIDESENSE_C906_TPU_LK
    if (ctl->operation == TPU_PROBE_LK) {
        c906_tpu_lk(base, received);
        return;
    }
#endif
    ctl->completed = 0;
    ctl->status = -1;
    start = probe_clock();
    probe_snapshot(ctl->regs_before);
    if (ctl->operation == TPU_PROBE_READ) {
        ctl->status = 0;
    } else if (ctl->operation == TPU_PROBE_RUN && ctl->repeats &&
               ctl->repeats <= TPU_PROBE_MAX_RUNS && ctl->timeout_us &&
               ctl->timeout_us <= 10000u && ctl->dmabuf_bytes >= 128 &&
               ctl->dmabuf_bytes <= TPU_PROBE_INPUT_OFF - TPU_PROBE_DMA_OFF) {
        for (i = 0; i < ctl->repeats; ++i) {
            uint64_t t = probe_clock();
            ctl->status = probe_execute_at(ctl, base, TPU_PROBE_DMA_OFF);
            ctl->run_ticks[i] = probe_clock() - t;
            if (ctl->status) break;
            ctl->completed++;
        }
    }
    probe_snapshot(ctl->regs_after);
    ctl->total_ticks = probe_clock() - start;
    clean_dcache_range(base, TPU_PROBE_CONTROL_BYTES);
}
