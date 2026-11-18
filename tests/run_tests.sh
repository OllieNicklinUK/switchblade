#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests of the Felucca sources (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             instructions / sample per preset and mix (tests/cpu_baseline.txt, +25 %), ns printed;
#                   target: loop instructions of the render functions in the pi32v2 disassembly
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note, the VOICE cap,
#                   no hanging notes on any MIDI / key routing.
# ENGINE MEMORY (tests/union_test.c, 1.5): engine switches on a part (every pair and A -> B -> A of the four machines),
#                   hashed against tests/union_golden.txt.
# UI renders (tests/ui_render.c): every screen in every palette from the real drawing code: the layout lint (no text
#                   off the screen, cut, hidden, overlapping or spilling out of its cell / card; only free text
#                   ellipsised), GREY gray, MONO neutral, the draw cost, the text audit (build/ui_new/text_audit.tsv);
#                   PNGs of GREY MONO GREEN PAPER NIGHT in build/ui_new (tests/ui_render.py), the findings in build/ui_new/report.txt;
#                   every frame of the rolling digits (lint, GREY, MONO), their filmstrips in build/ui_slot;
#                   the alignment: every text / icon / keycap meant to be centred, or on its neighbours' line, by its
#                   ink against its box (cells, chips, buttons, rows, knobs, the roll's strip, the keycaps' pills), every
#                   screen in FLAT and LINE and every palette, over every value it can show; 1 px off or more fails
#                   (build/ui_new/align.txt). MENU > LARGE: every screen again (FLAT, LINE, every palette) and the page / value
#                   sweep with the tall cards, the same lint and alignment; sheet_LARGE_GREY.png, sheet_LARGE_MONO.png.
# UI (tests/ui_test.c): the UI sources against stub display / buttons / knobs: sound loads keep the steps and
#                   the track's ARP / SCL / SLICER, the SAVE > PHRASES loader and its REPLACE? dialog, the
#                   one-step undo of both (SAVE held), REC on TRACKS / SEQ / ARP, STEP and ARP while recording,
#                   MIDI IN ROUT, saves refused while playing and the OVERWRITE? dialog, MUTE on TRACKS KNOB 1.
# Audio / persistence / editor: the real C paths against simulated DMA and NOR flash: bounded overload
#                   fades, shared-voice limits, deferred settings and retries, failed-save rollback, the autosave
#                   (1.2: when it writes, the power-on restore, damaged copies, a write cut short, wear over a session),
#                   malformed transfers, transport-stop timeouts, MIDI and UART recovery; the MENU settings over the
#                   editor (MENU_DESC / MENU_SET: every item, clamping, unknown ids, saving, USB SERIAL applied later).
# CHORD (tests/chord_test.c): the chord keys (src/chord.c): diatonic triads / sevenths of several scales and roots,
#                   the fixed shapes and voicings (at most 4 notes), names, MONO plays the root, a release ends
#                   exactly what its key / MIDI note started, recording, the ARP, MIDI IN.
# FUN10 (tests/fun10_test.c, 1.2): LFO 2 (SYNC one cycle per note value at several tempi and on the external clock, OFF bit
#                   for bit as before; TRIG NOTE / FREE; POL UNI 0..+1, the matrix's AMP), NUDGE (live recording keeps a
#                   note's time, QUANTIZE ON plays as before, OFF plays the nudges late and early once a pass, never twice
#                   with live notes, RATCH on its start, nudges 0 play the same either way, a tempo change of the external
#                   clock, SEQ > AUTOMATION's QUANTIZE and NUDGE rows, STEP steps held + PRESETS), 128 motion records, FUN10 round trip, FUN9 /
#                   user presets of 99 / 103 (Felucca FUN9 / FUN8 / FUN7 are refused in FOUR: project_test.c).
# RATCH (tests/ratchet_test.c): a step's ratchet (x1..x4): its parts in the sequencer (equal, gated, chords
#                   whole, one chance roll, swing, no slide or tie out, STOP), FUN8 round trip and older projects x1,
#                   user preset patterns, SEQ > AUTOMATION's RATCH rows and the roll drawing.
# STEP 1.5 (tests/step15_test.c, #178 #173): SEQ > STEP's KNOB 3 LEN on a note (clean TIEs written after it and taken
#                   back, stopping at the next note and at LEN, chords, undo, TIME elsewhere,
#                   the longer note played), a step's VEL as it plays (96 by default, ACC 127) and
#                   SEQ > AUTOMATION's VEL rows (SHOW / HIDE, KNOB 4, EDIT, undo).
# DETAIL 1.5.1 (tests/detail_test.c, #199): SEQ > DETAIL (STEP -> DETAIL -> AUTOMATION), KNOB 1..4 the cursor step's
#                   CHANCE RATCH NUDGE VEL walked with PRESETS (NO NOTE on a REST / empty step), the same values as
#                   AUTOMATION's rows (gone at their default; EDIT there deletes one), EDIT resets, undo per gesture, a chord's ratchet, playback, STEP's marks.
# SEQ 1.5.5 (tests/seq155_test.c, #209 #186 #85): CONDITIONS (1:2 .. 4:4 by each track's loops from PLAY / a SONG
#                   section, FILL / !FILL with GLO + E4, a NUDGE early step 1, no lock on a failed one, the chance draws
#                   as before), DETAIL's KNOB 1 past 100 % and AUTOMATION's COND rows, FUN10 round trip and what 1.5 /
#                   1.5.1 do with them; SEQ TOOLS' EUCLID (hits / steps / rotation, notes, OCT+ / OCT-,
#                   undo) and DOUBLE / HALF (ties, locks, the steps' own fields, 64 steps, undo).
# 1.5.5 (tests/ui155_test.c): PAN reads -100..+100 % (#199, the stored range as before); SEQ > AUTOMATION: KNOB 3 keeps
#                   its row in place (a step's records in the order they were added, #199); MENU > CONTROL > SELECT (#188):
#                   BPM by default, PAGE steps the family's pages (round, HOME MIXER CLOCK one round), GLO + SELECT the tempo;
#                   OCT- (1.5.6: #205's "OCT- held + a knob = default" taken out): the octave on the press as in 1.5.1, a
#                   knob with OCT- held an ordinary turn, the menu's OCT- back; SAVE > PROJECT ERASE (#199): KNOB 2, the dialog,
#                   refused playing / empty (tests/persistence_test.c: the flash side, a cut before the record keeps it);
#                   CLEAN INIT (#190): every engine's basic sound (no sends, INSERT, LFO / ENV / MOD amounts; neutral ENV),
#                   heard and ending; TOOLS' KNOB 2 on, EDIT + F5; one undo; INIT as before. OCT- next to
#                   EUCLID (OCT- cancels, OCT+ applies, no octave) and FILL (GLO + E4).
# 1.5.6 (tests/v156_test.c): HUMANIZE (#213): the matrix's VEL (a note-on's velocity, RAND per note, clamped 1..127) and
#                   TIME (a sequencer step late, 0..30 ms, at most 7/16 of the step, with NUDGE, a ratchet on time),
#                   appended after DEPTH; nothing changes without such a slot (RAND draws, mod_begin, timing); saved.
#                   LEDS (#212): INV LO / INV HI after INV (INV's lit LEDs at the mid level), saved as INV + a level byte.
#                   SELECT (#84): MODW (the track's mod wheel, CC1 out) and VEL (the keys' velocity) after BPM / PAGE.
# 1.6 (tests/v16_test.c): SEQ's piano roll follows the play position (#242, Discussion #238): a melodic pattern over 16 steps,
#                   the transport playing: the roll's bank is the playhead's (STEP and DETAIL), the cursor and the pattern
#                   untouched; not for 16 steps, stopped, AUTOMATION, HOME; held from a knob's move of the
#                   cursor, an edit of its step or step entry, free again after 3 s or one pass round; reset by stop / track.
# MOD (tests/mod_test.c): the modulation matrix: slots that do nothing are bit-identical, every source on each
#                   kind of destination, clamping, MIDI CC1 / CC11 / aftertouch routing, the cost of 4 active
#                   slots (at most +6 %, mod_test.c MOD_COST_MAX), demos in build/mod_demo/.
# INSERT (tests/insert_test.c, 1.5; #78 #177): the per-track INSERT (src/fx.c track_insert): TYPE OFF and MIX 0 bit for
#                   bit (POLY, SPREAD), each TYPE at its corners on 3 and 8 voices (no DC, bounded, MIX 50 % halfway
#                   to the wet), the MIX glide and TYPE changes, MIX / TYPE as automation and locks, FUN10 and user
#                   presets of 109 and 104, the pages' labels, the cost per type; demos in build/insert_demo/.
# DIST (tests/dist_test.c; src/fx.c track_dist, 2026-10-21 the integer drive of fm1_drive_1.h, which replaced FX.1 / FX.2's Scream-style loop): P_DIST 0 a
#                   bit-identical bypass, the switch-on without a click, the output bounded (<= 32767) on 8 FS noise / squares / sweeps and through the real part path
#                   (OVERDRIVE ACID, JUNO FAT SAW BASS x3, OB-Z x8), silence in -> exactly silence out within 50 ms and the state exactly 0 after a silence, a P_DIST sweep
#                   (every block) without a step at the block edges, the level within +-3 dB of the old DIST on a saw chord, the alias floor (a 2 kHz sine printed, a saw
#                   at 0.7 FS pinned to the drive's own: it has no anti-aliasing), the harmonic density rising with the knob, the cost; --wav writes the listening renders.
# SOUND 1.5 (tests/sound15_test.c; #104 #175): ANALOG's filter TYPE (LP bit for bit, every other engine ignores it, BP / HP
#                   responses around the cutoff, RES, bounded at the corners), ENV SYNC (OFF bit for bit, the note values
#                   on the knob, attack / decay / release times at 60 / 120 / 173 BPM and on the external clock), the cost;
#                   demos in build/sound15_demo/ (analog_bp, analog_hp, env_sync).
# FOUR (tests/four_test.c): the four-machine contract: ENGINES[] is OB-Z WAVE ACID DRIFT, their HOME knobs, ACID mono.
# OB-Z (tests/obx_test.c, tests/dspfft.h): the SEM-style filter morph on one voice (spectra through a plain FFT):
#                   MODE 0 is the low-pass of before the morph bit for bit, MODE 32 notches at the cutoff, 64 passes
#                   the highs, 96 is the band-pass, 120 the low-pass again and 127 the 4-pole; the crossfade between
#                   every pair of corners (16 / 48 / 80 / 108: each leg moves its band one way, none is inert), the
#                   corners meet without a step of their own (MODE 62 -> 63 against 63 -> 64), the crossfade and the
#                   4-pole bounded and adding no DC at RES 127; SLOP 0 is inert over different note-ons and SLOP 127
#                   spreads the voices within +-6 cents; DTN moves the second oscillator by its cents; dspfft.h's two
#                   refusals (an n it cannot transform, a band below 0 Hz).
# ACID (tests/acid_test.c, tests/acid_ref/, tests/acid_meas.h): the 303 voice (bass303.c, eng_acid.c) through the real voice path against
#                   the donor's own vectors (tests/acid_ref.sh regenerates them from fm1-x0x's bass303.c): the filter's impulse response on
#                   a 5 x 5 CUT / RES grid within 1 dB, the RES 127 resonance within 3 % of the donor's and tracking CUT, an accented step >= 3 dB
#                   louder and brighter, a 48 -> 60 slide (LEGATO and MONO: no retrigger, pitch rising across 50-70 ms, no dip), DECY 0 vs 127,
#                   the edit slots the vectors do not reach (E0 WAVE saw / square harmonics, E1 TUNE the measured pitch, E6 DRV level and
#                   harmonics, E7 SQL's RES / accent decay on an accented step only), the cost per voice (<= 3 x OB-Z, <= 1080 instructions /
#                   sample: macOS only, a loud SKIP elsewhere), amp() in [0, 32767], done() ends released and held notes.
# DRIFT (tests/drift_test.c, tests/drift_ref.cpp, tests/drift_ref_cfg.h): the Juno voice (eng_drift.c, dsp_va.c; the Ultramaster KR-106, GPL-3.0). Tier 1: the
#                   lifted ladder against the donor's own kr106::VCF on 40 cases (a 5 x 5 cutoff x resonance grid at OS 2, a spread at OS 1 (the shipped one) and OS 4, loud inputs),
#                   within 1e-4 of the peak; the half-band pair (unity gain, image rejection). Tier 2, through the real voice path: 24 dB/oct, the THD
#                   of DRV 0 / 127, RES 127 self-oscillating within 3 % of the cutoff, DRFT (0: two renders identical; 127: within +-8 cents), VAR (0: one
#                   cutoff; 127: spread), the MIX spectrum (sub / saw / pulse), P_FTYPE's HPF corners (and values above 3 = flat), a bounded sweep of the
#                   corners, the donor's DCO (inharmonic energy < -50 dB, the spectrum and the waveform against the ideal, PW, the auto PWM), KTR, the two DC
#                   blockers, the voice start (reset when finished, kept on a retrigger), and the cost: the DRIFT part inside its pin, the dearest legal
#                   four-track mix measured and printed (macOS only, a loud SKIP elsewhere).
# CHORUS (tests/chorus_test.c, tests/chorus_ref.cpp, tests/chorus_ref_cfg.h; Task 6.2): DRIFT's Juno chorus (dsp_chorus.c, the per-part stereo hook of fx.c; the
#                   Ultramaster KR-106's KR106Chorus.h + BBDFilter.h, GPL-3.0). Per mode I / II / I+II: the LFO rate (+-5 %) and the delay sweep (+-10 %) against the
#                   donor's and its constants, L / R in antiphase, the 1 kHz sidebands against the donor's, 15 renders against the donor's own kr106::Chorus (noise
#                   off), block-size independence, the fades; the int part block (mid / side, the guard clamp, an out-of-range mode is OFF); through the mix: CHOR OFF
#                   bit for bit as before, other engines ignore it, stereo, PAN, SPREAD, the worst case inside the mixer's integer limits; the parameter and its
#                   legacy value; the cost per part (macOS only, a loud SKIP elsewhere).
# WAVE (tests/void_tables_test.py, tests/void_test.c): the VOID WAVE plugin's voice (the WAVE engine, was VOID). Its wavetables against the plugin's
#                   frames recomputed with numpy (every keyframe and level band-limited, the crossfade within its limit;
#                   skipped without numpy); the engine: pitch, one oscillator (MIX / OSC2 hidden and inert), the clipper's clamp, the seven filter types bit for bit as before Task 4.5 (also with the slow loop forced), VOWEL (F1 / F2, level against LP24, bounded, cost), WARP (off bit for bit, aliasing, the old TBL2 values load as off), OB-Z / ACID / DRIFT playing VOWEL, the instrumented build (TYPE switched every block: no table read out of range; no int32 overflow at the corners), band-limited at C8, bounded and without DC at
#                   the corners (every table x TYPE, RES and DRV 127), the presets' levels, the cost at its voice cap
#                   against OB-Z RAVE; its filter against the plugin's own Filter.cpp (tools/void_ref.cpp, built from
#                   ../VoidWave or VOIDWAVE_DIR; skipped without it); demos in build/void_demo/.
# AUDITION (tests/audition.c, tools/audition_index.py, 7.1): build/host/audition build/audition renders every factory preset through the mix path to
#                   a 6 s WAV + summary.json and reports the ones outside the loudness / DC limits (exit 1; NOT part of this suite);
#                   tools/audition_index.py build/audition writes the rating page. Here: --selftest, the rules against synthetic signals.
# PERFORM (tests/perform_test.c): the FX hold layer (src/perform.c): 1/16 starts, stereo buffer effects, the
#                   too-long REPEAT, the SLICER interplay, silent layer keys, idle bit-identical, cost; build/perform_demo/;
#                   OCT UP / DN (the harmonizer): pitch, stereo, clicks, the shimmer bounded, cost; FLANGER / PHASER (1.2): no DC,
#                   bounded, the sweep in time with the tempo, clean release, stacked with REPEAT / LPF / HPF, cost; the key
#                   map (any effect or none on any key, a key remapped while held, FX LATCH); build/fx_demo/; 1.5.1: the
#                   delay's dotted TIME (lengths, the 1/4D clamp, the knob order), REV / ECHO THROW (the sends as at 127,
#                   the glide, ECHO's capped feedback bounded, the tails ringing out, assignable); build/throw_demo/.
# REVERB (tests/reverb_test.c): REVERB TYPE (src/fx.c): ROOM bit for bit as before, SPRING's decay against SIZE,
#                   its chirp (group delay rising with frequency), stability at the corners, level, a model change
#                   without a click, its cost against ROOM (+30 % at most); demos in build/fx_demo/. 1.2's HALL: SPRING
#                   bit for bit as before, decay against SIZE and DAMP, stability, exact silence after the tail (27
#                   settings and levels), no offset, level, stereo, echo density / ringing / flutter against ROOM, SPRING
#                   and COMB8 (8 combs + 4 allpasses, host only, for comparison), model changes both ways, cost; the
#                   listening set in build/reverb_demo/ (drums, pluck, piano: dry and each model at SIZE 40 / 90 / 127).
# INPUT (tests/input_test.c): the key / button debounce of hal/fm1_input.h against the TIMER5 scan and bouncing
#                   contacts: a press within 2 scans (<= 2.3 ms), one note per bouncy press, no early or hanging
#                   release, stray samples ignored, fast repeats, the encoders' detents; the LED scan: lit LEDs every
#                   frame, dim ones a short pulse (the second line write) every frame, each only on its own column;
#                   the breath (#119): dark .. ~60 % of lit (DIM LO ~30 %), smooth, no dark run over ~10 ms near its peak;
#                   the power-on sweep (1.1, hal/fm1_led_anim.h): its length, every LED every frame at its level, the
#                   head left to right with its tail, the buttons, the end on the idle glow, the tick as before after;
#                   the idle animation (1.2, MENU > ANIM IDLE): endless, every LED every frame, the light there and back,
#                   the buttons breathing, a key down ends it before its next frame.
# CLICK (tests/click_test.c, 1.1): the metronome (src/click.c) and the count-in (src/seq.c) through audio.c: each beat at
#                   the first sample of its block, with a 1/16 track's steps 1 5 9 13 (internal and external clock, DIV 1/8
#                   and SWING too), the accent, the frequencies, length, LEVEL, MASTER, OFF / REC / ON, USB audio bit for bit
#                   without it; COUNT-IN 1 / 2 BARS (step 1 exactly N bars after PLAY, STOP cancels, PLAY again nothing,
#                   never with an external clock), notes in its last eighth onto step 1; build/click_demo/.
# USB audio (tests/uac_test.c): the UAC1 descriptors as a host parses them (with and without CDC; 44100 and
#                   48000 Hz), the ring and packetiser: 44.1 frames per packet, every frame in order (bit for bit),
#                   underrun / overrun, restart; SET_CUR; the 44.1 -> 48 kHz resampler (every frame against a
#                   double-precision one, response, SNR, cost); the stream at 48 kHz (47..49 frames per packet,
#                   fast and slow I2S clocks, switching rates mid-stream).
# ROBUST (tests/robust_test.c): damaged or crafted stored data and editor requests: engine numbers past the last refused, a retained older
#                   RAM project bounded, user preset patterns inside their fields, malformed requests never stop PLAY.
# Sanitizers (ASan + UBSan, when the compiler has them; SANITIZE=0 skips): the stored-data and protocol tests again
#                   (loader, M-UPGRADE entry, editor, projects, backup, ROBUST) and two short fuzz runs with fixed
#                   seeds: tests/fuzz_ed.c (editor SysEx and raw USB-MIDI packets), tests/fuzz_proj.c (mutated project
#                   stores -> import -> restore -> render). Longer runs: build/host/asan/fuzz_ed 300000 7 (iterations, seed), the same for the others.
#                   UBSan leaves out the DSP's intended wraps (signed overflow, shifts) and the XIP rebase of a user zone's
#                   offset (bounds, object-size, pointer-overflow: correct on the device's flat flash, not in C's model).
# Change baseline entries only for reviewed, intentional differences in sound or cost;
# retain every unaffected golden / CPU / target entry. VERBOSE=1: every render.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function -ffp-contract=off"   # (-ffp-contract=off: ACID and DRIFT are float; the device has no fused multiply-add, so a golden must not depend on the host having one)
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
$CC -o "$OUT/flash_id_test" tests/flash_id_test.c
run "flash identification: both documented FM-1 parts, two agreeing reads, 1 MiB, protection -> RAM-only" "$OUT/flash_id_test"

