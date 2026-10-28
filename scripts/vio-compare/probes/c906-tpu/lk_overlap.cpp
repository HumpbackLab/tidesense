// Scheduling experiment using an FP64 covariance update workload.
// This is not a replay of an entire OpenVINS backend.
#include <Eigen/Dense>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <sys/ioctl.h>
#include <time.h>
#include "cvi_sys.h"
#include "lk_shared.h"

namespace {
double wall_ms() {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
double cpu_ms() {
    timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec * 1e-6;
}
struct CovarianceWork {
    Eigen::MatrixXd initial, p, h, hp, s, gain;
    CovarianceWork() {
        constexpr int n = 160, observations = 32;
        Eigen::MatrixXd a = Eigen::MatrixXd::Random(n, n);
        initial = a * a.transpose() / n + Eigen::MatrixXd::Identity(n, n);
        h = Eigen::MatrixXd::Random(observations, n) / std::sqrt(double(n));
        p = initial;
    }
    void step() {
        hp.noalias() = h * p;
        s.noalias() = hp * h.transpose();
        s.diagonal().array() += 0.5;
        // S is SPD. Solve in FP64, then perform the covariance downdate.
        gain = s.llt().solve(hp);
        p.noalias() -= hp.transpose() * gain;
        p = (0.5 * (p + p.transpose())).eval();
    }
    double run(unsigned iterations) {
        p = initial;
        for (unsigned i = 0; i < iterations; ++i) step();
        return p.sum();
    }
};
}

int lk_tpu_overlap(int fd, uint32_t phys, void *shared) {
    auto *metadata = reinterpret_cast<lk_tpu_control *>(
        static_cast<unsigned char *>(shared) + LK_TPU_META_OFF);
    const lk_tpu_control expected = *metadata;
    CovarianceWork work;
    unsigned iterations = 0;
    double start = wall_ms();
    do { work.step(); ++iterations; } while (wall_ms() - start < 30.0);
    start = wall_ms();
    const double reference = work.run(iterations);
    const double backend_alone = wall_ms() - start;
    const unsigned long command = _IOW('T', 2, uint32_t);
    if (CVI_SYS_IonFlushCache(phys, shared, LK_TPU_BYTES)) return -1;
    start = wall_ms();
    if (ioctl(fd, command, &phys)) return -1;
    const double lk_alone = wall_ms() - start;
    if (CVI_SYS_IonInvalidateCache(phys, shared, TPU_PROBE_CONTROL_BYTES)) return -1;
    if (reinterpret_cast<tpu_probe_control *>(shared)->status) return -1;

    for (unsigned trial = 0; trial < 3; ++trial) {
        if (CVI_SYS_IonFlushCache(phys, shared, LK_TPU_BYTES)) return -1;
        double backend_wall = 0, backend_cpu = 0, result = NAN;
        const double begin = wall_ms();
        std::thread backend([&]() {
            // Give the calling thread time to submit and sleep in the ioctl.
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            const double w = wall_ms(), c = cpu_ms();
            result = work.run(iterations);
            backend_cpu = cpu_ms() - c;
            backend_wall = wall_ms() - w;
        });
        const double c = cpu_ms();
        const int ret = ioctl(fd, command, &phys);
        const double wait_cpu = cpu_ms() - c;
        backend.join();
        const double overlap = wall_ms() - begin;
        if (ret || CVI_SYS_IonInvalidateCache(phys, shared, TPU_PROBE_CONTROL_BYTES) ||
            reinterpret_cast<tpu_probe_control *>(shared)->status) return -1;
        double max_delta = 0;
        for (unsigned p = 0; p < expected.count; ++p) {
            if (metadata->valid[p] != expected.valid[p]) return -1;
            if (expected.valid[p]) {
                const double delta = std::hypot(metadata->output[p].x - expected.output[p].x,
                                                metadata->output[p].y - expected.output[p].y);
                if (!std::isfinite(delta)) return -1;
                max_delta = std::max(max_delta, delta);
            }
        }
        std::printf("OVERLAP trial=%u matrix=160 observations=32 iterations=%u "
                    "backend_alone_ms=%.3f lk_alone_ms=%.3f sequential_ms=%.3f "
                    "overlap_ms=%.3f backend_overlap_ms=%.3f backend_cpu_ms=%.3f "
                    "lk_wait_cpu_ms=%.4f checksum_error=%.12g lk_delta_max_px=%.9g\n",
                    trial, iterations, backend_alone, lk_alone, backend_alone + lk_alone,
                    overlap, backend_wall, backend_cpu, wait_cpu, std::abs(result - reference), max_delta);
        if (!std::isfinite(result) || std::abs(result - reference) > 1e-9 || max_delta > 1e-5) return -1;
    }
    return 0;
}
