/* SPDX-License-Identifier: GPL-3.0-only
 * CZ-Z (V2 Phase 1): what tests/cz_ref.cpp cannot hold to MAME, because it is Switch Blade's and not the chip's
 * (eng_cz.c): the four LINE modes, the DCW from CUT / DCWE / KEYF, the 8-step DCW envelope (its steps, SUS while
 * held, the steps after it on release, END), RING and NOISE, the preset's envelope and oct2 in the part's slots, and
 * the output (no DC, nothing silent). Brightness is measured as the first difference's RMS over the signal's RMS. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("cz: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define NB 300u                                        /* ~0.22 s */
typedef struct { double rms, bright, dc; } meas_t;
static int32_t buf[NB * CTL];

/* part 1 on CZ-Z preset pi with E overrides (-999: the preset's), one note for NB blocks; the last two thirds measured */
/* The scenarios below were written on the first voicing's CZ BASS and CZ BRASS. Since 2026-11-19 the bank is the CZ-230S's tones,
 * so those two are kept here as they were (their edits, ADSR and 8-step DCW envelope in the part's slots): the checks measure what
 * they always did. Every factory preset is still played as itself where the test loops over all of them. */
#define CZ_OLD_BASS 1000u
#define CZ_OLD_BRASS 1001u
static void cz_load(uint32_t pi)
{
    static const int16_t E[2][8] = {{0, 0, 0, 0, 10, 0, 0, 110}, {0, 0, 2, 8, 10, 20, 0, 100}};
    static const uint8_t ENV[2][4] = {{0, 60, 80, 20}, {8, 60, 90, 30}};
    static const uint8_t PX[2][19] = {{99, 55, 0, 0, 0, 0, 0, 0, 99, 30, 0, 0, 0, 0, 0, 0, 1, 1, 2},
                                      {70, 40, 50, 0, 0, 0, 0, 0, 99, 60, 0, 0, 0, 0, 0, 0, 1, 2, 2}};
    track_t *t = &trk[0];
    uint32_t i, w;
    if (pi < CZ_OLD_BASS) {
        host_preset(t, ENGI_CZZ, pi);
        return;
    }
    w = pi - CZ_OLD_BASS;
    host_preset(t, ENGI_CZZ, 0);
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = E[w][i];
    t->p[P_ATK] = ENV[w][0], t->p[P_DEC] = ENV[w][1], t->p[P_SUS] = ENV[w][2], t->p[P_REL] = ENV[w][3];
    t->p[P_ED_FLT] = 0;
    t->p[P_VOICE] = V_LEGATO;
    for (i = 0; i < 19u; i++)
        t->p[CZ_ENV + i] = PX[w][i];
    t->p[CZ_ENV_OK] = 1;
}

static meas_t play(uint32_t pi, const int16_t *e, uint32_t note, uint32_t gate_blocks)
{
    meas_t r = {0, 0, 0};
    uint32_t b, i, n = 0;
    double s = 0, d = 0, m = 0;
    host_tracks_init();
    memset(trk[0].v, 0, sizeof trk[0].v);              /* (silent voices: the last play's tail would be retriggered) */
    lim_env = LIM_T;                                   /* (the master's limiter and DC filter as at power-on) */
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = dc_l = dc_r = dce_l = dce_r = 0;
    cz_load(pi);
    trk[0].p[P_LEVEL] = 100;
    if (e && e[0] == -998)                             /* (the envelope held at 99: DCWE's full reach) */
        trk[0].p[CZ_ENV + 8] = trk[0].p[CZ_ENV + 9] = 99;
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;   /* (the dry voice) */
    for (i = 0; e && i < 8u; i++)
        if (e[i] != -999)
            trk[0].p[P_E0 + i] = e[i];
    trk_note_on(&trk[0], note, 100);
    for (b = 0; b < NB; b++) {
        int32_t o[2 * CTL];
        if (b == gate_blocks)
            trk_note_off(&trk[0], note);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            buf[b * CTL + i] = o[2 * i];
    }
    if (gate_blocks >= NB)
        trk_note_off(&trk[0], note);                   /* (let go: a part's held keys outlive host_tracks_init) */
    for (i = (gate_blocks >= NB ? NB * CTL / 3u : 1u); i < NB * CTL; i++) {   /* (a note released early: all of it) */
        s += (double)buf[i] * buf[i];
        d += (double)(buf[i] - buf[i - 1u]) * (buf[i] - buf[i - 1u]);
        m += buf[i];
        n++;
    }
    r.rms = sqrt(s / n);
    r.bright = r.rms > 0 ? sqrt(d / n) / r.rms : 0;
    r.dc = m / n;
    return r;
}

