/* SPDX-License-Identifier: GPL-3.0-only */
/* (V2, 2026-11-18) A host cost pin, per host: HOST_PIN(mac, linux). The pins are host instruction counts (the measured
 * figure + 10 %), and a count is the compiler's and the ISA's: the same code reads 1.14x .. 1.66x on x86-64 GCC (the WSL2
 * build machine) what it reads on the Mac's arm64 clang, and not by one factor. Each Linux value was measured on the
 * unchanged Switch Blade 0.2-beta code (commit b3b8293 + the flash ID port, no DSP change) and is + 10 % like the Mac's.
 * Neither says anything about the FM-1 itself: that is PERF on the device (docs/V2_BASELINE.md). */
#pragma once
#ifdef __APPLE__
#define HOST_PIN(mac, linux) (mac)
#else
#define HOST_PIN(mac, linux) (linux)
#endif
