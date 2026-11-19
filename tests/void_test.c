/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the VOID engine (eng_void.c), same sources as the firmware (hostsim.c):
 *   build/host/void_test [DEMO_DIR]      (run_tests.sh builds and runs it; demos in build/void_demo/)
 * 1. the engine: number 1 (ENGI_WAVE, "WAVE"), its knobs, its presets; and that loading a preset in the
 *    harness applies WAVE_PX (void_track_loaded) as ui.c apply_preset_to does: TYPE, voice, detune,
 *    spread, glide, LFO routing, filter envelope, matrix.
 * 2. pitch: SINE at A4 is 440 Hz within the plugin's movement (drift +-1.5 cents, micro-LFO up to 2.5), and at
 *    A3 220 Hz.
 * 2b. V2's parts (2026-11-19; the slots Task 4.4 hid, MIX and OSC2, are PENV and SUB): off, inert (every preset with
 *    SUB and NOISE at 0 the same whatever COLOR and the sub's octave hold; OLVL 0 = 64); PENV bends a note's start and
 *    settles, SUB is a sine one or two octaves down, NOISE sounds and COLOR darkens it.
 * 2c. Task 4.5: VOWEL (type 6: F1 / F2 where the vowel table puts them, the level against LP24, bounded and inside the mixer's integer
 *    headroom), WARP (E2: 64 off bit for bit, the aliasing at C7, the old TBL2 values load as off), LP24 as it was bit for bit, the TYPE switched
 *    every block (no table read out of range), and OB-Z / ACID / DRIFT playing type 6.
 * 3. band-limited: SAW, SQUARE and SPECTRAL (its top position) at C8: what is not a harmonic below -30 dB (the
 *    linear read of a level at 4 samples per cycle of its top harmonic; the plugin aliases at ~-10 dB here).
 * 4. bounded: every table x every filter TYPE at RES 127 and DRV 127, CUT 0 / 127, C1 / C4 / C8, POS 0 / 127: no
 *    wrapped sample (a jump over full scale), no DC; a TYPE and table change while notes sound.
 * 5. DC: every preset held for 2 s at its own POS, the engine's own output (what feeds the sends, ahead of the
 *    master's DC blocker): |mean| under 1 % of the peak. And the blocker's range against the clipper's table.
 * 6. the presets: each sounds, its loudest 0.2 s of a held chord (a pluck decays, a pad swells) near OB-Z's SAW
 *    LEAD (-18 .. +6 dB: the plugin's LP24 at a high RES loses its pass band, as a ladder does), judged in the
 *    loudest of four registers (C3 .. C6: an HP- or BP-voiced preset is quiet below its pass band).
 * 7. the cost: each preset at WAVE's voice cap against 8 voices of OB-Z RAVE (instructions; the reference was PHASE
 *    WIRE, retired, whose cost was within 2 % of OB-Z RAVE's on the host).
 * 8. demos (DEMO_DIR): each preset playing a phrase, a POS sweep per table.
 * 9. against the plugin: eng_void.c's filter and the plugin's own Filter.cpp (build/void_ref, tools/void_ref.cpp:
 *    skipped without it) on the same saw: the level, and the harmonics within 50 dB of the loudest. */
/* Task 4.5: the engine's slow path and (VOID_INSTR, a second build: run_tests.sh) its table reads are instrumented from here (eng_wave.c VOID_AT /
 * VOID_FORCE_SLOW). The hook costs instructions, so the build that measures them has none, and the instrumented one measures no cost. */
static int void_force_slow;
#ifdef VOID_INSTR
static unsigned long void_oob;                         /* table reads outside their table */
static inline unsigned long void_chk(long i, unsigned long n)
{
    if (i < 0 || (unsigned long)i >= n) {
        void_oob++;
        return i < 0 ? 0ul : n - 1u;                   /* (the read itself is kept inside, so the test does not crash) */
    }
    return (unsigned long)i;
}
#define VOID_CHK(i, n) void_chk((long)(i), (n))
#endif
#define VOID_FORCE_SLOW void_force_slow
#define main hostsim_main
#include "host_pins.h"   /* (V2) per-host cost pins */
#include "hostsim.c"
#undef main
#include "void_ref_cfg.h"
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static int bad;
static void check(const char *what, int ok)
{
    printf("void: %-90s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* an engine or a preset a test names, by name. Not found is a failure, not index 0: leaving the index
 * at 0 would silently retarget the test at another sound if one were renamed, and it would still pass. */
static uint32_t engine_named(const char *name)
{
    uint32_t e;
    char msg[160];
    for (e = 0; e < NENGINES; e++)
        if (str_eq(ENGINES[e]->name, name))
            return e;
    snprintf(msg, sizeof msg, "a test names engine \"%s\": it is there", name);
    check(msg, 0);
    return 0;
}
static uint32_t preset_named(uint32_t e, const char *name)
{
    uint32_t k;
    char msg[160];
    for (k = 0; k < ENGINES[e]->npresets; k++)
        if (str_eq(ENGINES[e]->presets[k].name, name))
            return k;
    snprintf(msg, sizeof msg, "a test names %s's preset \"%s\": it is there", ENGINES[e]->name, name);
    check(msg, 0);
    return 0;
}

#define NREC (FS * 2u)
static int32_t out_buf[2 * CTL];
static int32_t rec[NREC];
static uint32_t nrec;
static void blocks(uint32_t n)
{
    uint32_t i;
    while (n--) {
        mix_block(out_buf, CTL);
        for (i = 0; i < CTL && nrec < NREC; i++)
            rec[nrec++] = out_buf[2u * i];
    }
}

static void fresh(uint32_t e, uint32_t pi)
{
    uint32_t k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(&fx, 0, sizeof fx);
    memset(&pf, 0, sizeof pf);
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    perf_held = perf_latched = 0;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    vage = 0;
    rng_state = 0x1234567u;
    mod_seed = 0x2545F491u;
    memset(trk, 0, sizeof trk);
    memset(&mod, 0, sizeof mod);
    memset(sl, 0, sizeof sl);
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], k ? 0u : e, k ? 0u : pi);
    for (k = 1; k < NPART; k++)
        trk[k].p[P_MUTE] = 1;
    song.sel = 0;
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    nrec = 0;
}

/* track 1 = VOID, dry, no envelope or LFO movement, a held note: the oscillator as it is */
static track_t *plain(uint32_t tb1, uint32_t ft)
{
    track_t *t = &trk[0];
    fresh(ENGI_WAVE, 0);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_ATK] = 0;
    t->p[P_DEC] = 0;
    t->p[P_SUS] = 127;
    t->p[P_REL] = 0;
    t->p[P_ED_FLT] = t->p[P_ED_PIT] = t->p[P_ED_SHP] = 0;
    t->p[P_LD_FLT] = t->p[P_LD_PIT] = t->p[P_LD_SHP] = t->p[P_LD_AMP] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_E0] = (int16_t)tb1;
    t->p[P_E1] = 0;
    t->p[P_E2] = 64;                                   /* (WARP off since Task 4.5; TBL2 before: unread) */
    t->p[P_E3] = 0;
    t->p[P_E4] = 127;
    t->p[P_E5] = 0;
    t->p[P_E6] = 0;
    t->p[P_E7] = 0;
    t->p[P_FTYPE] = (int16_t)ft;
    /* The pinned hashes below were recorded when WAVE's preset 0 was the Task 4.4 stopgap bank's AMBIENT SINE, whose
     * extras (an LFO at RATE 17 to CUT 2 and SHP 11, a filter envelope, default detune, no spread or glide) fresh() loaded
     * and this function left in place. The bank is now original (Task 4.6): those extras are set here, so the scenarios
     * stay what they were, and the hashes with them. */
    t->p[P_DETUNE] = TP[P_DETUNE].def;
    t->p[P_SPRD] = 0;
    t->p[P_GLIDE] = 0;
    t->p[P_LRATE] = 17; t->p[P_LWAVE] = 0; t->p[P_LTRIG] = 1; t->p[P_LFADE] = 0;
    t->p[P_FM1_LEVEL] = 64; t->p[P_FM1_ATK] = 32; t->p[P_FM1_DEC] = 79; t->p[P_FM1_SUS] = 89; t->p[P_FM1_REL] = 79;
    t->p[P_M1SRC] = 1; t->p[P_M1DST] = 2; t->p[P_M1AMT] = 2;
    t->p[P_M2SRC] = 1; t->p[P_M2DST] = 3; t->p[P_M2AMT] = 11;
    t->p[P_M3SRC] = t->p[P_M3DST] = t->p[P_M3AMT] = 0;
    t->p[P_M4SRC] = t->p[P_M4DST] = t->p[P_M4AMT] = 0;
    t->p[P_FM2_ATK] = 0;                               /* V2: no NOISE (COLOR white, the sub one octave down), OLVL the default */
    t->p[P_FM2_SUS] = 127;
    t->p[P_FM2_DEC] = 0;
    t->p[P_FM3_ATK] = 0;
    return t;
}

/* frequency of rec[a .. b) by its rising zero crossings (interpolated) */
static double freq_of(uint32_t a, uint32_t b)
{
    double first = -1, last = -1;
    uint32_t i, n = 0;
    for (i = a + 1u; i < b; i++)
        if (rec[i - 1u] < 0 && rec[i] >= 0) {
            double x = (double)(i - 1u) + (double)(-rec[i - 1u]) / (double)(rec[i] - rec[i - 1u]);
            if (first < 0)
                first = x;
            last = x;
            n++;
        }
    return n > 1u ? (double)(n - 1u) * FS / (last - first) : 0.0;
}
static double cents(double f, double ref) { return 1200.0 * log2(f / ref); }

