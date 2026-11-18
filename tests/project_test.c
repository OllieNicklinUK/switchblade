/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project format (firmware/src/project.c, -DPROJ_HOST part). FM1-Four has one format, FOR4
 * (magic "FOR4", 3840 bytes, 112 parameters per track (FM1-Four Task 2.2: P_MOUT), engines 0..3 = OB-Z WAVE ACID DRIFT). Every Felucca
 * format (FUN1..FUN10, any size) is refused, not converted: an engine byte 1..3 of a Felucca project would
 * otherwise load as another machine. Also: the round trip (engines, steps, chain, motion with locks, name), a
 * damaged hash, an engine past the fourth, the retired FM6 area written 0 and not read.
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1                             /* (trk_def_engine: engines.c, through hostsim.c) */
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void reseal(project_store_t *st)          /* a valid hash over whatever the bytes are now */
{
    uint32_t sum = proj_hash(st->raw, PROJ_STORE_SIZE - 4u);
    memcpy(st->raw + PROJ_STORE_SIZE - 4u, &sum, 4);
}

static void four_project(project_t *a)
{
    uint32_t k, s;
    memset(a, 0, sizeof *a);
    a->magic = PROJ_MAGIC; a->size = sizeof *a; a->parts = NPART; a->phys = PROJ_PHYS; a->sel = 2;
    chain_defaults(&a->chain);
    for (k = 0; k < NTRK; k++) {
        a->t[k].engine = (uint8_t)k;                    /* OB-Z WAVE ACID DRIFT */
        a->t[k].preset = (uint8_t)(31u - k);
        a->t[k].p[P_LEVEL] = (int16_t)(40 + k);
        a->t[k].p[P_E7] = (int16_t)(k * 2u);
        for (s = 0; s < NSTEP; s++) {
            a->t[k].step[s].note[0] = (uint8_t)(36u + (s + k) % 40u);
            a->t[k].step[s].n = (uint8_t)(s % 3u);
            a->t[k].step[s].time = (uint8_t)(s % 3u);
            a->t[k].step[s].vel = (uint8_t)(64u + k);
        }
    }
    a->sum = proj_sum(a);
}

