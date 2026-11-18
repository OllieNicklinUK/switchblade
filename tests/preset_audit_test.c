/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* V2 (Phase 0.7): the factory library, audited through the real load and the real render. After Quantum
 * GammaWave's tests/preset_audit_test.c (5.0, GPL-3.0-only), on Switch Blade's own loader (hostsim.c host_preset:
 * the fields ui.c apply_preset_to writes, WAVE_PX / DRIFT_PX / NANO_PX through their *_track_loaded).
 *
 * For every preset of every machine: load it, hold one note (C4) on part 1 at LEVEL 100, render ~1 s through the
 * whole mix (sends and buses included), and measure the sounding part (from 1/3 of the way on: past the attacks).
 *
 *   structure   E0..E7 inside the machine's own descriptor (desc: per model where a machine has models), the four
 *               envelope stages 0..127, ENV->FLT -64..63, the four sends stored as 0 (default) or 1..128, a name
 *               of 1..14 printable characters. FAILS: a preset out of range is a load that silently clamps, or a
 *               page that shows a value the sound does not have.
 *   silence     RMS under 8 (one 16-bit LSB is 1). FAILS: a factory sound that makes nothing is broken.
 *   names       a name repeated inside one machine. FAILS: PRESETS shows two the same.
 *   level       the peak and the share of the note at |x| >= 32000. REPORTED, not failed: how hot is too hot
 *               is a listening decision.
 *   duplicates  two presets of one machine with the same parameters. REPORTED, not failed, as a count: in Phase 0
 *               the "SLOT nn" placeholders (presets 32..49 of the four 0.2 machines) and the four new machines'
 *               INIT presets are copies by design until Phase 5 voices them.
 *
 * It does not listen. A sound that passes exists, is in range and is named once; whether it is good needs the FM-1
 * and ears (Task 7.2's sign-off by the user).
 *
 *   --levels   one CSV row a preset: engine, index, machine, name, rms dBFS, peak (tools/gen_preset_levels.py)
 */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define NBLK 1400u                                     /* 1400 x 32 samples ~ 1.0 s */
#define NAME_MAX_SHOWN 14u                             /* the PRESETS row's room (tools/four_presets.py checks it too) */
static int32_t abuf_l[NBLK * CTL];
#define LVL_WIN 128u                                   /* 128 x 32 samples ~ 93 ms */
static double lvl_rms;                                 /* render_note_here: the loudest window's RMS */

/* one preset from power-on state: in a child process, as regress.c runs each job (the buses' tails, a machine's
 * engine memory, the noise seeds of the preset before must not reach this one's measurement) */
static void render_note_here(uint32_t e, uint32_t pi, uint32_t note, int32_t *peak, double *rms, double *hot);
static void render_note(uint32_t e, uint32_t pi, uint32_t note, int32_t *peak, double *rms, double *hot)
{
    struct { int32_t peak; double rms, hot, lvl; } r = {0, 0, 0, 0};
    int fd[2];
    pid_t pid;
    if (pipe(fd) || (pid = fork()) < 0) {
        render_note_here(e, pi, note, peak, rms, hot);
        return;
    }
    if (!pid) {
        close(fd[0]);
        render_note_here(e, pi, note, &r.peak, &r.rms, &r.hot);
        r.lvl = lvl_rms;
        if (write(fd[1], &r, sizeof r) != (ssize_t)sizeof r)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &r, sizeof r) != (ssize_t)sizeof r)
        r.rms = -1;                                     /* (the child crashed: reported as silent) */
    close(fd[0]);
    waitpid(pid, 0, 0);
    *peak = r.peak; *rms = r.rms; *hot = r.hot; lvl_rms = r.lvl;
}
static void render_note_here(uint32_t e, uint32_t pi, uint32_t note, int32_t *peak, double *rms, double *hot)
{
    uint32_t b, i, n = 0, hot_n = 0;
    double acc = 0;
    host_tracks_init();
    host_preset(&trk[0], e, pi);
    trk[0].p[P_LEVEL] = 100;
    trk_note_on(&trk[0], note, 100);
    for (b = 0; b < NBLK; b++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            abuf_l[b * CTL + i] = o[2 * i];
    }
    *peak = 0;
    {   /* the level: the loudest LVL_WIN blocks (~93 ms) anywhere in the note, so a pluck is measured on its body, not
         * on the tail after it has decayed (the sounding part below would put a CLAV 40 dB under a pad) */
        static double bs[NBLK];
        double w = 0, best = 0;
        for (b = 0; b < NBLK; b++) {
            bs[b] = 0;
            for (i = 0; i < CTL; i++)
                bs[b] += (double)abuf_l[b * CTL + i] * abuf_l[b * CTL + i];
            w += bs[b];
            if (b >= LVL_WIN)
                w -= bs[b - LVL_WIN];
            if (b + 1u >= LVL_WIN && w > best)
                best = w;
        }
        lvl_rms = sqrt(best / (LVL_WIN * CTL));
    }
    for (i = (NBLK / 3u) * CTL; i < NBLK * CTL; i++) {
        int32_t a = abuf_l[i] < 0 ? -abuf_l[i] : abuf_l[i];
        acc += (double)abuf_l[i] * abuf_l[i];
        n++;
        if (a > *peak)
            *peak = a;
        if (a >= 32000)
            hot_n++;
    }
    *rms = n ? sqrt(acc / (double)n) : 0;
    *hot = n ? (double)hot_n / (double)n : 0;
}

