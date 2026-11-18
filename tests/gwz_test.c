/* SPDX-License-Identifier: GPL-3.0-only
 * GW-Z (V2 Phase 2, eng_gwz.c): Quantum GammaWave's WHEEL, VOICE and ADDTV as ORGAN, VOX and ADD, behind the machine
 * contract. The models by preset, the descriptors (rename, never re-range), ORGAN's registrations, single-trigger
 * percussion and rotor, VOX's vowels and voice cap, ADD's spectrum and spectral decay, the state reset on a model change. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("gwz: %-98s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define NB 600u
typedef struct { double rms, bright, wob; } meas_t;
static int32_t L[NB * CTL];

static uint32_t named(const char *n)
{
    uint32_t i;
    for (i = 0; i < ENGINES[ENGI_GWZ]->npresets; i++)
        if (!strcmp(ENGINES[ENGI_GWZ]->presets[i].name, n))
            return i;
    printf("gwz: no preset %s\n", n);
    bad++;
    return 0;
}
static void fresh(void)
{
    host_tracks_init();
    memset(trk[0].v, 0, sizeof trk[0].v);
    trk[0].rr = 0;
    lim_env = LIM_T;
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = dc_l = dc_r = dce_l = dce_r = 0;
}
static void load(uint32_t pi)
{
    fresh();
    host_preset(&trk[0], ENGI_GWZ, pi);
    memset(gw_mem(&trk[0]), 0, sizeof(gw_mem_t));      /* (from rest: the wheels' phases as at the first note) */
    trk[0].p[P_LEVEL] = 100;
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_VOICE] = V_POLY;
}
static meas_t run(uint32_t note)
{
    meas_t r = {0, 0, 0};
    uint32_t b, i, n = 0;
    double s = 0, d = 0, bmin = 1e30, bmax = 0, bs = 0;
    trk_note_on(&trk[0], note, 100);
    for (b = 0; b < NB; b++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            L[b * CTL + i] = o[2 * i];
            bs += (double)o[2 * i] * o[2 * i];
        }
        if ((b & 63u) == 63u) {                         /* the rotor: the level of each 64 blocks (~46 ms: several of the registration's cycles) */
            if (b >= NB / 3u) {
                bmin = bs < bmin ? bs : bmin;
                bmax = bs > bmax ? bs : bmax;
            }
            bs = 0;
        }
    }
    trk_note_off(&trk[0], note);
    for (i = NB * CTL / 3u; i < NB * CTL; i++) {
        s += (double)L[i] * L[i];
        d += (double)(L[i] - L[i - 1u]) * (L[i] - L[i - 1u]);
        n++;
    }
    r.rms = sqrt(s / n);
    r.bright = r.rms > 0 ? sqrt(d / n) / r.rms : 0;
    r.wob = bmin > 0 ? 10.0 * log10(bmax / bmin) : 99;
    return r;
}