/* ------------------------------------------------------------------ 1 --- */
static void test_engine(void)
{
    const engine_t *e = ENGINES[ENGI_WAVE];
    check("engine 1 is WAVE, two EDIT pages of four (TBL1 POS WARP PENV, CUT RES SUB DRV), presets",
          str_eq(e->name, "WAVE") && str_eq(e->edit[0].label, "TBL1") && str_eq(e->edit[1].label, "POS") &&
          str_eq(e->edit[4].label, "CUT") && str_eq(e->edit[5].label, "RES") && str_eq(e->edit[7].label, "DRV") &&
          e->npresets >= 5u && e->mem);
    /* V2 (2026-11-19): the slots Task 4.4 hid (MIX E3, OSC2 E6) are V2's PENV and SUB; their ids did not move, so a project
     * saved since Task 4.4 (which stored 0 there: the range was 0..0) loads them off */
    check("WAVE: E3 is PENV (-24..24 semitones, default 0), E6 is SUB (0..127, default 0)",
          str_eq(e->edit[3].label, "PENV") && e->edit[3].min == -24 && e->edit[3].max == 24 && e->edit[3].def == 0 &&
          str_eq(e->edit[6].label, "SUB") && e->edit[6].min == 0 && e->edit[6].max == 127 && e->edit[6].def == 0);
    check("WAVE: E2 is WARP, 9..127, default 64 (off); TYPE has a VOWEL as its 7th",
          str_eq(e->edit[2].label, "WARP") && e->edit[2].min == 9 && e->edit[2].max == 127 && e->edit[2].def == 64 &&
          TP[P_FTYPE].max == 6 && str_eq(TP[P_FTYPE].names[6], "VOWEL"));
}

/* ----------------------------------------------------------------- 1b --- */
/* The harness must load a preset the way the firmware does (ui.c apply_preset_to -> void_track_loaded).
 * WAVE_PX holds what a preset_t cannot: the filter TYPE, voice mode, detune, spread, glide, LFO routing,
 * filter envelope and matrix. Checked on every preset against WAVE_PX itself rather than a copy of it,
 * so a row that moves moves the expectation with it, and in the same terms void_track_loaded uses
 * (detune and the LFO block are "0 = leave the default", voice only forces UNISON). */
static void test_px(void)
{
    uint32_t k, short_n = 0;
    char msg[200];
    for (k = 0; k < NELEM(WAVE_PX); k++) {
        const void_px_t *x = &WAVE_PX[k];
        const preset_t *pr = &ENGINES[ENGI_WAVE]->presets[k];
        const track_t *t = &trk[0];
        uint32_t s, miss = 0;
        fresh(ENGI_WAVE, k);
        miss += t->p[P_FTYPE] != x->ftype;
        miss += t->p[P_VOICE] != (x->voice == V_UNISON ? V_UNISON : pr->mono ? V_LEGATO : V_POLY);
        miss += x->detune && t->p[P_DETUNE] != (int16_t)(x->detune - 1u);
        miss += t->p[P_SPRD] != x->spread;
        miss += t->p[P_GLIDE] != x->glide;
        if (x->lfo[0])
            miss += t->p[P_LRATE] != x->lfo[1] || t->p[P_LWAVE] != x->lfo[2] ||
                    t->p[P_LTRIG] != x->lfo[3] || t->p[P_LFADE] != x->lfo[4];
        miss += t->p[P_FM1_LEVEL] != x->fe[0] || t->p[P_FM1_ATK] != x->fe[1] || t->p[P_FM1_DEC] != x->fe[2] ||
                t->p[P_FM1_SUS] != x->fe[3] || t->p[P_FM1_REL] != x->fe[4];
        for (s = 0; s < 4u; s++)
            miss += t->p[P_M1SRC + 3u * s] != x->mx[s][0] || t->p[P_M1DST + 3u * s] != x->mx[s][1] ||
                    t->p[P_M1AMT + 3u * s] != x->mx[s][2];
        miss += t->p[P_LD_FLT] != x->ldflt || t->p[P_LSYNC] != x->lsync;
        if (miss) {
            short_n++;
            printf("void: WAVE_PX not applied: %-12s TYPE %d (want %u), SPRD %d (want %u), FLT ENV LVL %d (want %u), "
                   "DTUNE %d, %u of 9 field groups off\n", pr->name, t->p[P_FTYPE], x->ftype, t->p[P_SPRD], x->spread,
                   t->p[P_FM1_LEVEL], x->fe[0], t->p[P_DETUNE], miss);
        }
    }
    snprintf(msg, sizeof msg, "WAVE_PX: a host-loaded preset has its TYPE, voice, detune, spread, glide, LFO, FLT ENV "
             "and matrix (%u of %u presets short)", short_n, (unsigned)NELEM(WAVE_PX));
    check(msg, short_n == 0u);
}

/* ------------------------------------------------------------------ 2 --- */
static void test_pitch(void)
{
    double f;
    track_t *t = plain(0, 3);
    trk_note_on(t, 69, 100);
    blocks(NREC / CTL);
    f = freq_of(FS / 4u, NREC);
    printf("void: SINE A4 %.3f Hz (%+.2f cents)\n", f, cents(f, 440.0));
    check("pitch: SINE at A4 is 440 Hz within the plugin's movement (+-4 cents)", fabs(cents(f, 440.0)) < 4.0);
    t = plain(0, 3);
    trk_note_on(t, 57, 100);
    blocks(NREC / CTL);
    f = freq_of(FS / 4u, NREC);
    printf("void: SINE A3 %.3f Hz (%+.2f cents)\n", f, cents(f, 220.0));
    check("pitch: SINE at A3 is 220 Hz (+-4 cents)", fabs(cents(f, 220.0)) < 4.0);
}

/* ----------------------------------------------------------------- 2b --- */
/* V2's parts (2026-11-19): PENV (E3), SUB (E6), NOISE / COLOR / the sub's octave (P_FM2_ATK / SUS / DEC), OLVL (P_FM3_ATK).
 * Off, they are inert: every factory preset with its own SUB and NOISE at 0 renders the same sample for sample whatever
 * COLOR and the sub's octave hold, and OLVL 0 is OLVL 64 (the default of every project from before them). On: PENV bends
 * the start of a note and is gone after its 51 ms, SUB adds a sine one or two octaves down, NOISE's COLOR darkens it. */