$CC -o "$OUT/upreset_test" tests/upreset_test.c
run "user presets (UP_PUT parser, bank round trip, versions, retired grid records and engines read as empty)" "$OUT/upreset_test"

$CC -o "$OUT/input_test" tests/input_test.c
run "keys and buttons: fast press, long release, bouncy contacts (one note each), glitches, encoders" "$OUT/input_test"

$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"

HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/core.h)
$CC -O2 -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/uac_test.c -lm
run "USB audio input: descriptors (with CDC), ring and packets, 44.1 -> 48 kHz resampler, 48 kHz stream" "$OUT/uac_test"
$CC -O2 -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/uac_test.c -lm
run "USB audio input: descriptors (without CDC), ring and packets, resampler, 48 kHz stream" "$OUT/uac_test_nocdc"
# USB descriptor layouts (#67): CDC UAC LAYOUT CDC-presented 48K; layout 0 and the console left out = 1.0's bytes
# (+ the 48 kHz rate with 48K = 1, the default; 48K = 0: byte for byte; + the AC collection's audio streaming
# interface first, FELUCCA_UAC_AS_FIRST = 1, the default since 1.6)
for v in 1.1.0.1.1 1.1.1.1.1 1.1.2.1.1 1.1.3.1.1 1.1.0.0.1 1.1.2.0.1 0.1.0.1.1 1.1.0.1.0 1.1.0.0.0 1.1.2.1.0 0.1.0.1.0 \
         1.0.0.1.1 1.0.1.1.1 1.0.0.0.1 0.0.0.1.1; do
    IFS=. read -r t_cdc t_uac t_lay t_on t_48 <<EOF
