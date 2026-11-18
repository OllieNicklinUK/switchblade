/* SPDX-License-Identifier: GPL-3.0-only */
/* ui_big.c: VOID FOUR's EDIT page (params.c "EDIT", GR_BIG; included by ui_draw.c). The machine's four controls,
 * engine_t.knob[] (KNOB 1..4 = CUT RES ENV + the machine's own: params.c page_id, so the knobs, MIDI LEARN, the locks
 * and MIDI OUT need no case of their own), as four big cards in a 2 x 2 grid:
 *
 *    0  header (ui_draw.c draw_head: head_big): the track's cushion, the machine, the sound, EDIT, the battery
 *   24  KNOB 1 CUTOFF    | KNOB 2 RESONANCE     two 240 x 94 strips (under CV_MAX), each two 116 x 92 cards
 *  118  KNOB 3 ENV       | KNOB 4 <machine's>   (3 px in from the screen's edges, 2 px apart)
 *  212  footer (draw_foot): the selected track's 16 steps, 28 px
 *
 * A card: the label in M (S when M cannot hold it beside the pill; never abbreviated), the K pill (the keycap: the
 * accent while the knob is hot), a 270 degree ring (KNOB_XL, r 34, 3 px: the value's arc; ENV, bipolar, from 12
 * o'clock), the value centred in the ring in XL (40 px figures), else L, else M: the first face whose every glyph is
 * in it and whose ink lies inside the ring (big_fits), the unit (S) in the ring's opening under it. The knob just
 * turned: value, arc and pill in the accent (ui.hot_col / hot_t, ~0.6 s); MONO: its label inverted too. A control
 * MOTION drives: its label in the theme colour. The value snaps (no rolling digits). Each strip is redrawn only
 * when what it shows changed (big_sig) */
#ifndef BIG_HOOK
#define BIG_HOOK(k, label, val, face) ((void)0)        /* host: what card k drew (its face: 0 XL, 1 L, 2 M) */
#endif
#define BIG_Y0 H_HEAD                                   /* the first strip; the second at BIG_Y0 + BIG_SH */
#define BIG_SH 94                                       /* a strip: two cards */
#define BIG_CW 116                                      /* a card */
#define BIG_CH 92
#define BIG_CX(k) (3 + 118 * (int32_t)((k) & 1u))       /* its left in the strip, its top: row 1 */
#define BIG_FOOT_Y (BIG_Y0 + 2 * BIG_SH)                /* the footer: 212 .. 239 */
#define BIG_FOOT_H (240 - BIG_FOOT_Y)
#define BIG_PY 6                                        /* the K pill's top (card rows 6 .. 18), the label on its line */
#define BIG_RX 24                                       /* the ring's box in the card: 24 .. 91 x 22 .. 89 */
#define BIG_RY 22
#define BIG_RD (2 * KNOB_XL_R)
#define BIG_FIT (KNOB_XL_R - KNOB_XL_W - 2)             /* the value's ink inside this radius: 2 px clear of the ring */
#define BIG_UY 76                                       /* the unit's line (S capitals rows 79 .. 87) */

static const char *const BIG_MLABEL[NENGINES] = {"DETUNE", "POSITION", "DECAY", "DRIVE",
                                                 "DETUNE", "DCW ENV", "ROTOR", "ENV POS"};   /* (V2: JP-Z CZ-Z GW-Z MICRO) */   /* knob[3] spelled out
                                                         * (the 4-letter edit[].label stays the MACHINE pages') */
static uint32_t big_sig[2];

static uint32_t big_id(const track_t *t, uint32_t k) { return ENGINES[t->eng_req % NENGINES]->knob[k & 3u]; }
/* knob[3] spelled out: the machine's, NANO's SUB model its own (E2 is OSC2 there, not the 303's DECAY) */
static const char *big_mlabel(const track_t *t)
{
    return t->eng_req % NENGINES == ENGI_ACID && sub_on(t) ? "OSC 2" : BIG_MLABEL[t->eng_req % NENGINES];
}
static const char *big_label(const track_t *t, uint32_t k)
{
    static const char *const L[3] = {"CUTOFF", "RESONANCE", "ENV"};
    return k < 3u ? L[k] : big_mlabel(t);
}

/* s in f, its ink centred across and its capitals' band (cap_y .. + cap_h of the line) up and down on a point: every
 * glyph's ink box inside a circle of r round it (the ring's inside; the boxes, not the string's: a sign's or a
 * point's ink is low or short, a corner of the string's box is empty) */
static int big_fits(const aafont_t *f, const char *s, int32_t cap_y, int32_t cap_h, int32_t r)
{
    int32_t b[4], mid2, cy2 = 2 * cap_y + cap_h, pen = 0;
    uint32_t prev = 0;
    text_ink(f, s, b);
    if (b[2] <= b[0])
        return 0;
    mid2 = b[0] + b[2];                                 /* (doubled: the half pixels kept) */
    for (; *s; s++) {
        uint32_t c = fold(f, (uint8_t)*s);
        const aag_t *g;
        if (prev)
            pen += kern(f, prev, c);
        g = &f->g[glyph(f, c)];
        if (g->bw) {
            int32_t x0 = 2 * (((pen + 8) >> 4) + g->bx) - mid2, x1 = x0 + 2 * g->bw;
            int32_t y0 = 2 * g->by - cy2, y1 = y0 + 2 * g->bh;
            int32_t dx = (x0 < 0 ? -x0 : x0) > (x1 < 0 ? -x1 : x1) ? (x0 < 0 ? -x0 : x0) : (x1 < 0 ? -x1 : x1);
            int32_t dy = (y0 < 0 ? -y0 : y0) > (y1 < 0 ? -y1 : y1) ? (y0 < 0 ? -y0 : y0) : (y1 < 0 ? -y1 : y1);
            if (dx * dx + dy * dy > 4 * r * r)
                return 0;
        }
        pen += g->adv;
        prev = c;
    }
    return 1;
}
/* a value as the big cards show it (param_format's, for these cards only): a cutoff of 10 kHz or more in whole kHz
 * ("16", unit kHz: "16.0" fits no ring in L) */
