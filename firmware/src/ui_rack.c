/* SPDX-License-Identifier: GPL-3.0-only */
/* ui_rack.c: Switch Blade V2's HOME, the rack (d:\Switchblade\2026-11-19-rack-ui.md; included by ui_big.c, drawn by
 * draw_home_big). Four units, one per track, in its machine's colour; the selected track (ALGORITHM) is 2U:
 *
 *    0  header (ui_draw.c draw_head), 24 px
 *   24  the rack, 168 px: three 1U units (30 px) and the selected one, 2U (78 px: 40 px of name, 38 px of scope)
 *  192  the knob panel, 48 px: KNOB 1..4 (the EDIT page's four: CUTOFF RES ENV + the machine's own)
 *
 * 1U: the rack ears and their screws, the faceplate (the machine's colour, lit at the top), the track's number and the
 *     sound's name in recessed windows (Oswald, glowing), a bar meter, the volume knob (LEVEL; SELECT moves the selected
 *     track's) with the MIDI light at its 12 o'clock, the machine's badge (its name and a little wave).
 * 2U: a dark faceplate, the sound's name large, a dot-matrix meter, the machine and the MIDI light, the selected track's
 *     signal as a scope (fx.c scope_tb) with the frame before as a ghost. While SELECT turns the level, or a KNOB turns,
 *     its name and value show over the scope.
 * The knob panel: brushed dark metal, four knob caps (tools/gen_ui_knobs.py) with drawn pointers and value arcs in the
 * selected machine's colour; the knob just turned lit with an outer arc and a dot.
 * Each strip is drawn again only when what it shows changed (rk_sig); the scope every frame while the track sounds. */
#include "ui_knobs.h"

#define RK_Y0 H_HEAD                                    /* the rack: 24 .. 191 */
#define RK_1U 30
#define RK_2T 40                                        /* the 2U unit: its top (name) and its scope strip */
#define RK_2S 38
#define RK_2U (RK_2T + RK_2S)
#define RK_KY (RK_Y0 + 3 * RK_1U + RK_2U)               /* the knob panel: 192 .. 239 */
#define RK_KH (240 - RK_KY)
typedef char rk_fits[RK_KY == 192 && RK_KH == 48 ? 1 : -1];
#define RK_FX 7                                         /* the faceplate: x 7 .. 232 */
#define RK_FW 226
#define RK_WIN RGB(10, 11, 14)                          /* a recessed window */
#define RK_SC_X 9                                       /* the scope: x 9 .. 230 */
#define RK_SC_W 222

/* the machines' rack colours (engine order): the faceplate and the glow of its windows. Defaults (2026-11-19), for the
 * user to review */
static const uint16_t RK_PLATE[NENGINES] = {RGB(46, 78, 154), RGB(179, 104, 28), RGB(150, 156, 164), RGB(176, 32, 36),
                                            RGB(31, 122, 140), RGB(150, 120, 200), RGB(143, 122, 30), RGB(31, 122, 90)};
static const uint16_t RK_GLOW[NENGINES] = {RGB(127, 178, 255), RGB(255, 194, 122), RGB(225, 230, 236), RGB(255, 96, 88),
                                           RGB(111, 240, 255), RGB(226, 206, 255), RGB(255, 227, 107), RGB(111, 255, 192)};
#define RK_BLACK RGB(0, 0, 0)
#define RK_WHITE RGB(255, 255, 255)
#define RK_GREEN RGB(47, 224, 106)
#define RK_AMBER RGB(245, 197, 66)
#define RK_RED RGB(255, 64, 64)

static uint32_t rk_sig[NTRK + 2u];                      /* the units, the 2U scope, the knob panel */
static uint8_t rk_meter[NTRK];                          /* 0..70: the meter, falling */
static uint8_t rk_ghost[RK_SC_W];                       /* the scope's last trace (its rows), the ghost */
static uint8_t rk_ghost_ok;