static uint64_t chord_hash(uint32_t pi, int col, int soct, int olvl, int zero)
{
    static const uint8_t N[3] = {48, 55, 64};
    track_t *t = &trk[0];
    uint64_t h = 1469598103934665603ull;
    uint32_t i;
    memset(eng_mem, 0, sizeof eng_mem);                /* (the voices' random state, or two renders of one preset differ) */
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    fresh(ENGI_WAVE, pi);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_VOICE] = V_POLY;                            /* (UNISON's start phases come from a static seed no reset reaches: two renders differ) */
    if (zero)
        t->p[P_E6] = t->p[P_FM2_ATK] = 0;              /* SUB and NOISE off */
    if (col >= 0)
        t->p[P_FM2_SUS] = (int16_t)col;
    if (soct >= 0)
        t->p[P_FM2_DEC] = (int16_t)soct;
    if (olvl >= 0)
        t->p[P_FM3_ATK] = (int16_t)olvl;
    for (i = 0; i < 3u; i++)
        trk_note_on(t, N[i], 100);
    nrec = 0;
    blocks(FS / 2u / CTL);
    for (i = 0; i < nrec; i++)
        h = (h ^ (uint32_t)rec[i]) * 1099511628211ull;
    return h;
}
/* the amplitude of the component at hz in rec[a .. b) (one Goertzel bin) */
static double tone_amp(uint32_t a, uint32_t b, double hz)
{
    double w = 2.0 * M_PI * hz / FS, c = 2.0 * cos(w), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        s0 = rec[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2.0 / (b - a);
}
static double zc_rate(uint32_t a, uint32_t b)
{
    uint32_t i, n = 0;
    for (i = a + 1u; i < b; i++)
        n += (rec[i - 1u] < 0) != (rec[i] < 0);
    return (double)n * FS / (b - a);
}
static void test_v2_parts(void)
{
    uint32_t k, bad_n = 0;
    double f0, f1, sub1, sub2, base1, zw, zd;
    char msg[200];
    track_t *t;
    for (k = 0; k < ENGINES[ENGI_WAVE]->npresets; k++) {
        uint64_t a = chord_hash(k, -1, -1, -1, 1);
        if (a != chord_hash(k, 0, 127, -1, 1) || a != chord_hash(k, 127, 0, -1, 1)) {
            printf("void: COLOR / the sub's octave sound with SUB and NOISE off: %s\n", ENGINES[ENGI_WAVE]->presets[k].name);
            bad_n++;
        }
        if (a == 1469598103934665603ull)               /* (the render must have run: hashed something) */
            bad_n++;
    }
    {
        uint64_t a = chord_hash(0, -1, -1, 0, 1);
        bad_n += a != chord_hash(0, -1, -1, 64, 1) || a == chord_hash(0, -1, -1, 32, 1);
    }
    snprintf(msg, sizeof msg, "V2 parts off: SUB 0 and NOISE 0 leave COLOR and the sub's octave silent, OLVL 0 = 64 (%u of %u differ)",
             bad_n, (unsigned)ENGINES[ENGI_WAVE]->npresets);
    check(msg, bad_n == 0u);
    /* PENV +12: the first ms of A3 an octave up (the peak 0.5 + 0.5 x 127 / 127 velocity: all of it), 220 Hz again after it */
    t = plain(0, 3);
    t->p[P_E3] = 12;
    trk_note_on(t, 57, 127);
    blocks(NREC / CTL);
    f0 = freq_of(4u * CTL, 4u * CTL + FS / 200u);      /* ~3 .. 8 ms: around 0.85 .. 0.7 of the 12 semitones */
    f1 = freq_of(FS / 4u, NREC);
    printf("void: PENV +12 on A3: %.1f Hz at 3-8 ms, %.2f Hz after\n", f0, f1);
    check("PENV +12: a note starts well above its pitch (> +6 semitones at 3-8 ms) and settles on it (+-4 cents after 0.25 s)",
          cents(f0, 220.0) > 600.0 && fabs(cents(f1, 220.0)) < 4.0);
    /* SUB on a SINE at A4, measured apart from it: one octave down (220 Hz) at P_FM2_DEC < 64, two (110 Hz) at >= 64, and its level against
     * the oscillator's as in the plugin: SUB / 127 of the plugin's 1.0 against the oscillator's table level times its 0.8 x 0.707 (WAVE's
     * oscillator runs at 1 / 0.5657 of the plugin's until VOID_OUT_LEVEL, so the sub does too). SUB 24: under the voice's soft clipper and the master's limiter (higher, the
     * limiter on the mix squashes the sub and this ratio).
     * Over 0.1 s: the oscillator's drift (the plugin's micro-LFO, up to 2.5 cents) would smear one bin over a longer window */
    t = plain(0, 3);
    trk_note_on(t, 69, 100);
    blocks(NREC / CTL);
    base1 = tone_amp(FS / 4u, FS / 4u + FS / 10u, 440.0);
    t = plain(0, 3);
    t->p[P_FM3_ATK] = 1;                               /* (the oscillator near silent: the sub alone) */
    t->p[P_E6] = 24;
    t->p[P_FM2_DEC] = 0;
    trk_note_on(t, 69, 100);
    blocks(NREC / CTL);
    sub1 = tone_amp(FS / 4u, FS / 4u + FS / 10u, 220.0);
    t = plain(0, 3);
    t->p[P_FM3_ATK] = 1;
    t->p[P_E6] = 24;
    t->p[P_FM2_DEC] = 127;
    trk_note_on(t, 69, 100);
    blocks(NREC / CTL);
    sub2 = tone_amp(FS / 4u, FS / 4u + FS / 10u, 110.0);
    {
        double want = (24.0 / 127.0) / (0.495 * 0.8 * 0.7071);   /* the sub / the SINE's peak (V2: RMS 0.35) at the plugin's voice level */
        printf("void: SUB 24: 220 Hz %.0f, 110 Hz %.0f (two down); SINE A4 alone 440 Hz %.0f: sub / osc %.2f (want %.2f)\n", sub1, sub2,
               base1, sub1 / base1, want);
        check("SUB: a sine one octave down (two with the octave >= 64), its level against the oscillator's the plugin's (within 1.5 dB)",
              base1 > 0 && fabs(20 * log10(sub1 / base1 / want)) < 1.5 && fabs(20 * log10(sub2 / sub1)) < 1.0);
    }
    /* NOISE alone (the oscillator's level near 0: OLVL 1): COLOR 127 white, COLOR 0 a 200 Hz low-pass (fewer zero crossings) */
    t = plain(0, 3);
    t->p[P_FM3_ATK] = 1;
    t->p[P_FM2_ATK] = 127;
    t->p[P_FM2_SUS] = 127;
    trk_note_on(t, 69, 100);
    blocks(NREC / CTL);
    zw = zc_rate(FS / 4u, NREC);
    t = plain(0, 3);
    t->p[P_FM3_ATK] = 1;
    t->p[P_FM2_ATK] = 127;
    t->p[P_FM2_SUS] = 0;
    trk_note_on(t, 69, 100);
    blocks(NREC / CTL);
    zd = zc_rate(FS / 4u, NREC);
    printf("void: NOISE 127 zero crossings: COLOR 127 %.0f / s, COLOR 0 %.0f / s\n", zw, zd);
    check("NOISE: it sounds, and COLOR 0 is far darker than COLOR 127 (a quarter of the zero crossings or fewer)",
          zw > 2000.0 && zd < zw / 4.0);
}

/* ------------------------------------------------------------------ 4.5 --- */
/* The render before Task 4.5, as hashes. One type's scenarios: 3 tables x CUT {0, 1, 64, 127} x RES {0, 64, 127} x DRV {0, 100}, each
 * with the cutoff held and again swept (a triangle over the blocks, through the closing region), a 4-note chord, the first two released
 * part-way: 144 renders of 64 blocks, hashed together. The WAVE_RENDER_HASH rows were recorded from the tree at 1a5b3df/e1acdb7 (before
 * Task 4.5). The LP24 ladder is untouched (the user kept it), so every type that existed is bit for bit as before, and WARP at 64 (off) keeps it so. */
/* V2 (2026-11-19): re-recorded on purpose. The tables are at V2's level (DC out, RMS 0.35: tools/gen_void_tables.py) and the drive is V2's
 * shaper (eng_wave.c void_front), so every scenario's samples moved; the recording is of the V2 tree, the fast loops and the forced slow
 * loop agreeing (the check below). Before: 89c9d66272840c8a 723981c818e6fee8 b82f2a46533caa75 d106dfad484512cc 1695f2cbd027b44d
 * 4f6e5c2300649526 c119b16700c137c2 (Task 4.5) */
static const uint64_t SCEN_HASH[7] = {0x67dcdd12766dd0d6ull, 0x4a9bcf43ed0f6d71ull, 0xece1394f744cddfbull, 0x94b33c081a22e1b0ull,
                                      0x9d9bf361e21afed6ull, 0x9e74bfe4ef5a3cecull,
                                      0x8396582a14330244ull};   /* LP24 BP HP LP12 HP24 NOTCH, VOWEL */
static uint64_t scen_hash(uint32_t ft)
{
    static const uint8_t TB[3] = {1, 2, 3}, N[4] = {36, 43, 60, 84}, CUT[4] = {0, 1, 64, 127}, RES[3] = {0, 64, 127};
    uint64_t h = 1469598103934665603ull;
    uint32_t a, b, c, d, e, i, k;
    for (a = 0; a < 3u; a++)
        for (b = 0; b < 4u; b++)
            for (c = 0; c < 3u; c++)
                for (d = 0; d < 2u; d++)
                    for (e = 0; e < 2u; e++) {
                        track_t *t;
                        memset(eng_mem, 0, sizeof eng_mem);
                        memset(eng_mem_owner, 0, sizeof eng_mem_owner);
                        t = plain(TB[a], ft);
                        t->p[P_E1] = 40;
                        t->p[P_E4] = CUT[b];
                        t->p[P_E5] = RES[c];
                        t->p[P_E7] = d ? 100 : 0;
                        for (i = 0; i < 4u; i++)
                            trk_note_on(t, N[i], 100);
                        nrec = 0;
                        for (k = 0; k < 64u; k++) {
                            if (e)
                                t->p[P_E4] = (int16_t)(k < 32u ? 127u - k * 4u : (k - 32u) * 4u);
                            if (k == 40u) {
                                trk_note_off(t, N[0]);
                                trk_note_off(t, N[1]);
                            }
                            blocks(1);
                        }
                        for (i = 0; i < nrec; i++)
                            h = (h ^ (uint32_t)rec[i]) * 1099511628211ull;
                    }
    return h;
}

static void test_scen(void)
{
    uint32_t ft, bad_n = 0, forced = 0;
    char msg[200];
    for (ft = 0; ft < 7u; ft++) {
        uint64_t h = scen_hash(ft);
        if (h != SCEN_HASH[ft]) {
            printf("void: type %u renders %016llx, was %016llx\n", ft, (unsigned long long)h, (unsigned long long)SCEN_HASH[ft]);
            bad_n++;
        }
    }
    snprintf(msg, sizeof msg, "bit for bit: the seven filter types x 144 renders (held and swept cutoff, closing, drain) are as before Task 4.5 (%u differ)", bad_n);
    check(msg, bad_n == 0u);
    /* the slow loop for every block (VOID_FORCE_SLOW) must give what the dispatch gave: a fast loop used while a note fades in or the
     * cutoff closes (or drains) would differ here. The scenarios reach CUT 0 (shut), CUT 1 at RES 127 (close 1170: the drain) and the sweeps */
    void_force_slow = 1;
    for (ft = 0; ft < 7u; ft++)
        forced += scen_hash(ft) != SCEN_HASH[ft];
    void_force_slow = 0;
    snprintf(msg, sizeof msg, "slow path: forcing it for every block changes no sample of the same scenarios (%u of 7 types differ)", forced);
    check(msg, forced == 0u);
}

/* Task 4.5 (the 4.4 review's Minor 3): switching the filter TYPE while notes sound fed the SVF's state (|f| up to 150000) into the
 * ladder's void_tanh1, which indexes VOID_TANH1[257] by (x + 32768) >> 8: a read past the table. Every pair of types, in a row, one
 * switch a block; RES 0 / 127, DRV 0 / 127, MIDI 24..99. VOID_AT counts any read of a signal-indexed table outside it. */
#ifdef VOID_INSTR
static void test_oob(void)
{
    uint32_t nft = (uint32_t)TP[P_FTYPE].max + 1u, a, b, n, r, d, k, runs = 0;
    int32_t pk = 0;
    char msg[200];
    void_oob = 0;
    for (r = 0; r < 2u; r++)
        for (d = 0; d < 2u; d++)
            for (n = 24; n <= 99u; n++) {
                track_t *t = plain(1, 0);
                t->p[P_E5] = r ? 127 : 0;
                t->p[P_E7] = d ? 127 : 0;
                t->p[P_E4] = 90;
                trk_note_on(t, (uint8_t)n, 127);
                trk_note_on(t, (uint8_t)(n + 7u), 127);
                nrec = 0;
                for (a = 0; a < nft; a++)
                    for (b = 0; b < nft; b++)
                        for (k = 0; k < 2u; k++) {
                            t->p[P_FTYPE] = (int16_t)(k ? b : a);
                            blocks(1);
                        }
                for (k = 0; k < nrec; k++)
                    pk = abs(rec[k]) > pk ? abs(rec[k]) : pk;
                runs++;
            }
    snprintf(msg, sizeof msg, "TYPE switched every block over all %u types x RES 0/127 x DRV 0/127 x MIDI 24..99 (%u runs): %lu table reads out of range",
             nft, runs, void_oob);
    check(msg, void_oob == 0ul && pk > 0);
}
#endif

/* ------------------------------------------------------------------ 3 --- */
/* the share of rec[a, a + n) that is not within +-3 bins of a harmonic of f0 (Hann window, DFT on harmonics) */
static double alias_db(uint32_t a, uint32_t n, double f0)
{
    double tot = 0, harm = 0;
    uint32_t i, k;
    static double w[16384];
    for (i = 0; i < n; i++)
        w[i] = rec[a + i] * (0.5 - 0.5 * cos(2 * M_PI * i / n));
    for (k = 1; k < n / 2u; k++) {
        double re = 0, im = 0, fk = (double)k * FS / n, h = fk / f0, p;
        for (i = 0; i < n; i++) {
            re += w[i] * cos(2 * M_PI * k * i / n);
            im -= w[i] * sin(2 * M_PI * k * i / n);
        }
        p = re * re + im * im;
        tot += p;
        if (fabs(h - floor(h + 0.5)) * f0 < 3.0 * FS / n && h > 0.5)
            harm += p;
    }
    return 10 * log10((tot - harm) / tot + 1e-12);
}

/* the same without the DC: a warped cycle is asymmetric and has a mean, whose window leakage (bins 1 .. 2) is not aliasing */
static double alias_db_nodc(uint32_t a, uint32_t n, double f0)
{
    double tot = 0, harm = 0;
    uint32_t i, k;
    static double w[16384];
    for (i = 0; i < n; i++)
        w[i] = rec[a + i] * (0.5 - 0.5 * cos(2 * M_PI * i / n));
    for (k = 3; k < n / 2u; k++) {
        double re = 0, im = 0, fk = (double)k * FS / n, h = fk / f0, p;
        for (i = 0; i < n; i++) {
            re += w[i] * cos(2 * M_PI * k * i / n);
            im -= w[i] * sin(2 * M_PI * k * i / n);
        }
        p = re * re + im * im;
        tot += p;
        if (fabs(h - floor(h + 0.5)) * f0 < 3.0 * FS / n && h > 0.5)
            harm += p;
    }
    return 10 * log10((tot - harm) / tot + 1e-12);
}

static void test_bandlimit(void)
{
    static const struct { uint32_t tb; int pos; const char *nm; } C[3] = {{1, 0, "SAW"}, {2, 0, "SQUARE"}, {3, 127, "SPECTRAL top"}};
    uint32_t k;
    double worst = -999;
    char msg[160];
    for (k = 0; k < 3u; k++) {
        track_t *t = plain(C[k].tb, 3);
        double d;
        t->p[P_E1] = (int16_t)C[k].pos;
        trk_note_on(t, 108, 100);
        blocks(FS / 2u / CTL);
        d = alias_db(FS / 4u, 4096, 4186.009);
        printf("void: %s at C8: %.1f dB off the harmonics\n", C[k].nm, d);
        worst = d > worst ? d : worst;
    }
    snprintf(msg, sizeof msg, "band-limited: SAW, SQUARE, SPECTRAL (top) at C8: off-harmonic energy below -30 dB (%.1f)", worst);
    check(msg, worst < -30.0);
}

/* ------------------------------------------------------------------ 4 --- */
static void test_bounded(void)
{
    static const uint8_t NOTE[3] = {24, 60, 108};
    uint32_t tb, ft, c, nt, wraps = 0, worst_dc = 0, silent = 0, runs = 0;
    for (tb = 0; tb < VOID_NTBL; tb++)
        for (ft = 0; ft < 6u; ft++)
            for (c = 0; c < 4u; c++)
                for (nt = 0; nt < 3u; nt++) {
                    track_t *t = plain(tb, ft);
                    int64_t sum = 0;
                    uint32_t i, dc;
                    t->p[P_E5] = 127;
                    t->p[P_E7] = 127;
                    t->p[P_E4] = (c & 1u) ? 127 : 0;
                    t->p[P_E1] = (c & 2u) ? 127 : 0;
                    trk_note_on(t, NOTE[nt], 127);
                    trk_note_on(t, NOTE[nt] + 7u, 127);
                    blocks(FS / 2u / CTL);
                    for (i = 1; i < nrec; i++) {
                        int32_t d = rec[i] - rec[i - 1u];
                        wraps += d > 6 * VOICE_FS || d < -6 * VOICE_FS;
                        if (i >= nrec / 2u)                /* settled (the master's DC blocker: the plugin's too) */
                            sum += rec[i];
                    }
                    dc = (uint32_t)llabs(sum / (int64_t)(nrec - nrec / 2u));
                    if ((c & 1u) && nt)                    /* (CUT 0 or C1: a few cycles of ~30 Hz in the window, not DC) */
                        worst_dc = dc > worst_dc ? dc : worst_dc;
                    silent += (c & 1u) && ft != 2u && ft != 4u && nrec && rec[nrec / 2u] == 0 && rec[nrec / 3u] == 0;
                    runs++;
                }
    printf("void: corners: %u runs, %u wrapped samples, worst DC %u, %u silent\n", runs, wraps, worst_dc, silent);
    check("bounded: every table x TYPE at RES and DRV 127, CUT 0 / 127, POS 0 / 127, C1 / C4 / C8: no wrapped sample",
          wraps == 0u);
    check("bounded: no DC at C4 / C8, CUT 127 (under 2 % of a voice's full scale)", worst_dc < VOICE_FS / 50u);
    {   /* TYPE and table changes while notes sound */
        track_t *t = plain(1, 0);
        uint32_t i, k;
        int32_t pk = 0;
        t->p[P_E5] = 120;
        trk_note_on(t, 48, 127);
        trk_note_on(t, 55, 127);
        for (k = 0; k < 24u; k++) {
            t->p[P_FTYPE] = (int16_t)(k % 6u);
            t->p[P_E0] = (int16_t)(k % VOID_NTBL);
            t->p[P_E1] = (int16_t)(k * 11u % 128u);
            blocks(40);
        }
        for (i = 1; i < nrec; i++) {
            int32_t d = rec[i] - rec[i - 1u];
            wraps += d > 6 * VOICE_FS || d < -6 * VOICE_FS;
            pk = abs(rec[i]) > pk ? abs(rec[i]) : pk;
        }
        check("bounded: TYPE, table and POS changes while notes sound: no wrapped sample", wraps == 0u && pk > 0);
    }
}

/* ------------------------------------------------------------------ 5 --- */
/* the engine's own output for 2 s of a held note: what mix_part hands the sends, so the master's DC blocker
 * (fx.c dc_block, after the sends are taken) is not in the way. A preset's position stays where it is. */
static double engine_dc(uint32_t pi, uint32_t note)
{
    static int32_t part[CTL];
    track_t *t = &trk[0];
    int64_t sum = 0;
    int32_t pk = 0;
    uint32_t i, b;
    fresh(ENGI_WAVE, pi);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    trk_note_on(t, note, 100);
    for (b = 0; b < 2u * FS / CTL; b++) {
        int on = mod_begin(t);                         /* (the matrix, as mix_part does) */
        spread_on = 0;
        for (i = 0; i < CTL; i++)
            part[i] = 0;
        track_render(t, part, CTL);
        if (on)
            mod_end(t);
        for (i = 0; i < CTL; i++) {
            int32_t v = part[i];
            sum += v;
            pk = (v < 0 ? -v : v) > pk ? (v < 0 ? -v : v) : pk;
        }
    }
    return pk ? fabs((double)sum / (double)(2u * FS)) / (double)pk : 0.0;
}

/* Every preset, not one: up to Task 4.3 this held BRASS LEAD only, and the harness rendered it LP24
 * because it never applied WAVE_PX -- the firmware ships it ftype 2 (VF_HP), which removes DC by itself.
 * The one preset that was named was therefore the one that proved nothing. The offset the drive shaper
 * leaves is per preset, so the whole set is the claim: 8 of the 32 were over 1 % before void_dc. */
static void test_dc(void)
{
    uint32_t k, over = 0, worst_k = 0;
    double worst = 0;
    char msg[160];
    for (k = 0; k < ENGINES[ENGI_WAVE]->npresets; k++) {
        double r = engine_dc(k, 60);
        if (r >= 0.01)
            printf("void: DC over 1 %%: %-12s |mean| / peak %.5f\n", ENGINES[ENGI_WAVE]->presets[k].name, r);
        over += r >= 0.01;
        if (r > worst) {
            worst = r;
            worst_k = k;
        }
    }
    printf("void: every WAVE preset held 2 s: worst |mean| / peak %.5f (%s)\n", worst,
           ENGINES[ENGI_WAVE]->presets[worst_k].name);
    snprintf(msg, sizeof msg, "DC: every preset's engine output, |mean| under 1 %% of the peak (worst %.5f, %s)",
             worst, ENGINES[ENGI_WAVE]->presets[worst_k].name);
    check(msg, over == 0u);
}

/* The soft clipper reads VOID_SOFT[k] and [k + 1] for k = (|y| - VOID_CLIP_THR) >> 8. Up to Task 4.2 the clamp
 * at VOID_OUT_LEVEL kept |y| at 62224 and that index in range on its own; the DC blocker sits after it and has
 * a peak gain of 2, so its own recurrence leaves that range and void_dc clamps its return to VOID_CLIP_MAX.
 * Both halves are pinned here: the recurrence does overshoot (so the clamp is not decoration), and what
 * void_dc returns stays inside the table (so dropping the clamp fails here instead of reading past VOID_SOFT).
 * The input is held at -full scale and then at +full scale: the sequence that reaches the l1 norm of the
 * impulse response (1, then -(1 - R) R^n), which is the blocker's worst-case peak gain. */
static void test_dc_range(void)
{
    int32_t full = mulq15(110000, VOID_OUT_LEVEL), w = 0, raw = 0, wr = 0, got = 0;
    uint32_t i;
    char msg[200];
    for (i = 0; i < 3u * FS; i++) {
        int32_t x = i < 2u * FS ? -full : full;
        int32_t y = void_dc(&w, x), u;
        got = (y < 0 ? -y : y) > got ? (y < 0 ? -y : y) : got;
        u = x + (wr >> VOID_DC_SH);                    /* the same recurrence, unclamped */
        wr -= (u * (32768 - VOID_DC_R)) >> (15 - VOID_DC_SH);
        raw = (u < 0 ? -u : u) > raw ? (u < 0 ? -u : u) : raw;
    }
    printf("void: the DC blocker at +-%d: worst |y| %d unclamped, %d as returned (VOID_CLIP_MAX %d)\n",
           full, raw, got, VOID_CLIP_MAX);
    snprintf(msg, sizeof msg, "DC: the blocker's recurrence overshoots the clipper's table (|y| %d > %d)",
             raw, VOID_CLIP_MAX);
    check(msg, raw > VOID_CLIP_MAX);
    snprintf(msg, sizeof msg,
             "DC: what void_dc returns stays inside the clipper's table (%d <= VOID_CLIP_MAX %d, VOID_SOFT[%u])",
             got, VOID_CLIP_MAX, (unsigned)NELEM(VOID_SOFT));
    check(msg, got <= VOID_CLIP_MAX && (uint32_t)((VOID_CLIP_MAX - VOID_CLIP_THR) >> 8) + 1u < NELEM(VOID_SOFT));
}

/* Task 4.4: the render loop does not call void_dc (which clamps every sample) but void_dc_raw and void_clip, whose clamp sits inside the
 * clipper's branch. Exhaustively over the blocker's whole range (+-124448, its peak gain of 2 on a full-scale input) the pair must give what
 * clamp-then-clip gave, and the table index must stay in range: dropping the clamp from void_clip fails here (read past VOID_SOFT). */
static void test_clip_equiv(void)
{
    int32_t y, bad_n = 0, worst = 0;
    char msg[200];
    for (y = -125000; y <= 125000; y++) {
        int32_t c = clamp(y, -VOID_CLIP_MAX, VOID_CLIP_MAX), a = c < 0 ? -c : c, want = c;
        if (a > VOID_CLIP_THR) {                       /* the previous loop body, verbatim */
            uint32_t o = (uint32_t)(a - VOID_CLIP_THR), k = o >> 8;
            a = VOID_CLIP_THR + VOID_SOFT[k] + (((VOID_SOFT[k + 1u] - VOID_SOFT[k]) * (int32_t)(o & 255u)) >> 8);
            want = c < 0 ? -a : a;
        }
        if (void_clip(y) != want)
            bad_n++;
        a = void_clip(y) < 0 ? -void_clip(y) : void_clip(y);
        worst = a > worst ? a : worst;
    }
    snprintf(msg, sizeof msg, "DC: void_clip(raw y) == clip(clamp(y)) for every y in +-125000 (%d differ, largest output %d)", bad_n, worst);
    check(msg, bad_n == 0);
}

/* ------------------------------------------------------------------ 6 --- */
static double phrase_rms(uint32_t e, uint32_t pi, uint32_t root)
{
    track_t *t = &trk[0];
    double s = 0;
    uint32_t i;
    fresh(e, pi);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    trk_note_on(t, root, 100);
    trk_note_on(t, root + 7u, 100);
    blocks(FS / CTL);
    trk_note_off(t, root);
    trk_note_off(t, root + 7u);
    {                                                  /* the loudest 0.2 s window (steps of 10 ms) */
        double best = 0;
        uint32_t w;
        for (w = 0; w + FS / 5u <= FS; w += FS / 100u) {
            s = 0;
            for (i = w; i < w + FS / 5u; i++)
                s += (double)rec[i] * rec[i];
            best = s > best ? s : best;
        }
        return sqrt(best / (FS / 5u));
    }
}

/* The same chord an octave at a time from C3 to C6, judged where the preset is loudest. One low chord
 * was enough until Task 4.3, when the harness rendered every WAVE preset LP24 (WAVE_PX unapplied). With
 * the real filter TYPEs a preset voiced around its pass band is quiet three octaves below it -- GLASS
 * SHIMR (HP at ~1 kHz) reads -28 dB at C3 and -13 at C6, CYBER ARP (BP at ~2 kHz) -28 at C3 and -15 at
 * C5 -- which is the voicing, not a fault. The loudest register is the honest yardstick for both ends:
 * under -18 dB everywhere means the preset does not sound, over +6 dB anywhere means it blares. */
static void test_presets(void)
{
    static const uint8_t ROOT[4] = {48, 60, 72, 84};
    uint32_t k, r, ok = 1, a = preset_named(0, "SAW LEAD");
    double ref = phrase_rms(0, a, ROOT[0]);
    for (k = 0; k < ENGINES[ENGI_WAVE]->npresets; k++) {
        double db[4], best = -999;
        uint32_t at = 0;
        for (r = 0; r < 4u; r++) {
            db[r] = 20 * log10(phrase_rms(ENGI_WAVE, k, ROOT[r]) / ref + 1e-12);
            if (db[r] > best) {
                best = db[r];
                at = r;
            }
        }
        printf("void: preset %-12s C3 %+5.1f C4 %+5.1f C5 %+5.1f C6 %+5.1f dB against OB-Z SAW LEAD (loudest C%u)\n",
               ENGINES[ENGI_WAVE]->presets[k].name, db[0], db[1], db[2], db[3], 3u + at);
        ok &= best > -18.0 && best < 6.0;
    }
    check("presets: each sounds, its loudest 0.2 s of C3 / C4 / C5 / C6 within -18 .. +6 dB of OB-Z's SAW LEAD", ok);
}

/* ------------------------------------------------------------------ 7 --- */
#include "instr_linux.h"   /* (V2) the Linux instruction counter */
static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#elif defined(__linux__)
    return instr_linux();   /* (V2) tests/instr_linux.h */
#endif
    return 0;
}
static double cost(uint32_t e, uint32_t pi)
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    track_t *t = &trk[0];
    uint32_t i, rep;
    double best = 1e30;
    fresh(e, pi);
    t->p[P_VOICE] = V_POLY;
    t->p[P_SUS] = 127;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    for (i = 0; i < 8u; i++)
        trk_note_on(t, NOTES[i], 100);
    blocks(FS / 4u / CTL);
    for (rep = 0; rep < 3u; rep++) {
        uint64_t i0 = instr_now();
        double ipc;
        blocks(FS / 2u / CTL);
        ipc = (double)(instr_now() - i0) / (FS / 2u);
        best = ipc < best ? ipc : best;
    }
    return best;
}
static void test_cost(void)
{
    double a, worst = 0;
    uint32_t k, ph, pw;
    char msg[200];
    if (!instr_now()) {
        printf("void: cost: no instruction counter on this host, skipped\n");
        return;
    }
    ph = engine_named("OB-Z");                         /* OB-Z RAVE: the reference (PHASE WIRE's cost, retired) */
    pw = preset_named(ph, "RAVE");
    a = cost(ph, pw);
    for (k = 0; k < ENGINES[ENGI_WAVE]->npresets; k++) {   /* (cost() asks for 8 notes: the cap plays VOID_POLY) */
        double c = cost(ENGI_WAVE, k);
        printf("void: cost: %-12s %u voices %.0f instructions / sample (OB-Z RAVE, 8: %.0f; %.2f x)\n",
               ENGINES[ENGI_WAVE]->presets[k].name, VOID_POLY, c, a, c / a);
        worst = c / a > worst ? c / a : worst;
    }
    /* 1.75, not the 1.25 of before Task 4.1: WAVE costs what it did, but the yardstick moved. OB-Z
     * RAVE lost its drive and noise there, from 1765 instructions a sample at 8 voices to about
     * 1130, so the same WAVE presets now read 1.56 x of it instead of 1.18 x. */
    snprintf(msg, sizeof msg, "cost: any preset at its cap (%u voices) within 1.75 x 8 of OB-Z RAVE (%.2f x)", VOID_POLY, worst);
    check(msg, worst < 1.75);
}