static uint32_t preset_named(const char *name)
{
    uint32_t i;
    for (i = 0; i < ENGINES[ENGI_CZZ]->npresets; i++)
        if (!strcmp(ENGINES[ENGI_CZZ]->presets[i].name, name))
            return i;
    printf("cz: no preset %s\n", name);
    bad++;
    return 0;
}

int main(void)
{
    uint32_t bass = CZ_OLD_BASS, k;                      /* (the first voicing's CZ BASS: cz_load) */
    int16_t e[8];
    meas_t a, b, c;
    setvbuf(stdout, NULL, _IONBF, 0);

    /* the DCW: CUT 0 with no envelope is the chip's plain cosine (dull), CUT up is brighter; DCWE adds it */
    for (k = 0; k < 8u; k++) e[k] = -999;
    e[0] = 0; e[2] = 0; e[6] = 0; e[7] = 0; e[5] = 0;
    e[4] = 0; a = play(bass, e, 48, NB);
    e[4] = 100; b = play(bass, e, 48, NB);
    printf("cz:   SAW, line 1: CUT 0 brightness %.3f, CUT 100 %.3f\n", a.bright, b.bright);
    check("DCW: CUT 0 (no envelope) a near-sine; CUT 100 brighter (the phase bent: the CZ's soft saw)", a.rms > 100 && b.bright > a.bright * 1.3);
    e[4] = 0; e[7] = 127;
    {
        int16_t e99[8];
        memcpy(e99, e, sizeof e99);
        e99[0] = -998;                                 /* (WAV1 stays the preset's: SAW) */
        c = play(bass, e99, 48, NB);
    }
    printf("cz:   DCWE 127 (CUT 0, the envelope held at 99): brightness %.3f\n", c.bright);
    check("DCWE: the 8-step DCW envelope opens the DCW (the preset's, sustained)", c.bright > a.bright * 1.1);
    e[4] = 100; e[7] = 0; e[5] = 127; a = play(bass, e, 84, NB); e[5] = 0; b = play(bass, e, 84, NB);
    check("KEYF: a high note darker with key follow", a.bright < b.bright);

    /* the modes: LINE 2 alone plays WAV2; 1+1' and 1+2' two lines; DTN beats */
    for (k = 0; k < 8u; k++) e[k] = -999;
    e[0] = 0; e[1] = 3; e[4] = 80; e[6] = 0; e[3] = 0; e[7] = 0;
    e[2] = 0; a = play(bass, e, 48, NB);
    e[2] = 1; b = play(bass, e, 48, NB);
    check("LINE 2 alone: WAV2's sound (double sine), not WAV1's (saw)", fabs(a.bright - b.bright) > a.bright * 0.1 && b.rms > 100);
    e[2] = 3; c = play(bass, e, 48, NB);
    check("LINE 1+2': both lines (neither alone)", fabs(c.bright - a.bright) > a.bright * 0.03 && fabs(c.bright - b.bright) > b.bright * 0.03 && c.rms > 100);
    e[2] = 2; e[3] = 0; a = play(bass, e, 48, NB); e[3] = 60; b = play(bass, e, 48, NB);
    check("LINE 1+1': DTN detunes line 2 (the sound changes)", fabs(a.rms - b.rms) > a.rms * 0.02);
    e[2] = 3; e[3] = 0; e[6] = 0; a = play(bass, e, 48, NB);
    e[6] = 1; b = play(bass, e, 48, NB);
    e[6] = 2; c = play(bass, e, 48, NB);
    check("MOD: RING and NOISE each change the sound of 1+2'", fabs(a.rms - b.rms) > a.rms * 0.05 && fabs(c.bright - a.bright) > a.bright * 0.1);
    {
        meas_t c2 = play(bass, e, 48, NB);
        check("  NOISE is the same every time (the voice's own generator, seeded)", c2.rms == c.rms && c2.bright == c.bright);
    }

    /* the 8-step envelope: steps, SUS held, released on, END */
    {
        voice_t *v;
        uint32_t i;
        host_tracks_init();
        cz_load(bass);                                   /* CZ BASS: rates 99 55, levels 99 30, SUS 1, END 1 */
        trk[0].p[P_REL] = 127;                           /* (a long amp release: the voice outlives the envelope's) */
        check("the preset's envelope in the part's slots (CZ_PX -> FM1..FM4), marked", trk[0].p[CZ_ENV_OK] == 1 &&
              trk[0].p[CZ_ENV] == 99 && trk[0].p[CZ_ENV + 8] == 99 && trk[0].p[CZ_ENV + 9] == 30 && trk[0].p[CZ_ENV + 16] == 1);
        trk_note_on(&trk[0], 48, 100);
        v = &trk[0].v[0];
        for (i = 0; i < 2000u; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
        check("  held: it settles on SUS (step 1, level 30) and stays there", v->s[1] == 1 && v->s[0] == 30 << 16);
        trk_note_off(&trk[0], 48);
        for (i = 0; i < 5u; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
        if (!(v->s[1] >= 2 && v->s[0] == 30 << 16))
            printf("cz:   (after the release: active %u gate %u stage %u step %d level %d)\n", v->active, v->gate, v->stage, v->s[1], v->s[0]);
        check("  released: on past SUS (END is SUS here: it holds its level)", v->s[1] >= 2 && v->s[0] == 30 << 16);
        cz_load(CZ_OLD_BRASS);                           /* CZ BRASS: rates 70 40 50, levels 99 60 0, SUS 1, END 2 */
        trk[0].p[P_REL] = 127;
        trk_note_on(&trk[0], 48, 100);
        for (i = 0; i < 2000u; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
        k = (uint32_t)v->s[0] >> 16;
        trk_note_off(&trk[0], 48);
        for (i = 0; i < 3000u; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
        if (!(k == 60u && v->s[0] == 0))
            printf("cz:   (CZ BRASS: held %u, then active %u step %d level %d)\n", k, v->active, v->s[1], v->s[0]);
        check("  CZ BRASS: held at 60, released down to END's level 0", k == 60u && v->s[0] == 0);
        host_preset(&trk[0], ENGI_OBX, 0);
        check("another machine's preset: the slots back to their defaults (no CZ envelope)", trk[0].p[CZ_ENV_OK] != 1);
    }

    /* every preset: sound, no DC, the eight steps in range */
    {
        uint32_t pi, quiet = 0, dc = 0;
        for (pi = 0; pi < ENGINES[ENGI_CZZ]->npresets; pi++) {
            a = play(pi, 0, 48, NB - 1u);
            quiet += a.rms < 50;
            dc += fabs(a.dc) > 300;
            if (a.rms < 50 || fabs(a.dc) > 300)
                printf("cz:   %s: rms %.0f dc %.0f\n", ENGINES[ENGI_CZZ]->presets[pi].name, a.rms, a.dc);
        }
        check("every CZ-Z preset sounds (dry, C3) and none sits off zero (the DC blocker)", !quiet && !dc);
    }
    printf(bad ? "cz: %d FAILED\n" : "cz: all passed\n", bad);
    return bad != 0;
}
