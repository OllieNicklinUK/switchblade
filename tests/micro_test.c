/* SPDX-License-Identifier: GPL-3.0-only
 * MICRO (V2 Phase 4, eng_micro.c): the Microwave's stepped, gritty character. The tables (16, 8 keyframes each, 8-bit,
 * at full scale), 64 waves with no crossfade (inside a step nothing changes, across one it does), the table and POS,
 * OSC2's detune, GRIT, EPOS sweeping the waves over the note, the filter and key tracking. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("micro: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define NB 240u
typedef struct { double rms, bright, early, late; uint64_t h; } meas_t;
static int32_t L[NB * CTL];

static meas_t play(const int16_t *e, uint32_t note)
{
    meas_t r = {0, 0, 0, 0, 1469598103934665603ull};
    uint32_t b, i, n = 0;
    double s = 0, d = 0, e1 = 0, e2 = 0, d1 = 0, d2 = 0;
    host_tracks_init();
    memset(trk[0].v, 0, sizeof trk[0].v);
    trk[0].rr = 0;
    lim_env = LIM_T;
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = dc_l = dc_r = dce_l = dce_r = 0;
    host_preset(&trk[0], ENGI_MICRO, 0);
    trk[0].p[P_LEVEL] = 100;
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_ATK] = 0; trk[0].p[P_DEC] = 90; trk[0].p[P_SUS] = 40; trk[0].p[P_REL] = 30;   /* (an envelope that moves) */
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = e[i];
    trk_note_on(&trk[0], note, 100);
    for (b = 0; b < NB; b++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            L[b * CTL + i] = o[2 * i];
            r.h = (r.h ^ (uint32_t)o[2 * i]) * 1099511628211ull;
        }
    }
    trk_note_off(&trk[0], note);
    for (i = 1; i < NB * CTL; i++) {
        double x = L[i], dx = L[i] - L[i - 1u];
        if (i < NB * CTL / 4u) e1 += x * x, d1 += dx * dx;
        else if (i >= NB * CTL * 3u / 4u) e2 += x * x, d2 += dx * dx;
        if (i >= NB * CTL / 3u) s += x * x, d += dx * dx, n++;
    }
    r.rms = sqrt(s / n);
    r.bright = r.rms > 0 ? sqrt(d / n) / r.rms : 0;
    r.early = e1 > 0 ? sqrt(d1 / e1) : 0;
    r.late = e2 > 0 ? sqrt(d2 / e2) : 0;
    return r;
}

int main(void)
{
    int16_t e[8] = {0, 64, 0, 0, 127, 0, 0, 0};       /* WT UPPER, POS 64, no sweep, one osc, open, no grit, no KTR */
    meas_t a, b, c;
    uint32_t t, k, i, distinct = 0;
    setvbuf(stdout, NULL, _IONBF, 0);

    /* the tables */
    {
        int ok = 1;
        for (t = 0; t < MW_NT; t++)
            for (k = 0; k < MW_NK; k++) {
                int pk = 0;
                for (i = 0; i < MW_N; i++) pk = abs(MW_WT[t][k][i]) > pk ? abs(MW_WT[t][k][i]) : pk;
                ok &= pk >= 126;
            }
        for (t = 0; t < MW_NT; t++)
            for (k = 0; k < t; k++)
                distinct += !memcmp(MW_WT[t], MW_WT[k], sizeof MW_WT[t]);
        check("16 tables of 8 keyframes, each at full 8-bit scale, no two tables the same", ok && !distinct && MW_NT == 16);
    }
    /* 64 waves, stepped: inside a step the same sound, across one another */
    e[1] = 64; a = play(e, 48);
    e[1] = 65; b = play(e, 48);                         /* POS 64 and 65: the same of the 64 waves (127 / 63 per wave) */
    e[1] = 72; c = play(e, 48);                         /* .. 72: the next waves */
    check("64 waves, no crossfade: POS inside one wave's step sounds the same (bit for bit), across steps not", a.h == b.h && a.h != c.h);
    /* the table */
    e[1] = 64; e[0] = 0; a = play(e, 48); e[0] = 10; b = play(e, 48);
    check("WT: another table is another sound", a.h != b.h && fabs(a.bright - b.bright) > 0.02 * a.bright);
    /* OSC2 */
    e[0] = 0; e[3] = 0; a = play(e, 48); e[3] = 40; b = play(e, 48);
    check("OSC2: the second oscillator, detuned, changes the sound (0: one oscillator)", fabs(a.rms - b.rms) > 0.02 * a.rms);
    /* GRIT */
    e[3] = 0; e[0] = 4; e[1] = 0; e[6] = 0; a = play(e, 48); e[6] = 127; b = play(e, 48);   /* BASIC sine: grit shows */
    printf("micro:   GRIT 0 brightness %.4f, GRIT 127 %.4f\n", a.bright, b.bright);
    check("GRIT: fewer bits and the hold add grit (a sine gets brighter)", b.bright > a.bright * 1.5);
    /* EPOS: the envelope sweeps the waves over the note */
    e[0] = 0; e[1] = 0; e[6] = 0; e[2] = 63; a = play(e, 48);
    e[2] = 0; b = play(e, 48);
    printf("micro:   EPOS 63: early %.4f late %.4f; EPOS 0: early %.4f late %.4f\n", a.early, a.late, b.early, b.late);
    check("EPOS: the envelope sweeps the table (the sound changes as it decays); 0: it stays",
          fabs(a.early - a.late) > 3.0 * fabs(b.early - b.late) && fabs(a.early - a.late) > 0.01);
    /* the filter */
    e[2] = 0; e[1] = 64; e[4] = 40; a = play(e, 48); e[4] = 127; b = play(e, 48);
    check("CUT: 40 darker than 127 (the 24 dB low-pass)", a.bright < b.bright * 0.7);
    e[4] = 60; e[7] = 127; a = play(e, 84); e[7] = 0; b = play(e, 84);
    check("KTR: a high note brighter with key tracking", a.bright > b.bright);
    e[4] = 60; e[5] = 127; e[7] = 0;
    {
        int ok = 1;
        for (k = 0; k <= 127u; k += 32u) {
            e[4] = (int16_t)k;
            a = play(e, 36);
            ok &= a.rms == a.rms && a.rms < 30000;
        }
        check("RES 127: stable at every CUT", ok);
    }
    printf(bad ? "micro: %d FAILED\n" : "micro: all passed\n", bad);
    return bad != 0;
}