static uint32_t rk_eng(const track_t *t) { return t->eng_req % NENGINES; }
static const char *rk_mname(const track_t *t) { return rk_eng(t) == ENGI_ACID && sub_on(t) ? "SUB" : ENGINES[rk_eng(t)]->name; }

/* the track's meter, 0..7 segments: the peak since the last frame on a log scale (~8 dB a segment), falling ~1 dB a frame */
static uint32_t rk_level(uint32_t c)
{
    track_t *t = &trk[c];
    int32_t a = t->p[P_MUTE] ? 0 : t->peak, lg = 0, v = 0;
    t->peak = 0;
    if (a >= 64) {
        while ((a >> lg) > 1)
            lg++;
        v = clamp(lg * 8 + (((a << 3) >> lg) & 7) - 58, 0, 70);   /* 8 log2(a): -48 dB .. +6 dB */
    }
    if (v < (int32_t)rk_meter[c] - 2)
        v = rk_meter[c] - 2;
    rk_meter[c] = (uint8_t)(v < 0 ? 0 : v);
    return (rk_meter[c] + 9u) / 10u;
}

/* text s cut to fit w px in f (no ellipsis: the windows are the edge) */
static void rk_fit(const aafont_t *f, const char *s, int32_t w, char *o, uint32_t n)
{
    uint32_t l;
    str_cpy(o, s, n);
    for (l = str_len(o); l && text_w(f, o) > w; l--)
        o[l - 1u] = 0;
}

/* the windows' text at the line top y: sharp, no halo (the user, 2026-11-19: a glow looks blurry), blended over the canvas
 * (cv_mixa) so it sits right on any plate. (big: unused, kept for the callers) */
static void rk_glow(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t col, uint16_t under, int big)
{
    (void)big;
    cv_mixc = col;
    cv_mixa = 16;
    cv_text_flags(x, y, f, s, col, under, 8u);
    cv_mixa = 0;
}
/* .. its ink centred across x .. x + w */
static void rk_glow_in(int32_t x, int32_t w, int32_t y, const aafont_t *f, const char *s, uint16_t col, uint16_t under)
{
    rk_glow(x + ink_in(f, s, w), y, f, s, col, under, 0);
}

/* a knob cap bitmap (tools/gen_ui_knobs.py) at its top-left */
static void rk_cap(int32_t x, int32_t y, uint32_t d)
{
    const uint16_t *c = d == 14u ? KCAP14_C : d == 18u ? KCAP18_C : d == 28u ? KCAP28_C : KCAP34_C;
    const uint8_t *a = d == 14u ? KCAP14_A : d == 18u ? KCAP18_A : d == 28u ? KCAP28_A : KCAP34_A;
    uint32_t i, j;
    for (j = 0; j < d; j++)
        for (i = 0; i < d; i++)
            cv_mix(x + (int32_t)i, y + (int32_t)j, c[j * d + i], a[j * d + i]);
}
/* a knob: its cap (diameter d) at the centre (cx, cy) and its pointer at angle a (degrees from 12 o'clock) */
static void rk_knob(int32_t cx, int32_t cy, uint32_t d, int32_t a)
{
    int32_t x0, y0, x1, y1, r8 = (int32_t)d * 4;
    rk_cap(cx - (int32_t)d / 2, cy - (int32_t)d / 2, d);
    ctl_pt(cx * 8, cy * 8, r8 * 3 / 10, a, &x0, &y0);   /* (the caps are even: the centre on a pixel corner) */
    ctl_pt(cx * 8, cy * 8, r8 * 8 / 10, a, &x1, &y1);
    cv_stroke(x0, y0, x1, y1, d >= 28u ? 8 : 5, RGB(236, 240, 244));
}
/* the angle of a value: 7:30 (225) .. 4:30 (495) o'clock */
static int32_t rk_angle(int32_t v, int32_t lo, int32_t hi) { return 225 + (hi > lo ? (v - lo) * 270 / (hi - lo) : 0); }