static void big_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    param_format(d, v, val, unit);
    fmt_named = 0;
    if (d->fmt == F_CUTOFF && CUTOFF_HZ[v & 127] >= 10000u)
        fmt_int(val, (int32_t)((CUTOFF_HZ[v & 127] + 500u) / 1000u));
}
/* the face of a value: 0 XL, 1 L, 2 M */
static int big_face_of(const char *v)
{
    if (xl_face_has(v) && big_fits(&AF_XL, v, AF_XL_CAP_Y, AF_XL_CAP_H, BIG_FIT))
        return 0;
    if (large_face_has(v) && big_fits(&AF_L, v, AF_L_CAP_Y, AF_L_CAP_H, BIG_FIT))
        return 1;
    return 2;
}
/* the same inside a ring of inner radius r (WAVE's: no XL) */
static int big_face_of_r(const char *v, int32_t r)
{
    return large_face_has(v) && big_fits(&AF_L, v, AF_L_CAP_Y, AF_L_CAP_H, r) ? 1 : 2;
}

/* card k (116 x 92) into the strip being drawn, its top-left at (x, y) */
static void draw_big_card(uint32_t k, uint32_t x, uint32_t y)
{
    const track_t *t = TSEL;
    int16_t *vp;
    const param_desc_t *d = page_desc(cur_page(), k, &vp);
    const char *label = big_label(t, k), *unit = "";
    char val[12];
    int hot = k == ui.hot_col && ui.hot_t, mot, face;
    uint16_t vc = VAL(k), lc, fill = hot ? T_ACCENT : T_KEY;
    int32_t px, lx = (int32_t)x + 8, ly, lw, room, v;
    const aafont_t *lf = &AF_M, *vf;
    cv_rrect((int32_t)x, (int32_t)y, BIG_CW, BIG_CH, 6, T_SURF, T_BG);
    if (!d || !vp)
        return;
    big_format(d, *vp, val, &unit);
    card_mot_of(vp);
    mot = card_mot_next;
    card_mot_next = 0;
    card_mot = (uint8_t)((card_mot & ~(1u << k)) | (uint32_t)mot << k);
    lc = hot ? T_ACCENT : mot ? T_THEME : T_MID;
    {   /* the knob: its keycap at the right */
        px = (int32_t)x + BIG_CW - 7 - kc_w(KC_K1 + k);
        cv_keycap(px, (int32_t)y + BIG_PY, KC_K1 + k, fill, T_INK, T_SURF);
    }
    room = px - 5 - lx;
    if (ink_w(lf, label) > room)                        /* M too wide beside the pill: S, on the same line */
        lf = &AF_S;
    ly = (int32_t)y + BIG_PY + (lf == &AF_M ? CAP_IN(M, KC_H) : CAP_IN(S, KC_H));
    lw = ink_w(lf, label);
    GFX_HOOK_ALIGN(0, (int32_t)y + BIG_PY, 0, (int32_t)y + BIG_PY + KC_H, AL_V, "big card label on its pill's line");
    if (hot && T_ACCENT == T_MID) {                     /* MONO: the label inverted, the chip 2 px round its ink */
        int32_t b[4], cy = ly + (lf == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y), chh = lf == &AF_M ? AF_M_CAP_H : AF_S_CAP_H;
        text_ink(lf, label, b);
        cv_rrect(lx + b[0] - 2, cy - 3, lw + 4, chh + 6, 3, T_ACCENT, T_SURF);
        cv_text_on(lx, ly, lf, label, T_INK, T_ACCENT);
    } else {
        cv_text_fit(lx, ly, lf, label, lc, T_SURF, room);
    }
    v = d->fmt == F_ENUM ? enum_rank(d, *vp) : *vp;
    knob((int32_t)x + BIG_RX, (int32_t)y + BIG_RY, KNOB_XL_R, KNOB_XL_COV, KNOB_XL_ANG, v, d->min, d->max, vc);
    face = big_face_of(val);
    if (face == 2 && val[0] == '+' && big_face_of(val + 1) < 2) {   /* the '+' all that kept it from L / XL ("+100"):
                                                                     * dropped ('-' always stays) */
        str_cpy(val, val + 1, sizeof val);
        face = big_face_of(val);
    }
    vf = face == 0 ? &AF_XL : face == 1 ? &AF_L : &AF_M;
    BIG_HOOK(k, label, val, face);
    GFX_HOOK_ALIGN((int32_t)x + BIG_RX, (int32_t)y + BIG_RY, (int32_t)x + BIG_RX + BIG_RD,
                   (int32_t)y + BIG_RY + BIG_RD, AL_HV, "big card value centred in its ring");
    cv_text_in((int32_t)x + BIG_RX, (int32_t)y + BIG_RY +
               (face == 0 ? CAP_IN(XL, BIG_RD) : face == 1 ? CAP_IN(L, BIG_RD) : CAP_IN(M, BIG_RD)),
               BIG_RD, vf, val, vc, T_SURF);
    if (unit[0]) {                                      /* in the ring's opening, under the value */
        GFX_HOOK_ALIGN((int32_t)x + BIG_RX, 0, (int32_t)x + BIG_RX + BIG_RD, 0, AL_H, "big card unit centred");
        cv_text_in((int32_t)x + BIG_RX, (int32_t)y + BIG_UY, BIG_RD, &AF_S, unit, T_MID, T_SURF);
    }
}

/* what card k shows, as a number: its value (the ring's) and its text (an engine's desc hook can name a value by
 * another parameter), its label, hot / motion state */
