/* SPDX-License-Identifier: GPL-3.0-only */
/* ui_rows.c: V2's value pages as a sheet of rows (ui.c row_page, rows_list; included by ui_big.c after ui_rack.c, whose
 * parts it uses). Each row is one of the old pages (four knobs); the focused row (ui.page) is 2U, KNOB 1..4 edit it;
 * ALGORITHM moves the focus (ui.c rows_turn):
 *
 *    0  header (ui_draw.c draw_head): the machine, the sound, the row's title and its place in the family
 *   24  four rows in view: three 1U (34 px: the row's title, four small knobs, their values) and the focused one, 2U
 *       (114 px: the title, four big knobs, labels and values, KNOB 1..4 keycaps). The view keeps the focus second from the
 *       top where it can; a scroll bar in the right ear shows where the view is in the sheet.
 * The rows are the rack's units: ears and screws, a dark faceplate tinted by the selected machine, its glow colour on the
 * focused row. The knob just turned: its arc and value lit (ui.hot_col). Each row is drawn again only when what it shows
 * changed (rw_sig). */
#define RW_Y0 H_HEAD                                    /* the rows: 24 .. 239 */
#define RW_1U 34
#define RW_2U (240 - RW_Y0 - 3 * RW_1U)                 /* 114 */
#define RW_VIEW 4u
#define RW_CX0 10                                       /* the cells: four of 55 px from x 10 */
#define RW_CW 55

static uint32_t rw_sig[RW_VIEW];
static uint8_t rw_mot;                                  /* the focused row's KNOBs MOTION drives (their labels lit, as the cards') */

/* the KNOBs of row pg whose parameter MOTION drives (card_mot_mask, ui_draw.c draw_columns) */
static uint32_t rw_motion(const page_t *pg)
{
    uint32_t k, m = 0;
    if (pg->scope != SC_TRACK && pg->scope != SC_ENGINE)
        return 0;
    for (k = 0; k < 4u; k++) {
        uint32_t id = page_id(pg, k);
        if (pg->graph == GR_MOD && id != 0xFFu)
            id += 3u * mod_ui_slot;
        if (id < P_COUNT && ((card_mot_mask[id / 32u] >> (id % 32u)) & 1u))
            m |= 1u << k;
    }
    return m;
}

/* the cell k of row pg: its label, value, unit, the knob's value angle and its start (bipolar: 12 o'clock), whether it is in
 * use (0: an empty slot) */
typedef struct {
    const char *label, *unit;
    char val[16];
    int32_t a, lo;
    int on;
} rw_cell_t;
static void rw_cell(const page_t *pg, uint32_t k, rw_cell_t *c)
{
    int16_t *vp;
    const param_desc_t *d;
    c->label = "";
    c->unit = "";
    c->val[0] = 0;
    c->a = c->lo = 225;
    c->on = 0;
    if (pg->graph == GR_MOD) {                          /* SLOT, then that slot's SRC DST AMT (as ui_draw.c's MOD cards) */
        const track_t *t = TSEL;
        uint32_t id = P_M1SRC + 3u * mod_ui_slot;
        int32_t s = t->p[id], dd = t->p[id + 1u];
        c->on = 1;
        if (k == 0u) {
            c->label = "SLOT";
            fmt_int(c->val, (int32_t)mod_ui_slot + 1);
            c->unit = "/4";
            c->a = rk_angle((int32_t)mod_ui_slot, 0, 3);
            return;
        }
        if (k == 1u) {
            c->label = "SRC";
            str_cpy(c->val, N_MSRC[clamp(s, 0, MS_N - 1)], sizeof c->val);
            c->a = rk_angle(clamp(s, 0, MS_N - 1), 0, MS_N - 1);
            return;
        }
        if (k == 2u) {
            c->label = "DST";
            str_cpy(c->val, mod_dst_name(t, dd), sizeof c->val);
            c->a = rk_angle(dd, TP[id + 1u].min, TP[id + 1u].max);
            return;
        }
    }
    d = page_desc(pg, k, &vp);
    if (!d || !vp)
        return;
    c->on = 1;
    c->label = d->label;
    if (pg->graph == GR_MOD)                          /* (the slot's AMT, whichever slot) */
        c->label = "AMT";
    param_format(d, *vp, c->val, &c->unit);
    c->a = rk_angle(*vp, d->min, d->max);
    if (d->min < 0 && d->max > 0)
        c->lo = rk_angle(0, d->min, d->max);
}

/* a row's title: MACHINE 1 / 2 by the machine's names for them ("OSC", "FLT") */
static const char *rw_title(const page_t *pg)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    const char *pt = pg->scope == SC_ENGINE ? e->page_title[pg->id[0] != P_E0] : 0;
    return pt && pt[0] ? pt : pg->title;
}

