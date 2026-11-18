# Switch Blade V2: baseline (2026-11-18)

The starting point of Phase 0 (`../../2026-11-17-eight-machines.md`), on branch `v2`. Everything below was measured on the
0.2-beta code (`main` 9d07527), with no DSP change. The only firmware change is the flash identification (Phase 0.1); the
other changes are test tooling.

## Build machine (new)

The Mac build is replaced by **Windows 10 + WSL2 Ubuntu 24.04** (`wsl -d Ubuntu-24.04 -u root`, the tree at `/mnt/d/Switchblade/FM1-Four`):
- **Host:** GCC 13.3, Python 3 with Pillow, fontTools and NumPy, valgrind.
- **Target:** the JieLi toolchain `~/.jieli/toolchain` (jieli-linux-toolchains-20260730.1, clang 4.0.1 for pi32v2) and the AC79 SDK `~/fw-AC79_AIoT_SDK` (tag AC79NN_SDK_V1.2.1_2023-12-13). On Linux x86-64 `build.sh` runs them natively, without Docker.
- **Commands:** `sh ./build.sh`, then `sh tests/run_tests.sh` (about 6 min), from the repo root inside WSL.
- **Git:**
  - `.gitattributes` keeps LF line endings (the global `core.autocrlf=true` would otherwise convert the `.sh` files to CRLF and break them in WSL).
  - `core.fileMode false`: the Windows copy lost the executable bits.

## Image and memory (`build.py check()`)

| | V2 baseline | Limit | Free |
|---|---:|---:|---:|
| app image | 413,356 B | APP_SLOT 581,564 B | **168,208 B** |
| RAM `.data+.bss` | 43,492 B | 73,728 B | 30,236 B (floor 4,096) |
| pool | 279,480 B | 368,640 B | 89,160 B (floor 8,192) |
| loader | 6,930 B | | |
| package | 610,086 B, identity FM-1_940 | | |

The flash identification port added +120 B to the image (413,236 → 413,356). Flash allocation for V2 (the plan, §2): about 90 KB for the four machines, 60 KB for the UI and 15 KB in reserve.

## Tests

- `sh tests/run_tests.sh`: **ALL HOST TESTS PASSED.**
  - The perform FLANGER/PHASER failure the Mac always had is gone; it was the Mac's timing.
  - ASan/UBSan stages run on Linux (macOS's ASan runtime aborted them).
- `regress`: 141 golden renders, 0 changed. `union_test`: 168 engine-switch sequences, 0 differ.
- `ui_render`: 204 screens × 4 palettes, 0 lint findings; alignment 1,481,763 measurements, 0 off by 1 px.
- New: `flash_id_test` (both documented FM-1 flash parts, see below).

## The host CPU counter on Linux (new)

The cost checks read macOS's `proc_pid_rusage ri_instructions`. On Linux every one of them silently skipped: `regress` reported "0 instr" and "0 CPU entries over budget" without measuring anything. `tests/instr_linux.h` adds the kernel's user-space instruction counter (`perf_event_open`, `PERF_COUNT_HW_INSTRUCTIONS`). WSL2 provides it, and it is exact run to run (the same loop read 6,000,017 three times).

x86-64 GCC counts are not the Mac's arm64 clang counts, and not by one factor: 1.14× to 1.66×. So:
- **`tests/cpu_baseline.txt` was re-recorded on this machine**, on unchanged code (`BUDGET_UPDATE=1`, goldens untouched, then two clean runs). The Mac file is in `build/v2_logs/cpu_baseline_mac.txt`. This follows the precedent in `BASELINE.md`.
- **The fixed pins in the unit tests** (`DIST_COST_MAX`, `ACID_COST_MAX`, `LADDER_COST_MAX`, `CHORUS_COST_MAX`, WAVE's VOWEL and WARP checks, perform's OCT UP, FLANGER and PHASER) are now `HOST_PIN(mac, linux)` (`tests/host_pins.h`). Each Linux value is the figure measured on this code + 10 %. The Mac values are unchanged.

Key figures (host instructions per sample, `-O2`, this machine):

| | Linux x86-64 | Mac arm64 (old file) |
|---|---:|---:|
| `cpu/mix/idle` | 474 | 321 |
| `cpu/mix/4parts_light` | 2,465 | 2,292 |
| `cpu/mix/4parts_full` | 2,562 | 2,248 |
| `cpu/DIST/worst_mix_off` | 3,408 | 3,654 |
| `cpu/DIST/worst_mix_on` | 3,658 | 5,126 (the retired Scream DIST) |

`target_budget.py` (static pi32v2 loop counts) passes unchanged: it reads the target's own disassembly, so the build machine does not matter.

**None of this is a device figure.** The device has never been measured. The first V2 build that goes onto the user's FM-1 gets PERF on the `worst8` mix and on 0.2-beta's worst mix; that gives the first conversion from host to device numbers (the plan, §3.4).

## Phase 0.1: saving on newer FM-1s

0.2-beta allowed flash writes only if the JEDEC ID read `0x856014` (the Puya P25Q80H, `project.c persist_boot`). Newer FM-1s ship a UCUN UC25HQ80 (`0xB36014`); on those units Switch Blade stayed RAM-only, and no project, preset or setting saved.

`hal/fm1_flash_id.h` (from Quantum GammaWave, the same author and licence) now allows writes on either documented 1 MiB part, under these conditions:
- two ID reads must agree;
- the capacity byte must say 1 MiB;
- the status register must be idle, with no block-protect bits set.

Anything else stays RAM-only, as before. `tests/flash_id_test.c` covers both parts, a floating bus, a shorted bus, reads that disagree, unknown parts, another density, BP bits and busy. Both chips use the same standard SPI NOR commands, so nothing else in the driver depends on the chip.

**Not verified on a UCUN unit**: nobody here has one. The user's unit, the older chip, must keep saving as it does now; check that on the first device test.

## Phase 0.5 and 0.6: MIDI and the load budget (2026-11-18)

- Image 426,636 B (+1.5 KB on Phase 0.4); RAM .data+.bss 45,316 B of 73,728 (+1.8 KB: the 256-event MIDI IN ring).
- 413 goldens unchanged. The host never turns the load budget on (its ticks do not advance inside the render), so it cannot change a golden; `tests/audio_test.c load_budget` drives it with simulated times.
- `cpu/mix/worst8` (host, Linux counter): **3,863 instructions / sample**. `regress.c` picks it from each machine's dearest measured line, per voice, within each machine's cap, every part sounding, 8 voices in all. Today: JUNO DRIFT KEYS x3 on two parts, NANO BASSLINE x1 on two. It moves as the four new machines get their real voices (Phases 1-4), and it is the mix to PERF on the device with 0.2-beta's worst DIST mix (`cpu/DIST/worst_mix_on`).
- `fm1_alnk0_irq` static count 31,155 (budget 30,886, +0.9 %).