int main(void)
{
    meas_t a, b;
    uint32_t k, org = named("FULL ORGAN"), vx = named("CHOIR AAH"), ad = named("GLASS BELL");
    setvbuf(stdout, NULL, _IONBF, 0);

    /* the models */
    load(org); k = gw_model(&trk[0]) == GW_ORGAN;
    load(vx); k &= gw_model(&trk[0]) == GW_VOX;
    load(ad); k &= gw_model(&trk[0]) == GW_ADD;
    check("the preset picks the model: ORGAN, VOX, ADD (GW_PX into P_RSV_LN + 2)", k);
    {
        int ok = 1;
        uint32_t m, s;
        for (m = 0; m < GW_N; m++) {
            fresh();
            host_preset(&trk[0], ENGI_GWZ, m == 0 ? org : m == 1 ? vx : ad);
            for (s = 0; s < 8u; s++) {
                const param_desc_t *d = track_desc(&trk[0], P_E0 + s), *e = &ENGINES[ENGI_GWZ]->edit[s];
                ok &= d->min == e->min && d->max == e->max && d->def == e->def;
            }
        }
        ok &= str_eq(track_desc(&trk[0], P_E7)->label, "SPECT");
        check("every model's descriptor keeps edit[]'s range and default in all 8 slots (renames only)", ok);
    }

    /* ORGAN */
    load(org); trk[0].p[P_E7] = 0; trk[0].p[P_E0] = 0 * 8 + 4; a = run(48);      /* FLUTE: one bar */
    load(org); trk[0].p[P_E7] = 0; trk[0].p[P_E0] = 15 * 8 + 4; b = run(48);     /* FULL: nine */
    printf("gwz:   ORGAN FLUTE brightness %.4f, FULL %.4f\n", a.bright, b.bright);
    check("ORGAN: REG FULL (nine bars) brighter than FLUTE (one)", b.bright > a.bright * 1.5);
    load(org); trk[0].p[P_E7] = 0; trk[0].p[P_E0] = 0 * 8 + 4; trk[0].p[P_E3] = 127; b = run(48);
    check("  TOP up: upper bars added to FLUTE (brighter)", b.bright > a.bright * 1.2);
    load(org); trk[0].p[P_E7] = 0; a = run(48);
    load(org); trk[0].p[P_E7] = 120; b = run(48);
    printf("gwz:   ORGAN level wobble: ROTR OFF %.2f dB, FAST %.2f dB\n", a.wob, b.wob);
    check("ORGAN: ROTR FAST moves the level (the rotors spinning up: QG eases them in, ~0.4 s and ~1.5 s); OFF steady",
          a.wob < 0.3 && b.wob > a.wob * 4);
    {   /* single trigger: the second key while the first is held gets no percussion */
        gw_mem_t *mm;
        uint32_t v1, v2;
        load(org);
        trk[0].p[P_E6] = 1 * 16 + 8;                     /* 2ND */
        trk_note_on(&trk[0], 60, 100);
        mm = gw_mem(&trk[0]);
        for (v1 = 0; v1 < NVOICE && !(trk[0].v[v1].active && trk[0].v[v1].note == 60); v1++)
            ;
        trk_note_on(&trk[0], 64, 100);
        for (v2 = 0; v2 < NVOICE && !(trk[0].v[v2].active && trk[0].v[v2].note == 64); v2++)
            ;
        check("ORGAN PERC: single trigger (the first key strikes, a second one held over it does not)",
              v1 < NVOICE && v2 < NVOICE && mm->vc.o[v1].perc == 32767 && mm->vc.o[v2].perc == 0);
        trk_note_off(&trk[0], 60); trk_note_off(&trk[0], 64);
    }
    load(org); trk[0].p[P_E4] = 30; a = run(60); load(org); trk[0].p[P_E4] = 127; b = run(60);
    check("ORGAN: the tone filter (CUT 30 darker than 127)", a.bright < b.bright * 0.8);

    /* VOX */
    load(vx); trk[0].p[P_E7] = 0; trk[0].p[P_E2] = 0; trk[0].p[P_E3] = 0; a = run(48);   /* A */
    load(vx); trk[0].p[P_E7] = 64; trk[0].p[P_E2] = 0; trk[0].p[P_E3] = 0; b = run(48);  /* I */
    printf("gwz:   VOX A brightness %.4f, I %.4f\n", a.bright, b.bright);
    check("VOX: another VOWEL is another sound (A and I)", fabs(a.bright - b.bright) > 0.05 * a.bright && a.rms > 50 && b.rms > 50);
    {
        uint32_t n = 0;
        load(vx);
        for (k = 0; k < 6u; k++) trk_note_on(&trk[0], 48 + 3 * k, 100);
        for (k = 0; k < NVOICE; k++) n += trk[0].v[k].active;
        check("VOX: 4 voices (QG's cap)", n == 4u && trk_nvoice(&trk[0]) == 4u);
        for (k = 0; k < 6u; k++) trk_note_off(&trk[0], 48 + 3 * k);
    }

    /* ADD */
    load(ad); trk[0].p[P_E2] = 0; trk[0].p[P_E7] = 0; a = run(48);       /* ORGAN profile, no decay */
    load(ad); trk[0].p[P_E2] = 0; trk[0].p[P_E7] = 127; b = run(48);     /* METAL */
    check("ADD: SPECTR moves the spectrum (ORGAN profile to METAL)", fabs(a.bright - b.bright) > 0.1 * a.bright);
    {
        gw_mem_t *mm;
        uint32_t vi, i;
        load(ad);
        trk[0].p[P_E2] = 100;
        trk_note_on(&trk[0], 48, 100);
        for (i = 0; i < 300u; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
        mm = gw_mem(&trk[0]);
        for (vi = 0; vi < NVOICE && !trk[0].v[vi].active; vi++)
            ;
        check("ADD DECAY: the upper partials die first, the fundamental holds",
              vi < NVOICE && mm->vc.a[vi].dk[0] == 65535 && mm->vc.a[vi].dk[7] < mm->vc.a[vi].dk[2] && mm->vc.a[vi].dk[2] < 65535);
        trk_note_off(&trk[0], 48);
    }
    /* a model change on the part: the state from rest */
    {
        gw_mem_t *mm;
        load(org);
        trk_note_on(&trk[0], 60, 100);
        { int32_t o[2 * CTL]; mix_block(o, CTL); }
        trk_note_off(&trk[0], 60);
        host_preset(&trk[0], ENGI_GWZ, ad);
        mm = gw_sync(&trk[0]);
        check("another model's preset: its state starts from rest (gw_sync)", mm->model == GW_ADD + 1u && !mm->vc.a[0].ph[0]);
    }
    printf(bad ? "gwz: %d FAILED\n" : "gwz: all passed\n", bad);
    return bad != 0;
}
