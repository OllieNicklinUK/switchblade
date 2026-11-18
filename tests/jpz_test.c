/* SPDX-License-Identifier: GPL-3.0-only
 * JP-Z (V2 Phase 3, eng_jpz.c): the Super Saw does what the published ESP2 analysis says (the detune law and knob curve,
 * the 25 / 128 centre and MIX / 128 sides, free-running phases), and the machine around it works (BAL, the filter types,
 * RES stable, KTR, the UNISON cap, WID's stereo, the seeding after a machine switch). Whether those published numbers
 * are the JP-8000's is for the JE-8086 renders the spec plans (needs the JP's OS files): not checked here. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("jpz: %-98s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define NB 300u
typedef struct { double rms, bright, side, peak, zc; } meas_t;
static int32_t L[NB * CTL], R[NB * CTL];

static void fresh(void)
{
    host_tracks_init();
    memset(trk[0].v, 0, sizeof trk[0].v);
    trk[0].rr = 0;                         /* (the note on voice 0 every time: its seven phases are voice 0's) */
    lim_env = LIM_T;
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = dc_l = dc_r = dce_l = dce_r = 0;
}
/* E overrides: -999 keeps the preset's */
static meas_t play(uint32_t pi, const int16_t *e, int ftype, uint32_t note)
{
    meas_t r = {0, 0, 0, 0, 0};
    uint32_t b, i, n = 0;
    double s = 0, d = 0, w = 0;
    fresh();
    host_preset(&trk[0], ENGI_JPZ, pi);
    jpz_mem(&trk[0])->seeded = 0;          /* (the same seven phases every play: how they line up makes the sound, so
                                           * two settings compare only from the same start) */
    trk[0].p[P_LEVEL] = 100;
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    if (ftype >= 0)
        trk[0].p[P_FTYPE] = (int16_t)ftype;
    for (i = 0; e && i < 8u; i++)
        if (e[i] != -999)
            trk[0].p[P_E0 + i] = e[i];
    trk_note_on(&trk[0], note, 100);
    for (b = 0; b < NB; b++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            L[b * CTL + i] = o[2 * i], R[b * CTL + i] = o[2 * i + 1];
    }
    trk_note_off(&trk[0], note);
    for (i = 0; i < NB * CTL / 3u; i++)
        if (abs(L[i]) > r.peak) r.peak = abs(L[i]);    /* (the peak: the whole note, its attack too) */
    for (i = NB * CTL / 3u; i < NB * CTL; i++) {
        int32_t m = (L[i] + R[i]) / 2, dm = m - (L[i - 1u] + R[i - 1u]) / 2;
        s += (double)m * m;
        d += (double)dm * dm;
        w += (double)(L[i] - R[i]) * (L[i] - R[i]);
        if (abs(L[i]) > r.peak) r.peak = abs(L[i]);
        r.zc += (L[i] >= 0) != (L[i - 1u] >= 0);
        n++;
    }
    r.rms = sqrt(s / n);
    r.bright = r.rms > 0 ? sqrt(d / n) / r.rms : 0;
    r.side = r.rms > 0 ? sqrt(w / n) / r.rms : 0;
    return r;
}