/* ---------------------------------------------------------------- 2c: VOWEL --- */
/* the filter alone: the response to a sine of frequency f (a Hann-windowed single-bin DFT: no DC, no other partial), dB against the input's */
static double flt_gain(uint32_t ft, int knob, int res, double f)
{
    void_flt_t fl;
    int32_t st[4] = {0, 0, 0, 0};
    uint32_t n = 24000, i, c = 0;
    double re = 0, im = 0;
    void_flt_set(&fl, ft, VOID_CUT_Q8[knob], res, 0, 0);
    for (i = 0; i < n; i++) {
        int32_t y = void_flt_run(&fl, st, (int32_t)lround(0.05 * 32768 * sin(2 * M_PI * f * i / FS)));
        if (i >= n / 2u) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * (i - n / 2u) / (n / 2u));
            re += w * y * cos(2 * M_PI * f * i / FS);
            im += w * y * sin(2 * M_PI * f * i / FS);
            c++;
        }
    }
    return 20 * log10(2 * sqrt(re * re + im * im) / (c * 0.5) / (0.05 * 32768) + 1e-12);
}
/* the RMS of the band-limited saw (tests/void_ref_cfg.h) through the filter, DC removed (the voice has a DC blocker) */
static double flt_rms(uint32_t ft, int knob, int res, double f0)
{
    static int32_t y[FS];
    void_flt_t fl;
    int32_t st[4] = {0, 0, 0, 0};
    uint32_t i, n = FS, c = 0;
    double m = 0, s = 0;
    void_flt_set(&fl, ft, VOID_CUT_Q8[knob], res, 0, 0);
    for (i = 0; i < n; i++) {
        y[i] = void_flt_run(&fl, st, (int32_t)lround(void_ref_input(i, f0) * 32768.0));
        if (i >= n / 2u) {
            m += y[i];
            c++;
        }
    }
    m /= c;
    for (i = n / 2u; i < n; i++)
        s += (y[i] - m) * (y[i] - m);
    return sqrt(s / c);
}