static uint32_t big_card_sig(uint32_t k)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(cur_page(), k, &vp);
    uint32_t id = big_id(TSEL, k), h = id * 7919u + (k == ui.hot_col && ui.hot_t) * 104729u +
               (uint32_t)(id < P_COUNT && ((card_mot_mask[id / 32u] >> (id % 32u)) & 1u)) * 15485863u;
    if (d && vp) {
        char val[12];
        const char *unit;
        big_format(d, *vp, val, &unit);
        h = str_hash(str_hash(h + (uint32_t)(*vp + 1000) * 131u + (uint32_t)d->min * 31u + (uint32_t)d->max, val), unit);
    }
    return h;
}

/* ---------------------------------------------------- the machines' own controls (UI.4) ---
 * FLAT draws each machine's panel as the hardware it is modelled on (assets/refs/): OB-Z the OB-6's chunky knobs with their 11 ticks
 * on bevelled cards between wood cheeks (the trim), WAVE the plugin's glowing amber rings, ACID the TB-303's black skirted knobs on
 * brushed silver with corner screws, DRIFT the Juno-106's four vertical faders (tan caps, 11 ticks each) under a red band with
 * a VCF bracket. OB-Z, ACID and DRIFT read their value on a 7-segment LED under the control (ui_led.c; the unit beside it), WAVE
 * inside its ring in Exo 2. A value an LED cannot spell ("OFF") goes in the recess as text. STYLE LINE keeps the old cards
 * (draw_big_card). The K pills are gone here: the position is the knob (left to right, top to bottom), the label and the
 * LED turn to the accent / hot colour while a knob is turned. */
#define EC_CW 104                                       /* a card (OB-Z, WAVE): 104 x 92 at x 13 and 123 */
#define EC_CX(k) (13 + 110 * (int32_t)((k) & 1u))
#define EC_LABY 5                                       /* a label's capitals: M rows 5 .. 15 of the strip */
typedef struct {
    const param_desc_t *d;
    int16_t *vp;
    const char *label, *unit;
    char val[12];
    int hot;
    uint16_t vc, lc;
    int32_t frac;                                       /* the value's place in its range, 0 .. 256 */
} edit_card_t;
static int edit_card_get(uint32_t k, edit_card_t *c)
{
    const track_t *t = TSEL;
    int32_t v, span;
    int mot;
    c->d = page_desc(cur_page(), k, &c->vp);
    c->label = big_label(t, k);
    c->unit = "";
    c->val[0] = 0;
    c->hot = k == ui.hot_col && ui.hot_t;
    c->vc = VAL(k);
    c->frac = 0;
    if (!c->d || !c->vp)
        return 0;
    big_format(c->d, *c->vp, c->val, &c->unit);
    card_mot_of(c->vp);
    mot = card_mot_next;
    card_mot_next = 0;
    card_mot = (uint8_t)((card_mot & ~(1u << k)) | (uint32_t)mot << k);
    c->lc = c->hot ? T_ACCENT : mot ? T_THEME : T_MID;
    v = c->d->fmt == F_ENUM ? enum_rank(c->d, *c->vp) : *c->vp;
    span = c->d->max - c->d->min;
    c->frac = span > 0 ? (v - c->d->min) * 256 / span : 0;
    if (c->frac < 0) c->frac = 0;
    if (c->frac > 256) c->frac = 256;
    return 1;
}
/* a label centred over [x, x + w), its capitals from row top: M when it fits, else S */
static void edit_label(int32_t x, int32_t w, int32_t top, const char *label, uint16_t col, uint16_t under)
{
    const aafont_t *f = w >= 100 && ink_w(&AF_M, label) <= w - 6 ? &AF_M : &AF_S;       /* (a narrow column: S) */
    GFX_HOOK_ALIGN(x, 0, x + w, 0, AL_H, "edit label centred");
    cv_text_in(x, top - (f == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y), w, f, label, col, under);
}
static int edit_numeric(const char *s)                  /* digits, a sign and points only (an LED can spell it) */
{
    int digit = 0;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') digit = 1;
        else if (*s != '-' && *s != '.' && *s != '+') return 0;
    }
    return digit;
}
/* the LED readout of a value with its unit, the pair centred on cx, the recess's top at y */
static void edit_led(int32_t cx, int32_t y, const edit_card_t *c, uint16_t under)
{
    uint32_t n = c->val[0] && edit_numeric(c->val) ? led_cells(c->val) : 4u;
    int32_t w, uw = c->unit[0] ? text_w(&AF_S, c->unit) + 3 : 0, x;
    if (n < 3u) n = 3u;
    if (n > 5u) n = 5u;
    w = led_w(LED_S, n);
    x = cx - HALF_UP(w + uw);
    if (c->val[0] && edit_numeric(c->val)) {
        cv_led(x, y, LED_S, n, c->val, under, c->hot ? LED_HOT : LED_ON);
    } else {                                            /* a word: in the recess, in S */
        cv_rrect(x, y, w, led_h(LED_S), 2, LED_BG, under);
        cv_text_in(x, y + CAP_IN(S, led_h(LED_S)), w, &AF_S, c->val, c->hot ? LED_HOT : LED_ON, LED_BG);
    }
    if (c->unit[0]) {
        GFX_HOOK_ALIGN(0, y, 0, y + led_h(LED_S), AL_V, "edit unit beside its LED");
        cv_text_on(x + w + 3, y + CAP_IN(S, led_h(LED_S)), &AF_S, c->unit, T_MID, under);
    }
}

/* OB-Z: the OB-6's knob at (cx, cy): 11 ticks over 270 degrees, a dark ring with a rim, a lighter cap with a highlight, a white
 * pointer at the value's angle (the accent while it is turned) */