/* the machine's little wave, in a 20 x 6 box at (x, y) */
static void rk_icon(uint32_t e, int sub, int32_t x, int32_t y, uint16_t c)
{
    static const uint8_t W[NENGINES + 1][11] = {
        {5, 4, 3, 2, 1, 0, 5, 4, 3, 2, 0},            /* OB-Z: saws */
        {3, 1, 0, 1, 3, 5, 6, 5, 3, 2, 3},            /* WAVE: a morphing sine */
        {0, 0, 0, 5, 5, 5, 0, 0, 0, 5, 5},            /* NANO: the 303's square */
        {0, 0, 5, 5, 5, 5, 5, 0, 0, 5, 5},            /* JUNO: a pulse */
        {5, 3, 1, 5, 2, 0, 4, 1, 5, 3, 0},            /* JP-Z: supersaw teeth */
        {5, 0, 3, 1, 2, 5, 0, 3, 1, 2, 5},            /* CZ-Z: phase distortion's resonance */
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},            /* GW-Z: drawbars (below) */
        {5, 5, 3, 3, 1, 1, 0, 0, 2, 2, 4},            /* MICRO: a stepped wavetable */
        {3, 1, 0, 1, 3, 5, 6, 5, 3, 1, 0}};           /* SUB: a sine */
    uint32_t k = sub ? NENGINES : e, i;
    if (k == ENGI_GWZ) {
        static const uint8_t H[6] = {4, 6, 3, 6, 2, 5};
        for (i = 0; i < 6u; i++)
            cv_rect(x + 1 + (int32_t)i * 3, y + 6 - H[i], 2, H[i], c);
        return;
    }
    for (i = 0; i + 1u < 11u; i++) {
        int32_t xa = (x + (int32_t)i * 2) * 8 + 4, xb = xa + 16;
        int32_t ya = (y + (W[k][i] > 5 ? 5 : W[k][i])) * 8 + 4, yb = (y + (W[k][i + 1u] > 5 ? 5 : W[k][i + 1u])) * 8 + 4;
        if (k <= ENGI_DRIFT && k != ENGI_WAVE && W[k][i] != W[k][i + 1u] && (k == ENGI_ACID || k == ENGI_DRIFT))
            xb = xa;                                    /* (a square's edges: straight up) */
        cv_stroke(xa, ya, xb, yb, 5, c);
        if (xb == xa)
            cv_stroke(xa, yb, xa + 16, yb, 5, c);
    }
}

/* the rack ears (both sides) and their screws on canvas rows 0 .. h - 1 (screw rows s0, s1) */
static void rk_ears(int32_t h, int32_t s0, int32_t s1)
{
    static const uint16_t EAR = RGB(138, 144, 152), EAR_HI = RGB(186, 192, 200), EAR_LO = RGB(84, 88, 96);
    cv_rect(0, 0, RK_FX, h, EAR);
    cv_rect(240 - RK_FX, 0, RK_FX, h, EAR);
    cv_rect(0, 0, 1, h, EAR_HI);
    cv_rect(240 - RK_FX, 0, 1, h, EAR_HI);
    cv_rect(RK_FX - 1, 0, 1, h, EAR_LO);
    cv_rect(239, 0, 1, h, EAR_LO);
    if (s0 >= 0) {
        ctl_screw(3, s0, RGB(200, 204, 210), RGB(60, 62, 68));
        ctl_screw(236, s0, RGB(200, 204, 210), RGB(60, 62, 68));
    }
    if (s1 >= 0) {
        ctl_screw(3, s1, RGB(200, 204, 210), RGB(60, 62, 68));
        ctl_screw(236, s1, RGB(200, 204, 210), RGB(60, 62, 68));
    }
}
/* the faceplate, rows y0 .. y0 + h - 1: plate lit at the top (from top: the highlight), darker to the bottom */
static void rk_plate(int32_t y0, int32_t h, uint16_t plate, int top)
{
    int32_t r;
    for (r = 0; r < h; r++)
        cv_rect(RK_FX, y0 + r, RK_FW, 1, ux_mix(ux_mix(plate, RK_WHITE, 14), RK_BLACK, (y0 + r) * 30 / (h + y0)));
    if (top)
        cv_rect(RK_FX, y0, RK_FW, 1, ux_mix(plate, RK_WHITE, 40));
}
/* a recessed window: dark, a shadow along its top, a lit edge under it */
static void rk_window(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t plate)
{
    cv_rrect(x, y + 1, w, h, 3, ux_mix(plate, RK_WHITE, 25), plate);
    cv_rrect(x, y, w, h, 3, RK_WIN, plate);
    cv_rect(x + 3, y, w - 6, 1, RK_BLACK);
}

