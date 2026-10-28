#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "cvi_sys.h"
#include "cvikernel/cvikernel.h"
#include "shared.h"

/* Pure command-format conversion; these functions do not submit TPU work. */
extern int cvi181x_dmabuf_size(uint8_t *, size_t, size_t *, size_t *);
extern int cvi181x_dmabuf_convert(uint8_t *, size_t, uint8_t *);
extern int cvi181x_dmabuf_relocate(uint8_t *, uint64_t, uint32_t, uint32_t);

#define PROBE_IOCTL _IOW('T', 1, uint32_t)
#define ELEMENTS 2048u

static uint64_t clock_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static uint16_t bf16(float value)
{
    uint32_t u;
    memcpy(&u, &value, sizeof(u));
    return (uint16_t)(u >> 16); /* test values have exact BF16 representations */
}

static int make_job(uint8_t *shared, uint64_t phys, int arithmetic)
{
    uint8_t *storage = calloc(1, 65536);
    cvk_reg_info_t reg = { .chip_ver_str = "cv181x", .cmdbuf_size = 65536,
                           .cmdbuf = storage };
    cvk_context_t *ctx = cvikernel_register(&reg);
    cvk_tl_shape_t shape = {1, 8, 8, 32};
    cvk_tg_t input = { .start_address = phys + TPU_PROBE_INPUT_OFF,
        .fmt = CVK_FMT_BF16, .shape = {1, 8, 8, 32} };
    cvk_tg_t output = input;
    cvk_tl_t *a = NULL, *b = NULL;
    size_t dma_bytes = 0, pmu_bytes = 0;
    uint32_t command_bytes = 0;
    uint8_t *commands;
    int ret = -1;
    if (!storage || !ctx) goto out;
    input.stride = ctx->ops->tg_default_stride(ctx, input.shape, input.fmt);
    output.stride = input.stride;
    output.start_address = phys + TPU_PROBE_OUTPUT_OFF;
    a = ctx->ops->lmem_alloc_tensor(ctx, shape, CVK_FMT_BF16, 1);
    b = ctx->ops->lmem_alloc_tensor(ctx, shape, CVK_FMT_BF16, 1);
    if (!a || !b) goto out;
    cvk_tdma_g2l_tensor_copy_param_t load = { .src = &input, .dst = a };
    ctx->ops->tdma_g2l_bf16_tensor_copy(ctx, &load);
    if (arithmetic) {
        cvk_tiu_mul_param_t mul = { .res_low = b, .a = a, .b_is_const = 1,
            .b_const = {.val = 0x4000, .is_signed = 1} };
        cvk_tiu_add_param_t add = { .res_low = b, .a_low = b, .b_is_const = 1,
            .b_const = {.val = 0x3f80, .is_signed = 1} };
        ctx->ops->tiu_mul(ctx, &mul);
        ctx->ops->tiu_add(ctx, &add);
    }
    cvk_tdma_l2g_tensor_copy_param_t save = {
        .src = arithmetic ? b : a, .dst = &output };
    ctx->ops->tdma_l2g_bf16_tensor_copy(ctx, &save);
    commands = ctx->ops->acquire_cmdbuf(ctx, &command_bytes);
    if (!commands || !command_bytes ||
        cvi181x_dmabuf_size(commands, command_bytes, &dma_bytes, &pmu_bytes) ||
        dma_bytes > TPU_PROBE_INPUT_OFF - TPU_PROBE_DMA_OFF)
        goto out;
    if (cvi181x_dmabuf_convert(commands, command_bytes, shared + TPU_PROBE_DMA_OFF) ||
        cvi181x_dmabuf_relocate(shared + TPU_PROBE_DMA_OFF,
                              phys + TPU_PROBE_DMA_OFF, dma_bytes, 0))
        goto out;
    ((struct tpu_probe_control *)shared)->dmabuf_bytes = dma_bytes;
    const struct tpu_probe_dma_header *h = (const void *)(shared + TPU_PROBE_DMA_OFF);
    printf("job command_bytes=%u dma_bytes=%zu segments=%u tiu=%u tdma=%u\n",
           command_bytes, dma_bytes, h->cpu_count, h->tiu_count, h->tdma_count);
    ret = 0;
out:
    if (ctx) {
        if (b) ctx->ops->lmem_free_tensor(ctx, b);
        if (a) ctx->ops->lmem_free_tensor(ctx, a);
        ctx->ops->cleanup(ctx);
        free(ctx);
    }
    free(storage);
    return ret;
}

static int compare_ticks(const void *ap, const void *bp)
{
    uint64_t a = *(const uint64_t *)ap, b = *(const uint64_t *)bp;
    return (a > b) - (a < b);
}