static void edit_knob_obz(int32_t cx, int32_t cy, int32_t frac, int hot)
{
    int32_t cx8 = cx * 8 + 4, cy8 = cy * 8 + 4, i, a = -135 + 270 * frac / 256, x0, y0, x1, y1;
    uint16_t ring = ux_mix(T_BG, T_TEXT, 7), rim = ux_mix(T_BG, T_TEXT, 30), cap = ux_mix(T_BG, T_TEXT, 22),
             hi = ux_mix(T_BG, T_TEXT, 40), ptr = hot ? T_ACCENT : T_TEXT;
    for (i = 0; i < 11; i++) {
        ctl_pt(cx8, cy8, 25 * 8, -135 + 27 * i, &x0, &y0);
        ctl_pt(cx8, cy8, 28 * 8, -135 + 27 * i, &x1, &y1);
        cv_stroke(x0, y0, x1, y1, 5, T_MID);
    }
    cv_disc(cx8, cy8, 19 * 8 + 5, rim);
    cv_disc(cx8, cy8, 19 * 8, ring);
    cv_disc(cx8, cy8, 14 * 8, cap);
    cv_arc(cx8, cy8, 14 * 8, 12 * 8 + 4, -140, -40, hi);
    ctl_pt(cx8, cy8, 4 * 8, a, &x0, &y0);
    ctl_pt(cx8, cy8, 14 * 8, a, &x1, &y1);
    cv_stroke(x0, y0, x1, y1, 7, ptr);
}
/* ACID: the TB-303's knob: a black skirt with a lighter rim, the black body, a silver cap, a white pointer on the body */
static void edit_knob_acid(int32_t cx, int32_t cy, int32_t frac, int hot)
{
    int32_t cx8 = cx * 8 + 4, cy8 = cy * 8 + 4, a = -135 + 270 * frac / 256, x0, y0, x1, y1;
    uint16_t skirt = T_TEXT, rim = ux_mix(T_TEXT, T_BG, 28), body = ux_mix(T_TEXT, T_BG, 10), cap = ux_mix(T_BG, T_SURF, 25),
             ptr = hot ? T_ACCENT : T_BG;
    cv_disc(cx8, cy8 + 8, 22 * 8, ux_mix(T_BG, T_SURF, 70));    /* its shadow */
    cv_disc(cx8, cy8, 21 * 8, skirt);
    cv_arc(cx8, cy8, 21 * 8, 19 * 8 + 4, -150, 30, rim);
    cv_disc(cx8, cy8, 17 * 8, body);
    cv_disc(cx8, cy8, 11 * 8, T_SURF);
    cv_disc(cx8, cy8, 10 * 8, cap);
    ctl_pt(cx8, cy8, 12 * 8, a, &x0, &y0);
    ctl_pt(cx8, cy8, 19 * 8, a, &x1, &y1);
    cv_stroke(x0, y0, x1, y1, 8, ptr);
}
/* WAVE: the plugin's ring: a faint track over 270 degrees, the value's arc in the theme colour with two wider, fainter ones under it (the glow) */
#define EW_RO 27                                        /* the ring's outer and inner radius */
#define EW_RI 21
static void edit_ring_wave(int32_t cx, int32_t cy, int32_t frac, int bipolar, uint16_t col, uint16_t under)
{
    int32_t cx8 = cx * 8 + 4, cy8 = cy * 8 + 4, a1 = -135 + 270 * frac / 256, s0 = bipolar ? (a1 < 0 ? a1 : 0) : -135,
            s1 = bipolar ? (a1 < 0 ? 0 : a1) : a1;       /* (a bipolar value's arc runs from 12 o'clock) */
    cv_arc(cx8, cy8, EW_RO * 8, EW_RI * 8, -135, 135, ux_mix(under, T_TEXT, 7));
    if (s1 > s0) {
        cv_arc(cx8, cy8, (EW_RO + 2) * 8, (EW_RI - 2) * 8, s0, s1, ux_mix(under, col, 16));
        cv_arc(cx8, cy8, (EW_RO + 1) * 8, (EW_RI - 1) * 8, s0, s1, ux_mix(under, col, 38));
        cv_arc(cx8, cy8, EW_RO * 8, EW_RI * 8, s0, s1, col);
    }
}
/* DRIFT's geometry (screen rows): the red band 24 .. 26, the VCF bracket at 36, the labels' capitals from 45, the faders' travel
 * 66 .. 178 (cap centres), the LEDs 191 .. 205 */
#define ED_BAND_Y 24
#define ED_VCF_Y 36
#define ED_LAB_Y 45
#define ED_F0 66
#define ED_F1 178
#define ED_LED_Y 191
#define ED_FX(k) (30 + 60 * (int32_t)(k))
static void edit_fader_drift(int32_t cx, int32_t frac, int hot)
{
    int32_t y, i, ty = ED_F1 - (ED_F1 - ED_F0) * frac / 256;
    uint16_t slot = ux_mix(T_BG, T_TEXT, 7), key = T_KEY, khi = ux_mix(T_KEY, T_TEXT, 30), ksh = ux_mix(T_KEY, T_BG, 45);
    cv_rrect(cx - 2, ED_F0 - 6, 4, ED_F1 - ED_F0 + 12, 2, slot, T_BG);
    for (i = 0; i < 11; i++) {                          /* the ticks, left of the slot */
        y = ED_F0 + (ED_F1 - ED_F0) * i / 10;
        cv_rect(cx - 15, y, 5, 1, T_MID);
    }
    cv_rrect(cx - 10, ty - 6, 20, 12, 2, key, T_BG);
    cv_rect(cx - 9, ty - 6, 18, 1, khi);
    cv_rect(cx - 9, ty + 5, 18, 1, ksh);
    cv_rect(cx - 9, ty, 18, 1, hot ? T_ACCENT : T_BG);   /* the dark line across the middle */
}