static int same_as(const preset_t *a, const preset_t *b)
{
    return !memcmp(a->e, b->e, sizeof a->e) && !memcmp(a->env, b->env, sizeof a->env) && a->fenv == b->fenv &&
           a->mono == b->mono && !memcmp(a->fx, b->fx, sizeof a->fx);
}

static int name_ok(const char *s)
{
    uint32_t n = 0;
    if (!s)
        return 0;
    for (; *s; s++, n++)
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E)
            return 0;
    return n >= 1u && n <= NAME_MAX_SHOWN;
}

int main(int argc, char **argv)
{
    int levels = argc > 1 && !strcmp(argv[1], "--levels");
    uint32_t e, i, k, total = 0, silent = 0, hot_n = 0, dupn = 0, dupp = 0, range = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!levels) {
        printf("preset audit: every factory preset through the real load and render (host, %u Hz, ~1 s of C4)\n", (unsigned)FS);
        printf("preset audit: %-7s %7s %6s %4s %5s %5s %5s  %s\n", "machine", "presets", "silent", "hot", "dupn", "dupp",
               "range", "level dBFS (loudest 93 ms) min / median / max");
    }
    for (e = 0; e < NENGINES; e++) {
        const engine_t *en = ENGINES[e];
        uint32_t n = en->npresets, es = 0, eh = 0, edn = 0, edp = 0, er = 0, m = 0;
        double db[NPRESET], t;
        if (!n)
            continue;
        for (i = 0; i < n; i++) {
            const preset_t *p = &en->presets[i];
            int32_t peak;
            double rms, hot;
            total++;
            host_tracks_init();
            host_preset(&trk[0], e, i);                  /* (a machine with models: the ranges of this preset's model) */
            for (k = 0; k < 8u; k++) {
                const param_desc_t *d = track_desc(&trk[0], P_E0 + k);
                if (p->e[k] < d->min || p->e[k] > d->max) {
                    er++;
                    printf("  range: %-7s %2u %-14s E%u = %d outside %d..%d\n", en->name, i, p->name, k, p->e[k], d->min, d->max);
                }
            }
            for (k = 0; k < 4u; k++)
                if (p->env[k] > 127u || p->fx[k] > 128u) {
                    er++;
                    printf("  range: %-7s %2u %-14s env[%u] %u / fx[%u] %u past 127\n", en->name, i, p->name, k, p->env[k], k, p->fx[k]);
                }
            if (p->fenv < -64 || p->fenv > 63 || p->mono > 1u) {
                er++;
                printf("  range: %-7s %2u %-14s ENV->FLT %d / mono %u\n", en->name, i, p->name, p->fenv, p->mono);
            }
            if (!name_ok(p->name)) {
                er++;
                printf("  name:  %-7s %2u \"%s\": empty, unprintable or longer than %u\n", en->name, i, p->name ? p->name : "", NAME_MAX_SHOWN);
            }
            for (k = 0; k < i; k++)
                if (p->name && en->presets[k].name && !strcmp(p->name, en->presets[k].name)) {
                    edn++;
                    printf("  name:  %-7s %2u \"%s\" also preset %u\n", en->name, i, p->name, k);
                }
            for (k = 0; k < i; k++)
                if (same_as(p, &en->presets[k])) {
                    edp++;
                    break;
                }
            render_note(e, i, 60u, &peak, &rms, &hot);
            t = lvl_rms > 0 ? 20.0 * log10(lvl_rms / 32768.0) : -200.0;   /* (the level: the loudest ~93 ms) */
            db[m++] = t;
            if (levels)
                printf("%u,%u,%s,%s,%.2f,%d\n", e, i, en->name, p->name, t, peak);
            if (rms < 8.0) {
                es++;
                printf("  silent: %-7s %2u %-14s rms %.1f, peak %d\n", en->name, i, p->name, rms, peak);
            }
            if (hot > 0.02) {
                eh++;
                if (!levels)
                    printf("  hot:   %-7s %2u %-14s %.0f %% of the note at the clipper's last stretch (peak %d)\n",
                           en->name, i, p->name, hot * 100.0, peak);
            }
        }
        for (i = 1; i < m; i++)                          /* (sort: the spread) */
            for (k = i; k > 0u && db[k - 1u] > db[k]; k--) {
                t = db[k]; db[k] = db[k - 1u]; db[k - 1u] = t;
            }
        if (!levels)
            printf("preset audit: %-7s %7u %6u %4u %5u %5u %5u  %6.1f / %6.1f / %6.1f  (spread %.1f dB)\n", en->name, n, es,
                   eh, edn, edp, er, db[0], db[m / 2u], db[m - 1u], db[m - 1u] - db[0]);
        silent += es; hot_n += eh; dupn += edn; dupp += edp; range += er;
    }
    if (levels)
        return 0;
    printf("preset audit: %u presets: %u silent, %u hot (reported), %u repeated names, %u exact duplicates (reported: "
           "Phase 0 placeholders), %u out of range\n", total, silent, hot_n, dupn, dupp, range);
    printf("preset audit: %s\n", silent || dupn || range ? "FAILED" : "passed (structure and signal; nobody listened)");
    return silent || dupn || range ? 1 : 0;
}