/* the sound's name of track t, in capitals */
static void rk_name(const track_t *t, char *n)
{
    sound_name(t, n);
    if (!n[0])
        str_cpy(n, "INIT", 16);
}

/* a 1U unit, track c, at screen row y */
static void rk_unit1(uint32_t c, int32_t y, uint32_t seg, int lit)
{
    track_t *t = &trk[c];
    uint32_t e = rk_eng(t), i;
    int mute = t->p[P_MUTE] != 0;
    uint16_t plate = mute ? ux_mix(RK_PLATE[e], RGB(70, 72, 76), 65) : RK_PLATE[e];
    uint16_t glow = mute ? ux_mix(RK_GLOW[e], RK_WIN, 60) : RK_GLOW[e];
    const param_desc_t *d = track_desc(t, P_LEVEL);
    char nm[16], fit[16], dg[2] = {(char)('1' + c), 0};
    int32_t a = rk_angle(t->p[P_LEVEL], d->min, d->max);
    cv_begin(240, RK_1U, RK_BLACK);
    rk_ears(RK_1U, 8, 22);
    rk_plate(1, RK_1U - 1, plate, 1);
    rk_window(11, 5, 18, 20, plate);                   /* the number */
    rk_glow_in(11, 18, 5 + HALF_UP(20 - AF_D_CAP_H) - AF_D_CAP_Y, &AF_D, dg, glow, RK_WIN);
    rk_window(33, 5, 121, 20, plate);                  /* the name */
    rk_name(t, nm);
    rk_fit(&AF_D, nm, 109, fit, sizeof fit);
    rk_glow(39, 5 + HALF_UP(20 - AF_D_CAP_H) - AF_D_CAP_Y, &AF_D, fit, glow, RK_WIN, 0);
    cv_rrect(158, 4, 8, 22, 1, RK_WIN, plate);         /* the meter: two columns of 7 */
    for (i = 0; i < 7u; i++) {
        uint16_t sc = i >= 6u ? RK_RED : i >= 4u ? RK_AMBER : RK_GREEN;
        uint16_t on = i < seg ? sc : ux_mix(sc, RK_WIN, 82);
        cv_rect(159, 23 - (int32_t)i * 3, 2, 2, on);
        cv_rect(163, 23 - (int32_t)i * 3, 2, 2, on);
    }
    cv_arc(181 * 8, 15 * 8, 92, 76, 225, 495, ux_mix(plate, RK_BLACK, 55));   /* the volume knob */
    if (a > 225)
        cv_arc(181 * 8, 15 * 8, 92, 76, 225, a, glow);
    rk_knob(181, 15, 18, a);
    cv_disc(181 * 8, 2 * 8 + 4, 11, lit ? RK_WHITE : ux_mix(plate, RK_BLACK, 45));  /* the MIDI light */
    rk_window(197, 5, 33, 20, plate);                  /* the badge */
    rk_glow_in(197, 33, 7 - AF_DS_CAP_Y, &AF_DS, rk_mname(t), glow, RK_WIN);
    rk_icon(e, e == ENGI_ACID && sub_on(t), 203, 17, glow);   /* (rows 17 .. 22: under the name) */
    cv_blit(0, (uint32_t)y);
}