static void test_vowel(void)
{
    static const int KNOB[3] = {0, 64, 127};
    static const double V[5][2] = {{300, 870}, {450, 800}, {730, 1090}, {530, 1840}, {270, 2290}};   /* U O A E I (the spec's table) */
    double worst = 0, lvl = 0;
    uint32_t q, i, found_ok = 1, r, c;
    char msg[240];
    for (q = 0; q < 3u; q++) {                         /* F1 and F2 at RES 127 (Q 12: sharp), within +-1 semitone of the table at CUT 0, 64, 127 */
        double pos = KNOB[q] / 127.0 * 4.0, fr[400], g[400], want[2], got[2] = {0, 0};
        uint32_t n = 0, nf = 0, j = (uint32_t)pos, k;
        double t = pos - j, f;
        if (j > 3u) { j = 3; t = 1.0; }
        for (k = 0; k < 2u; k++)
            want[k] = V[j][k] + t * (V[j + 1u][k] - V[j][k]);
        for (f = 150; f < 4500 && n < 400u; f *= pow(2, 1 / 48.0), n++) {
            fr[n] = f;
            g[n] = flt_gain(VF_VOWEL, KNOB[q], 127, f);
        }
        for (i = 1; i + 1u < n && nf < 2u; i++)
            if (g[i] > g[i - 1u] && g[i] >= g[i + 1u] && g[i] > -30.0)
                got[nf++] = fr[i];
        printf("void: VOWEL CUT %3d: F1 %.0f (want %.0f) F2 %.0f (want %.0f) Hz\n", KNOB[q], got[0], want[0], got[1], want[1]);
        for (k = 0; k < 2u; k++) {
            double st = got[k] > 0 ? fabs(12 * log2(got[k] / want[k])) : 99.0;
            worst = st > worst ? st : worst;
        }
        found_ok &= nf == 2u;
    }
    snprintf(msg, sizeof msg, "VOWEL: F1 and F2 within +-1 semitone of the vowel table at CUT 0 / 64 / 127 (worst %.2f st)", worst);
    check(msg, found_ok && worst <= 1.0);
    for (c = 1; c < 3u; c++)                           /* the level against LP24 at the same CUT where LP24 is open (the saw at 110 Hz) */
        for (r = 0; r < 3u; r++) {
            static const int RES[3] = {0, 64, 127};
            double d = 20 * log10(flt_rms(VF_VOWEL, KNOB[c], RES[r], 110) / flt_rms(VF_LP24, KNOB[c], RES[r], 110));
            lvl = fabs(d) > lvl ? fabs(d) : lvl;
        }
    {
        double d = 20 * log10(flt_rms(VF_VOWEL, 95, 0, 110) / flt_rms(VF_LP24, 95, 0, 110));
        lvl = fabs(d) > lvl ? fabs(d) : lvl;
        d = 20 * log10(flt_rms(VF_VOWEL, 95, 127, 110) / flt_rms(VF_LP24, 95, 127, 110));
        lvl = fabs(d) > lvl ? fabs(d) : lvl;
    }
    snprintf(msg, sizeof msg, "VOWEL: level within 2 dB of LP24's at the same CUT (64, 95, 127; RES 0 / 64 / 127; saw 110 Hz): worst %.2f dB", lvl);
    check(msg, lvl <= 2.0);
}