/* one OB-Z / WAVE card (104 x 92) at x in the strip being drawn */
static void edit_card_card(uint32_t k, int32_t x, uint32_t wave)
{
    edit_card_t c;
    int32_t cx = x + EC_CW / 2;
    uint16_t under = T_BG;
    ctl_card(x, 1, EC_CW, 92, 6, T_SURF, under);
    if (!edit_card_get(k, &c))
        return;
    edit_label(x, EC_CW, 1 + EC_LABY, c.label, c.lc, T_SURF);
    if (!wave) {
        edit_knob_obz(cx, 1 + 47, c.frac, c.hot);
        edit_led(cx, 1 + 73, &c, T_SURF);
        BIG_HOOK(k, c.label, c.val, c.val[0] && edit_numeric(c.val) ? 3 : 4);
    } else {
        int face;
        char val[12];
        str_cpy(val, c.val, sizeof val);
        edit_ring_wave(cx, 1 + 52, c.frac, c.d->min < 0 && c.d->max > 0, c.vc, T_SURF);
        face = big_face_of_r(val, EW_RI - 3);
        if (face == 2 && val[0] == '+' && big_face_of_r(val + 1, EW_RI - 3) < 2) {
            str_cpy(val, val + 1, sizeof val);
            face = big_face_of_r(val, EW_RI - 3);
        }
        BIG_HOOK(k, c.label, val, face);
        GFX_HOOK_ALIGN(cx - EW_RI, 1 + 52 - EW_RI, cx + EW_RI, 1 + 52 + EW_RI, AL_HV, "edit wave value centred in its ring");
        cv_text_in(cx - EW_RI, 1 + 52 - EW_RI + (face == 1 ? CAP_IN(L, 2 * EW_RI) : CAP_IN(M, 2 * EW_RI)), 2 * EW_RI,
                   face == 1 ? &AF_L : &AF_M, val, c.vc, T_SURF);
        if (c.unit[0]) {
            GFX_HOOK_ALIGN(cx - EW_RI, 0, cx + EW_RI, 0, AL_H, "edit wave unit centred");
            cv_text_in(cx - EW_RI, 1 + 52 + 14 - AF_S_CAP_Y, 2 * EW_RI, &AF_S, c.unit, T_MID, T_SURF);
        }
    }
}
/* ACID: a knob over its label and LED, on the brushed silver, at column centre cx */
static void edit_card_acid(uint32_t k, int32_t cx)
{
    edit_card_t c;
    if (!edit_card_get(k, &c))
        return;
    edit_label(cx - 56, 112, EC_LABY, c.label, c.hot ? T_ACCENT : T_TEXT, T_BG);
    edit_knob_acid(cx, 46, c.frac, c.hot);
    edit_led(cx, 71, &c, T_BG);
    BIG_HOOK(k, c.label, c.val, c.val[0] && edit_numeric(c.val) ? 3 : 4);
}
/* DRIFT: the whole page into the strip being drawn (cv_oy set to the strip's top: everything clips) */
static void edit_page_drift(void)
{
    uint32_t k;
    int32_t y = ED_VCF_Y;
    GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H, "edit VCF label centred");
    cv_text_in(0, y - 4 - AF_S_CAP_Y, 240, &AF_S, "VCF", T_MID, T_BG);
    for (k = 0; k < 2u; k++) {                          /* the bracket: rules either side with a tick at each end */
        int32_t x0 = k ? 134 : 10, x1 = k ? 230 : 106;
        cv_rect(x0, y, x1 - x0, 1, T_LINE);
        cv_rect(k ? x1 - 1 : x0, y, 1, 5, T_LINE);
    }
    for (k = 0; k < 4u; k++) {
        edit_card_t c;
        int32_t cx = ED_FX(k);
        if (!edit_card_get(k, &c))
            continue;
        edit_label(cx - 30, 60, ED_LAB_Y, c.label, c.lc, T_BG);   /* (S: 'RESONANCE' fills its column) */
        edit_fader_drift(cx, c.frac, c.hot);
        edit_led(cx, ED_LED_Y, &c, T_BG);
        BIG_HOOK(k, c.label, c.val, c.val[0] && edit_numeric(c.val) ? 3 : 4);
    }
}

/* the page's two strips (draw_columns), each when it changed */
static void draw_edit_big(void)
{
    uint32_t r, k, e = TSEL->eng_req % NENGINES;
    (void)motion_mask(song.sel, card_mot_mask);
    for (k = 0; k < 4u; k++)                            /* (no rolling digits here: a roll the last page left, done) */
        ui.roll[k].from[0] = 0;
    for (r = 0; r < 2u; r++) {
        uint32_t sig = str_hash(TSEL->eng_req * 977u + song.sel * 31u + ux.gen * 7u + ux.style + 1u, "EDIT");
        uint32_t lo = !ux.style && e == ENGI_DRIFT ? 0u : 2u * r, hi = !ux.style && e == ENGI_DRIFT ? 4u : 2u * r + 2u;   /* (DRIFT: the faders cross the seam) */
        for (k = lo; k < hi; k++)
            sig = sig * 2654435761u + big_card_sig(k);
        if (!ui.force && sig == big_sig[r])
            continue;
        big_sig[r] = sig;
        cv_begin(240, BIG_SH, T_BG);
        if (!ux.style && e == ENGI_ACID)
            ctl_brushed(0, 240, BIG_Y0 + (int32_t)r * BIG_SH, BIG_SH);
        trim_cv(TG_EDIT, 0, BIG_Y0 + (int32_t)r * BIG_SH, 240, BIG_SH);
        if (ux.style) {
            for (k = 2u * r; k < 2u * r + 2u; k++)
                draw_big_card(k, (uint32_t)BIG_CX(k), 1u);
            cv_rect(BIG_CX(1) - 2, 1, 1, BIG_CH, T_RULE);   /* LINE: the dividers (no SURF): between the cards, under the */
            cv_rect(BIG_CX(0), r ? BIG_SH - 1 : 0, BIG_CX(1) + BIG_CW - BIG_CX(0), 1, T_RULE);   /* header, between the strips, above the footer */
            if (!r)
                cv_rect(BIG_CX(0), BIG_SH - 1, BIG_CX(1) + BIG_CW - BIG_CX(0), 1, T_RULE);
        } else if (e == ENGI_DRIFT) {
            cv_oy = -(BIG_Y0 + (int32_t)r * BIG_SH);
            edit_page_drift();
            cv_oy = 0;
        } else if (e == ENGI_ACID) {
            for (k = 2u * r; k < 2u * r + 2u; k++)
                edit_card_acid(k, (k & 1u) ? 174 : 66);
            ctl_screw(7, r ? 87 : 6, ux_mix(T_BG, T_SURF, 60), T_TEXT);
            ctl_screw(233, r ? 87 : 6, ux_mix(T_BG, T_SURF, 60), T_TEXT);
        } else {
            for (k = 2u * r; k < 2u * r + 2u; k++)
                edit_card_card(k, EC_CX(k), e == ENGI_WAVE);
        }
        cv_blit(0, BIG_Y0 + r * BIG_SH);
    }
}

