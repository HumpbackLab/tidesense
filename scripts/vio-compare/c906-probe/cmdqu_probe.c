#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/sort.h>
#include "rtos_cmdqu.h"

#define NPROBE 100

static u64 times_ns[NPROBE];

static int cmp_u64(const void *a, const void *b)
{
	u64 av = *(const u64 *)a;
	u64 bv = *(const u64 *)b;
	return (av > bv) - (av < bv);
}

static int __init cmdqu_probe_init(void)
{
	int i, ret;
	u64 total = 0;
	for (i = 0; i < NPROBE; ++i) {
		cmdqu_t msg = {0};
		u64 start;
		msg.ip_id = IP_SYSTEM;
		msg.cmd_id = SYS_CMD_INFO_LINUX;
		msg.resv.mstime = 200;
		msg.param_ptr = i + 1;
		start = ktime_get_ns();
		ret = rtos_cmdqu_send_wait(&msg, SYS_CMD_INFO_LINUX);
		if (ret) {
			pr_err("cmdqu_probe: call %d returned %d\n", i, ret);
			return ret;
		}
		if (msg.ip_id != IP_SYSTEM || msg.cmd_id != SYS_CMD_INFO_LINUX ||
		    msg.param_ptr != i + 1) {
			pr_err("cmdqu_probe: mismatched reply at call %d\n", i);
			return -EIO;
		}
		times_ns[i] = ktime_get_ns() - start;
		total += times_ns[i];
	}
	sort(times_ns, NPROBE, sizeof(times_ns[0]), cmp_u64, NULL);
	pr_info("cmdqu_probe: %d roundtrips min=%llu us median=%llu us p95=%llu us max=%llu us mean=%llu us\n",
		NPROBE, times_ns[0] / 1000, times_ns[NPROBE / 2] / 1000,
		times_ns[(NPROBE - 1) * 95 / 100] / 1000,
		times_ns[NPROBE - 1] / 1000, total / NPROBE / 1000);
	return 0;
}

static void __exit cmdqu_probe_exit(void) {}

module_init(cmdqu_probe_init);
module_exit(cmdqu_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Temporary TideSense A53/C906 command-queue roundtrip probe");