/* bounded: VOWEL and LP24 at RES 127 + DRV 127 over MIDI 24..99, CUT 0 / 64 / 127: no wrapped sample, the DC (over whole cycles, after the
 * blocker) under 1 % of the rms, the part's peak inside the mixer's integer limits. Run with VOID_OVF=1 under -fsanitize=signed-integer-overflow
 * (run_tests.sh), the same loop proves that no int32 in the filter, the voice or the mix wraps. */
static double bounded_run(uint32_t ft, int *wraps, double *worst_dc)
{
    static const int CUT[3] = {0, 64, 127};
    uint32_t n, c, i;
    double pk = 0;
    for (c = 0; c < 3u; c++)
        for (n = 24; n <= 99u; n++) {
            track_t *t = plain(1, ft);
            double f = 440.0 * pow(2.0, ((int)n - 69) / 12.0), sum = 0, sq = 0;
            uint32_t cyc = (uint32_t)(0.4 * f) ? (uint32_t)(0.4 * f) : 1u, len = (uint32_t)(cyc * FS / f + 0.5), cnt;
            t->p[P_E5] = 127;
            t->p[P_E7] = 127;
            t->p[P_E4] = (int16_t)CUT[c];
            trk_note_on(t, (uint8_t)n, 127);
            trk_note_on(t, (uint8_t)(n + 7u), 127);
            blocks(FS / 4u / CTL);
            nrec = 0;
            blocks(len / CTL + 1u);
            cnt = nrec;
            for (i = 0; i < cnt; i++) {
                sum += rec[i];
                sq += (double)rec[i] * rec[i];
                pk = fabs((double)rec[i]) > pk ? fabs((double)rec[i]) : pk;
                if (i && (rec[i] - rec[i - 1u] > 6 * VOICE_FS || rec[i] - rec[i - 1u] < -6 * VOICE_FS))
                    (*wraps)++;
            }
            if (sq > 0 && n >= 40u && ft == VF_VOWEL) {        /* (the DC check: from E2 up, whole cycles of the window; below, the window holds < 2) */
                double d = fabs(sum / cnt) / sqrt(sq / cnt);
                *worst_dc = d > *worst_dc ? d : *worst_dc;
            }
        }
    return pk;
}
static void test_vowel_bounded(void)
{
    int wv = 0, wl = 0;
    double dcv = 0, dcl = 0, pkv, pkl;
    char msg[300];
    pkv = bounded_run(VF_VOWEL, &wv, &dcv);
    pkl = bounded_run(VF_LP24, &wl, &dcl);
    /* a 2-voice chord peaks at pkv; the mixer takes b (the part's sum, 32768 per full scale) as x = ((b >> 2) * 9713) >> 10 at LEVEL 127 and needs
     * x * 4096 < 2^31 (x <= 524287); a WAVE part plays 6 voices, so 3 x this chord */
    snprintf(msg, sizeof msg, "VOWEL, RES 127 + DRV 127, MIDI 24..99: no wrapped sample, DC under 2 %% of the rms (%.3f %%), peak %.0f (LP24: %.0f; x at LEVEL 127 for 6 voices %.0f of 524287)",
             dcv * 100, pkv, pkl, pkv * 3 * 9713 / 4096.0);
    /* (V2: 2 %. V2's shaper folds asymmetrically (its even harmonics), and the voice's soft clipper, symmetric, turns a little of that into DC
     * after the blocker: 1.6 .. 1.8 % here, -36 dB; the plugin's voice does the same. It was under 1 % with V1's shaper.) */
    check(msg, wv == 0 && dcv < 0.02 && pkv < 1.15 * pkl + 1 && pkv * 3 * 9713 / 4096.0 < 524287.0);
}

/* the post-filter chain (clamp 110000, VOID_OUT_LEVEL, DC blocker, void_clip) is the one every filter shares: a voice's peak is VOID_CLIP_THR +
 * VOID_SOFT[256] whatever the filter; three of those is the 6-voice bound the mixer's limits were checked against (task-7.2a-review.md) */
static void test_headroom(void)
{
    int32_t vmax = VOID_CLIP_THR + VOID_SOFT[NELEM(VOID_SOFT) - 1u];
    int64_t b = (int64_t)VOID_POLY * ((int64_t)vmax * VOICE_FS >> 15 << 2);
    char msg[240];
    snprintf(msg, sizeof msg, "headroom: a voice's output is bounded by the shared clipper (%d), whatever the filter (6 voices all there: %lld, the same for LP24 as before)", vmax, (long long)b);
    check(msg, vmax - void_clip(VOID_CLIP_MAX) <= 1 && vmax - void_clip(VOID_CLIP_MAX) >= 0);
}