/* the 2U unit's top (the name): track c at screen row y */
static void rk_unit2_top(uint32_t c, int32_t y, uint32_t seg, int lit)
{
    track_t *t = &trk[c];
    uint32_t e = rk_eng(t), i, j;
    int mute = t->p[P_MUTE] != 0;
    uint16_t glow = mute ? ux_mix(RK_GLOW[e], RK_WIN, 55) : RK_GLOW[e], plate = ux_mix(RGB(18, 20, 24), RK_GLOW[e], 5);
    char nm[16], fit[16], dg[2] = {(char)('1' + c), 0};
    int32_t mw;
    cv_begin(240, RK_2T, RK_BLACK);
    rk_ears(RK_2T, 9, -1);
    cv_rect(RK_FX, 1, RK_FW, RK_2T - 1, plate);
    cv_rect(RK_FX, 1, RK_FW, 1, ux_mix(plate, RK_WHITE, 22));
    rk_window(11, 5, 17, 19, plate);                   /* the number */
    rk_glow_in(11, 17, 5 + HALF_UP(19 - AF_D_CAP_H) - AF_D_CAP_Y, &AF_D, dg, glow, RK_WIN);
    rk_name(t, nm);                                    /* the name: large, else the 1U size */
    rk_fit(&AF_DL, nm, 158, fit, sizeof fit);
    if (str_eq(fit, nm))
        rk_glow(33, 6 - AF_DL_CAP_Y, &AF_DL, fit, glow, plate, 1);
    else {
        rk_fit(&AF_D, nm, 158, fit, sizeof fit);
        rk_glow(33, 12 - AF_D_CAP_Y, &AF_D, fit, glow, plate, 1);
    }
    for (j = 0; j < 3u; j++)                           /* the dot matrix: the meter, 7 columns */
        for (i = 0; i < 7u; i++) {
            uint16_t sc = i >= 6u ? RK_RED : i >= 5u ? RK_AMBER : glow;
            cv_disc((199 + (int32_t)i * 5) * 8 + 4, (8 + (int32_t)j * 5) * 8 + 4, 12, i < seg ? sc : ux_mix(sc, plate, 82));
        }
    mw = ink_w(&AF_DS, rk_mname(t));                   /* the machine, the MIDI light before it */
    rk_glow(230 - mw, 26 - AF_DS_CAP_Y, &AF_DS, rk_mname(t), glow, plate, 0);
    cv_disc((224 - mw) * 8 + 4, 30 * 8, 13, lit ? RK_WHITE : ux_mix(plate, RK_WHITE, 14));
    cv_blit(0, (uint32_t)y);
}

/* what shows over the scope: the level while SELECT turns it, a KNOB's name and value while it turns; 0 none */
static int rk_overlay(char *s)
{
    char v[12];
    const char *unit = "";
    if (ui.lvl_t) {
        const param_desc_t *d = track_desc(TSEL, P_LEVEL);
        param_format(d, TSEL->p[P_LEVEL], v, &unit);
        str_cpy(s, "LEVEL ", 32);
    } else if (ui.hot_t && ui.hot_col < 4u) {
        int16_t *vp;
        const param_desc_t *d = home_param(ui.hot_col, &vp);
        if (!d || !vp)
            return 0;
        big_format(d, *vp, v, &unit);
        str_cpy(s, home_label(TSEL, ui.hot_col), 32);
        str_cpy(s + str_len(s), " ", 2);
    } else {
        return 0;
    }
    str_cpy(s + str_len(s), v, 12);
    str_cpy(s + str_len(s), unit, 8);
    return 1;
}

/* the 2U unit's scope: the selected track's signal (fx.c scope_tb), triggered on a rising zero crossing, auto-scaled; the
 * trace before it as a ghost; flat while the track is silent */