/* the header's left on EDIT (draw_head, no message or layer showing): from x, the machine (M, THEME) and the sound
 * (M, TEXT; ellipsised), "EDIT" (S, MID) ending at r */
static void head_big(int32_t x, int32_t r)
{
    char pn[16];
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    int32_t ew = text_w(&AF_S, "EDIT");
    sound_name(TSEL, pn);
    GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
    x = cv_text(x, HEAD_MY, &AF_M, e->name, T_THEME) + 7;
    if (pn[0]) {
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        cv_free_text(x, HEAD_MY, &AF_M, pn, T_TEXT, T_BG, r - ew - 8 - x);
    }
    GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
    cv_text(r - ew, HEAD_SY, &AF_S, "EDIT", T_MID);
}

/* ------------------------------------------------------------------ HOME --- */
/* Switch Blade's HOME (ui_draw.c draw_columns, ui.home without a layer: big_home): a rack of four units, one per track, each
 * in the style of its machine, over the four knob values. Its knobs are the EDIT page's four (2026-10-20: CUT RES ENV + the
 * machine's own, ui.c home_param, ui_input.c home_turn); the dedicated encoders stay: ALGORITHM the track, PRESETS the sound,
 * SELECT the track's LEVEL.
 *
 *    0  header (draw_head): the track's cushion, the transport and the BPM (an LED), the battery
 *   24  four rack units, 40 px each (a canvas of its own): rack ears with two screws each side, a faceplate in its machine's
 *       look (OB-Z: black, wood cheeks, a blue pinstripe; WAVE: near black, an amber underline with its glow; ACID: brushed
 *       silver; DRIFT: dark, a red band), the track's number on an LED (large), the machine's badge, the sound's category, its
 *       name (M), its number in the machine's list on an LED, and an activity LED that lights while the track sounds (its
 *       meter, a MIDI IN note); the selected track has a white outline (1.5 px) round its faceplate; a muted track's unit is dim
 *  184  KNOB 1..4 (240 x 28, four 57 x 26 bevelled cells): CUTOFF RES ENV and the machine's own, the label (S) over the value
 *       (M) and its unit, as EDIT's cards format them (big_format); the knob just turned: its label and value in the accent; a
 *       control MOTION drives: its label in the theme colour
 *  212  footer (draw_foot, as EDIT's): the selected track's 16 steps, 28 px
 * The unit's colours are its machine's own palette (UI_MACHINE_PALS), not the selected track's. The old level bar of the sound
 * panel and the tabs' meters are gone: the activity LED shows that the track sounds, MIXER shows how loud. LINE (no SURF): the
 * cells are separated by rules instead of bevels. Each strip is drawn again only when what it shows changed (home_sig) */
#ifndef HOME_HOOK
#define HOME_HOOK(cat, num, name, face) ((void)0)       /* host: the selected unit's category, number and name (face 2: M) */
#endif
#define HOME_TY H_HEAD                                  /* the rack: 24 .. 183 */
#define HU_H 40                                         /* a unit */
#define HOME_CY (HOME_TY + 4 * HU_H)                    /* the cells: 184 .. 211 */
#define HOME_CH 28
typedef char home_fits[HOME_CY + HOME_CH == BIG_FOOT_Y ? 1 : -1];   /* (EDIT's footer: the steps do not move) */
#define HU_FX 12                                        /* the faceplate: x 12 .. 227, rows 1 .. 38 of the unit */
#define HU_FW 216
#define HU_LX 24                                        /* the track's LED recess (large, 19 x 24), rows 8 .. 31 */
#define HU_LY 8
#define HU_BX 48                                        /* the badge (15 tall, rows 6 .. 20) and the name's left */
#define HU_BY 6
#define HU_BH 15
#define HU_NY 23                                        /* the name's capitals (M): rows 23 .. 33 */
#define HU_PX 194                                       /* the preset number's LED (2 cells, 22 x 15), rows 6 .. 20 */
#define HU_AX 205                                       /* the activity LED's centre, row 28 */
#define HU_AY 28
#define HC_H 26                                         /* a cell (rows 1 .. 26) */
#define HC_LY 3                                         /* its label's capitals (S): rows 3 .. 11 */
#define HC_VY 14                                        /* its value's capitals (M): rows 14 .. 24 */
static uint32_t home_sig[NTRK + 1u];                    /* the units, the cells */

/* the sound's category, its number ("07 / 32"; a user preset's slot) and name, of track t's sound */
static void home_sound_of(const track_t *t, char *cat, char *num, char *name)
{
    uint32_t total, cur = eng_list_pos_of(t, &total), u = user_of(t), np = preset_shown(t->eng_req % NENGINES);
    sound_name(t, name);
    if (u < UP_SLOTS) {
        str_cpy(cat, "USER", 16);
        str_cpy(num, "SLOT ", 16);
        num[5] = (char)('0' + (u + 1u) / 10u % 10u);
        num[6] = (char)('0' + (u + 1u) % 10u);
        num[7] = 0;
    } else {
        str_cpy(cat, CAT_NAME[track_cat(t) % NCAT], 16);
        num[0] = (char)('0' + (cur + 1u) / 10u % 10u);
        num[1] = (char)('0' + (cur + 1u) % 10u);
        str_cpy(num + 2, " / ", 14);
        fmt_int(num + 5, (int32_t)np);
    }
    (void)total;
}