$v
EOF
    $CC -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -DT_48K="$t_48" -o "$OUT/usb_desc_test" \
        tests/usb_desc_test.c
    run "USB descriptors: CDC $t_cdc (presented $t_on), UAC $t_uac (48 kHz $t_48), layout $t_lay" "$OUT/usb_desc_test"
done
# FELUCCA_UAC_AS_FIRST=0 (#67): the AC collection of 1.0 .. 1.5 (MIDI first), 1.0's bytes; the parser model of the
# kernel AppleUSBAudio (macOS <= 15) stops before the audio streaming interface there
for v in 1.1.0.1.1 1.1.0.0.1 1.1.0.1.0 1.1.0.0.0 0.1.0.1.1; do
    IFS=. read -r t_cdc t_uac t_lay t_on t_48 <<EOF
$v
EOF
    $CC -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -DT_48K="$t_48" -DT_ASF=0 \
        -o "$OUT/usb_desc_test" tests/usb_desc_test.c
    run "USB descriptors (AC collection MIDI first): CDC $t_cdc (presented $t_on), UAC $t_uac (48 kHz $t_48)" \
        "$OUT/usb_desc_test"
done

[ -f build/felucca.fwsc ] || { echo "run ./build.sh first"; exit 1; }

$CC -DOWN_PKG=1 -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (own loader)" "$OUT/ota_test" build/felucca.fwsc

