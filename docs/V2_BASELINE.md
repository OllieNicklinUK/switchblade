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

## Phase 0.7: the preset audit and the level report (2026-11-18)

- `tests/preset_audit_test.c` (in the suite): every factory preset loaded as PRESETS loads it, one C4 held for ~1 s, each preset in its own child process from power-on state. Fails on: E0..E7 outside the machine's (model's) range, an envelope stage or send past its range, a bad name (empty, unprintable, over 14), a name repeated in a machine, silence (RMS < 8). Reports: hot (2 % of the note at |x| >= 32000), exact duplicates.
- Today: 400 presets, 0 silent, 0 hot, 0 repeated names, 0 out of range; 264 exact duplicates, all Phase 0 placeholders (4 x 18 SLOT copies, 4 x 48 INIT twins).
- Levels (loudest ~93 ms, dBFS; `--levels`, `tools/gen_preset_levels.py`): target -14.1 (the median of the 128 voiced presets). Spread inside a machine: OB-Z 16.4 dB, WAVE 14.0, NANO 25.2, JUNO 12.6; 54 voiced presets are more than 3 dB off. The corrections go into the presets in Phase 5; the tool writes no firmware (LEVEL is the part's own fader on Switch Blade, kept across a preset load).
- Found on the way: measuring presets one after another in one process let the last preset's state (buses, engine memory) change the next one's level by up to 14 dB. The audit forks per preset; anything that measures presets must start each from power-on state.

## Phase 1: CZ-Z (2026-11-18)

- The voice: `firmware/src/cz_core.h` (the uPD933 after MAME's `upd933.cpp`, Devin Acker, BSD-3-Clause) and `firmware/src/eng_cz.c` (two lines, LINE 1 / 2 / 1+1' / 1+2', RING, NOISE, the 8-step DCW envelope, a DC blocker). Tables `build/gen/cz_tables.h` (cosine 4 KB, envelope rates).
- `tests/cz_ref.cpp`: MAME's file built unmodified on the host; 17,340,000 samples, 0 different; the cosine 0 of 2048 different. `tests/cz_test.c`: the parts that are ours (modes, DCW controls, the envelope's SUS and release, RING / NOISE, the preset's slots, every preset sounds with no DC).
- Image 433,648 B (+7.0 KB); RAM unchanged (45,316 B).
- Host CPU: CZ-Z presets 1,261 .. 2,027 instructions / sample for 8 voices (idle 474), about 190 a voice in the two-line modes; `worst8` unchanged at 3,863 (JUNO is still dearer per voice). Static target count: `cz_render` 114, `cz_wave` 18 (out of line; it holds one divide a line a sample: the device figure to watch).
- Goldens: the 50 CZ-Z lines replaced (the stub's are gone); the 363 others unchanged. Preset audit: CZ-Z level median -16.9 dBFS (target -14.1), spread 16.9 dB.

## Phase 3: JP-Z (2026-11-18, before Phase 2 at the user's request)

- `firmware/src/eng_jpz.c`: 7 free-running naive saws (the ESP2 detune law and knob curve, 25/128 centre + MIX/128 sides), OSC2 by BAL, a one-pole HPF at f0 (Q8, so no truncation DC), a 12/24 dB TPT SVF (P_FTYPE), KTR, WID = the Juno chorus as the part's stereo; engine memory 7 phases x 8 voices + a chorus, seeded per claim. Not checked against a JP-8000 (no OS files).
- Image 435,380 B (+1.7 KB); RAM unchanged. Host CPU: JP-Z presets 2,117 .. 2,380 instructions / sample for 8 voices, ~240 a voice; `worst8` unchanged (3,861). Static: `jpz_render` 216, no divides.
- Goldens: the 50 JP-Z lines replaced, the others unchanged. `union_test` 168 -> 264 sequences (the four new machines to and from the four originals; the 168 unchanged). Preset audit: JP-Z median -10.9 dBFS, spread 19.8 dB.

## Phase 2: GW-Z (2026-11-18)

- `firmware/src/eng_gwz.c`: Quantum GammaWave's WHEEL / VOICE / ADDTV as GW-Z's ORGAN / VOX / ADD models, by preset; every slot 0..127; ORGAN with a tone filter and a continuous ROTR speed; `engine_t.block` added (voice.c track_render). Engine memory: the model's per-part state and 8 voices.
- Image 443,488 B (+8.1 KB). Host CPU: GW-Z presets up to 2,453 instructions / sample for 8 voices (VOX 4), ~250 a voice; `worst8` unchanged (3,865). Static: `gw_render` 901 (4 divides: VOX's per-block coefficient work), `gw_block` 80.
- Goldens: the 50 GW-Z lines replaced, the others unchanged; `union_test`: only the GW-Z (6) switch lines re-recorded. Preset audit: GW-Z median -14.5 dBFS (on the library's -14.1), spread 21.8 dB.

## Phase 4: MICRO (2026-11-18) — all eight machines voiced

- `firmware/src/eng_micro.c`: two stepped 8-bit wavetable oscillators (16 own PPG-style tables, 64 waves each, no crossfade, no sample interpolation), EPOS sweep, OSC2 detune, GRIT (bits + hold), a 24 dB TPT SVF pair with KTR, a DC blocker. `eng_stub.c` removed.
- Image 460,836 B (+17 KB, 16 KB of it the tables; ~120 KB of the app area left). RAM unchanged (45,316 B).
- Host CPU per voice (heaviest preset, 8 voices, less idle): JUNO ~400 (cap 3), NANO ~510 (cap 1), GW-Z ~250, JP-Z ~240, CZ-Z ~190, MICRO ~180. `worst8` unchanged at 3,861 (JUNO + NANO). Static: `micro_render` 169, no divides.
- Preset audit, all 400 pass: level medians OB-Z -12.3, WAVE -14.3, NANO -15.2, JUNO -11.8, JP-Z -10.9, CZ-Z -16.9, GW-Z -14.5, MICRO -15.1 dBFS; 72 exact duplicates left, all the SLOT 33..50 placeholders of the four 0.2 machines.

## WAVE from the VOID WAVE V2 plugin (2026-11-19) — the "lean" upgrade, build v0.3-v2test5

- Source: the V2 plugin, `D:/VOID SYNTH/VoidWave` (git e280794); the reference tests now build its `Filter.cpp` (`VOIDWAVE_DIR`, default `../../VOID SYNTH/VoidWave`; `tools/void_ref/JuceHeader.h` gained `SmoothedValue`). Every case matches within 0.06 dB (levels) and 0.55 dB (harmonics), the drive cases included.
- `tools/gen_void_tables.py`: the frames normalised as V2's `normalize()` (DC out, RMS 0.35, peak at most 0.999); SQUARE's level per width in `VOID_SQ_GAIN`; SPECTRAL's 17 keyframes chosen again; `VOID_NCOL` (the noise colour).
- `eng_wave.c`: V2's drive shaper (not its 4x oversampling); FLT ENV on V2's law (63 = 4 octaves); PENV in E3 (was the hidden MIX), SUB in E6 (was OSC2), NOISE / COLOR / sub octave in P_FM2_ATK / SUS / DEC (a new NOISE page, WAVE only, after FLT ENV), OLVL (the oscillator's level, a preset's own) in P_FM3_ATK. SUB and NOISE at 0 run the old loops (8 specialised loops now: `void_render` static cost 8,579 -> 26,981, code, not per-sample cost).
- Presets: all 50 from the V2 factory set, `tools/wave_v2_presets.py` -> `presets/four/wave.py` (generated) and `docs/void_presets.md` (31 with nothing lost). WAVE categories ATMOS 10, LEAD 12, KEYS 14, BASS 14; the N_OLD rule no longer applies to WAVE. The 2026-10-20 bank is in git history.
- Host CPU: WAVE presets 1,284 .. 2,637 instructions / sample at the 6-voice cap (the old bank 1,436 .. 2,492); the heaviest (SUB ROLL, REESE DEEP: UNISON + SUB) 1.53 x 8 voices of OB-Z RAVE, inside the 1.75 x limit. Image 467,360 B (+6.5 KB on test4); RAM unchanged.
- Goldens: the 50 WAVE lines replaced, plus the two four-machine mixes (a WAVE part each) and the union_test lines; CPU baseline and target budget: only WAVE's entries. VOWEL's corner DC bound 1 % -> 2 % (V2's asymmetric fold through the symmetric clipper: 1.6-1.8 %).
- Levels (preset audit): WAVE median -18.4 dBFS (the library's -14.1), spread 26.7 dB, the FORMNT pads the quietest (-35.4): V2's own Phase B levels, carried by OLVL. To set in Phase 5 by ear.

## New preset banks for JP-Z, JUNO, CZ-Z; WAVE +4 dB and more basses (2026-11-19) — build v0.3-v2test6

- JP-Z: all 50 from Alan Marcero's JP-8000 / JP-8080 bank `jp80x0_rev_2`, with his permission (CREDITS.md; `presets/jp/`, `tools/jpz_alan_presets.py`, `docs/jpz_presets.md`): the 50 whose oscillators JP-Z plays. `eng_jpz.c` gained OSC2 PULSE (its width; DC-free) and TRI, OSC2 FINE, the JP's filter envelope (FLT ENV, now on JP-Z's pages too), its pitch envelope (to OSC1+2 or OSC2), OSC SHIFT and AMP LEVEL, in the retired operator ids, set per preset from `JPZ_PX` by `jpz_track_loaded`. Fixed: the SVF's floored `>> 13` integrated into DC at a cutoff near 30 Hz (1999 TRANCE: -879 -> -12); it rounds now. Categories LEAD 22, ATMOS 17, KEYS 3, BASS 8.
- JUNO: presets 33..50 (the SLOT placeholders) are 18 keys and leads from the Ultramaster KR-106's factory banks (Juno-60 and Juno-106; `tools/drift_kr106_presets.py`, real units from `tools/kr106_values.cpp` -> `presets/kr106/kr106_values.txt`, `docs/drift_presets.md`). `DRIFT_LX` carries their HPF, LFO (rate, delay, to pitch and filter) and octave switch (P_FM4_DEC); releases capped at 4 s (a voice must end). Categories BASS 12, LEAD 18, KEYS 20; the 32 seeds unchanged.
- CZ-Z: all 50 from the Casio CZ-230S's tones as CZ-101 SysEx (`tools/czz_cz230s_presets.py`, `docs/czz_presets.md`; Casio's factory data, used at the user's decision): line 1's 8-step DCW envelope exactly, the 8-step DCA as an ADSR, the vibrato as the LFO (`CZ_LX`), line 2's interval in semitones (slot 18: 64 + semitones; 0..4 still the old octaves). Lost: line 2's own envelopes, second waveforms, the DCO (pitch) envelopes. Categories KEYS 22, LEAD 13, ATMOS 11, BASS 4.
- WAVE: +4 dB after the clipper (`VOID_POST`: the clipper and filter unchanged); the four stabs swapped for V2's ACID TECHNO, SUB SAW LAYR, SUB STAB TEK, REESE WOBBLE (KEYS 10, BASS 18). VOWEL's corner DC bound 2 -> 2.5 % (the corner now reaches the master's limiter).
- Tests: `jpz_test.c` and `cz_test.c` pin their scenarios to the first voicing's TRANCE LEAD / CZ BASS / CZ BRASS (the banks changed under them); the WAVE scenario hashes re-recorded. Goldens, union, CPU and budget: only these four machines' entries (plus the two four-machine mixes and the switch sequences). All host tests pass.
- Levels (preset audit, dBFS median): WAVE -13.4 (was -18.4), JUNO -12.7, JP-Z -12.0, CZ-Z -15.6. Image 473,400 B; RAM unchanged.

## JP-Z bank: Roland factory + Alan Marcero (2026-11-20)

- JP-Z's 50: 21 of Roland's JP-8000 factory patches, the user's choice (singles S006 S007 S008 S010 S011 S028..S031 S073..S080 S096, performance parts P01U P12U P28L), read from the JP-8000 v1.05 firmware ROM (`../ROMS/jp8000_v1.05.bin`, not in the repo: single patches at 0x562B6, 160 bytes; performances at 0x5B2B6, 0x180 bytes, Upper +0x40, Lower +0xE0; bytes 0x00..0x49 are the SysEx patch layout, the rest internal: converted with VELOCITY off), and 29 of Alan Marcero's (18 leads, 4 atmos, 3 keys, 4 bass). `tools/jpz_presets.py` replaces `tools/jpz_alan_presets.py` (the same conversion). Categories LEAD 18, ATMOS 13, KEYS 10, BASS 9.
- Every JP-Z name is at most 10 characters (the home screen's large type); the other machines' names are unchanged (12).
- Goldens and the CPU baseline: only the JP-Z preset entries re-recorded. Roland's factory data: settle the licensing before the repo goes public (as CZ-Z).

## Scenes plan G.1: SONG leaves GLOBAL (2026-11-20)

- `d:\Switchblade\2026-11-20-scenes-and-performance.md` rev 6. GLO's only page is **GLOBAL** (`GR_SCENE`, a placeholder until G.4); the keyboard plays no notes there (`song.grid` 3: every key the screen's). The SONG page, its timeline, the A..D keys, TOOLS' DELETE ROW / CLEAR SONG, the header's song disc and section, and song playback are gone.
- `song_chain.c` stays in the tree, out of the build. `chain_busy()` is a stub (0) until its guards go. FOR4 unchanged: a save writes default chain bytes, a load still checks them, so every older project loads as before. Editor command 33 SONG keeps its reply shape, refuses every set / start (rc 1), op 3 still stops (`web/EDITOR_PROTOCOL.md`).
- Image 462,704 B (-10.7 KB); RAM .data+.bss 32,020 B (-13.3 KB: the song's four pattern sources, for the scenes' buffers).
- Tests retired with SONG: ui_test `test_chain`, `test_song_timeline`, `test_song_lanes`, `test_song_lanes_drawn`, `test_song_pictures`, `chain_screens` and the "while a song plays" checks; motion_test's song motion / locks; seq155 `cond_song` (a scene variant comes with G.2); persistence's pending-SONG and song-source checks; detail_test's song check; ui_render's 16 SONG screens (S_GLOBAL in their place) and its SONG band lint. editor_test's SONG test rewritten for the retired reply. Goldens unchanged (0 changed); all host tests pass.

## Scenes plan G.2: the scenes (2026-11-20)

- `firmware/src/scene.c` (included at the end of `project.c`): 16 slots, one bank for the device. A scene is a FOR4 image (`project_capture` + `proj_pack`: every parameter, engine, preset, the steps, the automation; mutes and transposes are parameters; the TRANCE GATE pattern in the unused chain bytes, row 15). Mirrored in `.pool` (16 x 3840 B + the PREV state + a decode buffer), read at power-on (`scene_boot`, main.c).
- Storage: `OBJ_SCENE0` .. +15 (types 10..25), A/B pairs at 0x9F000 .. 0xBEFFF (the retired user-sample space; a sector with no valid record reads as empty). A save stopped writes at once; playing, the mirror only (msg2 TO FLASH AT STOP), written at STOP by `scene_poll`.
- GLOBAL's keys (`ui_input.c global_keys`): a white key recalls; OCT+ (SHIFT) + a white key saves (empty at once, full after 1 s held); black 1..4 mute parts 1..4, 5 PREV (swaps with the state before the last recall), 7 NEXT (the next filled slot); 6 MUTATE is G.6. OCT+/- shift no octave on GLOBAL. LEDs: filled slots, sounding parts. The page shows the scene now / queued / count until G.4's grid.
- Playing, a recall is queued to the end of the longest track's loop (`seq.c scn_tick`, LEN x DIV): the tracks wait at their top while the main loop applies it, then start step 1 with the wait carried (in time with the clock; CONDITIONS from the scene's loop 1). The current slot cancels a queued one, another key replaces it, STOP applies it at once. `scn_tick_slow` is noinline: the audio ISR's static cost stays in budget (`tests/target_budget.py`).
- Image 465,820 B (+3.1 KB); RAM 32,100 B; pool 349,040 of 368,640 B (19.6 KB spare, floor 8 KB).
- Tests: ui_test `test_scenes` (keys silent, save / recall exact, PREV, the 1 s hold, mutes, NEXT, the loop-end switch in time, cancel / replace / STOP, save while playing); persistence_test `scenes_test` (flash at once / at STOP, power-on read back, old bytes read empty); storage_test (the scenes' sectors apart from every other object). All host tests pass; goldens unchanged.

## Scenes plan G.3: channel 16 (2026-11-20)

- `midi_control.c midi_event`: on channel 16, before ROUT's block filter, a Program Change selects scene PC mod 16 at once (an Elektron's PROG CH SEND on CH 16: A01..A16, later banks wrapping; playing, the next block, every track from step 1), CC 85 a scene (mod 16) as a GLOBAL key (playing: at the loop's end), CC 86 the MORPH amount (kept for G.6). Only these three: channel 16's notes and its other CCs go on as ROUT says (CH13-16 / TRACK: part 4 still plays there), a narrower take than rev 6's "channel 16 taken". MENU > MIDI > PC IN OFF ignores the scenes' PC and CC 85 too.
- Tests: midi_control_test `scenes_midi_test` (PC mod 16, CC 85, CC 86, channel 1's PC still a sound, CH13-16 notes / CC 102 still part 4's, PC IN OFF, playing: at once from step 1).