int main(int argc, char **argv)
{
    CVI_U64 phys = 0;
    void *allocation = NULL;
    int fd = -1, ret = 1;
    unsigned i, call, errors, repeats, calls;
    int read_only, arithmetic;
    uint64_t roundtrips[1000], total_wall = 0;
    if (argc < 2 || argc > 4 ||
        (strcmp(argv[1], "read") && strcmp(argv[1], "copy") && strcmp(argv[1], "affine"))) {
        fprintf(stderr, "usage: %s read|copy|affine [C906-repeats=1] [A53-calls=1]\n", argv[0]);
        return 2;
    }
    repeats = argc > 2 ? (unsigned)atoi(argv[2]) : 1;
    calls = argc > 3 ? (unsigned)atoi(argv[3]) : 1;
    if (!repeats || repeats > 1000 || !calls || calls > 1000) return 2;
    read_only = !strcmp(argv[1], "read");
    arithmetic = !strcmp(argv[1], "affine");
    if (CVI_SYS_IonAlloc_Cached(&phys, &allocation, "c906_tpu_probe", TPU_PROBE_BYTES) ||
        !allocation || phys > 0x8ff40000u - TPU_PROBE_BYTES) {
        fprintf(stderr, "shared ION allocation failed or out of bounds\n");
        goto done;
    }
    uint8_t *shared = allocation;
    struct tpu_probe_control *ctl = allocation;
    memset(shared, 0, TPU_PROBE_BYTES);
    ctl->magic = TPU_PROBE_MAGIC;
    ctl->version = TPU_PROBE_VERSION;
    ctl->operation = read_only ? TPU_PROBE_READ : TPU_PROBE_RUN;
    ctl->repeats = repeats;
    ctl->timeout_us = 5000;
    if (!read_only && make_job(shared, phys, arithmetic)) {
        fprintf(stderr, "command generation failed\n"); goto done;
    }
    uint16_t *input = (void *)(shared + TPU_PROBE_INPUT_OFF);
    uint16_t *output = (void *)(shared + TPU_PROBE_OUTPUT_OFF);
    for (i = 0; i < ELEMENTS; ++i) input[i] = bf16((int)(i % 32) - 16.0f);
    fd = open("/dev/c906-tpu-probe", O_RDWR);
    if (fd < 0) { perror("open bridge"); goto done; }
    printf("mode=%s physical=0x%08llx repeats=%u calls=%u elements=%u\n",
           argv[1], (unsigned long long)phys, repeats, calls, ELEMENTS);
    for (call = 0; call < calls; ++call) {
        uint32_t phys32 = phys;
        uint64_t begin, elapsed, sum = 0;
        memset(output, 0xa5, ELEMENTS * sizeof(*output));
        ctl->status = -999;
        ctl->completed = 0;
        if (CVI_SYS_IonFlushCache(phys, shared, TPU_PROBE_BYTES)) goto done;
        begin = clock_ns();
        if (ioctl(fd, PROBE_IOCTL, &phys32) < 0) { perror("C906 mailbox"); goto done; }
        if (CVI_SYS_IonInvalidateCache(phys, shared, TPU_PROBE_BYTES)) goto done;
        elapsed = clock_ns() - begin;
        roundtrips[call] = elapsed;
        total_wall += elapsed;
        errors = 0;
        if (!read_only) for (i = 0; i < ELEMENTS; ++i) {
            float x = (int)(i % 32) - 16.0f;
            uint16_t expected = bf16(arithmetic ? 2*x+1 : x);
            if (output[i] != expected) {
                if (errors < 4) fprintf(stderr, "element=%u expected=%04x got=%04x\n",
                                      i, expected, output[i]);
                ++errors;
            }
        }
        if (call == 0 || ctl->status || errors) {
            printf("C906 status=%d completed=%u total_us=%.3f roundtrip_us=%.3f errors=%u\n",
                   ctl->status, ctl->completed, ctl->total_ticks / 25.0, elapsed / 1000.0, errors);
            for (i = 0; i < TPU_PROBE_REGS; ++i)
                printf("reg[%u] before=%08x after=%08x\n", i, ctl->regs_before[i], ctl->regs_after[i]);
        }
        if (ctl->status || errors || (!read_only && ctl->completed != repeats)) goto done;
        if (!read_only) {
            for (i = 0; i < repeats; ++i) sum += ctl->run_ticks[i];
            qsort(ctl->run_ticks, repeats, sizeof(ctl->run_ticks[0]), compare_ticks);
            if (call == 0)
                printf("C906 execution_us min=%.3f median=%.3f p95=%.3f max=%.3f mean=%.3f\n",
                       ctl->run_ticks[0]/25.0, ctl->run_ticks[repeats/2]/25.0,
                       ctl->run_ticks[(repeats-1)*95/100]/25.0,
                       ctl->run_ticks[repeats-1]/25.0, sum/25.0/repeats);
        }
    }
    qsort(roundtrips, calls, sizeof(roundtrips[0]), compare_ticks);
    printf("PASS mode=%s calls=%u repeats=%u roundtrip_us min=%.3f median=%.3f p95=%.3f max=%.3f mean=%.3f\n",
           argv[1], calls, repeats, roundtrips[0]/1000.0, roundtrips[calls/2]/1000.0,
           roundtrips[(calls-1)*95/100]/1000.0, roundtrips[calls-1]/1000.0,
           total_wall/1000.0/calls);
    ret = 0;
done:
    if (fd >= 0) close(fd);
    if (allocation) CVI_SYS_IonFree(phys, allocation);
    return ret;
}
