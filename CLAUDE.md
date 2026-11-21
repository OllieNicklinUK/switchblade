# Switch Blade V2 — context for AI agents

Switch Blade is custom firmware for the M-VAVE / Hügelton **FM-1** (JieLi AC79, 240×240 screen, 27 keys F3..G5,
four knobs + SELECT / ALGORITHM / PRESETS, 14 buttons: FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+;
**there is no SHIFT button**). It descends from Felucca (Leo Kuroshita, GPL-3.0). V2 has **eight machines** (any four on the
four parts): OB-Z, WAVE, NANO, JUNO, JP-Z, CZ-Z, GW-Z, MICRO, 50 presets each.

Read `docs/V2_BASELINE.md` (the change log, newest at the end) before changing anything.

## Current release: v0.3 (2026-11-21, works on the user's device)

- Built from branch **`v2-rackui`** (worktree `D:\Switchblade\FM1-Four-rackui`), tag `v0.3`.
  `build/release-0.3/switchblade-0.3.fwsc`, identity FM-1_9403, version `v0.3`; installer site `build/v0.3-site`.
- What it holds: the rack UI, GLOBAL's scenes (G.1–G.3), the JP-Z Roland + Alan Marcero bank, the Juno chorus fade fix,
  and everything of the v0.3-v2test1..6 builds (`docs/V2_DEVICE_TEST.md`).

## Branches and trees

| Branch | Tree | What |
|---|---|---|
| `v2-rackui` | `D:\Switchblade\FM1-Four-rackui` | **the shipping line**: `v2` + the rack UI. Build releases from here |
| `v2` | `D:\Switchblade\FM1-Four` | engines, presets, scenes; merged into `v2-rackui` (`git merge v2`) |
| `main` | (GitHub) | the 0.2-beta line; behind `v2` |

New work: the scenes plan's G.4 (the GLOBAL screen) belongs on `v2-rackui` (it is drawn in the rack style). Engine and
preset work can stay on `v2` and be merged across.

## Build, test, install (WSL Ubuntu-24.04; JieLi toolchain `~/.jieli`, SDK `~/fw-AC79_AIoT_SDK`)

```
wsl -d Ubuntu-24.04 -u root -- sh -c "cd /mnt/d/Switchblade/FM1-Four-rackui && sh build.sh"                 # dev build
wsl -d Ubuntu-24.04 -u root -- sh -c "cd /mnt/d/Switchblade/FM1-Four-rackui && sh build.sh --release 0.3"   # release X.Y
wsl -d Ubuntu-24.04 -u root -- sh -c "cd /mnt/d/Switchblade/FM1-Four-rackui && sh tests/run_tests.sh"       # host tests (> 10 min)
python web/make_site.py build/release-0.3/switchblade-0.3.fwsc 0.3 build/v0.3-site                          # installer site
cd build/v0.3-site && py -3 -m http.server 8000      # then http://localhost:8000/webapp/installer/ in Chrome / Edge
```

- Build gates (`tools/build.py`): RAM, a pool headroom of at least 8 KiB, and the image size.
- Test gates: `tests/target_budget.py` checks the audio ISR's static cost. Put heavy work behind a `noinline` slow path.
- Goldens: `GOLDEN_UPDATE=1` and `BUDGET_UPDATE=1` re-record them. Do this only for an intended sound change, and keep
  every unaffected entry.
- **Known:** 98 `ui_test` checks of the old HOME / EDIT / palette screens fail on `v2-rackui`. They come from the rack UI's
  first pass, which the old tests were not rewritten for. Nothing else fails. Compare the failure list before and after a
  change.

## The UI (rack style, `v2-rackui`)

- HOME (`firmware/src/ui_rack.c`): four rack units in their machine's colour. The selected part is 2U, with its name large,
  a dot-matrix meter and its own scope. The others are 1U: name, meter, volume knob, MIDI light, the machine's badge.
- Value pages (`ui_rows.c`, `ui.c rows_list` / `rows_turn`): each page is a row of a sheet, and the focused row is 2U with
  big knobs. ALGORITHM moves along the rows.
