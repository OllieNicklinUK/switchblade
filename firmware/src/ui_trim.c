/* SPDX-License-Identifier: GPL-3.0-only */
/* ui_trim.c: the machines' trim (Switch Blade: the machine look is the one colour scheme). A few rectangles per machine, drawn on the
 * strips of the header, EDIT and the footer (HOME's rack units carry their own looks, ui_big.c), to evoke the panel the machine is modelled on (assets/refs/): the OB-6's blue
 * pinstripes and wood cheeks (OB-Z), the Juno-106's red and blue section bands (DRIFT), the TB-303's dark step section
 * with its red LEDs (ACID), the VOID WAVE plugin's amber glow rule (WAVE). No bitmaps: data, drawn with cv_rect /
 * lcd_fill, on the free rows and columns only (docs/plans/2026-10-19-switch-blade-ui.md, "UI.3").
 *
 * Where there is room: the header's ink lies in rows 4 .. 19, so rows 0 .. 3 and 20 .. 23 are free; the cards, the tabs
 * and the sound panel sit 3 px in from the screen's edges (x 0 .. 2 and 237 .. 239) and 1 px apart vertically (the BG
 * rows between the strips: y 24, 67 / 68, 147 / 148, 211 on HOME; 24, 117 / 118, 211 on EDIT); the footer's steps lie in
 * rows 7 .. 22 of its 28 (y 219 .. 234), so its rows 0 .. 5 and 24 .. 27 are free. A trim rectangle never lies under a
 * text, an icon or a card: the host lint (tests/ui_render.c GFX_HOOK_TRIM) checks every canvas.
 *
 * Only a machine palette (MACHINE mode, ux.trim) has a trim; a fixed palette and CUSTOM look as they always did, and so
 * does STYLE LINE (the dividers are the seams). The splash is not themed. A strip draws the trim right after cv_begin:
 * a canvas of a strip is the whole of what it shows, so the trim is part of its background and is redrawn with it */
#define TG_HEAD 1u                                      /* the header (every page) */
#define TG_HOME 2u                                      /* HOME's strips: tabs, the sound, the cards */
#define TG_EDIT 4u                                      /* EDIT's two strips */
#define TG_BOTH (TG_HOME | TG_EDIT)                     /* ... and the footer, shared by both */
#define TG_FOOTE 8u                                     /* the footer of EDIT only (OB-Z's cheeks) */
typedef struct { uint8_t pg, x, y, w, h; uint16_t c; uint8_t k; } trim_t;   /* screen pixels; k: 0 a flat colour c, 1 wood (ui_ctl.c ctl_wood; c is its mean) */
#define TK_WOOD 1u
#define TRIM_N(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

/* OB-Z, the OB-6: fine blue pinstripes on the free rows, wood end cheeks down both edges of HOME and EDIT */
#define OB_STRIPE RGB(0x1a, 0x40, 0x6a)
#define OB_WOOD RGB(0x3c, 0x2e, 0x30)
static const trim_t TRIM_OBZ[] = {
    {TG_HEAD, 0, 1, 240, 1, OB_STRIPE},   {TG_HEAD, 0, 22, 240, 1, OB_STRIPE},
    {TG_EDIT, 0, 24, 8, 188, OB_WOOD, TK_WOOD}, {TG_EDIT, 232, 24, 8, 188, OB_WOOD, TK_WOOD},   /* EDIT: the OB-6's cheeks, 8 px, grained */
    {TG_FOOTE, 0, 212, 2, 28, OB_WOOD, TK_WOOD}, {TG_FOOTE, 238, 212, 2, 28, OB_WOOD, TK_WOOD},
    {TG_EDIT, 8, 24, 224, 1, OB_STRIPE},  {TG_EDIT, 8, 118, 224, 1, OB_STRIPE},
    {TG_EDIT, 8, 211, 224, 1, OB_STRIPE}, {TG_BOTH, 2, 237, 236, 1, OB_STRIPE},
};
/* WAVE, the plugin: a 1 px amber rule with its glow under the header, a dim one over the footer */
#define WV_AMBER RGB(0xf5, 0xa6, 0x23)
#define WV_GLOW RGB(0xa4, 0x61, 0x36)
static const trim_t TRIM_WAVE[] = {
    {TG_HEAD, 0, 22, 240, 1, WV_AMBER}, {TG_HEAD, 0, 23, 240, 1, WV_GLOW}, {TG_BOTH, 3, 211, 234, 1, WV_GLOW},
};
/* ACID, the TB-303: a hairline under the header, the dark step section as the footer's bottom band, red LEDs over the
 * footer's beat steps (steps 1, 5, 9, 13: ui_graph.c bar_x + 3) */