/* the colours of a unit: its machine's palette's tokens */
static uint16_t hu_mid(const ui_pal_t *p, int pct) { return ux_mix(p->bg, p->text, pct); }

/* unit c (240 x 40) */
static void home_unit(uint32_t c)
{
    track_t *t = &trk[c];
    uint32_t e = t->eng_req % NENGINES, sel = c == song.sel, mute = t->p[P_MUTE] != 0, lit = (uint32_t)midi_lit(c), sig, act;
    const ui_pal_t *p = &UI_MACHINE_PALS[e];
    int32_t m = mute ? 0 : meter_px(t->peak), y0 = HOME_TY + (int32_t)c * HU_H, bw, nx;
    char cat[16], num[16], name[16], d[2] = {(char)('1' + c), 0}, pn[4];
    uint16_t fill = e == ENGI_ACID ? p->text : p->theme, light = ux_luma(p->text) > ux_luma(p->bg) ? p->text : p->bg,
             ink = ux_luma(fill) > 150u ? (light == p->text ? p->bg : p->text) : light,
             tx = mute ? hu_mid(p, 45) : p->text, ear = ux_mix(T_BG, T_TEXT, 16);
    t->peak = 0;
    if (m < ts.meter[c] - 1)
        m = ts.meter[c] - 1;                            /* (falls as the MIXER's: ~2 dB a frame) */
    ts.meter[c] = (uint8_t)(m < 0 ? 0 : m);
    act = !mute && (ts.meter[c] >= 3 || lit);
    home_sound_of(t, cat, num, name);
    sig = str_hash(str_hash(str_hash(1u + sel + mute * 2u + act * 4u + ux.gen * 8u + ux.style * 7919u + e * 977u, cat), num), name);
    if (!ui.force && sig == home_sig[c])
        return;
    home_sig[c] = sig;
    cv_begin(240, HU_H, T_BG);
    if (ux.style)                                       /* LINE: a rule over each unit (under the header, between the units) */
        cv_rect(RULE_X0, 0, RULE_W, 1, T_RULE);
    cv_rect(0, 1, HU_FX, 38, ear);                      /* the rack ears, two screws each */
    cv_rect(HU_FX + HU_FW, 1, 240 - HU_FX - HU_FW, 38, ear);
    ctl_screw(6, 8, ux_mix(ear, T_TEXT, 45), T_BG);
    ctl_screw(6, 31, ux_mix(ear, T_TEXT, 45), T_BG);
    ctl_screw(233, 8, ux_mix(ear, T_TEXT, 45), T_BG);
    ctl_screw(233, 31, ux_mix(ear, T_TEXT, 45), T_BG);
    if (e == ENGI_ACID)                                 /* the faceplate */
        ctl_brushed_c(HU_FX, HU_FW, y0 + 1, 38, p->bg, p->surf);
    else
        cv_rect(HU_FX, 1, HU_FW, 38, p->bg);
    if (e == ENGI_OBX) {                                /* the OB-6: wood cheeks, a blue pinstripe */
        ctl_wood(HU_FX + 2, 3, 6, 34, HU_FX + 2, y0 + 3);
        ctl_wood(HU_FX + HU_FW - 8, 3, 6, 34, HU_FX + HU_FW - 8, y0 + 3);
        cv_rect(HU_FX + 8, 36, HU_FW - 16, 1, ux_mix(p->bg, p->theme, 75));
    } else if (e == ENGI_WAVE) {                        /* the plugin: an amber underline and its glow */
        cv_rect(HU_FX + 2, 34, HU_FW - 4, 1, ux_mix(p->bg, p->theme, 14));
        cv_rect(HU_FX + 2, 35, HU_FW - 4, 1, ux_mix(p->bg, p->theme, 36));
        cv_rect(HU_FX + 2, 36, HU_FW - 4, 1, p->theme);
    } else if (e == ENGI_DRIFT) {                       /* the Juno: a red band across the top */
        cv_rect(HU_FX + 2, 2, HU_FW - 4, 3, p->theme);
    }
    cv_led(HU_LX, HU_LY, LED_L, 1u, d, p->bg, LED_ON);  /* the track's number */
    bw = ink_w(&AF_S, ENGINES[e]->name) + 14;           /* the badge */
    cv_rrect(HU_BX, HU_BY, bw, HU_BH, 4, mute ? ux_mix(fill, p->bg, 55) : fill, p->bg);
    GFX_HOOK_ALIGN(HU_BX, HU_BY, HU_BX + bw, HU_BY + HU_BH, AL_HV, "rack badge label centred");
    cv_text_in(HU_BX, HU_BY + CAP_IN(S, HU_BH), bw, &AF_S, ENGINES[e]->name, ink, mute ? ux_mix(fill, p->bg, 55) : fill);
    nx = HU_BX + bw + 8;
    GFX_HOOK_ALIGN(0, HU_BY, 0, HU_BY + HU_BH, AL_V, "rack category on its badge's line");
    cv_text_fit(nx, HU_BY + CAP_IN(S, HU_BH), &AF_S, cat, hu_mid(p, 70), p->bg, HU_PX - 6 - nx);
    GFX_HOOK_ALIGN(0, HU_NY, 0, HU_NY + AF_M_CAP_H, AL_V, "rack name on its band");
    cv_free_text(HU_BX, HU_NY - AF_M_CAP_Y, &AF_M, name, tx, p->bg, HU_PX - 6 - HU_BX);
    pn[0] = num[0]; pn[1] = num[1]; pn[2] = 0;
    if (num[0] == 'S') {                                /* a user preset: "SLOT 07" -> "07" */
        pn[0] = num[5]; pn[1] = num[6];
    }
    cv_led(HU_PX, HU_BY, LED_S, 2u, pn, p->bg, LED_ON);
    cv_disc(HU_AX * 8 + 4, HU_AY * 8 + 4, 24, act ? p->accent : hu_mid(p, 15));
    if (sel) {                                          /* the white outline, 1.5 px: a line and a half-line inside it */
        uint16_t o2 = ux_mix(p->bg, T_TEXT, 55);
        cv_frame(HU_FX, 1, HU_FW, 38, T_TEXT);
        cv_frame(HU_FX + 1, 2, HU_FW - 2, 36, o2);
        HOME_HOOK(cat, num, name, 2);
    }
    cv_blit(0, (uint32_t)y0);
}