- Fonts: Oswald SemiBold, faces D / DL / DS (OFL). Knob caps come from `tools/gen_ui_knobs.py`.
- **Text is always drawn sharp, with no glow or halo** (the user's rule).
- Design: `D:\Switchblade\2026-11-19-rack-ui.md`, mockup `D:\Switchblade\SB-new.jpg`.

## GLOBAL and the scenes (plan `D:\Switchblade\2026-11-20-scenes-and-performance.md`, rev 6)

- **Screen rule:** every screen except EDIT works on its own keys and four knobs. No menus, dialogs or hidden modes.
- **GLO** opens **GLOBAL** (`GR_SCENE`). SONG is removed: `song_chain.c` stays in the tree but out of the build, and
  `chain_busy()` is a 0 stub. FOR4 projects keep their chain bytes, so old projects load. Editor command 33 refuses.
- **GLOBAL's keys** (`ui_input.c global_keys`) play no notes (`song.grid` 3):
  - a white key recalls slot 1..16;
  - **OCT+ held is SHIFT**: OCT+ + a white key saves (an empty slot at once, a full one after the key is held 1 s);
  - black keys 1..4 mute parts 1..4, black 5 (D#4) is PREV, black 7 (G#4) is NEXT, black 6 (MUTATE) is still to come.
- **Scenes** (`firmware/src/scene.c`, included at the end of `project.c`):
  - one bank of 16 for the device, each a FOR4 image (every parameter, machine, preset, steps, automation), with the
    TRANCE GATE pattern in chain row 15;
  - mirrored in `.pool` and read at boot (`scene_boot`);
  - flash objects 10..25 (A/B at 0x9F000..0xBEFFF);
  - **flash is written only while stopped** (an erase silences audio ~40 ms), so a save while playing waits in RAM for STOP.
- **Recall while playing** waits for the end of the longest track's loop (`seq.c scn_tick`, LEN × DIV). Every track then
  starts step 1 in time, and CONDITIONS count from the scene's loop 1.
- **MIDI on channel 16**, before ROUT: a Program Change recalls scene PC mod 16 at once (the Syntakt's PROG CH SEND on 16),
  CC 85 selects a scene as a key does, and CC 86 holds MORPH's amount. Channel 16's notes still route.
- **To do:**
  - G.4: the screen (a 4×4 grid, 16 effect chips, the header; the user signs off the look);
  - G.5: the new effects (GLITCH, BUILD, DOWNLIFTER, TRANCE GATE, PUMP, RANDOMISE, the CRUSH key) and the new default
    `perf_map`;
  - G.6: MORPH (knob 1, CC 86), MUTATE, latched effects per scene;
  - G.7: device PERF.

## Recent patches (newest first; details in `docs/V2_BASELINE.md`)

- **Juno chorus fade fix** (`dsp_chorus.c`, JUNO and JP-Z): the fade bounced every 8 samples, giving faint high tones at
  odd multiples of FS/16 around the note. 66 goldens were re-recorded (b917c32).
- **Scenes G.1–G.3** (448cb72, cc3b2b6, 75fd6e7): SONG removed, GLOBAL scenes, channel 16.
- **JP-Z bank** (798660c): 21 Roland JP-8000 factory patches and 29 of Alan Marcero's (his permission).
  - Converter: `tools/jpz_presets.py`. It reads `D:\Switchblade\ROMS\jp8000_v1.05.bin`; the ROM is **never** committed.
  - Patch layout in the ROM: singles at 0x562B6 (160 B each), performances at 0x5B2B6 (0x180 B).
  - JP-Z names are at most 10 characters (the home screen's large type).
- **Device test 6 fixes** (2b1ce3b): JP-Z's LFO pitch and pitch envelope depths scaled down.
- **v0.3-v2test6** (7a71988): the JUNO 33–50 KR-106 presets, CZ-Z's CZ-230S tones, WAVE +4 dB and more basses.
- **v0.3-v2test5** (639988c): WAVE upgraded to the VOID WAVE V2 plugin.

## Rules

1. **Never flash the device without asking** in that session.
2. **Stage only your own files** (`git add <paths>`, never `-A`). Other Claude sessions work in these trees at the same
   time: a chorus fix was once swept into an unrelated commit.
3. **Licensing is open:** the JP-Z Roland factory data and the CZ-Z Casio tones. Settle it before these branches are
   public. `origin` (GitHub) is public.
4. Saving must keep working on the newer FM-1 flash (UCUN 0xB36014) as well as the Puya part.
5. Keep `docs/V2_BASELINE.md` updated with every change, and this file when the release, branches or plan status change.
