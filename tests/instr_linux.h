/* SPDX-License-Identifier: GPL-3.0-only */
/* (V2, 2026-11-18) The host CPU checks' instruction counter on Linux (WSL2 included): the kernel's user-space count of
 * retired instructions for this process (perf_event_open, PERF_COUNT_HW_INSTRUCTIONS), the Linux counterpart of
 * macOS's proc_pid_rusage ri_instructions. Exact run to run on the same build (measured: 6000017 for the same loop,
 * three runs in a row). 0 when the kernel has no counter for us (a VM without a PMU, perf_event_paranoid > 2): the
 * checks then skip, as they did before. The figures are an x86-64 host's: compare like with like (V2_BASELINE.md). */
#pragma once
#if defined(__linux__)
#include <linux/perf_event.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static uint64_t instr_linux(void)
{
    static int fd = -2;
    uint64_t v;
    if (fd == -2) {
        struct perf_event_attr a;
        memset(&a, 0, sizeof a);
        a.type = PERF_TYPE_HARDWARE;
        a.size = sizeof a;
        a.config = PERF_COUNT_HW_INSTRUCTIONS;
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        a.inherit = 0;
        fd = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
    }
    if (fd < 0 || read(fd, &v, sizeof v) != (ssize_t)sizeof v)
        return 0;
    return v;
}
#endif