int main(void)
{
    int16_t e[8];
    uint32_t k, i;
    meas_t a, b, c;
    setvbuf(stdout, NULL, _IONBF, 0);

    /* the detune law: the seven increments of one block, from the voice's own phases */
    {
        static const double DT[7] = {0, 128, -128, 408, -412, 704, -720};
        int ok = 1;
        uint32_t dt;
        for (dt = 0; dt < 128u; dt += 9u) {
            jpz_mem_t *mm;
            uint32_t p0[7], q, vi;
            double base, want, got;
            fresh();
            host_preset(&trk[0], ENGI_JPZ, 0);
            trk[0].p[P_E0] = (int16_t)dt;
            trk[0].p[P_VOICE] = V_POLY;
            trk_note_on(&trk[0], 60, 100);
            { int32_t o[2 * CTL]; mix_block(o, CTL); }
            mm = jpz_mem(&trk[0]);
            for (vi = 0; vi < NVOICE && !trk[0].v[vi].active; vi++)
                ;
            for (q = 0; q < 7u; q++) p0[q] = mm->ph[vi][q];
            { int32_t o[2 * CTL]; mix_block(o, CTL); }
            base = (double)(uint32_t)(mm->ph[vi][0] - p0[0]);
            for (q = 1; q < 7u; q++) {
                got = (double)(uint32_t)(mm->ph[vi][q] - p0[q]) / base;
                want = 1.0 + DT[q] * JPZ_DTN[dt] * 512.0 / 1073741824.0;
                ok &= fabs(1200.0 * log2(got / want)) < 0.1;
            }
            trk_note_off(&trk[0], 60);
        }
        check("detune: every side saw's increment as the microcode's law (DT x knob curve x 512 >> 30) within 0.1 cent", ok);
        check("  the knob curve: 0 at 0, rising, 32 at 64, 128 at 120, steepest at the top (the MCU's steps)",
              JPZ_DTN[0] == 0 && JPZ_DTN[64] == 32 && JPZ_DTN[120] == 128 && JPZ_DTN[126] - JPZ_DTN[125] == 96);
    }

    /* MIX: the sides in or out; the level kept by the normalisation */
    for (k = 0; k < 8u; k++) e[k] = -999;
    e[2] = -64; e[4] = 127; e[5] = 0; e[7] = 0; e[0] = 64;
    e[1] = 0; a = play(0, e, 3, 48);
    e[1] = 127; b = play(0, e, 3, 48);
    printf("jpz:   MIX 0: rms %.0f; MIX 127: rms %.0f (%.1f dB)\n", a.rms, b.rms, 20 * log10(b.rms / a.rms));
    check("MIX: the side saws change the sound; the level within 4 dB of MIX 0's (the normalisation)",
          fabs(a.bright - b.bright) > 0.01 * a.bright && fabs(20 * log10(b.rms / a.rms)) < 4.0);

    /* free phase: one note twice is not the same twice */
    {
        double s0 = 0, s1 = 0;
        uint32_t j;
        int32_t first[CTL * 4];
        fresh();
        host_preset(&trk[0], ENGI_JPZ, 0);
        trk[0].p[P_VOICE] = V_LEGATO;
        for (j = 0; j < 2u; j++) {
            trk_note_on(&trk[0], 60, 100);
            for (i = 0; i < 4u; i++) {
                int32_t o[2 * CTL];
                uint32_t q;
                mix_block(o, CTL);
                for (q = 0; q < CTL; q++) {
                    if (!j) first[i * CTL + q] = o[2 * q];
                    else s1 += fabs((double)o[2 * q] - first[i * CTL + q]), s0 += fabs((double)first[i * CTL + q]);
                }
            }
            trk_note_off(&trk[0], 60);
            for (i = 0; i < 200u; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
        }
        check("free-running: the same note twice starts differently (the phases are never reset)", s1 > 0.1 * s0);
    }
    /* seeding: WAVE, then JP-Z on the same part: the seven phases are spread, not all zero (the region was zeroed) */
    {
        jpz_mem_t *mm;
        uint32_t q, same = 0, vi;
        fresh();
        host_preset(&trk[0], ENGI_WAVE, 0);
        trk_note_on(&trk[0], 60, 100);
        { int32_t o[2 * CTL]; mix_block(o, CTL); }
        trk_note_off(&trk[0], 60);
        host_preset(&trk[0], ENGI_JPZ, 0);
        mm = jpz_mem(&trk[0]);
        check("  switching to JP-Z zeroes its region (WAVE had it), so it is unseeded", !mm->seeded);
        trk_note_on(&trk[0], 60, 100);
        for (vi = 0; vi < NVOICE && !trk[0].v[vi].active; vi++)
            ;
        for (q = 1; q < 7u; q++) same += mm->ph[vi % NVOICE][q] == mm->ph[vi % NVOICE][0];
        check("  the first note seeds it: the seven phases differ (not in phase)", mm->seeded && !same);
        trk_note_off(&trk[0], 60);
    }

    /* the filter */
    for (k = 0; k < 8u; k++) e[k] = -999;
    e[2] = -64; e[5] = 20; e[7] = 0; e[6] = 0;
    e[4] = 40; a = play(0, e, 0, 48);
    e[4] = 110; b = play(0, e, 0, 48);
    check("CUT: LP 24 at CUT 40 darker than at 110", a.bright < b.bright * 0.7);
    e[4] = 60; a = play(0, e, 0, 48); b = play(0, e, 2, 48); c = play(0, e, 3, 48);
    printf("jpz:   CUT 60: LP24 %.4f HP %.4f LP12 %.4f (rms %.0f %.0f %.0f)\n", a.bright, b.bright, c.bright, a.rms, b.rms, c.rms);
    check("TYPE: HP brighter than LP12, LP12 brighter than LP24 (the second stage)", b.bright > c.bright && c.bright > a.bright);
    e[5] = 127;
    {
        int ok = 1;
        int32_t ft;
        for (ft = 0; ft < 7; ft++) {
            for (k = 0; k <= 127u; k += 32u) {
                e[4] = (int16_t)k;
                a = play(0, e, ft, 36);
                ok &= a.peak < 400000 && a.rms == a.rms;
            }
        }
        check("RES 127: stable on every TYPE and CUT (bounded peaks, no NaN)", ok);
    }
    e[5] = 20; e[4] = 60;
    e[6] = 127; a = play(0, e, 0, 84); e[6] = 0; b = play(0, e, 0, 84);
    check("KTR: a high note brighter with key tracking", a.bright > b.bright);
    /* BAL: OSC2 alone an octave up is not the Super Saw */
    for (k = 0; k < 8u; k++) e[k] = -999;
    e[4] = 30; e[5] = 0; e[7] = 0; e[3] = 12; e[0] = 0; e[1] = 0;   /* (one saw, a low cutoff: the fundamental's crossings) */
    e[2] = -64; a = play(0, e, 3, 48); e[2] = 63; b = play(0, e, 3, 48);
    printf("jpz:   BAL -64: %.0f zero crossings, +63 (OSC2 +12): %.0f\n", a.zc, b.zc);
    check("BAL +63: OSC2 alone, an octave up (twice the zero crossings)", b.zc > a.zc * 1.8 && b.zc < a.zc * 2.2);

    /* WID: mono at 0, stereo above */
    for (k = 0; k < 8u; k++) e[k] = -999;
    e[7] = 0; a = play(0, e, -1, 60); e[7] = 100; b = play(0, e, -1, 60);
    printf("jpz:   WID 0 side %.4f, WID 100 side %.4f\n", a.side, b.side);
    check("WID: 0 mono (L = R), 100 stereo (the chorus)", a.side < 1e-6 && b.side > 0.05);

    /* UNISON: at most 2 voices */
    {
        uint32_t nv = 0;
        fresh();
        host_preset(&trk[0], ENGI_JPZ, 0);
        trk[0].p[P_VOICE] = V_UNISON;
        trk_note_on(&trk[0], 60, 100);
        for (i = 0; i < NVOICE; i++) nv += trk[0].v[i].active;
        check("UNISON: 2 voices (each 7 saws), not 8", nv == JPZ_UNI && trk_nvoice(&trk[0]) == JPZ_UNI);
        trk_note_off(&trk[0], 60);
    }
    /* every preset sounds */
    {
        uint32_t pi, quiet = 0;
        for (pi = 0; pi < ENGINES[ENGI_JPZ]->npresets; pi++) {
            a = play(pi, 0, -1, 48);
            if (a.rms < 50 && a.peak < 2000) {     /* (a pluck has decayed by the measured part: its peak still shows it) */ quiet++; printf("jpz:   %s rms %.0f\n", ENGINES[ENGI_JPZ]->presets[pi].name, a.rms); }
        }
        check("every JP-Z preset sounds (dry, C3)", !quiet);
    }
    /* headroom (device test 2: the mix ran 8x hot, the saws in phase clipped the SVF and wrapped voice_amp: crackle):
     * the mix's worst case, all seven saws at their extreme together, under voice_amp's 2^16 at every MIX; and every
     * preset on C2..C7, 1.5 s each, never leaves the SVF states on their +-150000 clamp at a block's end */
    {
        int32_t mx, worst = 0;
        uint32_t pi, nt, hit = 0, blk;
        for (mx = 0; mx <= 127; mx++) {
            int32_t pk = (((32767 * (25 + 6 * mx)) >> 7) * jpz_norm(mx)) >> 12;
            if (pk > worst) worst = pk;
        }
        printf("jpz:   the mix's worst-case peak %d (voice_amp takes < 65536)\n", worst);
        check("headroom: the mix's worst case (seven saws in phase, any MIX) under voice_amp's 2^16", worst < 65536);
        for (pi = 0; pi < ENGINES[ENGI_JPZ]->npresets; pi++)
            for (nt = 36; nt <= 96; nt += 12) {
                fresh();
                host_preset(&trk[0], ENGI_JPZ, pi);
                trk[0].p[P_VOICE] = V_POLY;
                trk_note_on(&trk[0], nt, 127);
                for (blk = 0; blk < FS * 3u / 2u / CTL; blk++) {
                    int32_t o[2 * CTL];
                    uint32_t vi;
                    mix_block(o, CTL);
                    for (vi = 0; vi < NVOICE; vi++) {
                        const voice_t *v = &trk[0].v[vi];
                        if (v->active && (abs(v->s[1]) >= 150000 || abs(v->s[4]) >= 150000 || abs(v->s[5]) >= 150000 ||
                                          abs(v->s[6]) >= 150000))
                            hit++;
                    }
                }
                trk_note_off(&trk[0], nt);
            }
        printf("jpz:   blocks ending on the SVF clamp: %u\n", hit);
        check("headroom: no preset drives the SVF onto its clamp (C2..C7)", !hit);
    }
    printf(bad ? "jpz: %d FAILED\n" : "jpz: all passed\n", bad);
    return bad != 0;
}