int main(void)
{
    static project_t a, c;
    static project_store_t st, st2;
    uint32_t i, k;
    int bad = 0, ok;

    bad += check("identity: PROJ_MAGIC is \"FOR4\" (little-endian bytes F O R 4)", PROJ_MAGIC == 0x34524F46u &&
                 !memcmp("FOR4", &(uint32_t){PROJ_MAGIC}, 4));
    bad += check("FOR4 fits one flash object, the retained cache in NOINIT", sizeof(project_store_t) <= 4096u - 256u &&
                 sizeof(project_store_t) == 3840u && 0xC8u + 4u * sizeof(project_store_t) <= 0x3D50u);
    bad += check("eight machines: NENGINES 8, P_COUNT 112 (P_MOUT just before P_E0)", NENGINES == 8u && P_COUNT == 112u &&
                 P_MOUT + 1u == P_E0);
    bad += check("name / FM6 area / MIDI offsets, 12 spare bytes before the retired area (MIDI IN's 3 among them)",
                 PROJ_NAME_OFF == 3824u && PROJ_FM6_OFF == 3312u && 68u + NTRK * (P_COUNT + 2u + NSTEP * 9u) +
                 sizeof(chain_config_t) + sizeof(motion_store_t) + 12u == PROJ_FM6_OFF);

    /* round trip: the four engines, presets, steps, the name, the retired FM6 area all 0 */
    four_project(&a);
    memcpy(a.name, "lofi jam", 8);
    a.sum = proj_sum(&a);
    ok = proj_pack(&st, &a) && ((uint32_t *)st.raw)[0] == PROJ_MAGIC && ((uint32_t *)st.raw)[1] == PROJ_STORE_SIZE &&
         st.raw[66] == P_COUNT;
    for (k = 0; k < NTRK * PROJ_FM6_PACKED; k++)
        ok &= st.raw[PROJ_FM6_OFF + k] == 0u;
    ok &= proj_import(&c, &st, sizeof st);
    for (k = 0; k < NTRK; k++)
        ok &= c.t[k].engine == k && c.t[k].preset == 31u - k;
    bad += check("FOR4 round trip: eng_req 0..3 and preset 31-k per track, steps, params", ok && c.sel == 2 &&
                 !memcmp(c.t, a.t, sizeof a.t) && !memcmp(c.g, a.g, sizeof a.g));
    bad += check("  the name round trips upper-cased and 0-padded", !memcmp(c.name, "LOFI JAM\0\0\0\0", 12) &&
                 !memcmp(st.raw + PROJ_NAME_OFF, "LOFI JAM\0\0\0\0", 12));
    bad += check("  the project_t form (a restored runtime copy) imports too", proj_import(&c, &a, sizeof a) &&
                 !memcmp(&c, &a, sizeof c));

    /* MIDI OUT: each track's MOUT (a parameter byte) and CLOCK OUT (header byte 65) round trip; the globals before keep
     * their bytes */
    {
        static project_t m;
        static project_store_t sm;
        m = a;
        for (k = 0; k < NTRK; k++)
            m.t[k].p[P_MOUT] = (int16_t)(k % 3u);
        m.g[G_CLKOUT] = 0;
        m.sum = proj_sum(&m);
        ok = proj_pack(&sm, &m) && sm.raw[65] == 0u && sm.raw[62] == m.sel && proj_import(&c, &sm, sizeof sm) &&
             !memcmp(c.g, m.g, sizeof m.g);
        for (k = 0; k < NTRK; k++)
            ok &= c.t[k].p[P_MOUT] == (int16_t)(k % 3u) && sm.raw[68u + k * (P_COUNT + 2u + NSTEP * 9u) + P_MOUT] == 64u + k % 3u;
        m.g[G_CLKOUT] = 1;
        m.sum = proj_sum(&m);
        ok &= proj_pack(&sm, &m) && sm.raw[65] == 1u && proj_import(&c, &sm, sizeof sm) && c.g[G_CLKOUT] == 1;
        bad += check("MIDI OUT: T1..T4 MOUT (INT EXT BOTH) and CLOCK OUT (byte 65) round trip", ok);
        sm.raw[66] = 111u;                                 /* a FOR4 of before P_MOUT: refused, no migration */
        reseal(&sm);
        bad += check("  a FOR4 of 111 parameters (before MOUT) is refused", !proj_import(&c, &sm, sizeof sm));
    }

    /* every Felucca magic is refused, at its own size and at FOR4's size, with a valid hash */
    {
        static const uint32_t OLD[] = {0x46554E41u, 0x46554E39u, 0x46554E38u, 0x46554E37u, 0x46554E36u,
                                       0x46554E35u, 0x46554E34u, 0x46554E33u, 0x46554E32u, 0x46554E31u};
        static const uint32_t SZ[] = {3840u, 3648u, 3584u, 3388u, 3388u, 3352u, 2680u, 2584u, 2552u, 688u};
        ok = 1;
        for (i = 0; i < sizeof OLD / sizeof OLD[0]; i++) {
            st2 = st;
            memcpy(st2.raw, &OLD[i], 4);
            reseal(&st2);
            ok &= !proj_import(&c, &st2, sizeof st2);      /* (the retained 3840-byte slot form) */
            ok &= !proj_import(&c, &st2, (int)SZ[i]);
            memcpy(&c, &a, sizeof c);
            memcpy(&c.magic, &OLD[i], 4);
            ok &= !proj_import(&c, &c, sizeof c);
        }
        bad += check("old Felucca magics FUN1..FUN10, every size, a valid hash: refused", ok);
    }
    {
        static uint8_t buf[PROJ_STORE_SIZE];
        memset(buf, 0, sizeof buf);
        buf[0] = 0x41; buf[1] = 0x4E; buf[2] = 0x55; buf[3] = 0x46;          /* "FUNA" little-endian */
        buf[4] = 0x00; buf[5] = 0x0F;                                         /* size 3840 */
        {
            uint32_t sum = proj_hash(buf, PROJ_STORE_SIZE - 4u);               /* a valid hash */
            memcpy(buf + PROJ_STORE_SIZE - 4u, &sum, 4);
        }
        bad += check("a Felucca FUN10 project must be refused", !proj_import(&c, buf, sizeof buf));
    }
    /* a Felucca FUN10 with the engine bytes 1..3 (the ones that could be misread) and a correct hash */
    st2 = st;
    {
        uint32_t m = 0x46554E41u;
        memcpy(st2.raw, &m, 4);
        reseal(&st2);
    }
    bad += check("a FUN10 with engine bytes 0..3 (Felucca) never loads as WAVE / ACID / DRIFT", !proj_import(&c, &st2, sizeof st2));

    /* damage */
    st2 = st; st2.raw[100] ^= 1u;
    bad += check("a changed byte: the hash refuses the project", !proj_import(&c, &st2, sizeof st2));
    st2 = st; st2.raw[PROJ_NAME_OFF + 3] ^= 1u;
    bad += check("a changed name byte: the hash refuses the project", !proj_import(&c, &st2, sizeof st2));
    bad += check("one byte short: refused", !proj_import(&c, &st, sizeof st - 1u));
    st2 = st; st2.raw[66] = P_COUNT - 1u; reseal(&st2);
    bad += check("another parameter count (np != P_COUNT), a valid hash: refused", !proj_import(&c, &st2, sizeof st2));
    st2 = st; st2.raw[68u + P_COUNT] = NENGINES; reseal(&st2);       /* track 0's engine byte */
    bad += check("an engine past the fourth (NENGINES), a valid hash: refused", !proj_import(&c, &st2, sizeof st2));
    memcpy(&c, &a, sizeof c); c.t[1].engine = NENGINES; c.sum = proj_sum(&c);
    bad += check("  the same in a project_t copy: refused", !proj_import(&c, &c, sizeof c));
    memset(&st2, 0xA5, sizeof st2);
    bad += check("a slot of garbage is empty", !proj_import(&c, &st2, sizeof st2));
    st2 = st; st2.raw[68u + P_COUNT - 1u] = 192u; reseal(&st2);
    bad += check("a parameter byte past 191 (value > 127), a valid hash: refused", !proj_import(&c, &st2, sizeof st2));

    /* the retired FM6 area is not read */
    st2 = st; st2.raw[PROJ_FM6_OFF + 5] ^= 1u;
    bad += check("the retired FM6 area: a changed byte, the hash refuses it", !proj_import(&c, &st2, sizeof st2));
    reseal(&st2);
    bad += check("  with its hash right: loads, the byte not read", proj_import(&c, &st2, sizeof st2) &&
                 !memcmp(c.t, a.t, sizeof a.t));

    /* motion with parameter locks, a name byte outside 32..126 */
    {
        project_t f;
        memcpy(&f, &a, sizeof f);
        f.motion.count = 4; f.motion.on = 8;
        f.motion.event[0] = (motion_event_t){3u << 6 | 2u, P_E3, 99};
        f.motion.event[1] = (motion_event_t){3u << 6 | 4u, P_REV, 50};
        f.motion.event[2] = (motion_event_t){3u << 6 | 5u, (uint8_t)(P_DIST | MOTION_LOCK), 40};
        f.motion.event[3] = (motion_event_t){3u << 6 | 6u, (uint8_t)(P_E5 | MOTION_LOCK), -3};
        f.sum = proj_sum(&f);
        ok = proj_pack(&st2, &f) && proj_import(&c, &st2, sizeof st2) && !memcmp(&f.motion, &c.motion, sizeof f.motion);
        bad += check("motion: events and locks (P_DIST, P_E5) kept", ok && c.motion.event[3].param == (P_E5 | MOTION_LOCK));
    }
    st2 = st; st2.raw[PROJ_NAME_OFF + 3] = 7u; reseal(&st2);
    bad += check("a name byte outside 32..126, hash right: loads, no name", proj_import(&c, &st2, sizeof st2) && !c.name[0] &&
                 c.t[1].p[P_LEVEL] == 41);
    memcpy(a.name, "ABCDEFGHIJKL", 12); a.sum = proj_sum(&a);
    bad += check("a name of 12 characters (no terminator)", proj_pack(&st2, &a) && proj_import(&c, &st2, sizeof st2) &&
                 !memcmp(c.name, "ABCDEFGHIJKL", 12));

    /* a step out of range packs inside its fields */
    four_project(&a);
    a.t[0].step[3].n = 1; a.t[0].step[3].note[0] = 200; a.t[0].step[3].note[2] = 128;
    a.t[0].step[3].vel = 255; a.t[0].step[3].hit = 0x05; a.t[0].step[3].acc = 0xF7;
    a.sum = proj_sum(&a);
    ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
    bad += check("a step out of range packs inside it (note, velocity 127; the grid bytes 0): it loads",
                 ok && c.t[0].step[3].note[0] == 127u && c.t[0].step[3].note[2] == 127u && c.t[0].step[3].vel == 127u &&
                 !c.t[0].step[3].hit && !c.t[0].step[3].acc);

    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