/* HOME's cell labels: EDIT's (big_label), short enough for a 57 px cell */
static const char *home_label(const track_t *t, uint32_t k)
{
    static const char *const L[3] = {"CUTOFF", "RES", "ENV"};
    return k < 3u ? L[k] : big_mlabel(t);
}
/* what card k shows: its value, unit, the value's colour (the EDIT page's KNOB k, ui.c home_param) */
static void home_card_of(uint32_t k, char *val, const char **unit, uint16_t *vc)
{
    int16_t *vp;
    const param_desc_t *d = home_param(k, &vp);
    *vc = VAL(k);
    *unit = "";
    if (!d || !vp) {
        str_cpy(val, "-", 12);
        return;
    }
    big_format(d, *vp, val, unit);
}

/* the four cells (240 x 28) */
static void home_cards(void)
{
    const char *L[4];
    char val[4][12];
    const char *unit[4];
    uint16_t vc[4];
    uint32_t k, sig = ux.gen * 31u + ux.style * 7u, mot = 0;
    for (k = 0; k < 4u; k++) {
        uint32_t id = big_id(TSEL, k);
        L[k] = home_label(TSEL, k);
        home_card_of(k, val[k], &unit[k], &vc[k]);
        mot |= (uint32_t)(id < P_COUNT && ((card_mot_mask[id / 32u] >> (id % 32u)) & 1u)) << k;
        sig = str_hash(str_hash(str_hash(sig * 2654435761u + vc[k] * 131u + (k == ui.hot_col && ui.hot_t) * 7u, val[k]),
                                unit[k]), L[k]);
    }
    sig += mot * 104729u;
    card_mot = (uint8_t)mot;
    if (!ui.force && sig == home_sig[NTRK])
        return;
    home_sig[NTRK] = sig;
    cv_begin(240, HOME_CH, T_BG);
    trim_cv(TG_HOME, 0, HOME_CY, 240, HOME_CH);
    for (k = 0; k < 4u; k++) {
        int32_t x = CARD_X(k), uw = unit[k][0] ? text_w(&AF_S, unit[k]) + 3 : 0, vw = ink_w(&AF_M, val[k]);
        int hot = k == ui.hot_col && ui.hot_t;
        uint16_t lc = hot ? T_ACCENT : (mot >> k) & 1u ? T_THEME : T_MID;
        if (ux.style) {                                 /* LINE: rules between the cells */
            if (k < 3u)
                cv_rect(x + CARD_W, 1, 1, HC_H, T_RULE);
        } else {
            ctl_card(x, 1, CARD_W, HC_H, 4, T_SURF, T_BG);
        }
        GFX_HOOK_ALIGN(x, 0, x + CARD_W, 0, AL_H, "home cell label centred");
        cv_text_in(x, 1 + HC_LY - AF_S_CAP_Y, CARD_W, &AF_S, L[k], lc, T_SURF);
        if (vw + uw > CARD_W - 6)
            uw = 0;
        BIG_HOOK(k, L[k], val[k], 2);
        if (uw) {                                       /* the value and its unit, the pair centred */
            int32_t gx = x + HALF_UP(CARD_W - (vw + uw));
            GFX_HOOK_ALIGN(0, 1 + HC_VY, 0, 1 + HC_VY + AF_M_CAP_H, AL_V, "home cell value on its band");
            cv_text_on(gx, 1 + HC_VY - AF_M_CAP_Y, &AF_M, val[k], vc[k], T_SURF);
            GFX_HOOK_ALIGN(0, 1 + HC_VY, 0, 1 + HC_VY + AF_M_CAP_H, AL_B, "home cell unit on its value's baseline");
            cv_text_on(gx + vw + 3, 1 + HC_VY - AF_M_CAP_Y + AF_M.asc - AF_S.asc, &AF_S, unit[k], T_MID, T_SURF);
        } else {
            GFX_HOOK_ALIGN(x, 1 + HC_VY, x + CARD_W, 1 + HC_VY + AF_M_CAP_H, AL_HV, "home cell value centred");
            cv_text_in(x, 1 + HC_VY - AF_M_CAP_Y, CARD_W, &AF_M, val[k], vc[k], T_SURF);
        }
    }
    if (ux.style)                                       /* LINE: the rule above the footer */
        cv_rect(CARD_X(0), HOME_CH - 1, CARD_X(3) + CARD_W - CARD_X(0), 1, T_RULE);
    cv_blit(0, HOME_CY);
}

static void draw_home_big(void)
{
    uint32_t c;
    (void)motion_mask(song.sel, card_mot_mask);
    for (c = 0; c < 4u; c++)                            /* (no rolling digits here: a roll the last page left, done) */
        ui.roll[c].from[0] = 0;
    if (ui.force)                                       /* the meters anew */
        for (c = 0; c < NTRK; c++)
            ts.meter[c] = 0;
    for (c = 0; c < NTRK; c++)
        home_unit(c);
    home_cards();
}