head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc

if [ -f build/gen/felucca_tables.h ]; then
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hostsim" tests/hostsim.c -lm
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/audition" tests/audition.c -lm
    run "AUDITION checks: peak / RMS / DC / silence rules against synthetic signals (the renderer itself, build/host/audition build/audition, is not a gate)" "$OUT/audition" --selftest
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/scale_test" tests/scale_test.c -lm
    run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/chord_test" tests/chord_test.c -lm
    run "chord keys: diatonic and fixed chords, voicings, MONO root, releases, recording, ARP, MIDI IN" "$OUT/chord_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/speaker_test" tests/speaker_test.c -lm
    run "SPEAKER EQ: FLAT / LOWCUT / BASS+ responses, BASS+ harmonics of the bass, the sub cut, no offset after, headroom on a full-scale kick (#180)" "$OUT/speaker_test"
    run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
    mkdir -p build/tracks_demo
    run "TRACKS: 4-track pattern, live recording (lengths, swing), voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/project_test" tests/project_test.c -lm
    run "project format (FOR4 round trip; every Felucca format refused; engines past the four refused)" "$OUT/project_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/motion_test" tests/motion_test.c -lm
    run "motion, whole-step chance, FUN7 migration, song restore, ARP repeat and the 1.2 ARP modes" "$OUT/motion_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/ratchet_test" tests/ratchet_test.c -lm
    run "RATCH: x1..x4 in a step (notes, chords), gates, chance, swing, projects, user presets, AUTOMATION RATCH rows" "$OUT/ratchet_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/step15_test" tests/step15_test.c -lm
    run "1.5 STEP: a note's LEN (TIEs on KNOB 3), a step's VEL (playback, AUTOMATION's VEL rows)" "$OUT/step15_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/detail_test" tests/detail_test.c -lm
    run "1.5.1 SEQ > DETAIL: a step's CHANCE RATCH NUDGE VEL on KNOB 1..4 (walk, entries, undo, chords, playback, marks)" "$OUT/detail_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/seq155_test" tests/seq155_test.c -lm
    run "1.5.5 SEQ: conditions and FILL, SONG, storage, editor; SEQ TOOLS EUCLID, DOUBLE, HALF" "$OUT/seq155_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/ui155_test" tests/ui155_test.c -lm
    run "1.5.5 fixes and UI: PAN +-100, AUTOMATION KNOB 3, SELECT PAGE, OCT- on the press (1.5.6), PROJECT ERASE, CLEAN INIT" "$OUT/ui155_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/v156_test" tests/v156_test.c -lm
    run "1.5.6: the matrix's VEL / TIME (#213), LEDS INV LO / HI (#212), SELECT MODW / VEL (#84)" "$OUT/v156_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/v16_test" tests/v16_test.c -lm
    run "1.6: SEQ's piano roll follows the play position (#242: STEP, DETAIL; held by the knobs, 3 s or one pass)" "$OUT/v16_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fun10_test" tests/fun10_test.c -lm
    run "1.2 (FUN10): LFO 2 SYNC / TRIG / POL, NUDGE (record, QUANTIZE, play, controls), 128 motion records, FUN10 / FUN9 / FUN8 / FUN7, user presets" "$OUT/fun10_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/midi_control_test" tests/midi_control_test.c -lm
    run "USB/TRS clock, bend, sustain, ownership and panic recovery" "$OUT/midi_control_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/theme_test" tests/theme_test.c -lm
    run "themes: contrast, text blending and font metrics" "$OUT/theme_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/text_ref_test" tests/text_ref_test.c -lm
    run "text: pens, kerning and pixels equal the reference renderer" "$OUT/text_ref_test"
    if python3 -c "import PIL" 2>/dev/null; then
        run "text spacing: glyph gaps against the font's own (16x supersampled), S 0.33, M 0.30, L 0.5, XL 1.1 px (Exo 2)" \
            python3 tests/text_spacing_test.py build/gen/ui_fonts.h
    fi
    $CC -O1 -w -Ibuild/gen -o "$OUT/settings_test" tests/settings_test.c
    run "settings: FOR4 object, Felucca PER1..PER4 refused, palette ids and preference preservation" "$OUT/settings_test"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test" tests/ui_test.c -lm
    run "UI: sounds keep steps, undo, recording, MIDI overflow, pending saves, panel recovery, song chain, GREY gray, MONO neutral" "$OUT/ui_test"
    FV=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)   # (the splash, ABOUT: the real version)
    $CC -O1 -w "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/ui_render" tests/ui_render.c -lm
    mkdir -p build/ui_new/ppm build/ui_slot
    run "UI renders: layout lint (every screen on each machine's palette, every page, engine and column value), alignment by ink (1 px fails), draw cost" \
        "$OUT/ui_render" build/ui_new build/ui_slot
    if python3 -c "import PIL" 2>/dev/null; then python3 tests/ui_render.py build/ui_new build/ui_slot; fi
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/audio_test" tests/audio_test.c -lm
    run "audio: overload protection, bounded fades and DMA diagnostics" "$OUT/audio_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/preset_audit_test" tests/preset_audit_test.c -lm
    run "preset audit (V2): every factory preset loads in range, sounds, is named once; levels and duplicates reported" "$OUT/preset_audit_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/click_test" tests/click_test.c -lm
    run "metronome and count-in: beats sample for sample (internal and external clock), accent, level, MASTER, not in USB, count-in timing, notes onto step 1" "$OUT/click_test"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/persistence_test" tests/persistence_test.c -lm
    run "persistence: deferred settings, retry and failed-save rollback" "$OUT/persistence_test"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/backup_test" tests/backup_test.c -lm
    run "full backup: CRC before writes, stale runtime, USB reset / timeout, malformed objects, older projects" "$OUT/backup_test"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/editor_test" tests/editor_test.c -lm
    run "editor: real C protocol, malformed transfers, queue recovery, MENU settings (writes build/host/menu.json)" \
        env MENU_JSON="$OUT/menu.json" "$OUT/editor_test"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/robust_test" tests/robust_test.c -lm
    run "robustness: engine numbers, retained old projects, preset patterns, malformed requests" \
        "$OUT/robust_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/mod_test" tests/mod_test.c -lm
    mkdir -p build/mod_demo
    run "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, cost, demos" "$OUT/mod_test" build/mod_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/insert_test" tests/insert_test.c -lm
    mkdir -p build/insert_demo
    run "INSERT (1.5): OFF / MIX 0 bit for bit, each TYPE (no DC, bounded, dry / wet), MIX glide, automation, stored, cost, demos" \
        "$OUT/insert_test" build/insert_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/sub_test" tests/sub_test.c -lm
    run "NANO SUB (2026-10-21): the models by preset, the pitch, the polyphony cap, every TYPE at full resonance (bounded, voices free, exact silence), a narrow pulse without DC, the alias floor, the cost" \
        "$OUT/sub_test"
    $CC -O2 -w -ffp-contract=off -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/dist_test" tests/dist_test.c -lm
    run "DIST (the drive): P_DIST 0 bypass bit for bit, no click on switch-on, bounded, silence in silence out, the state settles, no zipper on a sweep, level against the old DIST, alias floor (pinned), intensity rising, cost" \
        "$OUT/dist_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/sound15_test" tests/sound15_test.c -lm
    mkdir -p build/sound15_demo
    run "1.5 sound items: ANALOG filter TYPE LP / BP / HP (responses, LP and other engines bit for bit), ENV SYNC (times at several tempi), cost, demos" \
        "$OUT/sound15_test" build/sound15_demo
    if python3 -c "import numpy" 2>/dev/null; then
        run "VOID wavetables: the plugin's frames, band-limited per octave, keyframes within their limits" \
            python3 tests/void_tables_test.py build/gen/void_tables.h
    else
        echo "== VOID wavetables: skipped (no numpy)"
    fi
    VW="${VOIDWAVE_DIR:-../VoidWave}"
    mkdir -p build/void_ref build/void_demo
    if [ -f "$VW/Source/DSP/Filter.cpp" ] && ${CXX:-c++} -std=c++17 -O2 -Itools/void_ref -I"$VW/Source/DSP" \
            -o "$OUT/void_ref" tools/void_ref.cpp "$VW/Source/DSP/Filter.cpp" 2>/dev/null; then
        "$OUT/void_ref" build/void_ref >/dev/null
    else
        rm -f build/void_ref/*.f32
    fi
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/void_test" tests/void_test.c -lm
    run "VOID: pitch, one oscillator, band-limited, bounded at the corners, preset levels, cost at its cap, the filter against the plugin's" \
        "$OUT/void_test" build/void_demo build/void_ref
    # the instrumented build (Task 4.5): every signal-indexed table read of eng_wave.c counted (TYPE switched every block over all type pairs), and
    # under -fsanitize=signed-integer-overflow the corner loops (VOWEL / LP24 at RES 127 + DRV 127, MIDI 24..99, WARP): no int32 wraps; no cost is measured
    $CC -O1 -w -DVOID_INSTR -fsanitize=signed-integer-overflow -fno-sanitize-recover=signed-integer-overflow -Ibuild/gen -Ifirmware/src -Itests \
        -o "$OUT/void_instr" tests/void_test.c -lm
    run "VOID instrumented: no table read out of range on a TYPE switch, no int32 overflow at the corners (VOWEL, LP24, WARP)" "$OUT/void_instr"
    $CC -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/four_test" tests/four_test.c -lm && "$OUT/four_test" || fail=1
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/obx_test" tests/obx_test.c -lm
    run "OB-Z: MODE 0 the legacy low-pass bit for bit, the LP / NOTCH / HP / BP morph, the 4-pole, SLOP, DTN" "$OUT/obx_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/acid_test" tests/acid_test.c -lm
    run "ACID: the 303 voice against the donor's vectors (filter grid, resonance, accent, slide, decay), the cost, amp() / done()" \
        "$OUT/acid_test" tests/acid_ref
    # DRIFT: the Juno voice (eng_drift.c, dsp_va.c), after the Ultramaster KR-106. Tier 1 holds the lifted filter to the donor's own
    # kr106::VCF (tests/drift_ref.cpp, built from ../ultramaster_kr106-main or KR106_DIR; without it (a) says NOT CHECKED), tier 2 the voice.
    KR=${KR106_DIR:-../ultramaster_kr106-main}
    mkdir -p build/drift_ref
    rm -f build/drift_ref/*.f32
    if [ -f "$KR/Source/DSP/KR106VCF_OPTIMIZED.h" ]; then
        # the donor is here, so (a) must run: a build failure fails the suite (it used to turn (a) into a silent "NOT CHECKED")
        if ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off -w -I"$KR/Source/DSP" -Itests -o "$OUT/drift_ref" tests/drift_ref.cpp; then
            "$OUT/drift_ref" build/drift_ref >/dev/null || fail=1
        else
            echo "DRIFT: tests/drift_ref.cpp does not build against the donor in $KR: FAIL"
            fail=1
        fi
    fi
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/drift_test" tests/drift_test.c -lm
    run "DRIFT: the filter against the donor's kr106::VCF (40 cases), the halfband, 24 dB/oct, DRV's THD, self-oscillation, DRFT, VAR, the MIX spectrum, the HPF (and its flat fallback), the corners, the donor's DCO (bandlimiting, spectrum, waveform, PW, PWM), KTR, the DC blockers, the voice start, the cost pin and the dearest legal mix" \
        "$OUT/drift_test" build/drift_ref
    # CHORUS (2026-10-21): the Juno-60 chorus of fm1_juno_chorus.h (dsp_chorus.c) against its own figures, the hook and the mix (the KR-106 donor comparison is gone with the KR-106 port)
    $CC -O2 -w -ffp-contract=off -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/chorus_test" tests/chorus_test.c -lm
    run "CHORUS: the Juno-60 chorus against its figures (rate, sweep, antiphase, sidebands), the fades, the part block and its guard, OFF bit for bit, stereo / PAN / SPREAD, the headroom, the cost pin" \
        "$OUT/chorus_test" build/chorus_ref
    rm -f build/chorus_ref/*.f32
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/slicer_test" tests/slicer_test.c -lm
    mkdir -p build/slicer_demo
    run "SLICER: no clicks, timing, sync with the sequencer, STUT, cost, demos" "$OUT/slicer_test" build/slicer_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/swing_test" tests/swing_test.c -lm
    run "SWING: track + global at most 100, sequencer and SLICER step lengths, the SWG display" "$OUT/swing_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/perform_test" tests/perform_test.c -lm
    mkdir -p build/perform_demo build/fx_demo build/throw_demo
    run "FX layer effects: on the 1/16, stereo, too-long REPEAT, SLICER, silent keys, idle bit-identical, clicks, OCT UP / DN, FLANGER, PHASER, the key map, dotted TIME, REV / ECHO THROW, cost, demos" "$OUT/perform_test" build/perform_demo build/fx_demo build/throw_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/reverb_test" tests/reverb_test.c -lm
    mkdir -p build/reverb_demo
    run "REVERB TYPE: ROOM / SPRING bit-identical, SPRING and HALL decay / stability / level, HALL silence / stereo / ringing, model changes, cost, demos" "$OUT/reverb_test" build/fx_demo build/reverb_demo
    # CZ-Z (V2 Phase 1): the uPD933 core (firmware/src/cz_core.h) against MAME's upd933.cpp (tests/cz_ref/, unmodified), sample for sample
    ${CXX:-c++} -std=c++20 -O2 -w -Ibuild/gen -Ifirmware/src -Itests/cz_ref -o "$OUT/cz_ref" tests/cz_ref.cpp
    run "CZ-Z: the uPD933 core bit-exact against MAME's upd933 (every waveform, window, DCW sweep, level, the ring)" "$OUT/cz_ref"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/cz_test" tests/cz_test.c -lm
    run "CZ-Z: LINE modes, CUT / DCWE / KEYF, the 8-step DCW envelope, RING, NOISE, the preset's slots, every preset sounds" "$OUT/cz_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
    run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/union_test" tests/union_test.c -lm
    run "engine memory (1.5): every switch between the four machines sounds as recorded (168 sequences)" \
        "$OUT/union_test" tests/union_golden.txt
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/descdump" tests/descdump.c -lm
    echo "== parameter and engine tables as JSON (for the editor mock test)"
    "$OUT/descdump" > "$OUT/desc.json" || fail=1
else
    echo "== skip hostsim (run ./build.sh once)"
fi

# ASan + UBSan: the stored-data and protocol paths, and short fuzz runs (fixed seeds: the same inputs every run)
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize=signed-integer-overflow,shift,bounds,object-size,pointer-overflow"
SAN="$SAN -fno-sanitize-recover=undefined -w"
san_ok() {
    printf 'int main(void){return 0;}\n' > "$OUT/san_probe.c" &&
        ${CC%% *} $SAN -o "$OUT/san_probe" "$OUT/san_probe.c" 2>/dev/null && "$OUT/san_probe"
}
if [ "${SANITIZE:-1}" = 0 ]; then
    echo "== skip the sanitizer runs (SANITIZE=0)"
elif [ ! -f build/gen/felucca_tables.h ]; then
    echo "== skip the sanitizer runs (run ./build.sh once)"
elif ! san_ok; then
    echo "== skip the sanitizer runs (the compiler has no ASan / UBSan)"
else
    mkdir -p "$OUT/asan"
    A="$OUT/asan"
    SCC="${CC%% *} $SAN -Ibuild/gen -Ifirmware/src"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    $SCC -o "$A/ldr_test" tests/ldr_test.c
    run "ASan/UBSan: update loader (other app -> this build)" "$A/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc
    $SCC -DOWN_PKG=1 -o "$A/ota_test" tests/ota_test.c
    run "ASan/UBSan: M-UPGRADE entry (own loader)" "$A/ota_test" build/felucca.fwsc
    for t in editor_test project_test backup_test robust_test fun10_test; do
        $SCC -o "$A/$t" tests/$t.c -lm
        run "ASan/UBSan: $t" "$A/$t"
    done
    $SCC -o "$A/fuzz_ed" tests/fuzz_ed.c -lm
    run "ASan/UBSan fuzz: editor SysEx and raw USB-MIDI packets (20000, seed 7)" "$A/fuzz_ed" 20000 7
    $SCC -o "$A/fuzz_proj" tests/fuzz_proj.c -lm
    run "ASan/UBSan fuzz: project stores -> import -> restore -> render (5000, seed 13)" "$A/fuzz_proj" 5000 13
fi

run "regression: target cost of the render loops (pi32v2 disassembly)" python3 tests/target_budget.py \
    build/felucca.dis tests/target_budget.txt

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
run "Switch Blade: boot logo header (size, <= 128 colours, black 0, indices, const, deterministic), identity FM-1_940 kept, release name" \
    python3 tests/switchblade_test.py build/gen/ui_logo.h

# web editor deferred (FM1-Four plan Phase 9): web/test_web.mjs, web/test_backup.mjs and the browser emulator (web/emu)
echo "== skip the web tests and the browser emulator (web editor deferred, FM1-Four plan Phase 9)"

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