static void rk_unit2_scope(uint32_t c, int32_t y, uint32_t seg)
{
    static int16_t snap[SCOPE_TN] __attribute__((section(".pool")));
    track_t *t = &trk[c];
    uint32_t e = rk_eng(t), w = scope_tw, i, trig = 0;
    uint16_t glow = RK_GLOW[e], plate = ux_mix(RGB(18, 20, 24), RK_GLOW[e], 5);
    int32_t cy = 18, peak = 2000, x, py = cy, amp = 15;
    int quiet = seg == 0u && !rk_meter[c];
    char ov[32];
    int over = rk_overlay(ov);
    uint32_t sig = 0x5C0Fu + c * 31u + (uint32_t)y * 7u + e * 977u + ux.gen * 13u;
    if (!quiet)
        sig += w * 2654435761u;                        /* (sounding: every frame) */
    if (over)
        sig = str_hash(sig, ov);
    if (!ui.force && sig == rk_sig[NTRK])
        return;
    rk_sig[NTRK] = sig;
    cv_begin(240, RK_2S, RK_BLACK);
    rk_ears(RK_2S, 29, -1);
    cv_rect(RK_FX, 0, RK_FW, RK_2S, plate);
    cv_rect(RK_FX, RK_2S - 1, RK_FW, 1, ux_mix(plate, RK_BLACK, 50));
    for (x = 0; x < RK_SC_W; x += 7)                   /* the dashed centre line */
        cv_rect(RK_SC_X + x, cy, 4, 1, ux_mix(plate, RK_WHITE, 16));
    for (i = 0; i < SCOPE_TN; i++) {
        snap[i] = quiet ? 0 : scope_tb[(w + i) & (SCOPE_TN - 1u)];
        if (snap[i] > peak)
            peak = snap[i];
        else if (-snap[i] > peak)
            peak = -snap[i];
    }
    for (i = 1; i < SCOPE_TN - RK_SC_W; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) {
            trig = i;
            break;
        }
    if (rk_ghost_ok && !ui.force)                      /* the ghost: the last trace, dim */
        for (x = 1; x < RK_SC_W; x++)
            cv_line(RK_SC_X + x - 1, rk_ghost[x - 1], RK_SC_X + x, rk_ghost[x], ux_mix(plate, glow, 35));
    for (x = 0; x < RK_SC_W; x++) {                    /* the trace: a halo, then the line */
        int32_t yy = cy - snap[trig + (uint32_t)x] * amp / peak;
        if (x) {
            cv_line_t(RK_SC_X + x - 1, py - 1, RK_SC_X + x, yy - 1, ux_mix(plate, glow, 30), 4);
            cv_line_t(RK_SC_X + x - 1, py, RK_SC_X + x, yy, ux_mix(glow, RK_WHITE, 20), 2);
        }
        rk_ghost[x] = (uint8_t)yy;
        py = yy;
    }
    rk_ghost_ok = 1;
    if (over) {                                         /* the turn: its name and value over the scope */
        int32_t ow = ink_w(&AF_M, ov) + 16;
        cv_rrect(120 - ow / 2, 8, ow, 21, 4, RK_WIN, plate);
        GFX_HOOK_ALIGN(120 - ow / 2, 8, 120 - ow / 2 + ow, 29, AL_HV, "rack overlay centred");
        cv_text_in(120 - ow / 2, 8 + CAP_IN(M, 21), ow, &AF_M, ov, RK_WHITE, RK_WIN);
    }
    cv_blit(0, (uint32_t)y + RK_2T);
}

