/* SPDX-License-Identifier: BSD-3-Clause
 * SC7U22 register protocol and initialization adapted from PX4's
 * src/drivers/imu/silan/sc7u22 (see LICENSE.PX4).
 * Standalone Linux SPI diagnostic; no PX4 runtime required.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int fd = -1;
static uint32_t speed = 1000000;
static volatile sig_atomic_t stopped;

static void stop(int sig) { (void)sig; stopped = 1; }
static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
static void delay_ms(unsigned ms)
{
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR && !stopped) {}
}
static int transfer(uint8_t *tx, uint8_t *rx, unsigned n)
{
    struct spi_ioc_transfer t = {
        .tx_buf = (uintptr_t)tx, .rx_buf = (uintptr_t)rx,
        .len = n, .speed_hz = speed, .bits_per_word = 8,
    };
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &t);
    if (ret != (int)n) {
        fprintf(stderr, "SPI transfer failed: %s (returned %d, expected %u)\n",
                ret < 0 ? strerror(errno) : "short transfer", ret, n);
        return -1;
    }
    return 0;
}
static int rd(uint8_t reg, uint8_t *data, unsigned n)
{
    uint8_t tx[385] = {0}, rx[385] = {0};
    if (n > sizeof(tx) - 1) return -1;
    tx[0] = reg | 0x80;
    if (transfer(tx, rx, n + 1)) return -1;
    memcpy(data, rx + 1, n);
    return 0;
}
static int wr(uint8_t reg, uint8_t value)
{
    uint8_t tx[] = {reg & 0x7f, value}, rx[2];
    return transfer(tx, rx, 2);
}
static int check(uint8_t reg, uint8_t expected)
{
    uint8_t v;
    if (rd(reg, &v, 1)) return -1;
    printf("reg 0x%02x = 0x%02x (expected 0x%02x) %s\n",
           reg, v, expected, v == expected ? "OK" : "FAIL");
    return v == expected ? 0 : -1;
}
static int fifo_reset(void)
{
    return wr(0x1d, 0x00) || wr(0x1d, 0x20);
}
static int initialize(void)
{
    if (wr(0x7f, 0) || wr(0x04, 0x50) || wr(0x4a, 0xa5)) return -1;
    delay_ms(1);
    if (wr(0x4a, 0xa5)) return -1;
    delay_ms(200);
    if (wr(0x7f, 0) || wr(0x04, 0x50) || wr(0x7d, 0)) return -1;
    delay_ms(1);
    /* +/-16 g, +/-2000 deg/s, performance filter, 1600 Hz. */
    if (wr(0x41, 3) || wr(0x43, 0) || wr(0x40, 0x8c) || wr(0x42, 0x8c)) return -1;
    delay_ms(2);
    if (wr(0x7d, 0x0e)) return -1;
    delay_ms(60);
    /* Headerless FIFO: accel XYZ followed by gyro XYZ, six 16-bit words. */
    if (wr(0x45, 0x88) || wr(0x1c, 6) || wr(0x1e, 24) || fifo_reset()) return -1;
    return check(0x01, 0x6a) || check(0x04, 0x50) || check(0x41, 3) ||
           check(0x43, 0) || check(0x40, 0x8c) || check(0x42, 0x8c) ||
           check(0x7d, 0x0e) || check(0x45, 0x88) || check(0x1c, 6) ||
           check(0x1d, 0x20) || check(0x1e, 24);
}
static int16_t be16(const uint8_t *b)
{
    return (int16_t)(((uint16_t)b[0] << 8) | b[1]);
}
int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/spidev0.0";
    char *end;
    long seconds = 10;
    if (argc > 4) goto usage;
    if (argc > 2) {
        seconds = strtol(argv[2], &end, 10);
        if (*end || seconds < 1 || seconds > 3600) goto usage;
    }
    if (argc > 3) {
        long hz = strtol(argv[3], &end, 10);
        if (*end || hz < 10000 || hz > 10000000) goto usage;
        speed = (uint32_t)hz;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    fd = open(dev, O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror(dev); return 1; }
    uint8_t mode = SPI_MODE_3, bits = 8;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) || ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed)) {
        perror("SPI configuration"); close(fd); return 1;
    }
    printf("SC7U22 device=%s mode=3 requested_speed=%u duration=%lds\n", dev, speed, seconds);
    /* Only select segment zero before identification; do not reset an unknown chip. */
    if (wr(0x7f, 0) || check(0x01, 0x6a)) { close(fd); return 1; }
    int result = 1;
    if (initialize()) goto out;
    double sum[6] = {0}, sq[6] = {0}, normsum = 0;
    int16_t lo[6], hi[6], last[6] = {0};
    for (int a = 0; a < 6; a++) { lo[a] = INT16_MAX; hi[a] = INT16_MIN; }
    unsigned long samples = 0, batches = 0, empty = 0, overflow = 0, clipped = 0, zero_accel = 0;
    double start = now(), report = start, temperature = NAN;
    while (!stopped && now() - start < seconds) {
        uint8_t status[2], raw[384], temp[2];
        delay_ms(5);
        if (rd(0x1f, status, 2)) goto out;
        unsigned words = ((status[0] & 15) << 8) | status[1];
        unsigned count = words / 6;
        if ((status[0] & 0x10) || count > 32) {
            overflow++;
            if (fifo_reset()) goto out;
            continue;
        }
        if (!count) { empty++; continue; }
        if (rd(0x21, raw, count * 12) || rd(0x22, temp, 2)) goto out;
        temperature = be16(temp) / 512.0 + 23.0;
        for (unsigned s = 0; s < count; s++) {
            double norm2 = 0;
            int bad = 0;
            for (int a = 0; a < 6; a++) {
                int16_t v = be16(raw + s * 12 + a * 2);
                last[a] = v;
                if (v < lo[a]) lo[a] = v;
                if (v > hi[a]) hi[a] = v;
                if (v >= 32760 || v <= -32760) { clipped++; bad = 1; }
                double x = v * (a < 3 ? 16.0 : 2000.0) / 32768.0;
                sum[a] += x; sq[a] += x * x;
                if (a < 3) norm2 += x * x;
            }
            if (bad && clipped <= 12)
                printf("ANOMALY t=%.6fs batch=%lu fifo_words=%u frame=%u/%u raw=%d,%d,%d,%d,%d,%d\n",
                       now()-start, batches, words, s+1, count,
                       last[0], last[1], last[2], last[3], last[4], last[5]);
            if (!last[0] && !last[1] && !last[2]) zero_accel++;
            normsum += sqrt(norm2);
            samples++;
        }
        batches++;
        /* Required by the reference driver after every FIFO burst. */
        if (fifo_reset()) goto out;
        if (now() - report >= 1) {
            printf("t=%.2fs samples=%lu acc[g]=%+.4f,%+.4f,%+.4f gyro[dps]=%+.3f,%+.3f,%+.3f temp=%.2fC\n",
                   now() - start, samples, last[0]/2048.0, last[1]/2048.0, last[2]/2048.0,
                   last[3]*2000.0/32768, last[4]*2000.0/32768, last[5]*2000.0/32768, temperature);
            report = now();
        }
    }
    printf("samples=%lu batches=%lu empty_polls=%lu overflows=%lu saturated_values=%lu zero_accel_frames=%lu captured_rate=%.1fHz\n",
           samples, batches, empty, overflow, clipped, zero_accel, samples/(now()-start));
    if (!samples) { fprintf(stderr, "FAIL: no FIFO samples\n"); goto out; }
    int accel_changes = 0, gyro_changes = 0;
    for (int a = 0; a < 6; a++) {
        double mean = sum[a] / samples;
        double var = fmax(0, sq[a]/samples - mean*mean);
        printf("%s%c mean=%+.6f stddev=%.6f raw_min=%d raw_max=%d\n",
               a < 3 ? "acc_" : "gyro_", "xyz"[a%3], mean, sqrt(var), lo[a], hi[a]);
        if (lo[a] != hi[a]) { if (a < 3) accel_changes++; else gyro_changes++; }
    }
    double gravity = normsum / samples;
    printf("mean_acc_norm=%.5fg final_temperature=%.2fC\n", gravity, temperature);
    result = stopped || samples < (unsigned long)seconds * 100 || overflow || clipped || zero_accel ||
             accel_changes != 3 || gyro_changes != 3 || gravity < 0.5 || gravity > 1.5 ||
             !isfinite(temperature) || temperature < -40 || temperature > 105;
    puts(result ? "FAIL: check counts, motion, saturation and register values above" :
         "PASS: SPI ID/configuration/FIFO/live accel and gyro/static gravity sanity checks");
    puts("This is not a calibrated accuracy, hardware self-test or IRQ test. Captured rate excludes FIFO reset losses.");
out:
    /* Leave the diagnostic device powered down, including on errors/signals. */
    if (wr(0x1d, 0)) result = 1;
    if (wr(0x7d, 0)) result = 1;
    close(fd);
    return result;
usage:
    fprintf(stderr, "Usage: %s [/dev/spidevN.0 [seconds=10 [speed_hz=1000000]]]\n", argv[0]);
    return 2;
}