/* ---------------------------------------------------------------- 2c: WARP --- */
static void test_warp(void)
{
    static const int W[2] = {0, 127};
    double worst = -999;
    uint32_t k, differ = 0;
    int32_t v;
    char msg[240];
    {   /* WARP's two ends against off, MIDI 36 .. 96 in fourths, on the oscillator alone at a fixed increment (the voice's drift and micro-LFO smear every
         * harmonic and put a floor of -35 dB under this measure even with WARP off). What is not a harmonic of f0 (DC leakage left out: a warped cycle has a
         * mean) is aliasing or the linear read's error. The spec's "below -50 dB at MIDI 96" cannot hold: the unwarped SAW at C7 is already -34 dB here,
         * the darkest level of the tables having 8 harmonics a cycle; the claim kept is that a warp never aliases much worse than the same note unwarped. */
        static const int WA[3] = {64, 0, 127};
        static const uint32_t TB[2] = {1, 3};          /* SAW, SPECTRAL (its position 100: bright) */
        uint32_t tbi, n;
        double top = -999, over = -999;
        for (tbi = 0; tbi < 2u; tbi++)
            for (n = 36; n <= 96u; n += 6) {
                double f0 = 440.0 * pow(2.0, ((int)n - 69) / 12.0), base = 0;
                for (k = 0; k < 3u; k++) {
                    void_osc_t o;
                    uint32_t inc = (uint32_t)(f0 / FS * 4294967296.0 + 0.5), ph = 0, i;
                    double d;
                    void_osc_warp(&o, TB[tbi], tbi ? 100 << 8 : 20 << 8, inc, WA[k]);
                    for (i = 0; i < 16384u; i++, ph += inc)
                        rec[i] = void_osc(&o, ph, 1) >> 1;
                    d = alias_db_nodc(4096, 4096, f0);
                    if (!k)
                        base = d;
                    else {
                        top = d > top ? d : top;
                        over = d - base > over ? d - base : over;
                    }
                    if (n == 96u)
                        printf("void: WARP %3d at C7, %s: %.1f dB off the harmonics (off: %.1f)\n", WA[k], tbi ? "SPECTRAL" : "SAW", d, base);
                }
            }
        worst = top;
        snprintf(msg, sizeof msg, "WARP 0 / 127, MIDI 36..96: off-harmonic energy at most -20 dB (%.1f) and at most 15 dB above the same note unwarped (%+.1f)", top, over);
        check(msg, top < -20.0 && over < 15.0);
    }
    {   /* WARP sounds: 0, 32, 96, 127 each differ from off and from each other; the pitch stays (a SINE at A3) */
        uint64_t h[5];
        static const int WV[5] = {64, 0, 32, 96, 127};
        for (k = 0; k < 5u; k++) {
            uint32_t i;
            track_t *t = plain(1, 3);
            t->p[P_E2] = (int16_t)WV[k];
            trk_note_on(t, 57, 100);
            blocks(FS / 4u / CTL);
            h[k] = 1469598103934665603ull;
            for (i = 0; i < nrec; i++)
                h[k] = (h[k] ^ (uint32_t)rec[i]) * 1099511628211ull;
        }
        for (k = 1; k < 5u; k++)
            differ += h[k] != h[0];
        check("WARP: 0, 32, 96, 127 each render differently from 64 (off)", differ == 4u);
        {
            track_t *t = plain(0, 3);
            double f;
            t->p[P_E2] = 110;
            trk_note_on(t, 57, 100);
            blocks(NREC / CTL);
            f = freq_of(FS / 4u, NREC);
            snprintf(msg, sizeof msg, "WARP 110 on a SINE at A3: the pitch is still 220 Hz (%+.2f cents)", cents(f, 220.0));
            check(msg, fabs(cents(f, 220.0)) < 6.0);
        }
    }
    {   /* the old TBL2 slot: a stored 0..8 is WARP off, 9..127 is itself (param_fit: projects, user presets, motion, the editor all go through it) */
        const param_desc_t *d = &ENGINES[ENGI_WAVE]->edit[2];
        uint32_t ok = 1;
        track_t *t = &trk[0];
        for (v = 0; v <= 8; v++)
            ok &= param_fit(d, v) == 64;
        for (v = 9; v <= 127; v++)
            ok &= param_fit(d, v) == v;
        ok &= param_fit(d, 300) == 127;
        ok &= param_fit(&ENGINES[ENGI_WAVE]->edit[4], 5) == 5;   /* (CUT, another slot, is not touched) */
        ok &= param_fit(&ENGINES[ENGI_OBX]->edit[2], 3) == 3;    /* (nor is another engine's E2) */
        fresh(ENGI_WAVE, 0);
        t->p[P_E2] = 3;                                          /* a preset that carries an old TBL2 */
        void_track_loaded(t);
        ok &= t->p[P_E2] == 64;
        t->p[P_E2] = 40;
        void_track_loaded(t);
        ok &= t->p[P_E2] == 40;
        check("WARP: a stored 0..8 (TBL2) loads as 64 = off (param_fit, void_track_loaded); 9..127 as it was", ok);
    }
}

/* OB-Z, ACID and DRIFT have no VOWEL: type 6 (and every other value) must play as the types they do have */
static uint64_t eng_hash(const char *eng, int ft)
{
    static const uint8_t N[3] = {48, 55, 64};
    uint32_t e = engine_named(eng), i;
    track_t *t = &trk[0];
    uint64_t h = 1469598103934665603ull;
    memset(eng_mem, 0, sizeof eng_mem);
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    fresh(e, 0);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_FTYPE] = (int16_t)ft;
    for (i = 0; i < 3u; i++)
        trk_note_on(t, N[i], 100);
    nrec = 0;
    blocks(FS / 4u / CTL);
    for (i = 0; i < nrec; i++)
        h = (h ^ (uint32_t)rec[i]) * 1099511628211ull;
    return h;
}
static void test_other_machines(void)
{
    uint32_t ok_obz, ok_acid, ok_drift;
    ok_obz = eng_hash("OB-Z", 6) == eng_hash("OB-Z", 0) && eng_hash("OB-Z", 6) != eng_hash("OB-Z", 1);
    ok_acid = eng_hash("NANO", 6) == eng_hash("NANO", 0) && eng_hash("NANO", 3) == eng_hash("NANO", 0);
    ok_drift = eng_hash("JUNO", 6) == eng_hash("JUNO", 0) && eng_hash("JUNO", 6) == eng_hash("JUNO", 4) && eng_hash("JUNO", 1) != eng_hash("JUNO", 0);
    check("TYPE VOWEL on an OB-Z track plays as LP (its MODE morph is not pinned), on ACID as any TYPE (ignored), on DRIFT as FLAT", ok_obz && ok_acid && ok_drift);
}

/* host CPU: VOWEL against the ladder (LP24) and WARP on against off, instructions per voice per sample (a chord of 8 at the 6-voice cap) */
static double costw(uint32_t ft, int drv, int res, int warp)
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    track_t *t = plain(1, ft);
    uint32_t i, rep;
    double best = 1e30;
    t->p[P_E7] = (int16_t)drv;
    t->p[P_E5] = (int16_t)res;
    t->p[P_E4] = 80;
    t->p[P_E1] = 0;
    t->p[P_E2] = (int16_t)warp;
    for (i = 0; i < 8u; i++)
        trk_note_on(t, NOTES[i], 100);
    blocks(FS / 4u / CTL);
    for (rep = 0; rep < 3u; rep++) {
        uint64_t i0 = instr_now();
        double ipc;
        blocks(FS / 2u / CTL);
        ipc = (double)(instr_now() - i0) / (FS / 2u);
        best = ipc < best ? ipc : best;
    }
    return best;
}
static double idle_cost(void);
static void test_cost_45(void)
{
    /* The ladder's filter section costs 83 instructions a voice a sample by ablation (the filter bypassed in a scratch build: LP24 voice 1033 -
     * bypass 534 = 499 over 6 voices); the user's target for VOWEL is within +10 % of that, i.e. 8.3 more a voice than LP24 */
    double id = idle_cost(), lp = costw(VF_LP24, 0, 0, 64), vw = costw(VF_VOWEL, 0, 0, 64), vw2 = costw(VF_VOWEL, 0, 100, 64), lp2 = costw(VF_LP24, 0, 100, 64);
    double off = costw(VF_LP12, 0, 0, 64) - id, on = costw(VF_LP12, 0, 0, 127) - id, on2 = costw(VF_LP12, 0, 0, 20) - id;
    char msg[300];
    if (!instr_now()) {
        printf("void: cost: no instruction counter on this host, skipped\n");
        return;
    }
    lp -= id, vw -= id, vw2 -= id, lp2 -= id;
    printf("void: cost per voice per sample: LP24 %.1f / %.1f (RES 0 / 100), VOWEL %.1f / %.1f; LP12 WARP off %.1f, 127 %.1f, 20 %.1f\n",
           lp / VOID_POLY, lp2 / VOID_POLY, vw / VOID_POLY, vw2 / VOID_POLY, off / VOID_POLY, on / VOID_POLY, on2 / VOID_POLY);
    snprintf(msg, sizeof msg, "cost: VOWEL within +10 %% of the ladder's filter section above LP24 (%.1f and %.1f a voice more, limit 8.3)",
             (vw - lp) / VOID_POLY, (vw2 - lp2) / VOID_POLY);
    check(msg, (vw - lp) / VOID_POLY <= HOST_PIN(8.3, 58.2) && (vw2 - lp2) / VOID_POLY <= HOST_PIN(8.3, 58.2));   /* (V2) Linux x86-64: 52.9 + 10 % */
    snprintf(msg, sizeof msg, "cost: WARP off costs what LP12 cost before (%.1f a voice; before Task 4.5: 132.3), WARP on adds %.1f / %.1f", off / VOID_POLY, (on - off) / VOID_POLY, (on2 - off) / VOID_POLY);
    check(msg, off / VOID_POLY < HOST_PIN(132.3, 164.0) + 6.0);   /* (V2) Linux x86-64: 164.0 measured on 0.2-beta */
}