/* a knob's arc: the track, then the value from lo (225, or 12 o'clock when bipolar) to a */
static void rw_arc(int32_t x8, int32_t y8, int32_t ro, int32_t ri, int32_t a, int32_t lo, uint16_t track, uint16_t col)
{
    cv_arc(x8, y8, ro, ri, 225, 495, track);
    if (a > lo)
        cv_arc(x8, y8, ro, ri, lo, a, col);
    else if (a < lo)
        cv_arc(x8, y8, ro, ri, a, lo, col);
}

/* the plate of a row (rows 0 .. h - 1 of the canvas): the rack's ears, a dark faceplate tinted toward the machine's colour */
static void rw_plate(int32_t h, uint16_t plate, int32_t s0, int32_t s1)
{
    cv_begin(240, (uint32_t)h, RK_BLACK);
    rk_ears(h, s0, s1);
    cv_rect(RK_FX, 1, RK_FW, h - 1, plate);
    cv_rect(RK_FX, 1, RK_FW, 1, ux_mix(plate, RK_WHITE, 18));
}

/* the scroll bar in the right ear: the view (first .. first + 3) in the n rows */
static void rw_scroll(int32_t y_in_view, int32_t h, uint32_t first, uint32_t n)
{
    int32_t top = (int32_t)(240 - RW_Y0), y0, y1, a, b;
    if (n <= RW_VIEW)
        return;
    y0 = (int32_t)first * top / (int32_t)n;            /* the bar in view rows, then this row's part of it */
    y1 = (int32_t)(first + RW_VIEW) * top / (int32_t)n;
    a = y0 - y_in_view;
    b = y1 - y_in_view;
    if (a < 0)
        a = 0;
    if (b > h)
        b = h;
    if (b > a)
        cv_rect(236, a, 2, b - a, RGB(240, 244, 248));
}

/* a 1U row: page i at screen row y (its place in the view: y_in_view) */
static void rw_row1(uint32_t i, int32_t y, int32_t y_in_view, uint32_t first, uint32_t n)
{
    const page_t *pg = &PAGES[i];
    uint32_t e = TSEL->eng_req % NENGINES, k;
    uint16_t glow = RK_GLOW[e], plate = ux_mix(RGB(26, 28, 33), RK_PLATE[e], 18), dim = ux_mix(glow, plate, 35);
    rw_cell_t c;
    rw_plate(RW_1U, plate, 9, 25);
    rk_glow(12, 4 - AF_DS_CAP_Y, &AF_DS, rw_title(pg), dim, plate, 0);
    cv_rect(12 + ink_w(&AF_DS, rw_title(pg)) + 6, 8, 214 - ink_w(&AF_DS, rw_title(pg)), 1, ux_mix(plate, RK_WHITE, 10));
    for (k = 0; k < 4u; k++) {
        int32_t x = RW_CX0 + (int32_t)k * RW_CW, cx = x + 9, cy = 23;
        rw_cell(pg, k, &c);
        if (!c.on)
            continue;
        rw_arc(cx * 8, cy * 8, 72, 60, c.a, c.lo, ux_mix(plate, RK_BLACK, 50), dim);
        rk_knob(cx, cy, 14, c.a);
        {
            char v[24];
            str_cpy(v, c.val, sizeof v);
            if (c.unit[0] && text_w(&AF_S, v) + text_w(&AF_S, c.unit) <= RW_CW - 22)
                str_cpy(v + str_len(v), c.unit, sizeof v - str_len(v));
            rk_fit(&AF_S, v, RW_CW - 22, v, sizeof v);
            GFX_HOOK_ALIGN(0, 17, 0, 17 + AF_S_CAP_H, AL_V, "row value on its knob's line");
            cv_text_on(x + 20, 17 - AF_S_CAP_Y, &AF_S, v, RGB(214, 218, 224), plate);
        }
    }
    rw_scroll(y_in_view, RW_1U, first, n);
    cv_blit(0, (uint32_t)y);
}

