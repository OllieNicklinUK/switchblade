# V2 device test (build v0.3-v2test5; test1 to test4 also work with this sheet)

The first V2 build on a real FM-1. It checks that saving still works on your unit, measures the CPU on the device for
the first time, and tries the new MIDI. About 30–40 minutes. Note what you see against each step; anything odd is
worth writing down, even if you are not sure it is a fault.

**The build (use this one):** `build/release-0.3-v2test5/switchblade-0.3-v2test5.fwsc`, identity `FM-1_9403`, version
`v0.3-v2test5`, SHA-256 `622d0e01…313fded6` (`SHA256SUMS` beside it). Branch `v2`, Phases 0 to 4: **all eight machines
have their own voices**. Its installer site is `build\v2test5-site`.
(test5 is test4 plus WAVE upgraded to the VOID WAVE V2 plugin: 50 new WAVE presets, PENV / SUB / NOISE, section 3b.
test4, `v0.3-v2test4`, `build\v2test4-site`, has the old WAVE bank.)
(test4 is test3 plus OCT+ held and the PRESETS knob to switch machine, section 3.)
(test3 is test2 with JP-Z's crackle fixed: its mix ran 8x hot, so the saws in phase clipped its filter and wrapped its
voice gain. test2, `v0.3-v2test2`, `build\v2test2-site`, has that crackle.)
(test1, `v0.3-v2test1`, `build\v2test-site`: Phases 0 and 1 only, JP-Z, GW-Z and MICRO a placeholder saw.)

**What it is not yet:** presets 33–50 of the four old machines are copies named SLOT 33..50, and the new machines'
presets are a first voicing, not yet set by ear. The screens look as before, with stand-in colours for the new
machines. All of that is expected.

**Also try on test2 (section 3):** JP-Z (key 5): DTN on the machine knob, WID on EDIT page 2 for stereo; GW-Z (key 7):
an ORGAN preset (ROTR on the machine knob speeds the rotor), a VOX choir, an ADD bell; MICRO (key 8): EPOS on the
machine knob sweeps the wavetable, GRIT on page 2. For CPU (section 4), add mix D: four parts of JP-Z SUPER PAD
(preset 17), 2 notes each.

## 0. Before installing (on 0.2-beta)

1. **Back up.** Serve the site (step 1 below), open `http://localhost:8000/webapp/editor/` in Chrome or Edge, connect,
   and use the **Backup** panel to save everything (projects, user presets, settings) to a file. Keep it.
2. **A CPU reading on 0.2-beta, for comparison.** This gives the first conversion from host numbers to the device:
   - Parts 1 and 4: JUNO, preset 23 **DRIFT KEYS**, JUNO page CHOR **I+II**. Hold or sequence 3 notes on each.
   - Parts 2 and 3: NANO, preset 2 **BASSLINE**. One note each.
   - All four playing together (a pattern on each is easiest). Read **MENU > SYSTEM > INFO > CPU**.
     Write it down, and note any clicks or dropouts.

## 1. Install

The command-line installer needs `python-rtmidi`, which is not on this PC, so use the web installer from a local copy:

```
cd D:\Switchblade\FM1-Four\build\v2test5-site
py -3 -m http.server 8000
```

Open `http://localhost:8000/webapp/installer/` in **Chrome or Edge**, connect the FM-1 by USB and install. Close the
editor tab and any DAW using the FM-1's MIDI port first. Leave the server window open until the FM-1 has restarted.

## 2. Saving (the most important check)

- [ ] MENU > SYSTEM > INFO shows **v0.3-v2test5**.
- [ ] Your 0.2-beta projects and user presets are still there, and load and sound as before.
- [ ] Change a MENU setting, save a **project** and a **user preset**, switch the FM-1 off and on: all three kept.
- [ ] Optional: the USB serial console's `status` shows `flash 1` (saving allowed).

If saving fails: stop, and roll back (section 6). Nothing else matters until this works.

## 3. The eight machines

- [ ] EDIT held, white keys 1..8: OB-Z, WAVE, NANO, JUNO, JP-Z, CZ-Z, GW-Z, MICRO.
- [ ] (test4) **OCT+ held, PRESETS knob turned:** the machines one a detent, in that order (each at its first preset);
      let go: the octave has not moved. OCT+ tapped alone still goes up an octave (now when you let go). SAVE held
      afterwards undoes the whole hold. (EDIT held + KNOB 1 does the same, as before.)
- [ ] **CZ-Z** (key 6) plays its own sound. Try a few presets of each kind (KEYS, BASS, LEAD, ATMOS). EDIT pages
      OSC (WAV1 WAV2 LINE DTN) and DCW (CUT KEYF MOD DCWE). CUT and the machine knob (DCWE) should brighten it.