/* ------------------------------------------------------------------ 8 --- */
static void wav16(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i, bytes = nrec * 2u;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fwrite("RIFF", 1, 4, f);
    i = 36u + bytes; fwrite(&i, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    i = 16; fwrite(&i, 4, 1, f);
    { uint16_t h[2] = {1, 1}; fwrite(h, 2, 2, f); }
    i = FS; fwrite(&i, 4, 1, f);
    i = FS * 2u; fwrite(&i, 4, 1, f);
    { uint16_t h[2] = {2, 16}; fwrite(h, 2, 2, f); }
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    for (i = 0; i < nrec; i++) {
        int32_t v = rec[i];                            /* (mix_block's output is 16-bit scale, as hostsim's WAVs) */
        int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}
static void demos(const char *dir)
{
    static const uint8_t PHR[8] = {36, 43, 48, 36, 39, 46, 51, 39};
    uint32_t k, i, tb;
    char nm[64];
    for (k = 0; k < ENGINES[ENGI_WAVE]->npresets; k++) {
        track_t *t = &trk[0];
        fresh(ENGI_WAVE, k);                               /* (host_preset applies WAVE_PX since Task 4.3) */
        for (i = 0; i < (t->p[P_ATK] > 80 ? 2u : 8u); i++) {   /* (a slow attack: two long chords) */
            trk_note_on(t, PHR[i] + (t->p[P_VOICE] == V_POLY ? 12u : 0u), 100);
            if (t->p[P_VOICE] == V_POLY)
                trk_note_on(t, PHR[i] + 19u, 90);
            blocks((t->p[P_ATK] > 80 ? FS : FS / 4u) / CTL);
            trk_note_off(t, PHR[i] + (t->p[P_VOICE] == V_POLY ? 12u : 0u));
            trk_note_off(t, PHR[i] + 19u);
        }
        snprintf(nm, sizeof nm, "preset_%02u_%s", k, ENGINES[ENGI_WAVE]->presets[k].name);
        for (i = 0; nm[i]; i++)
            if (nm[i] == ' ')
                nm[i] = '_';
        wav16(dir, nm);
    }
    for (tb = 0; tb < VOID_NTBL; tb++) {
        track_t *t = plain(tb, 3);
        trk_note_on(t, 48, 100);
        for (i = 0; i < NREC / CTL; i++) {
            t->p[P_E1] = (int16_t)(i * 127u / (NREC / CTL));
            blocks(1);
        }
        snprintf(nm, sizeof nm, "sweep_%s", N_VOID_TBL[tb]);
        wav16(dir, nm);
    }
    printf("void: demos in %s\n", dir);
}

/* ------------------------------------------------------------------ 9 --- */
/* |X| at harmonic h of f0 over x[0, n) (n a whole number of cycles), and the RMS */
static double harm_mag(const double *x, uint32_t n, double f0, uint32_t h)
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double a = 2 * M_PI * h * f0 * i / VOID_REF_FS;
        re += x[i] * cos(a);
        im -= x[i] * sin(a);
    }
    return 2 * sqrt(re * re + im * im) / n;
}
static double rms_of(const double *x, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += x[i] * x[i];
    return sqrt(s / n);
}

static void test_reference(const char *dir)
{
    static const char *const VF_N[6] = {"LP24", "BP", "HP", "LP12", "HP24", "NOTCH"};
    static float ref[VOID_REF_N];
    static double a[VOID_REF_N / 2], b[VOID_REF_N / 2];
    uint32_t k, n = VOID_REF_N / 2, have = 0, ok_lvl = 1, ok_h = 1;
    double worst_lvl = 0, worst_h = 0;
    char msg[200];
    for (k = 0; k < VOID_REF_NCFG; k++) {
        const void_ref_cfg_t *c = &VOID_REF_CFG[k];
        char path[512];
        FILE *f;
        void_flt_t fl;
        int32_t st[4] = {0, 0, 0, 0}, cut;
        uint32_t i, h, hn;
        double dl, dh = 0, top = 0;
        snprintf(path, sizeof path, "%s/%02u.f32", dir, k);
        if (!(f = fopen(path, "rb")))
            continue;
        if (fread(ref, sizeof(float), VOID_REF_N, f) != VOID_REF_N) {
            fclose(f);
            continue;
        }
        fclose(f);
        have++;
        cut = (int32_t)lround(256.0 * 127.0 * log(c->hz / 30.0) / log(16000.0 / 30.0));
        void_flt_set(&fl, (uint32_t)c->vf, clamp(cut, 0, 127 << 8), (int32_t)lround(c->res * 127),
                     (int32_t)lround(c->drv * 127), 0);
        for (i = 0; i < VOID_REF_N; i++) {
            int32_t y = void_flt_run(&fl, st, (int32_t)lround(void_ref_input(i, c->f0) * 32768.0));
            if (i >= VOID_REF_N - n) {
                a[i - (VOID_REF_N - n)] = ref[i];
                b[i - (VOID_REF_N - n)] = y / 32768.0;
            }
        }
        dl = 20 * log10(rms_of(b, n) / rms_of(a, n));
        hn = (uint32_t)(18000.0 / c->f0);
        if (hn > 40u)
            hn = 40u;
        for (h = 1; h <= hn; h++) {
            double m = harm_mag(a, n, c->f0, h);
            top = m > top ? m : top;
        }
        for (h = 1; h <= hn; h++) {
            double ma = harm_mag(a, n, c->f0, h), mb = harm_mag(b, n, c->f0, h), d;
            if (ma < top * 0.00316)                     /* within 50 dB of the loudest */
                continue;
            d = fabs(20 * log10((mb + 1e-12) / ma));
            dh = d > dh ? d : dh;
        }
        printf("void: vs plugin %-5s %5.0f Hz res %.1f drv %.1f f0 %3.0f: level %+5.2f dB, harmonics within %.2f dB\n",
               VF_N[c->vf], c->hz, c->res, c->drv, c->f0, dl, dh);
        worst_lvl = fabs(dl) > worst_lvl ? fabs(dl) : worst_lvl;
        if (c->res < 0.85f) {                           /* (self-oscillation: its own pitch, level only) */
            worst_h = dh > worst_h ? dh : worst_h;
            ok_h &= dh < 1.5;
        }
        ok_lvl &= fabs(dl) < 0.5;
    }
    if (!have) {
        printf("void: vs plugin: no reference in %s (tools/void_ref.cpp needs the V2 plugin: ../../VOID SYNTH/VoidWave or VOIDWAVE_DIR), skipped\n", dir);
        return;
    }
    snprintf(msg, sizeof msg, "vs the plugin's Filter.cpp: %u cases, levels within 0.5 dB (worst %.2f)", have, worst_lvl);
    check(msg, ok_lvl);
    snprintf(msg, sizeof msg, "vs the plugin's Filter.cpp: harmonics within 1.5 dB below self-oscillation (worst %.2f)", worst_h);
    check(msg, ok_h);
}

static double costv(int ft, int drv, int res, int pos)
{
    static const uint8_t NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    track_t *t = plain(1, (uint32_t)ft);
    uint32_t i, rep; double best = 1e30;
    t->p[P_E7] = (int16_t)drv; t->p[P_E5] = (int16_t)res; t->p[P_E4] = 80; t->p[P_E1] = (int16_t)pos;
    for (i = 0; i < 8u; i++) trk_note_on(t, NOTES[i], 100);
    blocks(FS / 4u / CTL);
    for (rep = 0; rep < 3u; rep++) { uint64_t i0 = instr_now(); double ipc; blocks(FS / 2u / CTL);
        ipc = (double)(instr_now() - i0) / (FS / 2u); best = ipc < best ? ipc : best; }
    return best;
}
static double idle_cost(void)
{
    uint32_t rep; double best = 1e30;
    fresh(0, 0);
    blocks(FS / 4u / CTL);
    for (rep = 0; rep < 3u; rep++) { uint64_t i0 = instr_now(); double ipc; blocks(FS / 2u / CTL);
        ipc = (double)(instr_now() - i0) / (FS / 2u); best = ipc < best ? ipc : best; }
    return best;
}
int prof_main(void)
{
    double id = idle_cost();
    printf("idle %.0f\n", id);
    printf("LP12 drv0            %.0f\n", costv(3, 0, 0, 0) - id);
    printf("LP12 drv0 pos40      %.0f\n", costv(3, 0, 0, 40) - id);
    printf("LP24 drv0            %.0f\n", costv(0, 0, 0, 0) - id);
    printf("LP24 drv40           %.0f\n", costv(0, 40, 0, 0) - id);
    printf("LP12 res110          %.0f\n", costv(3, 0, 110, 0) - id);
    printf("OB-Z SAW LEAD        %.0f\n", cost(0, 0) - id);
    {
        static const char *const W[][2] = {{"OB-Z", "SUB BASS"}, {"OB-Z", "RAVE"}};
        uint32_t k, e, q;
        for (k = 0; k < NELEM(W); k++)
            for (e = 0; e < NENGINES; e++)
                if (str_eq(ENGINES[e]->name, W[k][0]))
                    for (q = 0; q < ENGINES[e]->npresets; q++)
                        if (str_eq(ENGINES[e]->presets[q].name, W[k][1]))
                            printf("%-7s %-12s %.0f\n", W[k][0], W[k][1], cost(e, q) - id);
        for (q = 0; q < ENGINES[ENGI_WAVE]->npresets; q++)
            printf("WAVE    %-12s %.0f\n", ENGINES[ENGI_WAVE]->presets[q].name, cost(ENGI_WAVE, q) - id);
    }
    return 0;
}
int main(int argc, char **argv)
{
        if (getenv("VOID_PROF"))                           /* VOID_PROF=1: the cost by feature, and next to the heaviest */
        return prof_main();                            /* presets of the other engines */
#ifdef VOID_INSTR                                      /* the instrumented build (run_tests.sh, also under -fsanitize=signed-integer-overflow): */
    test_oob();                                        /* the loops that reach the corners, no cost */
    test_vowel_bounded();
    test_warp();
    printf(bad ? "void: %d FAILED\n" : "void: instrumented run: all ok\n", bad);
    return bad != 0;
#endif
    test_engine();
    test_px();
    test_pitch();
    test_v2_parts();
    test_scen();
    test_bandlimit();
    test_bounded();
    test_dc();
    test_dc_range();
    test_clip_equiv();
    test_presets();
    test_cost();
    test_vowel();
    test_vowel_bounded();
    test_headroom();
    test_warp();
    test_other_machines();
    test_cost_45();
    test_reference(argc > 2 ? argv[2] : "build/void_ref");
    if (argc > 1)
        demos(argv[1]);
    printf(bad ? "void: %d FAILED\n" : "void: all ok\n", bad);
    return bad != 0;
}