/* the focused row, 2U: page i at screen row y (114 rows: one canvas) */
static void rw_row2(uint32_t i, int32_t y, int32_t y_in_view, uint32_t first, uint32_t n)
{
    const page_t *pg = &PAGES[i];
    uint32_t e = TSEL->eng_req % NENGINES, k;
    uint16_t glow = RK_GLOW[e], plate = ux_mix(RGB(18, 20, 24), RK_GLOW[e], 6);
    rw_cell_t c[4];
    for (k = 0; k < 4u; k++)
        rw_cell(pg, k, &c[k]);
    rw_plate(RW_2U, plate, 9, RW_2U - 9);
    rk_glow(12, 6 - AF_D_CAP_Y, &AF_D, rw_title(pg), glow, plate, 0);
    for (k = 0; k < 4u; k++) {
        int32_t x = RW_CX0 + (int32_t)k * RW_CW, cx = x + RW_CW / 2, cy = 52;
        int hot = ui.hot_t && ui.hot_col == k;
        uint16_t vc = hot ? ux_mix(glow, RK_WHITE, 45) : glow;
        char kc[3] = {'K', (char)('1' + k), 0};
        if (!c[k].on)
            continue;
        cv_disc(cx * 8, cy * 8, 160, RGB(12, 13, 16));     /* the well, the arc, the cap */
        rw_arc(cx * 8, cy * 8, 152, 134, c[k].a, c[k].lo, RGB(40, 44, 50), vc);
        if (hot) {
            int32_t dx, dy;
            ctl_pt(cx * 8, cy * 8, 166, c[k].a, &dx, &dy);
            cv_disc(dx, dy, 26, ux_mix(glow, RK_BLACK, 40));
            cv_disc(dx, dy, 17, ux_mix(glow, RK_WHITE, 60));
        }
        rk_knob(cx, cy, 28, c[k].a);
        cv_rrect(cx + 13, 26, 15, 11, 3, hot ? glow : RGB(48, 52, 58), plate);   /* KNOB k's keycap, top right of the knob */
        GFX_HOOK_ALIGN(cx + 13, 26, cx + 28, 37, AL_HV, "row K chip centred");
        cv_text_in(cx + 13, 26 + CAP_IN(S, 11), 15, &AF_S, kc, hot ? RGB(10, 11, 14) : RGB(200, 204, 210),
                   hot ? glow : RGB(48, 52, 58));
        GFX_HOOK_ALIGN(x, 0, x + RW_CW, 0, AL_H, "row label centred");
        cv_text_in(x, 75 - AF_S_CAP_Y, RW_CW, &AF_S, c[k].label, (rw_mot >> k) & 1u ? RK_WHITE : ux_mix(glow, plate, 30), plate);
        {
            char v[24];
            const aafont_t *f = &AF_M;
            str_cpy(v, c[k].val, sizeof v);
            if (c[k].unit[0])
                str_cpy(v + str_len(v), c[k].unit, sizeof v - str_len(v));
            if (text_w(f, v) > RW_CW - 2)
                str_cpy(v, c[k].val, sizeof v);
            if (text_w(f, v) > RW_CW - 2)
                f = &AF_S;
            rk_fit(f, v, RW_CW - 2, v, sizeof v);
            GFX_HOOK_ALIGN(x, 0, x + RW_CW, 0, AL_H, "row value centred");
            cv_text_in(x, (f == &AF_M ? 89 - AF_M_CAP_Y : 91 - AF_S_CAP_Y), RW_CW, f, v, hot ? RK_WHITE : RGB(232, 236, 240),
                       plate);
        }
    }
    rw_scroll(y_in_view, RW_2U, first, n);
    cv_blit(0, (uint32_t)y);
}

static void draw_rows(void)
{
    uint8_t r[NPAGES];
    uint32_t n = rows_list(cur_page()->fam, r), at = 0, first, v, i;
    int32_t y = RW_Y0;
    for (i = 0; i < n; i++)
        if (r[i] == ui.page)
            at = i;
    first = at ? at - 1u : 0u;                          /* the focus second from the top, the view full where it can be */
    if (n >= RW_VIEW && first + RW_VIEW > n)
        first = n - RW_VIEW;
    for (v = 0; v < RW_VIEW; v++) {
        uint32_t ri = first + v, foc = ri == at, sig, k;
        int32_t h = foc ? RW_2U : RW_1U;
        if (ri >= n) {                                  /* past the sheet's end: an empty unit */
            sig = 0xE3u + v * 7u + (uint32_t)y * 31u + ux.gen;
            if (ui.force || sig != rw_sig[v]) {
                rw_sig[v] = sig;
                rw_plate(h, RGB(16, 17, 20), 9, h - 9);
                cv_blit(0, (uint32_t)y);
            }
            y += h;
            continue;
        }
        if (foc) {
            rw_mot = (uint8_t)rw_motion(&PAGES[r[ri]]);
            card_mot = rw_mot;
        }
        sig = 0x5157u + r[ri] * 977u + foc * 7u + (uint32_t)y * 31u + ux.gen * 3u + first * 131u + n * 7919u +
              (TSEL->eng_req % NENGINES) * 104729u + (foc && ui.hot_t ? ui.hot_col + 1u : 0u) * 15485863u + (foc ? rw_mot : 0u) * 7u;
        for (k = 0; k < 4u; k++) {
            rw_cell_t c;
            rw_cell(&PAGES[r[ri]], k, &c);
            sig = str_hash(str_hash(sig * 2654435761u + (uint32_t)c.a, c.val), c.unit);
        }
        if (ui.force || sig != rw_sig[v]) {
            rw_sig[v] = sig;
            if (foc)
                rw_row2(r[ri], y, y - RW_Y0, first, n);
            else
                rw_row1(r[ri], y, y - RW_Y0, first, n);
        }
        y += h;
    }
}