#define AC_DARK RGB(0x0d, 0x14, 0x1b)
#define AC_LED RGB(0xc8, 0x3a, 0x30)
static const trim_t TRIM_ACID[] = {
    {TG_HEAD, 3, 22, 234, 1, AC_DARK}, {TG_BOTH, 0, 236, 240, 4, AC_DARK},
    {TG_BOTH, 15, 213, 3, 3, AC_LED},  {TG_BOTH, 71, 213, 3, 3, AC_LED},
    {TG_BOTH, 127, 213, 3, 3, AC_LED}, {TG_BOTH, 183, 213, 3, 3, AC_LED},
};
/* DRIFT, the Juno-106: red section bands along the top of the header, the last one blue, a 1 px rule of the same colours
 * under them, and the same two-colour rule on the seams between HOME's and EDIT's strips */
#define JU_RED RGB(0xa8, 0x30, 0x32)
#define JU_BLUE RGB(0x26, 0x6b, 0xa1)
static const trim_t TRIM_DRIFT[] = {
    {TG_HEAD, 3, 0, 46, 3, JU_RED},     {TG_HEAD, 51, 0, 58, 3, JU_RED},   {TG_HEAD, 113, 0, 28, 3, JU_RED},
    {TG_HEAD, 143, 0, 48, 3, JU_RED},   {TG_HEAD, 193, 0, 44, 3, JU_BLUE},
    {TG_HEAD, 3, 22, 46, 1, JU_RED},    {TG_HEAD, 51, 22, 58, 1, JU_RED},  {TG_HEAD, 113, 22, 28, 1, JU_RED},
    {TG_HEAD, 143, 22, 48, 1, JU_RED},  {TG_HEAD, 193, 22, 44, 1, JU_BLUE},
    {TG_EDIT, 0, 24, 240, 3, JU_RED},                                  /* EDIT: the Juno's red band under the header */
    {TG_EDIT, 3, 211, 188, 1, JU_RED},  {TG_EDIT, 193, 211, 44, 1, JU_BLUE},
};
/* V2 placeholders until the UI phase: one rule under the header in the machine's theme colour */
static const trim_t TRIM_JPZ[] = {{TG_HEAD, 0, 22, 240, 1, RGB(0x50, 0xaa, 0xff)}};
static const trim_t TRIM_CZZ[] = {{TG_HEAD, 0, 22, 240, 1, RGB(0xe6, 0x46, 0x46)}};
static const trim_t TRIM_GWZ[] = {{TG_HEAD, 0, 22, 240, 1, RGB(0xe6, 0xaa, 0x50)}};
static const trim_t TRIM_MICRO[] = {{TG_HEAD, 0, 22, 240, 1, RGB(0x46, 0xc8, 0xdc)}};
/* by engine (ENGI_OBX .. ENGI_MICRO), the palette's trim index - 1 (gfx.c palette_set) */
static const struct { const trim_t *r; uint8_t n; } TRIM[NENGINES] = {
    {TRIM_OBZ, (uint8_t)TRIM_N(TRIM_OBZ)}, {TRIM_WAVE, (uint8_t)TRIM_N(TRIM_WAVE)},
    {TRIM_ACID, (uint8_t)TRIM_N(TRIM_ACID)}, {TRIM_DRIFT, (uint8_t)TRIM_N(TRIM_DRIFT)},
    {TRIM_JPZ, (uint8_t)TRIM_N(TRIM_JPZ)}, {TRIM_CZZ, (uint8_t)TRIM_N(TRIM_CZZ)},
    {TRIM_GWZ, (uint8_t)TRIM_N(TRIM_GWZ)}, {TRIM_MICRO, (uint8_t)TRIM_N(TRIM_MICRO)},
};

/* the trim of the pages in mask (TG_*) that lies on the canvas about to be drawn: its top left at screen (x0, y0), w x h
 * (call right after cv_begin) */
static void trim_cv(uint32_t mask, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    const trim_t *r;
    uint32_t i, n;
    if (!ux.trim || ux.style)
        return;
    r = TRIM[(ux.trim - 1u) % NENGINES].r;
    n = TRIM[(ux.trim - 1u) % NENGINES].n;
    for (i = 0; i < n; i++) {
        int32_t ax = r[i].x > x0 ? r[i].x : x0, ay = r[i].y > y0 ? r[i].y : y0;
        int32_t bx = r[i].x + r[i].w < x0 + w ? r[i].x + r[i].w : x0 + w;
        int32_t by = r[i].y + r[i].h < y0 + h ? r[i].y + r[i].h : y0 + h;
        if (!(r[i].pg & mask) || bx <= ax || by <= ay)
            continue;
        GFX_HOOK_TRIM(ax - x0, ay - y0, bx - x0, by - y0, r[i].k == TK_WOOD ? 0u : r[i].c);   /* (0: wood, a grain of shades) */
        if (r[i].k == TK_WOOD)
            ctl_wood(ax - x0, ay - y0, bx - ax, by - ay, ax, ay);
        else
            cv_rect(ax - x0, ay - y0, bx - ax, by - ay, r[i].c);
    }
}
