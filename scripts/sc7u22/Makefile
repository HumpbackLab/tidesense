SDK_ROOT := $(abspath ../..)
CROSS_COMPILE ?= $(SDK_ROOT)/host-tools/gcc/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
CFLAGS := -O2 -std=c11 -Wall -Wextra -Werror

sc7u22_test: sc7u22_test.c
	$(CC) $(CFLAGS) -static $< -lm -o $@

.PHONY: clean
clean:
	rm -f sc7u22_test