/* the knob panel: KNOB 1..4 */
static void rk_knobs(void)
{
    uint16_t glow = RK_GLOW[rk_eng(TSEL)], well = RGB(14, 16, 19), track = RGB(38, 42, 48);
    uint32_t k, sig = 0x4B4Eu + ux.gen * 7u + rk_eng(TSEL) * 131u + (ui.hot_t ? ui.hot_col + 1u : 0u) * 7919u;
    int32_t a[4], lo[4], bip[4];
    for (k = 0; k < 4u; k++) {
        int16_t *vp;
        const param_desc_t *d = home_param(k, &vp);
        int32_t v = d && vp ? *vp : 0, mn = d ? d->min : 0, mx = d ? d->max : 1;
        a[k] = rk_angle(v, mn, mx);
        bip[k] = mn < 0 && mx > 0;
        lo[k] = bip[k] ? rk_angle(0, mn, mx) : 225;
        sig = sig * 2654435761u + (uint32_t)a[k] * 31u + (uint32_t)bip[k];
    }
    if (!ui.force && sig == rk_sig[NTRK + 1u])
        return;
    rk_sig[NTRK + 1u] = sig;
    cv_begin(240, RK_KH, RGB(26, 29, 33));
    ctl_brushed_c(0, 240, RK_KY, RK_KH, RGB(24, 27, 31), RGB(44, 48, 54));
    cv_rect(0, 0, 240, 1, RK_BLACK);
    cv_rect(0, 1, 240, 1, RGB(64, 68, 76));
    ctl_screw(6, 7, RGB(150, 154, 160), RGB(40, 42, 46));
    ctl_screw(233, 7, RGB(150, 154, 160), RGB(40, 42, 46));
    ctl_screw(6, 41, RGB(150, 154, 160), RGB(40, 42, 46));
    ctl_screw(233, 41, RGB(150, 154, 160), RGB(40, 42, 46));
    for (k = 0; k < 4u; k++) {
        int32_t cx = 30 + 60 * (int32_t)k, cy = 26, x8 = cx * 8, y8 = cy * 8;   /* (an even cap: its centre on a pixel corner) */
        int hot = ui.hot_t && ui.hot_col == k;
        cv_disc(x8, y8, 172, well);                     /* the well, the track, the value */
        cv_arc(x8, y8, 164, 146, 225, 495, track);
        if (a[k] > lo[k])
            cv_arc(x8, y8, 164, 146, lo[k], a[k], glow);
        else if (a[k] < lo[k])
            cv_arc(x8, y8, 164, 146, a[k], lo[k], glow);
        if (hot) {                                      /* the knob just turned: an outer arc and a dot */
            int32_t dx, dy;
            cv_arc(x8, y8, 188, 176, a[k] - 40, a[k] + 40, ux_mix(glow, RK_WHITE, 25));
            ctl_pt(x8, y8, 182, a[k], &dx, &dy);
            cv_disc(dx, dy, 30, ux_mix(glow, RK_BLACK, 40));
            cv_disc(dx, dy, 20, ux_mix(glow, RK_WHITE, 60));
        }
        rk_knob(cx, cy, 34, a[k]);
    }
    cv_blit(0, RK_KY);
}

static void draw_rack(void)
{
    uint32_t c, seg[NTRK];
    int32_t y = RK_Y0;
    if (ui.force) {
        for (c = 0; c < NTRK; c++)
            rk_meter[c] = 0;
        rk_ghost_ok = 0;
    }
    for (c = 0; c < NTRK; c++)
        seg[c] = rk_level(c);
    for (c = 0; c < NTRK; c++) {
        track_t *t = &trk[c];
        int sel = c == song.sel, lit = !t->p[P_MUTE] && midi_lit(c);
        char nm[16];
        uint32_t sig;
        rk_name(t, nm);
        sig = str_hash(1u + (uint32_t)sel * 2u + (uint32_t)lit * 4u + (uint32_t)(t->p[P_MUTE] != 0) * 8u + seg[c] * 16u +
                       (uint32_t)y * 977u + rk_eng(t) * 7919u + (uint32_t)t->p[P_LEVEL] * 131071u + ux.gen * 3u +
                       (rk_eng(t) == ENGI_ACID && sub_on(t)) * 524287u, nm);
        if (ui.force || sig != rk_sig[c]) {
            rk_sig[c] = sig;
            if (sel)
                rk_unit2_top(c, y, seg[c], lit);
            else
                rk_unit1(c, y, seg[c], lit);
        }
        if (sel)
            rk_unit2_scope(c, y, seg[c]);
        y += sel ? RK_2U : RK_1U;
    }
    rk_knobs();
}