- [ ] Presets now go to **50** on every machine ("10 / 50" on the panel).

## 3b. WAVE, from the V2 plugin (test5)

WAVE's 50 presets are now converted from the VOID WAVE **V2** plugin's factory set: ATMOS 1-10, LEAD 11-22, KEYS
23-36, BASS 37-50. `docs/void_presets.md` lists each one and what it lost. The old 32-preset bank is gone, so a saved
project that used a WAVE preset by number now loads the new sound in that slot.

- [ ] EDIT held, white key 2 (WAVE): presets 1-50 all play. At least try SUPRSAW WARM (1), FORMNT CHOIR (6),
      ACID SCREECH (12), DEEP HSE KEY (23), RAVE HOOVER (34), SUB KICK (42) and REESE DNB (50).
- [ ] **PENV** (MACHINE 1, 4th knob, -24..+24): SUB KICK and the HOOVERs start with a pitch sweep. On any preset,
      turning it gives a ~50 ms blip at the start of each note and nothing after.
- [ ] **SUB** (MACHINE 2, 3rd knob): a sine under the note (SUB ROLL, REESE DEEP); turning it up on a bass adds weight.
- [ ] **NOISE page** (EDIT pages, after FLT ENV; WAVE only): NOISE and COLOR. Plucks 23-28 use it for their attack;
      turning COLOR down darkens it.
- [ ] The mod wheel opens the filter on almost every preset and brings in vibrato on the leads (GOOD VIBES, ZEN FLUTE).
- [ ] Levels: nothing far louder or quieter than its neighbours. The FORMNT pads are the quietest (they were in the
      plugin too). Note any preset that clicks, crackles or sounds broken.
- [ ] CPU: hold a note of REESE DNB (UNISON takes the part's 6 voices) plus a WAVE pad chord on another part, and read
      MENU > SYSTEM > INFO > CPU.

## 4. CPU on the device

Read **MENU > SYSTEM > INFO > CPU** for each mix, with all four parts playing. Note any click, dropout or cut-off note.

| Mix | What | Host figure |
|---|---|---|
| A | Section 0.2's mix again (JUNO DRIFT KEYS x3 on parts 1 and 4, CHOR I+II; NANO BASSLINE x1 on 2 and 3) | 3,863 (`worst8`) |
| B | All four parts CZ-Z preset 49 **SHIMMER PD**, 2 notes each (8 voices) | ~2,030 for 8 voices |
| C | Mix A, then turn DIST up to full on all four parts | (DIST is the known heavy one) |

CZ-Z is the one to watch: it does one hardware divide per line per sample. If B reads high or clicks, that is the fix
for the next build.

Optional, the USB serial console (a COM port appears when MENU > SYSTEM > USB SERIAL is ON; any serial terminal):
`status` prints `cpu_pct`, `audio_max_us` (the slowest half-buffer), `audio_late` (late buffers: clicks),
`voices_shed` (the overload fade), `load_tight_count` (the new load limit came on) and `midi_in_shed`.

## 5. MIDI with the Syntakt (TRS into the FM-1)

Syntakt track set to a MIDI track on **channel 1** (plays part 1); MENU > MIDI > MIDI IN left at CH1-4.

- [ ] Notes play part 1; channels 2..4 play parts 2..4.
- [ ] The MIDI track's CC slots set to **102..109** move the part's E1..E8 (watch the EDIT pages); 74 moves CUT.
- [ ] BANK **5** and PROG **10** on the MIDI track: at the next pattern change part 1 becomes **CZ-Z preset 11**
      (Switch Blade takes program numbers from 0, so the panel shows one more; if the Syntakt counts its PROG from 1
      you will see preset 10 instead: note which). BANK 0..7 is the machine (0 OB-Z .. 7 MICRO), 8 your user presets.
- [ ] MENU > MIDI > **PC IN OFF**: pattern changes no longer change the sound. ON again: they do.
- [ ] Hold a chord and twist several CC knobs fast for a few seconds: no stuck notes, and no sudden all-notes-off.
- [ ] MENU > SYSTEM now also holds SCREEN WAKE (it moved from MIDI to make room for PC IN).

## 6. Rolling back to 0.2-beta

```
cd D:\Switchblade\FM1-Four\build\v2rollback-site
py -3 -m http.server 8000
```

Then the installer page as in step 1. **Note:** a project saved in this build using JP-Z, CZ-Z, GW-Z or MICRO will not
open in 0.2-beta (it refuses machines it does not know rather than guess). Projects using only the four old machines
are fine, and so is the backup from step 0.

## What to send back

The CPU readings (0.2-beta mix, A, B, C), whether saving held, anything that clicked, stuck or looked wrong, and your
first impression of CZ-Z's sound.
