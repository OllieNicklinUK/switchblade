/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Renders of the firmware UI (firmware/src/ui*.c on tests/ui_test.c's stubs), the layout lint, the GREY and MONO
 * layout lint and the host draw cost. Switch Blade has one colour scheme, the four machines' palettes (OB-Z WAVE ACID DRIFT):
 * every screen is drawn with each forced, then in MACHINE mode on each machine's track.
 *   ui_render OUTDIR [SLOTDIR]   (run by tests/run_tests.sh; tests/ui_render.py makes the PNGs;
 *                                 SLOTDIR: filmstrips of the rolling digits, one per machine)
 * Layout lint, over every screen below in every machine palette, every page of every engine and every value of every
 * column: the ink box of every text and icon drawn (gfx.c GFX_HOOK_TEXT) as it lands on the screen. A finding:
 * a box off the screen, cut by its canvas, partly covered by a later strip or fill, overlapping another box, or
 * ellipsised when it is not free text (labels and values must fit; names and messages may be ellipsised), or
 * spilling out of its cell: touching a rounded rectangle of its canvas (gfx.c cv_rrect: a cell, card, row, button,
 * GFX_HOOK_CELL) without lying inside it, though still on the screen (the FX map's REVERSE), or drawn in the
 * colour it is drawn on (gfx.c GFX_HOOK_INK: a text, icon or keycap label that cannot be seen).
 * MENU > STYLE LINE: every screen again in every palette, the same lint; a text or icon on a divider
 * (ui_draw.c lcd_rule, GFX_HOOK_RULE: the rules between the strips; cv_rule's are cells) is a finding too.
 * Rolling digits: every frame of a header BPM roll and a card roll, up and down, in every machine palette (lint).
 * Output: OUTDIR/ppm/<MACHINE>_<screen>.ppm, OUTDIR/report.txt (findings, ellipsised
 * free text, the draw cost), OUTDIR/text_audit.tsv (OB-Z, per screen: texts, icons and keycaps with their ink-box px,
 * the ellipsised ones, the texts closer than 2 px to their cell's edge, the words).
 * Alignment (gfx.c GFX_HOOK_ALIGN): every place where a text, icon or keycap is meant to sit centred (in a cell, a
 * chip, a button, a row, a column, a knob, a strip) or on a line shared with its neighbours (an icon on its label's
 * line, a word on its keycap's, a unit on its value's baseline) declares its box; the ink drawn there (a text: its
 * glyphs' boxes across, its capitals' band up and down; an icon: its cell's nibbles; a keycap: its pill; also the
 * keycaps' labels in their pills, from the data, and rows of keys or bars against their panel) is measured against
 * it, on every screen above in FLAT and LINE and every palette, the page / value sweep, and align_sweeps (every value
 * each place can show). 0.5 px is the odd pixel left over (it goes left / up); 1 px or more is a finding.
 * OUTDIR/align.txt: per place, how often measured, the worst offsets and where.
 * Exit 1 on a finding or an alignment 1 px or more off. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

typedef struct { int16_t x0, y0, x1, y1; uint8_t flags; char s[28]; char fnt; } rbox_t;   /* fnt: S M L (its face) */
static rbox_t pend[256], scr[1200];
static int16_t cells[64][4];                    /* GFX_HOOK_CELL: the cells and cards of the canvas being drawn */
static uint32_t ncells, nspill;
static FILE *aud;                               /* OUTDIR/text_audit.tsv: GREY, the text of each screen */
static char tight[400];                         /* .. the texts of this screen closer than 2 px to their cell's edge */
static uint32_t ntight;
static uint32_t npend, nscr, nfind, nfree;
static uint64_t px_visited, n_text, blit_px;
static FILE *rep;
static const char *cur_name = "";
static char free_seen[64][40];
static uint32_t nfree_seen;
/* the alignment check (gfx.c GFX_HOOK_ALIGN): a place declares the box its next item(s) are meant to be centred in,
 * across and / or up and down; the items' ink as drawn (a text: its glyphs' boxes across, its capitals' band up and
 * down, a text of no letter or figure: across only; an icon: the ink of its cell; a keycap: its pill; a cushion: the
 * cushion) against the box: the offset of the ink's centre from the box's, in half pixels. 0.5 is the odd pixel
 * left over (it goes left / up: a +0.5 is counted apart); 1 px or more is a finding. Per place (its tag): how
 * often it was measured, the worst offsets and where, in OUTDIR/align.txt */
typedef struct { int32_t x0, y0, x1, y1, ix0, iy0, ix1, iy1; uint32_t mode, left, nv; const char *tag; char s[24]; } al_t;
typedef struct { const char *tag; uint32_t n, nfail, half_lo, half_hi; int32_t wh2, wv2; char ex_h[112], ex_v[112]; } alstat_t;
static al_t al_stk[8];
static uint32_t al_n, al_fail, al_lost, al_count, al_off;
static int32_t al_cy0, al_cy1;                  /* GFX_HOOK_PEN: the capitals' band of the text being hooked */
static alstat_t al_st[160];
static uint32_t al_nst;
static FILE *al_rep;
static alstat_t *al_stat(const char *tag)
{
    uint32_t i;
    for (i = 0; i < al_nst && strcmp(al_st[i].tag, tag); i++) ;
    if (i == al_nst && al_nst < 160u) { memset(&al_st[i], 0, sizeof al_st[i]); al_st[i].tag = tag; al_nst++; }
    return &al_st[i < 160u ? i : 159u];
}
/* one measurement: offsets in half pixels (h2, v2; 99: not measured) */
static void al_record(const char *tag, int32_t h2, int32_t v2, const char *s, int32_t bx0, int32_t by0, int32_t bx1,
                      int32_t by1, int32_t ix0, int32_t iy0, int32_t ix1, int32_t iy1)
{
    alstat_t *a = al_stat(tag);
    int32_t ah = h2 == 99 ? 0 : h2 < 0 ? -h2 : h2, av = v2 == 99 ? 0 : v2 < 0 ? -v2 : v2;
    a->n++;
    al_count++;
    if (h2 == 1 || v2 == 1) a->half_hi++;
    if (h2 == -1 || v2 == -1) a->half_lo++;
    if (ah > a->wh2 || (!a->ex_h[0] && h2 != 99)) {
        if (ah > a->wh2) a->wh2 = ah;
        snprintf(a->ex_h, sizeof a->ex_h, "%+.1f '%s' %s ink x %d..%d box %d..%d", h2 / 2.0, s, cur_name, ix0, ix1, bx0, bx1);
    }
    if (av > a->wv2 || (!a->ex_v[0] && v2 != 99)) {
        if (av > a->wv2) a->wv2 = av;
        snprintf(a->ex_v, sizeof a->ex_v, "%+.1f '%s' %s ink y %d..%d box %d..%d", v2 / 2.0, s, cur_name, iy0, iy1, by0, by1);
    }
    if (ah >= 2 || av >= 2) {
        a->nfail++;
        al_fail++;
        if (al_rep && a->nfail <= 3u)
            fprintf(al_rep, "OFF  %-28s %-26s '%s' across %+.1f, up/down %+.1f  (ink %d,%d-%d,%d box %d,%d-%d,%d)\n", tag, cur_name,
                    s, h2 == 99 ? 0.0 : h2 / 2.0, v2 == 99 ? 0.0 : v2 / 2.0, ix0, iy0, ix1, iy1, bx0, by0, bx1, by1);
    }
}
static void hk_align(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t mode, const char *tag)
{
    al_t *a;
    if (al_off) return;                         /* (the draw cost: timed without the check)  */
    if (al_n >= 8u) { al_lost++; return; }
    a = &al_stk[al_n++];
    memset(a, 0, sizeof *a);
    a->x0 = x0; a->y0 = y0; a->x1 = x1; a->y1 = y1; a->mode = mode; a->tag = tag;
    a->left = mode >> 8 ? mode >> 8 : 1u;
    a->ix0 = a->iy0 = 1 << 20; a->ix1 = a->iy1 = -(1 << 20);
}
/* an item drawn (cell: a cv_rrect, else ink): its extent across (x0..x1), up and down (y0..y1; vy 0: none), to the
 * place declared last (AL_PASS, its item done: to the one before it too) */
static void al_item_k(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int vy, const char *s, int cell)
{
    al_t *a;
    int32_t h2 = 99, v2 = 99;
    uint32_t pass;
    if (!al_n) return;
    a = &al_stk[al_n - 1u];
    if (!(a->mode & 32u) != !cell) return;          /* (AL_CELLS: its cells only; else ink only) */
    if (x0 < a->ix0) a->ix0 = x0;
    if (x1 > a->ix1) a->ix1 = x1;
    if (vy) {
        if (y0 < a->iy0) a->iy0 = y0;
        if (y1 > a->iy1) a->iy1 = y1;
        a->nv++;
    }
    if (!a->s[0] || strlen(a->s) + strlen(s) + 2u < sizeof a->s)
        snprintf(a->s + strlen(a->s), sizeof a->s - strlen(a->s), "%s%s", a->s[0] ? "+" : "", s);
    if (--a->left) return;
    al_n--;
    pass = a->mode & 16u;
    if (a->mode & 1u) h2 = (a->ix0 + a->ix1) - (a->x0 + a->x1);       /* AL_H: the centres */
    if (a->mode & 8u) h2 = 2 * (a->ix1 - a->x1);                      /* AL_R: the right edges */
    if (a->nv && (a->mode & 2u)) v2 = (a->iy0 + a->iy1) - (a->y0 + a->y1);
    if (a->nv && (a->mode & 4u)) v2 = 2 * (a->iy1 - a->y1);           /* AL_B: the baselines (capitals' bottoms) */
    al_record(a->tag, h2, v2, a->s, a->x0, a->y0, a->x1, a->y1, a->ix0, a->iy0, a->ix1, a->iy1);
    if (pass) al_item_k(x0, y0, x1, y1, vy, s, cell);
}
static void al_item(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int vy, const char *s) { al_item_k(x0, y0, x1, y1, vy, s, 0); }
static int al_alnum(const char *s)
{
    for (; *s; s++)
        if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9')) return 1;
    return 0;
}
static void al_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags)
{
    if (!al_n || ((flags & 4u) && !strcmp(s, "icon"))) return;      /* (an icon: GFX_HOOK_ICON) */
    if (flags & 4u) al_item(x0, y0, x1, y1, 1, s);                    /* a keycap: its pill */
    else al_item(x0, al_cy0, x1, al_cy1, al_alnum(s), s);
}
static void al_flush(void)                      /* a canvas blitted or begun: declared places left without their items */
{
    uint32_t i;
    for (i = 0; i < al_n; i++)
        if (al_rep && al_lost + i < 12u) fprintf(al_rep, "LOST %-28s %s (declared, nothing drawn after it)\n", al_stk[i].tag, cur_name);
    al_lost += al_n;
    al_n = 0;
}
static int32_t icon_cell(uint32_t *size, uint32_t id);
static void hk_icon(int32_t x, int32_t y, uint32_t size, uint32_t id);   /* (after the icons: their cells' nibbles) */
#define GFX_HOOK_ALIGN(x0, y0, x1, y1, mode, tag) hk_align(x0, (y0) + cv_oy, x1, (y1) + cv_oy, mode, tag)
static uint8_t pen_h;                           /* GFX_HOOK_PEN: the line height of the text being hooked (its face) */
/* (the capitals' band: H's ink; a figures-only face, XL: 1's, as gen_aa_font.py's AF_XL_CAP_Y) */
#define PEN_CAP(f) (glyph_at((f), 'H') >= 0 ? 'H' : '1')
#define GFX_HOOK_PEN(y, f) (pen_h = (f)->h, al_n ? (al_cy0 = (y) + cv_oy + (f)->g[glyph((f), PEN_CAP(f))].by, al_cy1 = al_cy0 + (f)->g[glyph((f), PEN_CAP(f))].bh) : 0)
#define GFX_HOOK_ITEM(x0, y0, x1, y1) al_item(x0, (y0) + cv_oy, x1, (y1) + cv_oy, 1, "cushion")
#define GFX_HOOK_ICON(x, y, size, id) hk_icon(x, (y) + cv_oy, size, id)
static char splash_seen[96];                    /* the splash scene: its texts as drawn, '|' between (draw) */
static uint8_t in_splash;
static void hk_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags)
{
    al_text(x0, y0, x1, y1, s, flags);
    if (in_splash && strlen(splash_seen) + strlen(s) + 2u < sizeof splash_seen)
        snprintf(splash_seen + strlen(splash_seen), sizeof splash_seen - strlen(splash_seen), "%s%s", splash_seen[0] ? "|" : "", s);
    n_text++;
    if (npend >= 256u) return;
    pend[npend] = (rbox_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (uint8_t)flags, {0}};
    strncpy(pend[npend].s, s, 27);
    pend[npend].fnt = (flags & 4u) ? 'K' : pen_h >= 45u ? 'X' : pen_h >= 30u ? 'L' : pen_h >= 18u ? 'M' : 'S';
    pen_h = 0;
    npend++;
}
static void hk_cell(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    al_item_k(x0, y0, x1, y1, 1, "cell", 1);
    if (ncells < 64u) {
        cells[ncells][0] = (int16_t)x0; cells[ncells][1] = (int16_t)y0;
        cells[ncells][2] = (int16_t)x1; cells[ncells][3] = (int16_t)y1;
        ncells++;
    }
}
static void hk_blit(uint32_t x, uint32_t y, uint32_t r0, uint32_t w, uint32_t h);
static void hk_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
/* STYLE LINE: the dividers drawn on the screen (ui_draw.c lcd_rule) while no later blit or fill covers them */
static int16_t rules[32][4];
static uint32_t nrules;
static void hk_rule(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (nrules < 32u) {
        rules[nrules][0] = (int16_t)x; rules[nrules][1] = (int16_t)y;
        rules[nrules][2] = (int16_t)(x + w); rules[nrules][3] = (int16_t)(y + h);
        nrules++;
    }
}
static void rules_cover(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t i, k = 0;
    for (i = 0; i < nrules; i++)
        if (!(rules[i][0] >= x0 && rules[i][2] <= x1 && rules[i][1] >= y0 && rules[i][3] <= y1))
            memcpy(rules[k++], rules[i], sizeof rules[i]);
    nrules = k;
}
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) hk_text(x0, y0, x1, y1, s, flags)
#define GFX_HOOK_BLIT(x, y, r0) hk_blit(x, y, r0, cv_w, cv_h)
#define GFX_HOOK_BEGIN() (npend = ncells = ntrim = 0, al_flush())
#define GFX_HOOK_CELL(x0, y0, x1, y1) hk_cell(x0, y0, x1, y1)
#define GFX_HOOK_RULE(x, y, w, h) hk_rule(x, y, w, h)
/* the machines' trim (ui_trim.c): a rectangle drawn on the canvas first, in canvas pixels; at the blit every pixel of it
 * must still be its colour: nothing the canvas holds (a text, an icon, a card, a keycap) was drawn over it, so the trim
 * lives on the free rows and columns only (an exact, pixel check: an icon's box is its whole cell, its ink is not) */
static int16_t trims[48][4];
static uint16_t trim_c[48];
static uint32_t ntrim, n_trim_rects, n_trim_checked, n_trim_bad;
#define GFX_HOOK_TRIM(x0, y0, x1, y1, c) hk_trim(x0, y0, x1, y1, c)
static void hk_trim(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    if (ntrim < 48u) {
        trims[ntrim][0] = (int16_t)x0; trims[ntrim][1] = (int16_t)y0; trims[ntrim][2] = (int16_t)x1; trims[ntrim][3] = (int16_t)y1;
        trim_c[ntrim] = c;
        ntrim++;
    }
    n_trim_rects++;
}
static uint32_t nink;                           /* GFX_HOOK_INK: inks drawn on their own colour */
static void hk_ink(uint16_t ink, uint16_t bg);
#define GFX_HOOK_INK(ink, bg) hk_ink(ink, bg)
#define GFX_HOOK_PIXELS(n) (px_visited += (n))
#define UI_FILL_HOOK(x, y, w, h) hk_fill(x, y, w, h)
/* MENU > LARGE: the tall cards' values by the face they got (ui_draw.c LARGE_HOOK), the characters L lacked */
static uint64_t lg_n[4];
static char lg_miss[96], lg_wide[16][12], lg_small[16][12];
static uint32_t lg_nwide, lg_nsmall;
static void lg_hook(const char *s, int why);
#define LARGE_HOOK(s, why) lg_hook(s, why)
static void cs_hook(const char *v, const char *u);
#define CARD_HOOK(v, u) cs_hook(v, u)
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint8_t large_on;                         /* the LARGE pass: every scene with MENU > LARGE ON */
/* the card values set in S (ui_draw.c CARD_HOOK: M too wide for the card with its unit), distinct, for the report
 * (FLAT / LINE: MENU > LARGE's tall cards have their own faces, lg_hook) */
static char cs_seen[64][16];
static uint32_t cs_n, cs_more;
static uint64_t cs_count;
static void cs_hook(const char *v, const char *u)
{
    char b[16];
    uint32_t i;
    if (large_on) return;
    cs_count++;
    snprintf(b, sizeof b, "%s%s%s", v, u[0] ? " " : "", u);
    for (i = 0; i < cs_n && strcmp(cs_seen[i], b); i++) ;
    if (i == cs_n) { if (cs_n < 64u) snprintf(cs_seen[cs_n++], 16, "%s", b); else cs_more++; }
}
static void lg_hook(const char *s, int why)
{
    uint32_t i;
    lg_n[why & 3]++;
    if (why == 1)
        for (; *s; s++) {
            uint32_t c = fold(&AF_L, (uint8_t)*s);
            if (glyph_at(&AF_L, c) < 0 && !strchr(lg_miss, (int)c) && strlen(lg_miss) + 1u < sizeof lg_miss)
                lg_miss[strlen(lg_miss)] = (char)c;
        }
    if (why == 2 || why == 3) {                     /* a few of each, distinct */
        char (*l)[12] = why == 2 ? lg_wide : lg_small;
        uint32_t *n = why == 2 ? &lg_nwide : &lg_nsmall;
        for (i = 0; i < *n && strcmp(l[i], s); i++) ;
        if (i == *n && *n < 16u) snprintf(l[(*n)++], 12, "%s", s);
    }
}

/* an icon drawn: its ink, read from its cell's nibbles here (not icons.c icon_ink's table: measured, not trusted) */
/* n alpha values of a cell from its bit stream (the canonical Huffman code AI_HC, as gen_aa_font.py huff_unpack:
 * decoded here bit by bit, not by gfx.c's table) */
static void icon_unpack(const uint8_t *d, uint8_t *out, uint32_t n)
{
    uint32_t k = 0, pos = 0;
    while (k < n) {
        uint32_t code = 0, first = 0, idx = 0, len, s, r;
        for (len = 0; len < 15u; len++) {
            code |= (d[pos >> 3] >> (7u - (pos & 7u))) & 1u;
            pos++;
            if (code - first < AI_HC[len]) break;
            idx += AI_HC[len];
            first = (first + AI_HC[len]) << 1;
            code <<= 1;
        }
        s = AI_HC[15u + idx + code - first];
        for (r = 0; r <= s >> 4 && k < n; r++) out[k++] = (uint8_t)(s & 15u);
    }
}
static void hk_icon(int32_t x, int32_t y, uint32_t size, uint32_t id)
{
    int32_t k, x0 = 99, y0 = 99, x1 = -1, y1 = -1;
    uint32_t i;
    const uint8_t *c;
    uint8_t cell[24 * 24];
    if (!al_n || (k = icon_cell(&size, id)) < 0) return;
    c = size == 24u ? AI24_DATA + AI24_OFF[k] : size == 16u ? AI16_DATA + AI16_OFF[k] : AI12_DATA + AI12_OFF[k];
    icon_unpack(c, cell, size * size);
    for (i = 0; i < size * size; i++)
        if (cell[i]) {
            int32_t px = (int32_t)(i % size), py = (int32_t)(i / size);
            if (px < x0) x0 = px;
            if (px + 1 > x1) x1 = px + 1;
            if (py < y0) y0 = py;
            if (py + 1 > y1) y1 = py + 1;
        }
    if (cv_flip) { int32_t t = x0; x0 = (int32_t)size - x1; x1 = (int32_t)size - t; }   /* (mirrored: the metronome) */
    al_item(x + x0, y + y0, x + x1, y + y1, 1, "icon");
}

static void finding(const char *what, const rbox_t *b, const rbox_t *o)
{
    static char seen[600][64];
    static uint32_t nseen;
    char key[64];
    uint32_t i;
    nfind++;
    snprintf(key, sizeof key, "%.20s|%.14s|%.24s", cur_name, what, b->s);
    for (i = 0; i < nseen && strcmp(seen[i], key); i++) ;
    if (i < nseen) return;                       /* each finding once per screen */
    if (nseen < 600u) strcpy(seen[nseen++], key);
    else return;
    fprintf(rep, "LINT %-22s %-32s '%s' (%d,%d)-(%d,%d)", cur_name, what, b->s, b->x0, b->y0, b->x1, b->y1);
    if (o) fprintf(rep, " / '%s' (%d,%d)-(%d,%d)", o->s, o->x0, o->y0, o->x1, o->y1);
    fputc('\n', rep);
}
/* a rectangle of the screen is drawn again: boxes inside it are gone, boxes it covers in part are hidden */
static void cover(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t i, k = 0;
    for (i = 0; i < nscr; i++) {
        rbox_t *b = &scr[i];
        int in = b->x0 >= x0 && b->x1 <= x1 && b->y0 >= y0 && b->y1 <= y1;
        int touch = b->x0 < x1 && x0 < b->x1 && b->y0 < y1 && y0 < b->y1;
        if (in) continue;
        if (touch && !(b->flags & 48u)) finding("hidden in part by a later draw", b, 0);
        if (touch && (b->flags & 48u)) scr[k++] = *b;   /* split across bands / scrolled: drawn on purpose */
        else scr[k++] = *b;
    }
    nscr = k;
}
static void hk_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    cover((int32_t)x, (int32_t)y, (int32_t)(x + w), (int32_t)(y + h));
    rules_cover((int32_t)x, (int32_t)y, (int32_t)(x + w), (int32_t)(y + h));
}
/* a text across two bands (the menu and the document draw 124 + 85 rows): each band draws its part, the same
 * screen box twice, cut at the band edge; together they are whole (flag 16) */
static int split_pair(rbox_t *b)
{
    uint32_t j;
    if (!(b->flags & 2u)) return 0;
    for (j = 0; j < nscr; j++) {
        rbox_t *o = &scr[j];
        if ((o->flags & 2u) && o->x0 == b->x0 && o->x1 == b->x1 && o->y0 == b->y0 && o->y1 == b->y1 && !strcmp(o->s, b->s)) {
            o->flags = (uint8_t)((o->flags & ~2u) | 16u);
            return 1;
        }
    }
    return 0;
}
/* a text or icon box that touches a cell or card of its canvas (GFX_HOOK_CELL) but spills past its edge; for the
 * audit, a text inside its smallest cell closer than 2 px to an edge */
static void contain(int32_t x, int32_t y)
{
    uint32_t i, c;
    for (i = 0; i < npend; i++) {
        const rbox_t *b = &pend[i];
        int32_t best = 1 << 30, gap = 99;
        for (c = 0; c < ncells && !(b->flags & 4u); c++) {   /* (texts only: an icon's box is its whole cell) */
            const int16_t *r = cells[c];
            int32_t a = (r[2] - r[0]) * (r[3] - r[1]), g;
            if (b->x0 < r[0] || b->x1 > r[2] || b->y0 < r[1] || b->y1 > r[3] || a >= best) continue;
            best = a;
            g = b->x0 - r[0];
            if (r[2] - b->x1 < g) g = r[2] - b->x1;
            if (b->y0 - r[1] < g) g = b->y0 - r[1];
            if (r[3] - b->y1 < g) g = r[3] - b->y1;
            gap = g;
        }
        if (gap < 2 && aud) {
            ntight++;
            if (strlen(tight) + strlen(b->s) + 8u < sizeof tight)
                snprintf(tight + strlen(tight), sizeof tight - strlen(tight), "%s'%s' %dpx", tight[0] ? ", " : "", b->s, (int)gap);
        }
        for (c = 0; c < ncells; c++) {
            const int16_t *r = cells[c];
            int touch = b->x0 < r[2] && r[0] < b->x1 && b->y0 < r[3] && r[1] < b->y1;
            int in = b->x0 >= r[0] && b->x1 <= r[2] && b->y0 >= r[1] && b->y1 <= r[3];
            int around = r[0] >= b->x0 && r[2] <= b->x1 && r[1] >= b->y0 && r[3] <= b->y1;   /* (a mark inside the box) */
            if (touch && !in && !around) {
                rbox_t t = *b, o = {(int16_t)(r[0] + x), (int16_t)(r[1] + y), (int16_t)(r[2] + x), (int16_t)(r[3] + y), 0, "cell"};
                t.x0 += (int16_t)x; t.x1 += (int16_t)x; t.y0 += (int16_t)y; t.y1 += (int16_t)y;
                nspill++;
                finding("spills out of its cell", &t, &o);
                break;
            }
        }
    }
}
static void trim_check(int32_t x, int32_t y)
{
    uint32_t i;
    for (i = 0; i < ntrim; i++) {
        const int16_t *t = trims[i];
        int32_t px, py, bad = 0;
        rbox_t o = {(int16_t)(t[0] + x), (int16_t)(t[1] + y), (int16_t)(t[2] + x), (int16_t)(t[3] + y), 0, "trim"};
        n_trim_checked++;
        for (py = t[1]; py < t[3]; py++)
            for (px = t[0]; px < t[2]; px++)
            {
                uint16_t px16 = swap16(cv_px[(uint32_t)py * cv_w + (uint32_t)px]);
                if (trim_c[i]) bad += px16 != trim_c[i];
                else {                                       /* wood: any shade between its two ends */
                    uint32_t wi;
                    for (wi = 0; wi <= 16u && px16 != ctl_blend(WOOD_LO, WOOD_HI, wi); wi++) ;
                    bad += wi > 16u;
                }
            }
        if (bad) {
            n_trim_bad++;
            finding("trim drawn over (under content)", &o, 0);
        }
    }
    ntrim = 0;
}
static void hk_blit(uint32_t x, uint32_t y, uint32_t r0, uint32_t w, uint32_t h)
{
    uint32_t i, k = 0;
    al_flush();
    contain((int32_t)x, (int32_t)y);
    trim_check((int32_t)x, (int32_t)y);
    ncells = 0;
    blit_px += (uint64_t)w * (h > r0 ? h - r0 : 0u);
    for (i = 0; i < npend; i++) {
        rbox_t b = pend[i];
        if (b.y1 <= (int32_t)r0) continue;           /* rows not blitted */
        if (b.y0 < (int32_t)r0) b.flags |= 2u;
        b.x0 += (int16_t)x; b.x1 += (int16_t)x; b.y0 += (int16_t)y; b.y1 += (int16_t)y;
        if (!split_pair(&b)) pend[k++] = b;
    }
    cover((int32_t)x, (int32_t)(y + r0), (int32_t)(x + w), (int32_t)(y + h));
    rules_cover((int32_t)x, (int32_t)(y + r0), (int32_t)(x + w), (int32_t)(y + h));
    for (i = 0; i < k && nscr < 1200u; i++)
        scr[nscr++] = pend[i];
    npend = 0;
}

/* SONG (ui_graph.c graph_song): its texts only in the lanes (their cushions, the slots' letters, + ADD), the ruler's
 * numbers under its line and ONE line at the panel's foot (at most a text left and one right, on one baseline) */
static uint32_t nsong_txt;
static void song_bands(void)
{
    uint32_t i, nl = 0;
    int32_t py = (int32_t)Y_GRAPH, base = -1;
    if (ui.home || ui.menu || ui.layer || ui.confirm || name_on() || cur_page()->graph != GR_SONG) return;
    for (i = 0; i < nscr; i++) {
        const rbox_t *b = &scr[i];
        int32_t k, lane = 0;
        if (b->y1 <= py || b->y0 >= py + (int32_t)H_GRAPH) continue;   /* (the header, the cards, the footer) */
        nsong_txt++;
        if ((b->flags & 4u) && strcmp(b->s, "icon")) continue;          /* (a keycap: with its hint's words) */
        for (k = 0; k < NTRK; k++)                    /* in one lane, or (+ ADD's, centred on the four) the lanes' */
            lane |= b->y0 >= py + SG_LY(k) && b->y1 <= py + SG_LY(k) + SG_LH;
        lane |= !strcmp(b->s, "+") && b->y0 >= py + SG_Y && b->y1 <= py + SG_Y + SG_H;
        if (lane && b->x0 >= SG_TX && b->x1 <= SG_X0 + SG_W) continue;
        if (b->y0 > py + SG_RY && b->y1 <= py + SG_RY + 16 && b->x0 >= SG_X0) continue;   /* the ruler's numbers */
        if (b->y0 >= py + SG_RY + 14 && b->y1 <= py + SG_IY + 2 && b->x0 >= SG_X0 && b->x1 <= SG_X0 + SG_W) {
            nl++;                                     /* the line: one baseline, two texts at most */
            if (base < 0) base = b->y1;
            if (nl > 2u || b->y1 - base > 3 || base - b->y1 > 3) finding("SONG: a second line", b, 0);
            continue;
        }
        finding("SONG: text outside its bands", b, 0);
    }
}
static void lint(void)
{
    uint32_t i, j;
    song_bands();
    for (i = 0; i < nscr; i++) {
        const rbox_t *b = &scr[i];
        if (b->x0 < 0 || b->y0 < 0 || b->x1 > 240 || b->y1 > 240) finding("off the screen", b, 0);
        if ((b->flags & 2u) && !(b->flags & 32u)) finding("cut by its canvas", b, 0);
        if ((b->flags & 1u) && !(b->flags & 8u)) finding("ellipsised (not free text)", b, 0);
        if ((b->flags & 9u) == 9u) {
            for (j = 0; j < nfree_seen && strcmp(free_seen[j], b->s); j++) ;
            if (j == nfree_seen && nfree_seen < 64u) {
                snprintf(free_seen[nfree_seen++], 40, "%s", b->s);
                fprintf(rep, "free   %-22s ellipsised '%s'\n", cur_name, b->s);
            }
            nfree++;
        }
        for (j = i + 1u; j < nscr; j++) {
            const rbox_t *o = &scr[j];
            if (b->x0 < o->x1 && o->x0 < b->x1 && b->y0 < o->y1 && o->y0 < b->y1) finding("overlaps", b, o);
        }
        for (j = 0; j < nrules; j++) {               /* a divider is a cell edge: nothing on it */
            const int16_t *r = rules[j];
            rbox_t o = {r[0], r[1], r[2], r[3], 0, "divider"};
            if (b->x0 < r[2] && r[0] < b->x1 && b->y0 < r[3] && r[1] < b->y1) finding("touches a divider", b, &o);
        }
    }
}

/* the audit of one screen (GREY): its texts (count, ink-box px), icons, keycaps, the ellipsised and tight texts,
 * and the words themselves */
static FILE *audf;
static void audit_scene(const char *name)
{
    uint32_t i, nt = 0, ni = 0, nk = 0, ne = 0, at = 0, ai = 0, ak = 0;
    char words[1200] = "";
    if (!audf) return;
    for (i = 0; i < nscr; i++) {
        const rbox_t *b = &scr[i];
        uint32_t a = (uint32_t)((b->x1 - b->x0) * (b->y1 - b->y0));
        if ((b->flags & 4u) && !strcmp(b->s, "icon")) { ni++; ai += a; continue; }
        if (b->flags & 4u) { nk++; ak += a; continue; }
        nt++; at += a;
        if (b->flags & 1u) ne++;
        if (strlen(words) + strlen(b->s) + 4u < sizeof words)
            snprintf(words + strlen(words), sizeof words - strlen(words), "%s%s", words[0] ? " | " : "", b->s);
    }
    fprintf(audf, "%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t%s\n", name, nt, at, ni, ai, nk, ak, ne, ntight, words, tight);
}
/* OUTDIR/font_audit.tsv (GREY, FLAT and LARGE): every text of every screen with its face (S M L), its ink box and
 * whether it was ellipsised */
static FILE *fontf;
static void font_scene(const char *mode, const char *name)
{
    uint32_t i;
    if (!fontf) return;
    for (i = 0; i < nscr; i++) {
        const rbox_t *b = &scr[i];
        if (b->flags & 4u) continue;                 /* (icons, keycaps) */
        fprintf(fontf, "%s\t%s\t%c\t%d\t%d\t%d\t%d\t%s%s\n", mode, name, b->fnt ? b->fnt : '?', b->x0, b->y0, b->x1, b->y1,
                b->s, (b->flags & 1u) ? "\t(ellipsised)" : "");
    }
}
static void hk_ink(uint16_t ink, uint16_t bg)
{
    if (ink != bg) return;
    if (nink < 20u) fprintf(rep, "%s: ink %04x drawn on its own colour (%s)\n", cur_name, ink, UI_MACHINE_PALS[(ux.trim - 1u) % UI_NMACHINE].name);
    nink++;
}
static void write_ppm(const char *dir, const char *pal, const char *name)
{
    char path[512];
    uint32_t i;
    FILE *f;
    snprintf(path, sizeof path, "%s/ppm/%s_%s.ppm", dir, pal, name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ the screens --- */
/* machine e's palette and trim forced (trim_machine is not updated, so the UI's palette_follow leaves it alone) */
static void pal(uint32_t p) { settings.palette = UI_PAL_MACHINE; palette_set(PAL_MACHINE_ID(p)); }
static void palm(uint32_t e) { pal(e); }
/* MACHINE mode as the menu sets it: the palette is the selected track's machine's (palette_apply) */
static uint32_t mach_bad;
static void machine_mode(void)
{
    settings.palette = UI_PAL_MACHINE;
    trim_machine = 0xFFu;
    palette_apply();
    if (ux.trim != 1u + TSEL->eng_req % NENGINES || T_BG != UI_MACHINE_PALS[TSEL->eng_req % NENGINES].bg) mach_bad++;
}
static void pal_sweep(void) { pal(TSEL->eng_req % NENGINES); }   /* the value sweeps: the palette of the machine on the selected track (as the UI sets it) */
static void state(void)                          /* a playing song with steps on every track */
{
    uint32_t i;
    ui_power_on();
    seq_fill = 0;                                /* (1.5.5: GLO's FILL, held in layer_glo_fill only; */
    eu.on = 0;                                   /* SEQ TOOLS' EUCLID, open in layer_seq_euclid*) */
    if (large_on) ui_prefs |= PREF_LARGE;
    song.playing = 1;
    song.g[G_BPM] = 124;
    clk_pos = 0; clk_step = 8; cin_total = cin_left = 0;   /* the bar's 3rd beat: the header's metronome at rest */
    for (i = 0; i < NTRK; i++) trk[i].seq_idx = 5;
    my_steps(&trk[1]);
    trk[0].step[2].flags |= SF_ACCENT;
    trk[0].step[6].flags |= SF_SLIDE;
    for (i = 0; i < SCOPE_N; i++)                 /* a stand-in signal for the scope */
        scope_buf[i] = (int16_t)((int32_t)((i * 37u) % 128u) * 200 - 12800 + (int32_t)((i % 32u) < 16u ? 3000 : -3000));
    scope_w = 0;
    for (i = 0; i < SCOPE_TN; i++)                /* the rack's: a sine and its third (a pad's shape) */
        scope_tb[i] = (int16_t)(9000.0 * sin(i * 0.0491) + 2500.0 * sin(i * 0.1473 + 0.6));
    scope_tw = 0;
}
static void eng(uint32_t e) { set_engine_of(TSEL, e); }
/* SONG's pictures (ui_graph.c sg_picture): four patterns that differ by slot k: T1 a bass line (accents on the
 * beats, LEN as the slot's), T2 held chords (a note and its ties), T3 a sparse melody, T4 a beat-like line (low notes on the beats) */
static void song_steps(uint32_t k)
{
    uint32_t i, j;
    for (j = 0; j < NTRK; j++)
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &trk[j].step[i];
            int on = j == 0u ? (i + k) % 3u != 1u : j == 1u ? i % 8u < 3u + k : j == 2u ? (i * 5u + k * 3u) % 7u < 2u
                                                                                         : i % 2u == 0u || (i + k) % 7u == 0u;
            memset(s, 0, sizeof *s);
            s->time = j == 1u && i % 8u ? ST_TIE : ST_NOTE;
            s->vel = 100;
            if (!on) { s->time = ST_REST; continue; }
            s->note[0] = (uint8_t)(j == 3u ? (i % 4u == 0u ? 36 : 42) : 48 + (i * 7u + k) % 12u), s->n = 1;
            if ((j == 0u && i % 4u == 0u) || (j == 3u && i % 8u == 0u)) s->flags |= SF_ACCENT;
        }
}
/* SONG's sources: project slot k saved with track 1's LEN steps of DIV (N_DIV's order), the patterns song_steps(k) */
static void song_src(uint32_t k, int16_t len, int16_t div, const char *name)
{
    song_steps(k);
    trk[0].p[P_SLEN] = len;
    trk[0].p[P_SDIV] = div;
    project_save_as(k, name);
}
/* SONG drawn as on entering it: the slots read again (ui_graph.c graph_project_used: twice a second) and the
 * timeline's view new */
static void sg_fresh(void)
{
    fm1_ms += 1000u;
    sgv.init = 0;
}

enum { S_HOME, S_HOME_IDLE, S_MESSAGE, S_MESSAGE_KEY, S_PRESETS, S_PRESETS_NOFAV, S_USER, S_PHRASES, S_PROJECT,
       S_TOOLS, S_SONG_EMPTY, S_SONG, S_STEP, S_AUTO_STEPS, S_AUTO_ADD_CHANCE, S_MOTION, S_MIXER, S_MIXER_HOME,
       S_MIXER_PAN, S_ENV, S_ENVDEST, S_LFO, S_MOD, S_FX, S_SLICER, S_DLY, S_SCL, S_CHORD, S_CHORD_WIDE, S_CHORD_OFF,
       S_ARP, S_VOICE, S_CLOCK, S_MENU_MIDI, S_EDIT_ANALOG, S_EDIT_WAVE, S_CONFIRM_SEQ, S_CONFIRM_PROJ,
       S_CONFIRM_USER, S_CONFIRM_PAT, S_CONFIRM_MOTION, S_CONFIRM_ERASE, S_MENU, S_MENU_SPEAKER, S_ABOUT,
       S_ABOUT_REC, S_ABOUT_CREDITS, S_ABOUT_END, S_UBOOT, S_CALIBRATION, S_SPLASH, S_BATT0, S_BATT1, S_BATT2,
       S_BATT3, S_BATT_USB, S_MOTION_REC, S_MOTION_OFF, S_MOTION_CARD, S_SONG_HOME, S_FX_PEEK, S_FX_HELD, S_FX_WAIT,
       S_FX_HARM, S_FX_MOD, S_FX_ASSIGN, S_MENU_HOLD, S_MENU_LEDS, S_MENU_END, S_MENU_SYSTEM, S_MENU_SLIDE, S_REVERB,
       S_GLO_PEEK, S_GLO_ACTIVE, S_GLO_EXT, S_SCL_PEEK, S_SCL_ACTIVE, S_EDIT_PEEK, S_EDIT_ACTIVE, S_EDIT_USER,
       S_LAYER_HINT, S_LAYER_LOCK, S_LAYER_LOCK_FX, S_SEQ_TOOLS, S_SEQ_TOOLS_SONG, S_NAME_USER, S_NAME_TYPING,
       S_NAME_123, S_NAME_EMPTY, S_NAME_FULL, S_NAME_PLAYING, S_PROJECT_NAMED, S_SONG_NAMED, S_PRESETS_CAT,
       S_PRESETS_CAT_WIDE, S_NAME_CAT, S_NAME_RANDOM, S_MENU_ANIM_IDLE, S_MOMENTARY, S_MOMENTARY_EDIT, S_SONG_LONG,
       S_SONG_PLAYING, S_SONG_LONG_PLAYING, S_SONG_UNSAVED, S_SONG_ADD, S_USER_FOOT, S_ROLL_EMPTY, S_ROLL_ACID,
       S_ROLL_CHORDS, S_ROLL_TIES, S_ROLL_LEN32, S_ROLL_HIGH, S_ROLL_LOW, S_ROLL_WIDE, S_ROLL_PLAYING, S_ROLL_LOCKS,
       S_ROLL_LOCK_HELD, S_MOTION_LOCKS, S_AUTO_LIST, S_AUTO_LIST_ADD, S_AUTO_NUDGE, S_AUTO_QNT, S_AUTO_ADD_NUDGE,
       S_ROLL_LEN, S_AUTO_VEL, S_AUTO_ADD_VEL, S_LFO2, S_REC_LAYER, S_REC_LAYER_SET, S_REC_LAYER_SONG, S_COUNTIN,
       S_COUNTIN2, S_MENU_AUDIO, S_HEAD_PLAY_REC, S_HEAD_REC_OTHER, S_HEAD_BPM_TURN, S_HEAD_BPM_LOCK,
       S_HEAD_BPM_LOCKED, S_HEAD_GLO_TURN, S_HEAD_DOWNBEAT, S_HEAD_BEAT_2, S_SONG_LANES, S_SONG_LANES_PLAYING,
       S_SONG_LANES_SILENT, S_SONG_PICTURES, S_HEAD_SLOT, S_HEAD_SLOT_EDITED, S_HEAD_SLOT_SONG, S_MENU_TUNE,
       S_MENU_INFO, S_HOME_BIG, S_HOME_BIG_T3, S_HOME_BIG_PLAYING, S_HOME_BIG_USER, S_HOME_BIG_EXT, S_HELP_HOME,
       S_HELP_USER, S_HELP_LFO, S_HELP_GLO, S_HELP_SEQ, S_MENU_HELP, S_INSERT_CRUSH, S_INSERT_FOLD, S_INSERT_PHASER,
       S_INSERT_OFF, S_INSERT_MIX, S_ENV_SYNC, S_ENV_DEST_SYNC, S_FILTER, S_LEARN_WAIT, S_LEARN_PICK, S_LEARN_SET,
       S_LEARN_LONG, S_GLO_LEARN, S_MENU_LCLEAR, S_MENU_LCLEAR_NONE, S_DLY_DOT, S_DLY_DOT16, S_FX_THROW, S_DETAIL,
       S_DETAIL_PLAYING, S_DETAIL_REST, S_ROLL_MARKS, S_HELP_DETAIL, S_DETAIL_COND, S_DETAIL_FILL, S_AUTO_COND,
       S_GLO_FILL, S_SEQ_EUCLID, S_SEQ_EUCLID_64, S_MENU_SELECT, S_PROJECT_ERASE, S_CONFIRM_ERASE_PROJ,
       S_TOOLS_CLEAN, S_CONFIRM_CLEAN, S_MENU_MIDI_TRACK, S_MENU_MIDI_TCH_UNUSED, S_MENU_MIDI_AUTO,
       S_HOME_BIG_STOPPED, S_MIXER_MIDI, S_EDIT_BIG_OBX, S_EDIT_BIG_WAVE, S_EDIT_BIG_ACID, S_EDIT_BIG_DRIFT,
       S_COUNT };
static const char *const S_NAME[S_COUNT] = {"home", "home_idle", "message", "message_key", "presets",
    "presets_nofav", "user", "phrases", "project", "tools", "song_empty", "song", "step", "auto_steps",
    "auto_add_chance", "automation", "mixer", "mixer_from_home", "mixer_pan", "env", "env_dest", "lfo", "mod", "fx",
    "slicer", "dly", "scl", "chord", "chord_wide", "chord_off", "arp", "voice", "clock", "menu_midi", "edit_analog",
    "edit_wave", "confirm_seq", "confirm_project", "confirm_user", "confirm_pattern", "confirm_motion",
    "confirm_erase", "menu", "menu_speaker", "about", "about_rec", "about_credits", "about_end", "uboot",
    "calibration", "splash", "batt_0", "batt_1", "batt_2", "batt_3", "batt_usb", "motion_rec", "motion_off",
    "motion_card", "song_home", "perform_peek", "perform_held", "perform_wait", "perform_harm", "perform_mod",
    "perform_assign", "menu_hold", "menu_leds", "menu_end", "menu_system", "menu_slide", "reverb_spring",
    "layer_glo_peek", "layer_glo_active", "layer_glo_ext", "layer_scl_peek", "layer_scl_active", "layer_edit_peek",
    "layer_edit_active", "layer_edit_user", "layer_hint", "layer_lock", "layer_lock_fx", "layer_seq_tools",
    "layer_seq_tools_song", "name_user", "name_typing", "name_123", "name_empty", "name_full", "name_playing",
    "project_named", "song_named", "presets_bass", "presets_other_empty", "name_category", "name_random",
    "menu_anim_idle", "momentary", "momentary_edit", "song_long", "song_playing", "song_long_playing",
    "song_unsaved", "song_add", "user_foot", "roll_empty", "roll_acid", "roll_chords", "roll_ties", "roll_len32_p2",
    "roll_high", "roll_low", "roll_wide", "roll_playing", "roll_locks", "roll_lock_held", "motion_locks",
    "auto_list", "auto_list_add", "auto_nudge", "auto_quantize", "auto_add_nudge", "roll_len", "auto_vel",
    "auto_add_vel", "lfo2", "layer_rec", "layer_rec_set", "layer_rec_song", "countin", "countin_2bars", "menu_audio",
    "head_play_rec", "head_rec_other", "head_bpm_turn", "head_bpm_lock", "head_bpm_locked", "head_glo_turn",
    "head_downbeat", "head_beat_2", "song_lanes", "song_lanes_playing", "song_lanes_silent", "song_pictures",
    "head_slot", "head_slot_edited", "head_slot_song", "menu_tune", "menu_info", "home_big",
    "home_big_t3", "home_big_playing", "home_big_user", "home_big_ext", "help_home", "help_user", "help_lfo",
    "layer_glo_help", "layer_seq_help", "menu_help", "insert_crush", "insert_fold", "insert_phaser", "insert_off",
    "insert_mix", "env_sync", "env_dest_sync", "filter", "learn_wait", "learn_pick", "learn_set", "learn_long",
    "layer_glo_learn", "menu_learn_clear", "menu_learn_clear_none", "dly_dotted", "dly_dotted_16", "perform_throw",
    "detail", "detail_playing", "detail_rest", "roll_marks", "help_detail", "detail_cond", "detail_fill",
    "auto_cond", "layer_glo_fill", "layer_seq_euclid", "layer_seq_euclid_64", "menu_select", "project_erase",
    "confirm_erase_project", "tools_clean", "confirm_clean", "menu_midi_track", "menu_midi_channels_unused",
    "menu_midi_auto_select", "home_big_stopped", "mixer_midi", "edit_big_obx", "edit_big_wave", "edit_big_acid",
    "edit_big_drift"};

/* the STEP page's piano roll (ui_graph.c graph_roll) on track 1, stopped unless said: an empty pattern; ACID in
 * A minor; POLY chords (Am7 F C G, tied); a line with ties, slides and accents in C major; LEN 32 on its second
 * page, playing; high (A6..) and low (C-1..) notes; a page wider than the view (edge marks); ACID playing with a
 * key held (A3, its row lit) */
static void step_put(step_t *st, uint32_t time, uint32_t flags, uint32_t n, const uint8_t *notes)
{
    uint32_t j;
    memset(st, 0, sizeof *st);
    st->time = (uint8_t)time; st->flags = (uint8_t)flags; st->n = (uint8_t)n; st->vel = 100;
    for (j = 0; j < n; j++) st->note[j] = notes[j];
}
static void roll_scene(int s)
{
    static const uint8_t AM7[] = {57, 60, 64, 67}, FM7[] = {53, 57, 60, 64}, CMA[] = {60, 64, 67}, GMA[] = {55, 59, 62, 67};
    static const uint8_t LINE[16] = {48, 0, 55, 60, 0, 59, 0, 0, 62, 64, 0, 67, 0, 65, 64, 0};
    static const uint8_t LINE_T[16] = {0, 1, 0, 0, 1, 0, 2, 2, 0, 0, 1, 0, 1, 0, 0, 2};   /* 0 note, 1 tie, 2 rest */
    track_t *t;
    uint32_t i;
    song.sel = 0; t = TSEL;
    song.playing = 0;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    go_page(GR_ROLL);
    ui.cursor = 0;
    switch (s) {
    case S_ROLL_EMPTY: break;
    case S_ROLL_ACID:
    case S_ROLL_PLAYING:
        load_pat16(t, PATTERNS[0].note, PATTERNS[0].flags);
        t->p[P_ROOT] = 9; t->p[P_SCALE] = 2; ui.cursor = 6;
        if (s == S_ROLL_PLAYING) {
            song.playing = 1; t->seq_idx = 9;
            kb_trk[3] = 0; kb_chn[3] = 1; kb_chord[3][0] = 57;            /* A3 held */
        }
        break;
    case S_ROLL_CHORDS:
        t->p[P_VOICE] = V_POLY; t->p[P_ROOT] = 9; t->p[P_SCALE] = 2;
        for (i = 0; i < 16u; i++) {
            const uint8_t *c = i / 4u == 0u ? AM7 : i / 4u == 1u ? FM7 : i / 4u == 2u ? CMA : GMA;
            uint32_t n = i / 4u == 2u ? 3u : 4u;
            if (i % 4u == 0u) step_put(&t->step[i], ST_NOTE, i == 8u ? SF_ACCENT : 0u, n, c);
            else if (i % 4u == 1u) step_put(&t->step[i], ST_TIE, 0, 0, c);
            else if (i % 4u == 2u) step_put(&t->step[i], ST_NOTE, 0, n, c);
            else step_put(&t->step[i], ST_REST, 0, 0, c);
        }
        ui.cursor = 4;
        break;
    case S_ROLL_TIES:
        t->p[P_ROOT] = 0; t->p[P_SCALE] = 1;
        for (i = 0; i < 16u; i++) {
            uint8_t n = LINE[i];
            uint32_t f = (i == 3u || i == 9u ? SF_SLIDE : 0u) | (i == 0u || i == 8u || i == 13u ? SF_ACCENT : 0u);
            if (LINE_T[i] == 0u) step_put(&t->step[i], ST_NOTE, f, 1, &n);
            else step_put(&t->step[i], LINE_T[i] == 1u ? ST_TIE : ST_REST, 0, 0, &n);
        }
        t->step[2].flags |= SF_SLIDE;                  /* (slides into a note, from before a tie) */
        ui.cursor = 9;
        break;
    case S_ROLL_LEN32:
        load_pat16(t, PATTERNS[0].note, PATTERNS[0].flags);
        t->p[P_SLEN] = 32;
        for (i = 16; i < 32u; i++) {
            uint8_t n = (uint8_t)(60 + (i * 5u) % 12u);
            if (i % 3u != 2u) step_put(&t->step[i], ST_NOTE, i % 8u == 0u ? SF_ACCENT : 0u, 1, &n);
        }
        ui.cursor = 20; song.playing = 1; t->seq_idx = 22;
        break;
    case S_ROLL_LEN:                                 /* 1.5 (#178): step 5's note made 4 steps long (KNOB 3 LEN, hot) */
        t->p[P_ROOT] = 0; t->p[P_SCALE] = 1;
        for (i = 0; i < 16u; i += 4u) {
            uint8_t n = LINE[i] ? LINE[i] : 60;
            step_put(&t->step[i], ST_NOTE, i == 8u ? SF_ACCENT : 0u, 1, &n);
        }
        (void)note_set_len(t, 0, 2);
        (void)note_set_len(t, 4, 4);
        ui.cursor = 4; ui.hot_col = 2; ui.hot_t = 30;
        break;
    case S_ROLL_HIGH:
    case S_ROLL_LOW:
        for (i = 0; i < 16u; i += 2u) {
            uint8_t n = (uint8_t)(s == S_ROLL_HIGH ? 112 + (i * 7u) % 15u : (i * 7u) % 15u);
            step_put(&t->step[i], ST_NOTE, 0, 1, &n);
        }
        ui.cursor = 2;
        break;
    case S_ROLL_WIDE:
        t->p[P_VOICE] = V_POLY;
        for (i = 0; i < 16u; i += 2u) {
            uint8_t n[2] = {(uint8_t)(36 + i), (uint8_t)(72 + i)};
            step_put(&t->step[i], ST_NOTE, 0, 2, n);
        }
        ui.cursor = 4;
        break;
    default: break;
    }
    ui.bank = (uint8_t)(ui.cursor / 16u);
}

/* 1.5.1 SEQ > DETAIL (#199; ui_graph.c graph_detail): the selected track's steps given CHANCE, RATCH, NUDGE and VEL
 * (QUANTIZE OFF: the nudges play) on its note steps (ACID) */
static void detail_values(track_t *t, int unused)
{
    static const uint8_t RT[16] = {1, 1, 2, 1, 1, 1, 3, 1, 1, 1, 1, 1, 4, 1, 1, 1};
    static const int8_t ND[16] = {0, 0, 0, 0, 3, 0, 0, 0, 0, 0, -4, 0, 0, 0, 0, 0};
    static const uint8_t CH[16] = {100, 100, 100, 100, 100, 100, 60, 100, 100, 25, 100, 100, 100, 100, 80, 100};
    static const uint8_t VL[16] = {0, 0, 70, 110, 0, 0, 40, 0, 120, 90, 0, 60, 0, 0, 100, 0};
    uint32_t i;
    t->p[P_SQNT] = 0;
    for (i = 0; i < 16u; i++) {
        step_t *st = &t->step[i];
        if (st->time != ST_NOTE || !st->n)
            continue;
        (void)unused;
        step_set_ratchet(st, RT[i]);
        step_set_nudge(st, ND[i]);
        step_set_chance(st, CH[i]);
        st->vel = VL[i];
    }
}
static void go_title_page(const char *title)
{
    uint32_t i;
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, title); i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}

/* HOME with four patterns (ui_big.c draw_home_big): T1 LEN 32, a line on its second page; T2 a melody with accents; T3
 * held notes (TIEs carried on, one TIE after a REST: nothing to carry), muted; T4 a line of low notes (LEN 32). Playing:
 * T2 selected, the tabs' meters moving (T3 muted: none); stopped: T1 selected, its cursor on its second page */
static void tracks_scene(int playing)
{
    static const uint8_t T3[16] = {0, 1, 1, 2, 0, 1, 2, 2, 0, 0, 1, 1, 2, 2, 1, 2};   /* 0 note, 1 tie, 2 rest */
    uint32_t i;
    for (i = 0; i < 16u; i++) {                      /* T4: notes on the beats, an accent on 1 */
        uint8_t n = (uint8_t)(36 + (i % 8u == 0u ? 0u : 7u));
        if (i % 2u == 0u) step_put(&trk[3].step[i], ST_NOTE, i == 0u ? SF_ACCENT : 0u, 1, &n);
        else step_put(&trk[3].step[i], ST_REST, 0, 0, &n);
    }
    trk[3].p[P_SLEN] = 32;
    for (i = 16; i < 32u; i++) trk[3].step[i] = trk[3].step[i - 16u];
    trk[0].p[P_SLEN] = 32;
    for (i = 16; i < 32u; i++) {
        uint8_t n = (uint8_t)(48 + (i * 5u) % 12u);
        if (i % 3u != 2u) step_put(&trk[0].step[i], ST_NOTE, i % 8u == 0u ? SF_ACCENT : 0u, 1, &n);
        else step_put(&trk[0].step[i], i == 23u ? ST_TIE : ST_REST, 0, 0, &n);
    }
    for (i = 0; i < 16u; i++) {
        uint8_t n = (uint8_t)(60 + i % 5u);
        step_put(&trk[2].step[i], T3[i] == 0u ? ST_NOTE : T3[i] == 1u ? ST_TIE : ST_REST, i == 8u ? SF_ACCENT : 0u,
                 T3[i] == 0u, &n);
    }
    trk[2].p[P_SLEN] = 16;
    trk[2].p[P_MUTE] = 1;
    trk[0].seq_idx = 22; trk[3].seq_idx = 12;
    trk[0].peak = 9000; trk[1].peak = 3000; trk[2].peak = 12000; trk[3].peak = 20000;   /* (T3 muted: no meter) */
    song.sel = 1;
    if (!playing) { song.playing = 0; song.sel = 0; ui.cursor = 20; ui.bank = 1; }
    go_home();
}

/* the INSERT of the selected track (1.5): TYPE, A B C, MIX */
static void set_ins_ui(int32_t ty, int32_t a, int32_t b, int32_t c, int32_t mix)
{
    TSEL->p[P_ITYPE] = (int16_t)ty;
    TSEL->p[P_IA] = (int16_t)a;
    TSEL->p[P_IB] = (int16_t)b;
    TSEL->p[P_IC] = (int16_t)c;
    TSEL->p[P_IMIX] = (int16_t)mix;
}

static void setup(int s)
{
    memset(kb_chn, 0, sizeof kb_chn);               /* no key held (roll_playing holds one) */
    state();
    switch (s) {
    case S_HOME: song.octave = 2; song.rec = 1; usb.config = 1; break;
    case S_HOME_IDLE: song.playing = 0; song.batt_raw = 570; ui.hot_col = 1; ui.hot_t = 30; break;
    case S_MESSAGE: ui_say("LOADED ", "07 A VERY LONG PATTERN NAME"); break;
    case S_MESSAGE_KEY: ui_message("[SAVE] HOLD TO UNDO"); break;            /* a message with a keycap */
    case S_PRESETS: favorite_set(0, 4, 1); favorite_set(0, 5, 1); go_page(GR_BROWSE); break;
    case S_PRESETS_NOFAV: favorites.filter = 1; go_page(GR_BROWSE); break;
    case S_USER: song.playing = 0; up_store(3, "MY LONG BASS NAME"); up_store(4, "PAD"); ui.uslot = 3; go_page(GR_USER); break;
    case S_PHRASES: go_page(GR_PATS); break;
    case S_PROJECT: song.playing = 0; project_save(1); song.g[G_SLOT] = 2; go_page(GR_SLOTS); ui.act = 4; break;
    case S_TOOLS: go_page(GR_TOOLS); ui.act = 1; break;
    case S_SONG_EMPTY: song.playing = 0; go_page(GR_SONG); break;
    case S_SONG:
        song.playing = 0; project_save(0); project_save(1);
        chain_config.count = 3;
        chain_config.row[0] = chain_row_of(0, 2); chain_config.row[1] = chain_row_of(1, 4); chain_config.row[2] = chain_row_of(2, 1);
        ui.song_row = 1; go_page(GR_SONG); sg_fresh(); chain_prepare(); events_block(32);
        break;
    case S_STEP: song.rec = 1; go_page(GR_ROLL); ui.cursor = 6; break;
    /* 1.2 SEQ > AUTOMATION: a step's CHANCE and RATCH as rows among the locks and events (step 5's CHANCE selected, its
     * value just turned); + ADD CHANCE on step 4; the first row (PLAY ON, the counts, OCT+ CLEAR) */
    case S_AUTO_STEPS:
        step_set_chance(&TSEL->step[0], 65); step_set_chance(&TSEL->step[4], 25); step_set_ratchet(&TSEL->step[4], 3);
        step_set_ratchet(&TSEL->step[8], 2); motion_set_event(TSEL, 2, P_LRATE, 70); motion_set_lock(TSEL, 4, P_REV, 100);
        go_page(GR_EVENTS); (void)auto_pick(EVC(EVK_CHANCE, 4)); ui.hot_col = 3; ui.hot_t = 30;
        break;
    case S_AUTO_ADD_CHANCE: go_auto_add(EV_CHANCE, 3); break;
    case S_MOTION:
        motion_set_event(TSEL, 1, P_REV, 40); motion_set_lock(TSEL, 2, P_REV, 100); step_set_chance(&TSEL->step[3], 50);
        go_auto_top();
        break;
    /* 1.1.5, the REC layer (REC held): over HOME, the settings as they come (CLICK OFF, COUNT-IN OFF, LEVEL MID);
     * over STEP armed, CLICK REC, COUNT-IN 2 BARS, LEVEL HIGH (KNOB 2 just turned); a song playing on an empty
     * track (CLEAR dimmed: STOP TO EDIT) */
    case S_REC_LAYER: go_home(); ui.layer = LAYER_REC; break;
    case S_REC_LAYER_SET: song.rec = 1; go_page(GR_ROLL); ui.cursor = 6; rp_put(RP_CLICK, 1u); rp_put(RP_COUNTIN, 2u);
        rp_put(RP_LEVEL, 2u); ui.layer = LAYER_REC; ui.hot_col = 1; ui.hot_t = 30; break;
    case S_REC_LAYER_SONG: song.sel = 3; track_defaults_steps(TSEL); go_page(GR_SONG); chain.running = 1; ui.layer = LAYER_REC;
        break;
    /* 1.1 (Discussion #131): the count-in over the page: 1 BAR on its second beat (3 to go); 2 BARS, the first bar's
     * first beat (STEP under it); the MENU's AUDIO tab with CLICK REC selected, COUNT-IN 1 BAR */
    case S_COUNTIN: song.playing = 0; song.rec = 1; cin_total = 4; cin_left = 3; go_home(); break;
    case S_COUNTIN2: song.playing = 0; song.rec = 1; cin_total = 8; cin_left = 8; go_page(GR_ROLL); break;
    case S_MENU_AUDIO: ui.menu = 1; ui.menu_sel = MI_CLICK; rp_put(RP_CLICK, 1u); rp_put(RP_COUNTIN, 1u); break;
    /* 1.2: the header's three slots (ui_draw.c draw_head): playing with the selected track armed; another track armed;
     * the BPM being turned (the accent's); BPM LOCK ON (the lock right of the BPM), and its message after a turn;
     * GLO with the tempo turned and BPM LOCK ON (the label right of the lock); the metronome on the bar's 1st beat
     * (the accent's) and its 2nd (mirrored) */
    case S_HEAD_PLAY_REC: song.rec = 1u << song.sel; go_home(); break;
    case S_HEAD_REC_OTHER: song.rec = 1u << ((song.sel + 2u) % NTRK); go_home(); break;
    case S_HEAD_BPM_TURN: song.g[G_BPM] = 128; ui.roll_bpm = 128; ui.bpm_t = 40; go_home(); break;
    case S_HEAD_BPM_LOCK: ui_prefs |= PREF_BPM_LOCK; go_home(); break;
    case S_HEAD_BPM_LOCKED: ui_prefs |= PREF_BPM_LOCK; go_home(); ui_message("BPM LOCKED"); break;
    case S_HEAD_GLO_TURN: ui_prefs |= PREF_BPM_LOCK; go_home(); ui.layer = LAYER_GLO; ui.bpm_t = 40; break;
    case S_HEAD_DOWNBEAT: clk_step = 1; song.rec = 1u << song.sel; go_home(); break;
    case S_HEAD_BEAT_2: clk_step = 5; song.rec = 1u << song.sel; go_home(); break;
    /* 1.2: SONG's lanes (ui_graph.c graph_song): four lanes, runs of one slot merged, "-" gaps, the cell selected (section
     * 3, T2); playing (section 2, its playhead across the lanes); the cursor on a "-" cell (T1 silent in section 2: the
     * section counts T2's loop) */
    case S_SONG_LANES: case S_SONG_LANES_PLAYING: case S_SONG_LANES_SILENT: {
        static const uint8_t R[5][5] = {{0, 0, 2, 1, 2}, {4, 0, 2, 1, 1}, {1, 1, 4, 1, 4}, {1, 0, 4, 3, 1}, {0, 0, 2, 4, 2}};
        uint32_t k;
        song.playing = 0;
        song_src(0, 16, 2, "VERSE"); song_src(1, 32, 2, "CHORUS"); song_src(2, 16, 2, "DRUMS"); song_src(3, 8, 2, "FILL");
        chain_config.count = 5;
        for (k = 0; k < 5u; k++) chain_config.row[k] = (chain_row_t){{R[k][0], R[k][1], R[k][2], R[k][3]}, R[k][4]};
        ui.song_row = s == S_SONG_LANES_SILENT ? 1 : 2; ui.song_trk = s == S_SONG_LANES_SILENT ? 0 : 1;
        go_page(GR_SONG); ui.msg_t = 0; sg_fresh();
        if (s == S_SONG_LANES_PLAYING) {
            chain_prepare(); events_block(32);
            for (k = 0; k < 4000u && chain.running && !(chain.row == 1u && chain.remaining == 1u); k++) events_block(div_samples(2));
            for (k = 0; k < 6u; k++) events_block(div_samples(2));
        }
        break;
    }
    /* 1.2: SONG's pictures (ui_graph.c sg_picture): A x4 (four passes, notched), B x2 with T1 64 steps (two to a
     * cell: the mean), C silent on T2 and T3, D not saved; the cursor on T4 (the beat) in section 2 */
    case S_SONG_PICTURES: {
        static const uint8_t R[4][5] = {{0, 0, 0, 0, 4}, {1, 1, 1, 1, 2}, {2, 4, 4, 2, 1}, {3, 0, 4, 3, 1}};
        uint32_t k;
        song.playing = 0;
        song_src(0, 16, 2, "VERSE"); song_src(2, 16, 2, "BREAK");
        song_steps(1);
        trk[0].p[P_SLEN] = 64; trk[0].p[P_SDIV] = 2;
        for (k = 16; k < 64u; k++) trk[0].step[k] = trk[0].step[k % 16u];
        for (k = 32; k < 64u; k += 2u) trk[0].step[k].time = ST_REST;   /* (thinner in its second half) */
        project_save_as(1, "CHORUS");
        chain_config.count = 4;
        for (k = 0; k < 4u; k++) chain_config.row[k] = (chain_row_t){{R[k][0], R[k][1], R[k][2], R[k][3]}, R[k][4]};
        ui.song_row = 1; ui.song_trk = 3;
        go_page(GR_SONG); ui.msg_t = 0; sg_fresh();
        break;
    }
    /* 1.2: the header's slot letter (ui_draw.c head_slot): "1 A" (PROJECT A loaded; another track armed: the REC mark
     * outlined after it); "1 A*" edited since, armed (the REC mark after it); a song playing, track 2 silent in its
     * section: "2 -" */
    case S_HEAD_SLOT: song.playing = 0; project_save(0); project_load(0); song.rec = 1u << ((song.sel + 2u) % NTRK); go_home(); ui.msg_t = 0; break;
    case S_HEAD_SLOT_EDITED:
        song.playing = 0; project_save(0); project_load(0); TSEL->step[3].note[0] = 70; TSEL->step[3].n = 1;
        song.rec = 1u << song.sel; go_home(); ui.msg_t = 0; break;
    case S_HEAD_SLOT_SONG:
        song.playing = 0; project_save(0);
        chain_config.count = 1; chain_config.row[0] = (chain_row_t){{0, CHAIN_SILENT, 0, 0}, 4};
        chain_prepare(); events_block(32); song.sel = 1; go_home(); ui.msg_t = 0; break;
    case S_MIXER:
        go_page(GR_TRK);
        trk[0].p[P_LEVEL] = 100; trk[1].p[P_LEVEL] = 84; trk[2].p[P_LEVEL] = 64; trk[3].p[P_LEVEL] = 110;
        trk[0].p[P_PAN] = -20; trk[1].p[P_PAN] = 24; trk[3].p[P_PAN] = -4; trk[1].p[P_REV] = 80;
        trk[2].p[P_MUTE] = 1; trk[3].p[P_MUTE] = 1; song.rec = 2u | 8u; trk[0].peak = 9000;
        break;
    case S_MIXER_HOME: go_home(); home_tap(); break;   /* 1.2: HOME tapped on HOME (its LED lit: MIXER is HOME's) */
    case S_MIXER_PAN:                           /* PAN just turned, the ends of each knob, OFF, armed while stopped */
        go_page(GR_TRK); song.playing = 0; song.sel = 1;
        trk[0].p[P_LEVEL] = 127; trk[1].p[P_LEVEL] = 0; trk[2].p[P_LEVEL] = 1; trk[3].p[P_LEVEL] = 104;
        trk[0].p[P_PAN] = -64; trk[1].p[P_PAN] = 63; trk[2].p[P_PAN] = 0; trk[3].p[P_PAN] = 1;
        trk[0].p[P_REV] = 127; trk[2].p[P_REV] = 1; trk[3].p[P_MUTE] = 1;
        song.rec = 1u | 8u; ui.hot_col = 3; ui.hot_t = 30;
        break;
    case S_ENV: go_title("ENV"); ui.hot_col = 2; ui.hot_t = 30; break;
    case S_ENVDEST: go_title("ENV DEST"); break;
    case S_ENV_SYNC:                                  /* 1.5: ENV SYNC on, the note values (the widest: 1/16D 1/32T) */
        TSEL->p[P_ESYNC] = 1; TSEL->p[P_ATK] = 52; TSEL->p[P_DEC] = 16; TSEL->p[P_REL] = 127;
        go_title("ENV"); ui.hot_col = 0; ui.hot_t = 30; break;
    case S_ENV_DEST_SYNC: TSEL->p[P_ESYNC] = 1; go_title("ENV DEST"); break;
    case S_FILTER: TSEL->p[P_FTYPE] = 1; go_title("FILTER"); break;   /* 1.5: OB-Z's TYPE CUT RES KTR */
    case S_LFO: go_title("LFO"); break;
    case S_MOD: TSEL->p[P_M1SRC] = 1; TSEL->p[P_M1DST] = 2; TSEL->p[P_M1AMT] = 40; TSEL->p[P_M2SRC] = 6;
        TSEL->p[P_M2DST] = 14; TSEL->p[P_M2AMT] = -64; mod_ui_slot = 1; go_title("MOD"); break;
    case S_FX: go_title("FX"); break;
    case S_SLICER: TSEL->p[P_SLCR] = 1; go_title("SLICER"); break;
    case S_DLY: go_title("DLY"); break;
    case S_SCL: TSEL->p[P_SCALE] = 2; go_title("SCL"); break;
    case S_CHORD:                                    /* A minor DIA7, the last chord on B: Bm7b5 */
    case S_CHORD_WIDE: {                             /* A harmonic minor DIA7 +OCT on G#: G#dim7 over three octaves */
        uint8_t out[CHORD_MAX];
        TSEL->p[P_VOICE] = V_POLY; TSEL->p[P_ROOT] = 9; TSEL->p[P_SCALE] = s == S_CHORD ? 2 : 7;
        TSEL->p[P_CHRD] = CH_DIA7; TSEL->p[P_VOIC] = s == S_CHORD ? VC_CLOSE : VC_BASS;
        chord_build(TSEL, s == S_CHORD ? 71u : 68u, out);
        go_title("CHORD"); ui.hot_col = 0; ui.hot_t = 30;
        break;
    }
    case S_CHORD_OFF: go_title("CHORD"); break;
    case S_ARP: go_title("ARP"); break;
    case S_VOICE: go_title("VOICE"); break;
    case S_CLOCK: go_title("CLOCK"); break;          /* (1.2: HOME > MIXER > CLOCK; up to 1.1.5 GLO > GLOBAL) */
    /* 1.2: the old SYSTEM page's items in the MENU: MIDI > MIDI IN (ROUT) on CH5-8; AUDIO > TUNE (-12 ct) selected;
     * SYSTEM > INFO (the USB state, the CPU load) */
    case S_MENU_MIDI: ui.menu = 1; ui.menu_sel = MI_MIDIIN; song.g[G_ROUTE] = 2; break;
    case S_MENU_TUNE: ui.menu = 1; ui.menu_sel = MI_TUNE; song.g[G_TUNE] = -12; break;
    /* 1.3: HOME > TRACKS playing (T2 selected), stopped (T1 selected, its cursor's page), K2 just turned; the MENU's DISPLAY
     * tab with HOME TRACKS selected (8 rows) */
    /* VOID FOUR's HOME (ui_big.c): T1 OB-Z ROYAL HORNS at -6 dB; T3 ACID (its pattern), PRESET just turned (hot);
     * playing with four patterns, the meters, T3 muted, VOLUME just turned, a MIDI note into T4; a user preset with a
     * long name (M, ellipsised) and LEVEL OFF; the external clock (TEMPO DIM, EXT); stopped, T1's second page */
    case S_HOME_BIG: song.sel = 0; apply_preset_to(TSEL, 9); TSEL->p[P_LEVEL] = 90; go_home(); break;
    case S_HOME_BIG_T3: song.sel = 2; go_home(); ui.hot_col = 1; ui.hot_t = 30; break;
    case S_HOME_BIG_PLAYING:
        tracks_scene(1); ui.hot_col = 2; ui.hot_t = 30; midi_lit_ms[3] = fm1_ms | 1u; break;
    /* 1.5 (#134): HOME LEVELS: the rows of TRACKS, the cards T1..T4 LEVEL (T3 muted: DIM), K2 just turned */
    /* 1.5 (Discussion #156): MENU > HELP ON: a page just entered with its hint in the footer (HOME, USER: in place of the
     * OCT+ / OCT- hint, LFO); a layer's map with its second footer row (GLO, SEQ TOOLS); the MENU row */
    case S_HELP_HOME: ui_prefs2 |= PREF_HELP >> 16; go_home(); break;
    case S_HELP_USER: song.playing = 0; ui_prefs2 |= PREF_HELP >> 16; go_page(GR_USER); break;
    case S_HELP_LFO: ui_prefs2 |= PREF_HELP >> 16; go_title("LFO"); break;
    case S_HELP_GLO: ui_prefs2 |= PREF_HELP >> 16; go_home(); ui.help_t = 0; ui.layer = LAYER_GLO; break;
    case S_HELP_SEQ: ui_prefs2 |= PREF_HELP >> 16; go_page(GR_ROLL); ui.help_t = 0; ui.layer = LAYER_SEQ; break;
    case S_MENU_HELP: ui_prefs2 |= PREF_HELP >> 16; ui.menu = 1; ui.menu_sel = MI_HELP; break;
    case S_HOME_BIG_USER:
        song.playing = 0; up_store(6, "A VERY LONG SOUND NAME"); up_load(6); TSEL->p[P_LEVEL] = 0; go_home(); break;
    case S_HOME_BIG_EXT: song.g[G_CLOCK] = 1; song.g[G_BPM] = 174; go_home(); break;
    /* 1.5: the INSERT pages (fx.c track_insert): CRUSH (#177: BITS RATE LPF), FOLD at MIX 60 %, PHASR, OFF; INSERT 2 */
    case S_INSERT_CRUSH: set_ins_ui(IT_CRUSH, 64, 96, 80, 127); go_title("INSERT"); break;
    case S_INSERT_FOLD: set_ins_ui(IT_FOLD, 100, 96, 100, 76); go_title("INSERT"); break;
    case S_INSERT_PHASER: set_ins_ui(IT_PHASER, 70, 110, 90, 127); go_title("INSERT"); break;
    case S_INSERT_OFF: go_title("INSERT"); break;
    case S_INSERT_MIX: set_ins_ui(IT_FLANGER, 40, 127, 110, 90); go_title("INSERT 2"); break;
    /* 1.5 MIDI LEARN (midi_learn.c): on, nothing picked (MIXER); T1 CUT picked, no CC yet (EDIT 2); T1 CUT on CC74 (its
     * message in the header); the widest line, a 5-letter label waiting; GLO's map with LEARN on (D4) */
    case S_LEARN_WAIT: go_page(GR_TRK); ml_toggle(); ui.msg_t = 0; break;
    case S_LEARN_PICK: go_title("MACHINE 2"); ml_toggle(); ml_knob(TSEL, P_E4); ui.msg_t = 0; ui.hot_col = 0; ui.hot_t = 30; break;
    case S_LEARN_SET: go_title("MACHINE 2"); ml_toggle(); ml_learn(74, 0, P_E4); ml_knob(TSEL, P_E4); break;
    case S_LEARN_LONG: {                             /* "T4 <label>: SEND A CC": the widest EDIT label of any engine */
        uint32_t e, k, be = 0, bk = 0, bw = 0;
        for (e = 0; e < NENGINES; e++)
            for (k = 0; k < 8u; k++) {
                const char *l = ENGINES[e]->edit[k].label;
                uint32_t w = l ? str_len(l) : 0u;
                if (w > bw) bw = w, be = e, bk = k;
            }
        song.sel = 3; eng(be); go_title("MACHINE 2"); ml_toggle();
        ml_knob(TSEL, P_E0 + bk); ui.msg_t = 0;
        break;
    }
    case S_GLO_LEARN: go_title("MACHINE 2"); ml_toggle(); ui.layer = LAYER_GLO; ui.msg_t = 0; break;
    /* MENU > MIDI with LEARN CLEAR picked: 16 CCs learned (the widest count), and none */
    case S_MENU_LCLEAR: {
        uint32_t k;
        for (k = 0; k < ML_N; k++) ml_learn(40u + k, k & 3u, P_ATK + k);
        ui.menu = 1; ui.menu_sel = MI_LCLEAR;
        break;
    }
    case S_MENU_LCLEAR_NONE: ui.menu = 1; ui.menu_sel = MI_LCLEAR; break;
    /* 1.5.5 (#202): MENU > MIDI with MIDI IN TRACK: T1 on 10, T2 on 2, T3 OMNI, T4 OFF, T1 CHANNEL selected; the same
     * channels with MIDI IN CH1-4 (the rows' values dim: unused), T2 CHANNEL selected */
    case S_MENU_MIDI_TRACK: case S_MENU_MIDI_TCH_UNUSED:
        ui.menu = 1;
        ui.menu_sel = s == S_MENU_MIDI_TRACK ? MI_TCH1 : MI_TCH2;
        song.g[G_ROUTE] = s == S_MENU_MIDI_TRACK ? ROUTE_TRACK : 0;
        mch_set(0, 9); mch_set(2, MCH_OMNI); mch_set(3, MCH_OFF);
        break;
    /* 1.5.5 (#208): MENU > MIDI > AUTO SELECT ON (selected); MIDI IN notes into T2 (selected) and T4 just now: their
     * cushions lit on HOME TRACKS (and the header's), on the MIXER T1 (selected) and T3 */
    case S_MENU_MIDI_AUTO: ui.menu = 1; ui.menu_sel = MI_MFOLLOW; ui_rec_prefs |= PREF_MIDI_FOLLOW >> 8; break;
    case S_HOME_BIG_STOPPED: tracks_scene(0); break;
    case S_MIXER_MIDI:
        go_page(GR_TRK);
        trk[2].p[P_MUTE] = 1;
        midi_lit_ms[0] = midi_lit_ms[2] = fm1_ms | 1u;
        break;
    /* 1.5.1: the DLY page with TIME 1/8D (KNOB 1 turned: its gauge) and 1/16D (the widest name); the FX layer's map
     * with REV THROW (D5, VERB) and ECHO THROW (E5, ECHO) held */
    case S_DLY_DOT: song.g[G_DTIME] = 10; go_title("DLY"); ui.hot_col = 0; ui.hot_t = 30; break;
    case S_DLY_DOT16: song.g[G_DTIME] = 11; go_title("DLY"); break;
    case S_FX_THROW:
        song.playing = 1; ui.layer = LAYER_FX;
        perf_held = perf_act = PF_BIT(PF_RTHR) | PF_BIT(PF_ETHR);
        break;
    case S_MENU_INFO:
        ui.menu = 3; ui.menu_sel = MI_INFO; song.cpu_q8 = 87u;   /* (34 %; USB: the host has none, OFF) */
        info.t = fm1_ms - INFO_CPU_MS;
        break;
    /* VOID FOUR (ui_big.c): EDIT, the four big cards of each machine: OB-Z ROYAL HORNS, K1 just turned (hot); WAVE
     * with ENV negative, playing; ACID (its pattern) at the ends (CUT top, ENV +100: the widest value, RES 100 %);
     * DRIFT's DRIVE at 0, RES 0, a track armed */
    case S_EDIT_BIG_OBX:
        song.sel = 0; eng(ENGI_OBX); apply_preset_to(TSEL, 9); go_title("EDIT"); ui.hot_col = 0; ui.hot_t = 30; break;
    case S_EDIT_BIG_WAVE:
        song.sel = 1; eng(ENGI_WAVE); TSEL->p[P_ED_FLT] = -40; TSEL->p[P_E1] = 88; go_title("EDIT");
        song.playing = 1; TSEL->seq_idx = 5; break;
    case S_EDIT_BIG_ACID:
        song.sel = 2; eng(ENGI_ACID); TSEL->p[P_E4] = 127; TSEL->p[P_E5] = 127; TSEL->p[P_ED_FLT] = 63; go_title("EDIT");
        break;
    case S_EDIT_BIG_DRIFT:
        song.sel = 3; eng(ENGI_DRIFT); TSEL->p[P_E6] = 0; TSEL->p[P_E5] = 0; TSEL->p[P_ED_FLT] = -64; song.rec = 8u;
        go_title("EDIT"); break;
    case S_EDIT_ANALOG: go_title("MACHINE 1"); break;
    case S_EDIT_WAVE: eng(ENGI_WAVE); go_title("MACHINE 1"); break;
    case S_CONFIRM_SEQ: ui.confirm = CF_CLEAR_SEQ; ui.confirm_trk = 2; break;
    case S_CONFIRM_PROJ: ui.confirm = CF_OVR_PROJ; ui.confirm_trk = 0; break;
    case S_CONFIRM_USER: song.playing = 0; up_store(6, "A VERY LONG SOUND NAME"); ui.confirm = CF_OVR_USER; ui.confirm_trk = 6; break;
    case S_CONFIRM_PAT: ui.confirm = CF_LOAD_PAT; ui.confirm_trk = 0; ui.ppick = 4; break;
    case S_CONFIRM_MOTION: ui.confirm = CF_CLEAR_MOTION; ui.confirm_trk = 3; break;
    case S_CONFIRM_ERASE: song.playing = 0; up_store(6, "A VERY LONG SOUND NAME"); ui.confirm = CF_ERASE_USER; ui.confirm_trk = 6; break;
    case S_MENU: ui.menu = 1; ui.menu_sel = 0; song.rec = 1; break;
    case S_MENU_SPEAKER: ui.menu = 1; ui.menu_sel = MI_LOWCUT; settings.lowcut = 2; break;
    case S_ABOUT: ui.menu = 2; ui.menu_scroll = 0; break;
    case S_ABOUT_REC: ui.menu = 2; ui.menu_scroll = 0; song.rec = 1; break;          /* the REC mark beside OCT- BACK */
    case S_ABOUT_CREDITS: ui.menu = 2; ui.menu_scroll = 360; break;
    case S_ABOUT_END: ui.menu = 2; ui.menu_scroll = (uint16_t)menu_scroll_max(); break;
    case S_UBOOT: ui.uboot = 3; break;
    /* the header's battery at each stock level (raw ADC: under 531, 531.., 561.., 591..) and on USB power */
    case S_BATT0: usb.config = 0; song.batt_raw = 500; go_home(); break;
    case S_BATT1: usb.config = 0; song.batt_raw = 540; go_home(); break;
    case S_BATT2: usb.config = 0; song.batt_raw = 570; go_home(); break;
    case S_BATT3: usb.config = 0; song.batt_raw = 600; go_home(); break;
    case S_BATT_USB: usb.config = 1; usb.suspended = 0; song.batt_raw = 500; go_home(); break;
    case S_MOTION_REC: song.rec = 1u << song.sel; go_auto_top(); break;          /* recording into the motion */
    case S_MOTION_OFF:                                                            /* stopped, PLAY OFF (OCT+ CLEAR) */
        song.playing = 0; motion_set_event(TSEL, 1, P_REV, 40); motion_set_enabled(TSEL, 0); go_auto_top(); break;
    case S_MOTION_CARD: {                                          /* #63: HOME, MOTION drives VOLUME (its label THEME),
                                                                    * PRESET just turned; a sound control too (no card) */
        const engine_t *en = ENGINES[TSEL->eng_req];
        motion_set_event(TSEL, 2, P_LEVEL, TSEL->p[P_LEVEL]);
        motion_set_event(TSEL, 6, en->knob[2], TSEL->p[en->knob[2]]);
        go_home(); ui.hot_col = 1; ui.hot_t = 30;
        break;
    }
    case S_SONG_HOME:                            /* the song playing: the disc and its row in the header */
        song.playing = 0; project_save(0); project_save(1);
        chain_config.count = 2;
        chain_config.row[0] = chain_row_of(0, 2); chain_config.row[1] = chain_row_of(1, 4);
        go_page(GR_SONG); chain_prepare(); events_block(32); go_home(); ui.msg_t = 0;
        break;
    /* the FX layer's map: held alone; REPEAT 1/16 + LPF + a mute playing with the macros turned;
     * at 72 BPM: a REPEAT 1/16 waiting for its 1/16 (shown THEME) beside TAPE STOP playing, and REPEAT 1/8
     * held, too long at that tempo (it and REVERSE dimmed) */
    case S_FX_PEEK: go_title("ENV"); ui.layer = LAYER_FX; break;
    case S_FX_HELD:
        go_home(); ui.layer = LAYER_FX;
        perf_held = perf_act = PF_BIT(PF_R16) | PF_BIT(PF_LPF) | PF_BIT(PF_M1 + 1);
        perf_k[0] = -40; perf_k[1] = 25; perf_k[3] = 30; ui.hot_col = 0; ui.hot_t = 30;
        break;
    case S_FX_WAIT:
        song.playing = 1; song.g[G_BPM] = 72; ui.layer = LAYER_FX;
        perf_held = PF_BIT(PF_R16) | PF_BIT(PF_R8) | PF_BIT(PF_TAPE); perf_act = PF_BIT(PF_TAPE);
        perf_k[2] = 100;
        break;
    case S_FX_HARM:                                 /* OCT UP playing, KNOB 4 its shimmer; OCT DN held under it */
        song.playing = 1; ui.layer = LAYER_FX;
        perf_ord[PF_ODN] = ++perf_seq; perf_ord[PF_OUP] = ++perf_seq;
        perf_held = perf_act = PF_BIT(PF_OUP) | PF_BIT(PF_ODN);
        perf_k[3] = 60; ui.hot_col = 3; ui.hot_t = 30;
        break;
    /* 1.2: FLANGER (B4) and PHASER (C5) held, KNOB 4 DEPTH 75 %; assigning: G3 remapped to PHASER and held (PRESETS
     * turned: the message, the footer PRESETS ASSIGN / EDIT DEFAULT), D5 given FLANGER, C5 NONE */
    case S_FX_MOD:
        song.playing = 1; ui.layer = LAYER_FX;
        perf_held = perf_act = PF_BIT(PF_FLG) | PF_BIT(PF_PHS);
        perf_k[3] = 25; ui.hot_col = 3; ui.hot_t = 30;
        break;
    case S_FX_ASSIGN:
        go_title("ENV"); ui.layer = ui.ly = ui.lock = LAYER_FX;
        perf_map_put(fx_keys, 1, PF_PHS); perf_map_put(fx_keys, 12, PF_FLG); perf_map_put(fx_keys, 11, PF_N);
        fx_map_sync(); perf_remap = 0;
        kb_layer = 1u << key_at(0, 1); lys.fxk = (uint8_t)(key_at(0, 1) + 1u);
        perf_held = perf_act = PF_BIT(PF_PHS);
        fx_say(key_at(0, 1), PF_PHS, "");
        break;
    case S_REVERB: go_title("REVERB"); song.g[G_RTYPE] = 1; ui.hot_col = 0; ui.hot_t = 30; break;   /* TYPE: SPRING */
    case S_MENU_HOLD: ui.menu = 1; ui.menu_sel = MI_HOLD; settings_hold = 2; break;
    case S_MENU_LEDS: ui.menu = 1; ui.menu_sel = MI_LEDS; settings_leds = LEDS_INV; break;
    case S_MENU_ANIM_IDLE: ui.menu = 1; ui.menu_sel = MI_ANIM; menu_put(MI_ANIM, 2); break;   /* (1.2: ANIM IDLE, #135) */
    /* 1.2 (#72 / #73): MOMENTARY, LFO held and HOME's KNOB 3 (EDIT's ENV) turned (the header's hint, the hot card); on EDIT 1 */
    case S_MOMENTARY:
    case S_MOMENTARY_EDIT: {
        int16_t *vp;
        song.playing = 0;
        if (s == S_MOMENTARY_EDIT)
            open_family(FAM_EDIT);
        if (s == S_MOMENTARY) home_param(2u, &vp);
        else page_desc(cur_page(), 0, &vp);
        mom.n = 1; mom.vp[0] = vp; mom.v[0] = *vp;
        if (s == S_MOMENTARY)
            *vp = (int16_t)(*vp > 0 ? *vp - 30 : *vp + 30);   /* (ENV: -64..63) */
        else
            *vp = (int16_t)(*vp > 64 ? *vp - 40 : *vp + 40);
        ui.hot_col = s == S_MOMENTARY ? 2u : 0; ui.hot_t = 40;
        ui_message("MOMENTARY  [OCT+] KEEP");
        break;
    }
    case S_MENU_END: ui.menu = 1; ui.menu_sel = MI_COUNT - 1u; ui_prefs = 0xFF; ui_rec_prefs = 0x2A; break;   /* the last row,
                                                     * every flag set (CLICK ON, LEVEL HIGH, COUNT-IN 2 BARS) */
    /* the MENU's tabs (1.0.5): menu DISPLAY (COLOR), menu_hold CONTROL, menu_speaker AUDIO, menu_system SYSTEM (USB
     * SERIAL); menu_slide: from DISPLAY to CONTROL with ANIM ON, the frame half way (draw) */
    case S_MENU_SYSTEM: ui.menu = 1; ui.menu_sel = MI_SERIAL; break;
    case S_MENU_SLIDE: ui.menu = 1; ui.menu_sel = MI_ACCEL; ui_prefs |= PREF_LATCH; break;
    /* the GLO SCL EDIT layers (ui_layer.c): just opened (a peek), and in use: GLO with T2 muted, T3 soloed (its key
     * held) and KNOB 1 turned; with CLK EXT (TAP dimmed); SCL at D# minor, KNOB 2 turned; EDIT on WAVE preset 3,
     * a favourite; a user preset; the hint after a tap */
    case S_GLO_PEEK: go_title("ENV"); ui.layer = LAYER_GLO; break;
    case S_GLO_ACTIVE:
        go_home(); ui.layer = LAYER_GLO; trk[1].p[P_MUTE] = 1; perf_solo = 4; trk[0].p[P_LEVEL] = 90;
        ui.hot_col = 0; ui.hot_t = 30;
        break;
    case S_GLO_EXT: go_home(); ui.layer = LAYER_GLO; song.g[G_CLOCK] = 1; trk[0].p[P_MUTE] = trk[3].p[P_MUTE] = 1; break;
    case S_SCL_PEEK: go_title("ENV"); ui.layer = LAYER_SCL; break;
    case S_SCL_ACTIVE: go_home(); ui.layer = LAYER_SCL; TSEL->p[P_ROOT] = 3; TSEL->p[P_SCALE] = 2; ui.hot_col = 1; ui.hot_t = 30; break;
    case S_EDIT_PEEK: go_title("MACHINE 1"); ui.layer = LAYER_EDIT; break;
    case S_EDIT_ACTIVE: eng(ENGI_WAVE); apply_preset_to(TSEL, 2); favorite_set(ENGI_WAVE, 2, 1); go_home(); ui.layer = LAYER_EDIT;
        ui.hot_col = 0; ui.hot_t = 30; break;
    case S_EDIT_USER: song.playing = 0; eng(ENGI_WAVE); up_store(6, "MY LONG WAVE NAME"); up_load(6); favorite_set(NENGINES, 6, 1);
        go_home(); ui.layer = LAYER_EDIT; break;
    case S_LAYER_HINT: go_page(GR_TRK); ui.msg_t = 0; layer_tap(LAYER_GLO); break;
    /* #83: locked open by a double tap (the lock after the header's name): EDIT and FX */
    case S_LAYER_LOCK: go_title("ENV"); ui.layer = ui.lock = LAYER_EDIT; break;
    case S_LAYER_LOCK_FX: go_title("ENV"); ui.layer = ui.lock = LAYER_FX; break;
    /* 1.2 SEQ TOOLS (SEQ held on STEP): a melodic track (the sequence's tools), a song playing on T4 (every tool dimmed:
     * STOP TO EDIT) */
    case S_SEQ_TOOLS: go_page(GR_ROLL); ui.layer = LAYER_SEQ; break;
    case S_SEQ_TOOLS_SONG: song.sel = 3; go_page(GR_ROLL); chain.running = 1; ui.layer = LAYER_SEQ; break;
    /* NAME (ui_name.c): USER SAVE prefilled; a letter cycling (RS: S, R next); 123 on a project; an empty project name
     * (the placeholder); 12 of the widest letters, the cursor past them; playing (OCT+ dim) */
    case S_NAME_USER: song.playing = 0; go_page(GR_USER); ui.uslot = 6; name_open(NK_USER_SAVE, 6); break;
    case S_NAME_TYPING:
        song.playing = 0; go_page(GR_USER); name_open(NK_USER_SAVE, 6);
        str_cpy(nm.s, "SUB BAS", sizeof nm.s); nm.len = 7; nm.cur = 6; nm.key = 9; nm.tap = 1; nm.t = fm1_ms;
        break;
    case S_NAME_123:
        song.playing = 0; go_page(GR_SLOTS); name_open(NK_PROJ_SAVE, 1);
        str_cpy(nm.s, "LIVE 2026", sizeof nm.s); nm.len = 9; nm.cur = 5; nm.num = 1;
        break;
    case S_NAME_EMPTY: song.playing = 0; go_page(GR_SLOTS); proj_name[0] = 0; name_open(NK_PROJ_SAVE, 2); break;
    case S_NAME_FULL:
        song.playing = 0; up_store(4, "PAD"); go_page(GR_USER); name_open(NK_USER_RENAME, 4);
        str_cpy(nm.s, "MWMWMWMWMWMW", sizeof nm.s); nm.len = nm.cur = 12;
        break;
    case S_NAME_PLAYING: go_page(GR_USER); name_open(NK_USER_SAVE, 6); ui_message("STOP TO SAVE"); break;
    case S_PROJECT_NAMED:
        song.playing = 0; project_save_as(0, "LOFI JAM"); project_save_as(1, "MWMWMWMWMWMW"); project_save_as(2, "");
        song.g[G_SLOT] = 2; go_page(GR_SLOTS); ui.act = 4; ui.msg_t = 0;
        break;
    /* 1.2 (Discussion #90): PRESETS > LIST BASS (factory basses and a user one); STRINGS (the widest name); NAME's category
     * (the widest, STRINGS) on a rename of a long name; PROJECT's random name */
    case S_PRESETS_CAT:
        song.playing = 0; up_store_cat(3, "MY DEEP BASS", CAT_BASS);
        eng(0); apply_preset(2); list_set(1u + CAT_BASS); go_page(GR_BROWSE);
        break;
    case S_PRESETS_CAT_WIDE: list_set(1u + CAT_STRINGS); go_page(GR_BROWSE); break;
    case S_NAME_CAT:
        song.playing = 0; up_store_cat(4, "MWMWMWMWMWMW", CAT_STRINGS); go_page(GR_USER); name_open(NK_USER_RENAME, 4);
        break;
    case S_NAME_RANDOM: song.playing = 0; go_page(GR_SLOTS); name_open(NK_PROJ_SAVE, 1); nm.rnd = 12345u; nm_random(); break;
    case S_SONG_NAMED:
        song.playing = 0; project_save_as(0, "LOFI JAM"); project_save_as(1, "MWMWMWMWMWMW");
        chain_config.count = 3;
        chain_config.row[0] = chain_row_of(0, 2); chain_config.row[1] = chain_row_of(1, 4); chain_config.row[2] = chain_row_of(2, 1);
        ui.song_row = 0; go_page(GR_SONG); sg_fresh(); ui.msg_t = 0;
        break;
    /* SONG's timeline (ui_graph.c graph_song): a long song scrolled to the cursor (row 10; D not saved: one bar,
     * outlined); a short one playing (row 2, 2 of 4 repeats left, 3 steps into the third); the long one playing far
     * from the cursor (the view on the playing row); the cursor on a slot not saved; on + ADD */
    case S_SONG_LONG: case S_SONG_LONG_PLAYING: {
        static const uint8_t R[15][2] = {{0, 4}, {1, 2}, {0, 4}, {1, 4}, {2, 1}, {0, 8}, {1, 2}, {3, 1}, {0, 4}, {1, 4},
                                         {2, 2}, {0, 4}, {1, 8}, {0, 2}, {1, 1}};
        uint32_t k;
        song.playing = 0;
        song_src(0, 16, 2, "VERSE"); song_src(1, 32, 2, "CHORUS"); song_src(2, 16, 6, "BRIDGE");   /* 1, 2, 8 bars */
        if (s == S_SONG_LONG_PLAYING) song_src(3, 8, 2, "FILL");
        chain_config.count = 15;
        for (k = 0; k < 15u; k++) chain_config.row[k] = chain_row_of(R[k][0], R[k][1]);
        ui.song_row = s == S_SONG_LONG ? 9 : 0; go_page(GR_SONG); ui.msg_t = 0; sg_fresh();
        if (s == S_SONG_LONG_PLAYING) {
            chain_prepare(); events_block(32);
            for (k = 0; k < 4000u && chain.running && chain.row < 11u; k++) events_block(div_samples(2));
            for (k = 0; k < 20u; k++) events_block(div_samples(2));
        }
        break;
    }
    case S_SONG_PLAYING: {
        uint32_t k;
        song.playing = 0;
        song_src(0, 16, 2, "VERSE"); song_src(1, 8, 2, "FILL"); song_src(2, 32, 2, "CHORUS");
        chain_config.count = 4;
        chain_config.row[0] = chain_row_of(0, 2); chain_config.row[1] = chain_row_of(1, 4);
        chain_config.row[2] = chain_row_of(2, 2); chain_config.row[3] = chain_row_of(0, 1);
        ui.song_row = 2; go_page(GR_SONG); ui.msg_t = 0; sg_fresh();
        chain_prepare(); events_block(32);
        for (k = 0; k < 600u && chain.running && !(chain.row == 1u && chain.remaining == 2u); k++) events_block(div_samples(2));
        for (k = 0; k < 3u; k++) events_block(div_samples(2));
        break;
    }
    case S_SONG_UNSAVED: case S_SONG_ADD:
        song.playing = 0; project_save_as(0, "LOFI JAM"); project_save(1);
        chain_config.count = 3;
        chain_config.row[0] = chain_row_of(0, 2); chain_config.row[1] = chain_row_of(1, 4); chain_config.row[2] = chain_row_of(2, 1);
        ui.song_row = s == S_SONG_UNSAVED ? 2 : 3; go_page(GR_SONG); ui.msg_t = 0; sg_fresh();
        break;
    case S_ROLL_EMPTY: case S_ROLL_ACID: case S_ROLL_CHORDS: case S_ROLL_TIES: case S_ROLL_LEN32: case S_ROLL_HIGH:
    case S_ROLL_LOW: case S_ROLL_WIDE: case S_ROLL_PLAYING: case S_ROLL_LEN: roll_scene(s); break;
    /* 1.1 parameter locks (ui_input.c lock_*): the roll's steps with a lock marked under them; a step held (the note
     * key of its entry) shows what KNOB 1..4 lock (HOME's: OB-Z E5 E6 ATK REL), the locked ones bright; a step key held after FX (DIST CHO DLY REV, REV and DLY locked); AUTOMATION's
     * LOCK count */
    case S_ROLL_LOCKS: case S_ROLL_LOCK_HELD:
        roll_scene(S_ROLL_ACID);
        motion_set_lock(TSEL, 2, P_REV, 100); motion_set_lock(TSEL, 6, P_E4, 20); motion_set_lock(TSEL, 6, P_ATK, 90);
        motion_set_lock(TSEL, 9, P_E5, 110); motion_set_lock(TSEL, 13, P_REV, 80);
        if (s == S_ROLL_LOCK_HELD) {
            ui.plk_src = 0;
            ui.entry_open = 1;
            fm1_in.notes = 1u << 10;
            ui.hot_col = 0; ui.hot_t = 30;
        }
        break;
    /* 1.5.1 SEQ > DETAIL (#199): its lanes on ACID (the cursor on step 7, KNOB 2 RATCH just turned), playing (step 10),
     * the cursor on a REST (the cards DIM); STEP's roll with the steps marked (a lock too);
     * HELP's hint on DETAIL */
    case S_DETAIL: case S_DETAIL_PLAYING: case S_DETAIL_REST: case S_ROLL_MARKS:
        roll_scene(s == S_DETAIL_PLAYING ? S_ROLL_PLAYING : S_ROLL_ACID);
        detail_values(TSEL, 0);
        if (s == S_ROLL_MARKS) {
            motion_set_lock(TSEL, 9, P_E5, 110);
            break;
        }
        go_title_page("DETAIL");
        ui.cursor = s == S_DETAIL_REST ? 5 : 6;
        if (s == S_DETAIL) { ui.hot_col = 1; ui.hot_t = 30; }
        break;
    case S_HELP_DETAIL:
        roll_scene(S_ROLL_ACID); ui_prefs2 |= PREF_HELP >> 16; go_title_page("DETAIL");
        break;
    /* 1.5.5 (#209): conditions on DETAIL's CHANCE lane (1:2 2:2 1:4 4:4 on ACID, the cursor on 4:4, KNOB 1 just turned;
     * FILL / !FILL on ACID, the cursor on !FILL), AUTOMATION's COND rows, GLO with FILL held */
    case S_DETAIL_COND: case S_AUTO_COND:
        roll_scene(S_ROLL_ACID);
        detail_values(TSEL, 0);
        step_set_chance_v(&TSEL->step[0], 101); step_set_chance_v(&TSEL->step[2], 102);
        step_set_chance_v(&TSEL->step[6], 106); step_set_chance_v(&TSEL->step[12], 109);
        if (s == S_AUTO_COND) {
            go_page(GR_EVENTS); (void)auto_pick(EVC(EVK_CHANCE, 12)); ui.hot_col = 3; ui.hot_t = 30;
            break;
        }
        go_title_page("DETAIL"); ui.cursor = 12; ui.hot_col = 0; ui.hot_t = 30;
        break;
    case S_DETAIL_FILL:
        roll_scene(S_ROLL_ACID);
        detail_values(TSEL, 0);
        step_set_chance_v(&TSEL->step[4], 110); step_set_chance_v(&TSEL->step[12], 111);
        go_title_page("DETAIL"); ui.cursor = 12; ui.hot_col = 0; ui.hot_t = 30;
        break;
    case S_GLO_FILL: go_home(); ui.layer = LAYER_GLO; seq_fill = 1; break;
    /* 1.5.5 (#186): SEQ TOOLS' EUCLID open: 5 / 16 turned by 2 on ACID (HELP ON); 7 / 64 over 64 steps (four rows) */
    case S_SEQ_EUCLID: case S_SEQ_EUCLID_64:
        roll_scene(S_ROLL_ACID);
        if (s == S_SEQ_EUCLID) ui_prefs2 |= PREF_HELP >> 16;
        if (s == S_SEQ_EUCLID_64) TSEL->p[P_SLEN] = 64;
        go_page(GR_ROLL); ui.layer = LAYER_SEQ; lys.trk = song.sel;
        eu.steps = 0; eu_open(TSEL);
        eu.hits = s == S_SEQ_EUCLID ? 5 : 7;
        eu.steps = s == S_SEQ_EUCLID ? 16 : 64;
        eu.rot = s == S_SEQ_EUCLID ? 2 : 0;
        eu_render(TSEL); ui.msg_t = 0;
        break;
    /* 1.5.5 (#188): MENU > CONTROL > SELECT PAGE (the tab's eighth row) */
    case S_MENU_SELECT: ui_prefs2 |= PREF_SEL_PAGE >> 16; ui.menu = 1; ui.menu_sel = MI_SELECT; break;
    /* 1.5.5 (#199): SAVE > PROJECT with KNOB 2 ERASE picked on a used slot; its dialog with the project's name */
    case S_PROJECT_ERASE: song.playing = 0; project_save_as(1, "DUB SKETCH"); song.g[G_SLOT] = 2; go_page(GR_SLOTS); ui.act = 2; break;
    case S_CONFIRM_ERASE_PROJ: song.playing = 0; project_save_as(1, "A VERY LONG NAME"); ui.confirm = CF_ERASE_PROJ; ui.confirm_trk = 1; break;
    /* 1.5.5 (#190): SAVE > TOOLS with KNOB 2 turned on past INIT (CLEAN INIT picked); its dialog */
    case S_TOOLS_CLEAN: go_page(GR_TOOLS); ui.act = 2; ui.tl_clean = 1; break;
    case S_CONFIRM_CLEAN: ui.confirm = CF_CLEAN_SOUND; ui.confirm_trk = 0; break;
    case S_MOTION_LOCKS:
        motion_set_event(TSEL, 1, P_REV, 40); motion_set_lock(TSEL, 2, P_REV, 100); motion_set_lock(TSEL, 6, P_ATK, 90);
        go_auto_top();
        break;
    /* 1.1.5 AUTO LIST, SEQ > AUTOMATION since 1.2 (ui_events.c): locks and automation events of the track, the 4th selected, its
     * VALUE just turned; empty (+ ADD LOCK: step 5, the first knob of HOME) */
    case S_AUTO_LIST:
        motion_set_event(TSEL, 0, P_LRATE, 70); motion_set_lock(TSEL, 2, P_REV, 100); motion_set_lock(TSEL, 2, P_ED_FLT, -30);
        motion_set_lock(TSEL, 6, P_ATK, 90); motion_set_lock(TSEL, 6, P_E4, 20); motion_set_event(TSEL, 8, P_PAN, -40);
        motion_set_lock(TSEL, 11, P_LD_FLT, 50); motion_set_lock(TSEL, 13, P_DLY, 80); motion_set_lock(TSEL, 15, P_GLIDE, 60);
        go_page(GR_EVENTS); ui.ev_row = 5; ui.hot_col = 3; ui.hot_t = 30;   /* (row 0: PLAY, 1: QUANTIZE) */
        break;
    case S_AUTO_LIST_ADD: ui.cursor = 4; go_page(GR_EVENTS); break;
    /* 1.2 (#162): AUTOMATION's NUDGE rows beside CHANCE and RATCH, QUANTIZE OFF, a NUDGE selected (KNOB 4 just
     * turned; step 9's on a REST: dimmed); QUANTIZE ON, its row selected (every NUDGE dimmed); + ADD NUDGE; LFO 2
     * (SYNC 1/8, TRIG FREE, POL UNI) */
    case S_AUTO_NUDGE:
    case S_AUTO_QNT:
        step_set_chance(&TSEL->step[0], 65); step_set_ratchet(&TSEL->step[4], 3); step_set_nudge(&TSEL->step[2], 3);
        step_set_nudge(&TSEL->step[6], -5); step_set_nudge(&TSEL->step[9], 2); TSEL->step[9].time = ST_REST;
        motion_set_lock(TSEL, 6, P_REV, 100);
        TSEL->p[P_SQNT] = s == S_AUTO_QNT;
        go_page(GR_EVENTS);
        if (s == S_AUTO_QNT) {
            ui.ev_row = 1;
        } else {
            (void)auto_pick(EVC(EVK_NUDGE, 6)); ui.hot_col = 3; ui.hot_t = 30;
        }
        break;
    case S_AUTO_ADD_NUDGE: TSEL->p[P_SQNT] = 0; go_auto_add(EV_NUDGE, 3); break;
    /* 1.5 (#173): AUTOMATION's VEL rows shown (+ ADD VEL's SHOW): every note step's velocity, step 5's just turned, the
     * ACC step 9's dimmed (it plays 127), among a CHANCE and a lock; + ADD VEL (SHOW) */
    case S_AUTO_VEL: {
        static const uint8_t VEL[8] = {100, 0, 64, 112, 100, 40, 127, 90};
        uint32_t i;
        track_defaults_steps(TSEL);
        TSEL->p[P_SLEN] = 16;
        for (i = 0; i < 16u; i += 2u) {
            uint8_t n = (uint8_t)(57 + (i * 5u) % 12u);
            step_put(&TSEL->step[i], ST_NOTE, i == 8u ? SF_ACCENT : 0u, 1, &n);
            TSEL->step[i].vel = VEL[i / 2u];
        }
        step_set_chance(&TSEL->step[2], 70); motion_set_lock(TSEL, 4, P_REV, 100);
        go_page(GR_EVENTS); ui.ev_vel = 1; (void)auto_pick(EVC(EVK_VEL, 4)); ui.hot_col = 3; ui.hot_t = 30;
        break;
    }
    case S_AUTO_ADD_VEL: go_auto_add(EV_VEL, 3); break;
    case S_LFO2: TSEL->p[P_LSYNC] = 6; TSEL->p[P_LTRIG] = 1; TSEL->p[P_LPOL] = 1; go_title("LFO 2"); break;
    case S_USER_FOOT: song.playing = 0; up_store(3, "MY BASS"); ui.uslot = 3; go_page(GR_USER); break;   /* EDIT NAME lit */
    default: break;
    }
}
static void draw(int s)
{
    nscr = npend = nrules = 0;
    ntight = 0;
    tight[0] = 0;
    memset(host_screen, 0, sizeof host_screen);
    if (s == S_CALIBRATION) {                     /* the blocking setup screen: its two drawing steps */
        setup_title();
        setup_show("TURN RIGHT", "ALGORITHM");
        return;
    }
    if (s == S_SPLASH) {                          /* the power-on splash (main.c, before the UI): its lines, the version
                                                   * shown FELUCCA_VERSION (run_tests.sh: src/felucca.c's) */
        static const char WANT[] = FELUCCA_VERSION "|Void Audio|on Felucca by H\xFCgelton";
        splash_seen[0] = 0;
        in_splash = 1;
        draw_splash();
        in_splash = 0;
        {   /* Switch Blade's logo (ui_logo.h): the generated header's limits, then the screen holding it exactly: every
             * pixel the palette's colour of its index, centred in x, SPL_LOGO_Y down; black (0) above, beside and
             * between it and the text, and the text below it (some ink in each of the three lines' bands) */
            uint32_t x, y, bad = 0, i, x0 = (240u - UI_LOGO_W) / 2u, ink[3] = {0, 0, 0};
            static const uint32_t band[3][2] = {{SPL_VER_Y, SPL_VER_Y + 22u}, {SPL_CRED_Y, SPL_CRED_Y + 17u},
                                                {SPL_CRED_Y + SPL_CRED_PITCH, SPL_CRED_Y + SPL_CRED_PITCH + 17u}};
            bad |= UI_LOGO_NCOL > 128 || UI_LOGO_W != 236 || UI_LOGO_H != 135 || UI_LOGO_PAL[0] != 0;
            for (i = 0; i < UI_LOGO_W * UI_LOGO_H; i++)
                bad |= UI_LOGO_PX[i] >= UI_LOGO_NCOL;
            for (y = 0; y < 240u; y++)
                for (x = 0; x < 240u; x++) {
                    uint16_t px = host_screen[y * 240u + x];
                    if (y >= SPL_LOGO_Y && y < SPL_LOGO_Y + UI_LOGO_H && x >= x0 && x < x0 + UI_LOGO_W)
                        bad |= px != swap16(UI_LOGO_PAL[UI_LOGO_PX[(y - SPL_LOGO_Y) * UI_LOGO_W + x - x0]]);
                    else if (y >= SPL_VER_Y)
                        for (i = 0; i < 3u; i++) {
                            if (y >= band[i][0] && y < band[i][1]) ink[i] += px != 0;
                        }
                    else
                        bad |= px != 0;
                }
            if (bad || !ink[0] || !ink[1] || !ink[2] || SPL_CRED_Y + SPL_CRED_PITCH + 17u > 240u) {
                if (rep) fprintf(rep, "FIND %-26s the splash is not the logo whole and centred on black with the three text lines under it (%s, ink %u %u %u)\n",
                                 cur_name, bad ? "logo/black off" : "ok", ink[0], ink[1], ink[2]);
                nfind++;
            }
        }
        if (strcmp(splash_seen, WANT)) {
            if (rep) fprintf(rep, "FIND %-26s the splash shows '%s', not '%s'\n", cur_name, splash_seen, WANT);
            nfind++;
        } else {
            static uint8_t said;
            if (!said++) printf("ui_render: the splash shows the version " FELUCCA_VERSION " (FELUCCA_VERSION) and its lines\n");
        }
        return;
    }
    ui.force = 1;
    mt.frame = ui.frame + 7u;                         /* (the MENU's tabs settled, as when it opens) */
    if (s == S_MENU_SLIDE) mt.pos = 0, mt.frame = ui.frame;   /* (one step of the slide from DISPLAY) */
    ui_draw();
}

/* every value of every column on every page of every engine: labels and values fit their column */
static uint32_t nsweep, nmotion;
/* #63: the page drawn again with MOTION driving each of its cards' track parameters (the motion icon beside
 * every label, the long ones too): the lint over it; the motion cleared again */
static void sweep_motion(void)
{
    uint32_t c, any = 0;
    for (c = 0; c < 4u; c++) {
        int16_t *vp = 0;
        const param_desc_t *d = ui.home ? home_param(c, &vp) : page_desc(cur_page(), c, &vp);
        if (ui.home && vp && (vp < TSEL->p || vp >= TSEL->p + P_COUNT))
            continue;                                   /* (HOME's TEMPO: a global, no motion) */
        uintptr_t id = vp ? ((uintptr_t)vp - (uintptr_t)TSEL->p) / sizeof *vp : P_COUNT;
        if (d && d->label && d->label[0] != '-' && id < P_COUNT && !motion_set_event(TSEL, c, (uint32_t)id, *vp))
            any = 1;
    }
    if (!any) return;
    draw(-1);
    for (c = 0; c < 4u; c++) nmotion += (card_mot >> c) & 1u;
    lint();
    memset(&motion, 0, sizeof motion);
}
static void sweep_columns(void)
{
    uint32_t e, i, c, esync_pass = 0;
    char name[64];
    for (e = 0; e < NENGINES; e++) {
        for (i = 0; i < NPAGES; i++) {
            state();
            eng(e);
            pal_sweep();
            TSEL->p[P_ESYNC] = (int16_t)esync_pass;
            ui.home = 0; ui.page = (uint8_t)i; page_entered();
            if (!page_visible(i)) continue;
            snprintf(name, sizeof name, "%s%s/%s%s", large_on ? "LARGE " : "", ENGINES[e]->name, PAGES[i].title,
                     esync_pass ? " ESYNC" : "");
            cur_name = name;
            for (c = 0; c < 4u; c++) {
                int16_t *vp;
                const param_desc_t *d = PAGES[i].scope == SC_GLOBAL || PAGES[i].scope == SC_TRACK || PAGES[i].scope == SC_ENGINE
                                        ? page_desc(cur_page(), c, &vp) : 0;
                int32_t v, v0;
                if (!d || !d->label || d->label[0] == '-' || PAGES[i].graph == GR_MOD) continue;
                v0 = *vp;
                for (v = d->min; v <= d->max; v++) {
                    *vp = (int16_t)v;
                    nscr = npend = 0;
                    ui.force = 1;
                    draw_columns();
                    lint();
                    nsweep++;
                    if (v - d->min > 300) v = d->max - 1;      /* wide ranges: the ends */
                }
                *vp = (int16_t)v0;
            }
            draw(-1);                                    /* the whole page */
            lint();
            sweep_motion();
            if (str_eq(PAGES[i].title, "ENV") && !esync_pass) {   /* 1.5: again with ENV SYNC's note values */
                esync_pass = 1;
                i--;
            } else {
                esync_pass = 0;
            }
        }
        state(); eng(e); pal_sweep(); go_home();   /* HOME's four knobs of this engine */
        snprintf(name, sizeof name, "%s%s/HOME", large_on ? "LARGE " : "", ENGINES[e]->name);
        cur_name = name;
        draw(-1);
        lint();
        sweep_motion();
        for (i = 0; i < ENGINES[e]->npresets; i++) {   /* VOID FOUR: every sound of this machine on HOME (its name in
                                                         * L or M, its category, its number) */
            apply_preset_to(TSEL, i);
            nscr = npend = 0;
            ui.force = 1;
            draw_columns();
            lint();
            nsweep++;
        }
        for (i = 0; i <= 127u; i++) {                   /* .. VOLUME (LEVEL OFF .. top) and TEMPO 40 .. 240 */
            TSEL->p[P_LEVEL] = (int16_t)i;
            song.g[G_BPM] = (int16_t)(40u + i * 200u / 127u);
            nscr = npend = 0;
            ui.force = 1;
            draw_columns();
            lint();
            nsweep++;
        }
        state(); eng(e); pal_sweep(); go_home();   /* HOME, this machine on every track, every one muted */
        for (i = 0; i < NTRK; i++)
            trk[i].p[P_MUTE] = 1, set_engine_of(&trk[i], e);
        snprintf(name, sizeof name, "%s%s/HOME MUTED", large_on ? "LARGE " : "", ENGINES[e]->name);
        cur_name = name;
        draw(-1);
        lint();
    }
    for (e = 0; e < 4u; e++) {                           /* the MOD page: every source and destination */
        int32_t v;
        state(); pal_sweep(); go_title("MOD");
        cur_name = large_on ? "LARGE MOD sweep" : "MOD sweep";
        for (v = 0; v < MD_N; v++) {
            TSEL->p[P_M1SRC] = (int16_t)(v % MS_N); TSEL->p[P_M1DST] = (int16_t)v; TSEL->p[P_M1AMT] = (int16_t)(e * 40 - 64);
            mod_ui_slot = (uint8_t)(v & 3u);
            draw(-1);
            lint();
        }
    }
}

/* the rolling digits (ui_draw.c roll_*): the old GLOBAL page's four (1.2: a page over HOME > CLOCK, whose KNOB 4 is
 * empty: ui_test.c ROLL_PG), SELECT and KNOB 4 turned together, BPM 129 -> 130 in the header
 * and TUNE 19 -> 20 on a card, then back. Every frame is linted and (GREY, MONO) checked for gray; with a directory,
 * filmstrips of both (each frame side by side, x4: the static frame before, then the roll's frames) as
 * DIR/<PALETTE>_bpm_roll.ppm and DIR/<PALETTE>_card_roll.ppm, the up roll above the down roll. */
#define FS_N (1u + ROLL_FRAMES)                   /* frames per filmstrip row */
#define FS_Z 4u                                   /* zoom */
#define FS_GAP 2u                                 /* px between frames, before the zoom */
static void roll_turns(int32_t s)                 /* one UI frame with KNOB 1 (CLOCK's BPM: SELECT is the LEVEL since
                                                   * 2026-10-20) and KNOB 4 turned by s */
{
    host_enc[panel.enc[EN_K1]] += s * panel.dir[EN_K1];
    host_enc[panel.enc[EN_K4]] += s * panel.dir[EN_K4];
    host_ticks += 16000u; fm1_ms += 16u;
    ui_input();
    ui_draw();
    host_ticks += 200000u;
}
static void roll_film_put(uint8_t *img, uint32_t iw, uint32_t row, uint32_t k, int32_t x0, int32_t y0, uint32_t w, uint32_t h)
{
    uint32_t x, y;
    for (y = 0; y < h * FS_Z; y++)
        for (x = 0; x < w * FS_Z; x++) {
            uint16_t c = swap16(host_screen[(uint32_t)(y0 + (int32_t)(y / FS_Z)) * 240u + (uint32_t)x0 + x / FS_Z]);
            uint8_t *p = img + (((row * (h + FS_GAP) + FS_GAP) * FS_Z + y) * iw + (k * (w + FS_GAP) + FS_GAP) * FS_Z + x) * 3u;
            p[0] = (uint8_t)((c >> 11) * 255u / 31u);
            p[1] = (uint8_t)(((c >> 5) & 63u) * 255u / 63u);
            p[2] = (uint8_t)((c & 31u) * 255u / 31u);
        }
}
static void roll_film_save(const char *dir, const char *pal, const char *name, const uint8_t *img, uint32_t iw, uint32_t ih)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s_%s.ppm", dir, pal, name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n%u %u\n255\n", iw, ih);
    fwrite(img, 1, (size_t)iw * ih * 3u, f);
    fclose(f);
}
static void roll_frames(const char *dir)
{
    enum { HX = 56, HW = 48, HH = H_HEAD };       /* the header crop: the tempo icon and the BPM strip */
    static uint8_t bimg[(FS_N * (HW + FS_GAP) + FS_GAP) * FS_Z * (2u * (HH + FS_GAP) + FS_GAP) * FS_Z * 3u];
    static uint8_t cimg[(FS_N * (COL_W + FS_GAP) + FS_GAP) * FS_Z * (2u * (COL_H + FS_GAP) + FS_GAP) * FS_Z * 3u];
    const uint32_t biw = (FS_N * (HW + FS_GAP) + FS_GAP) * FS_Z, bih = (2u * (HH + FS_GAP) + FS_GAP) * FS_Z;
    const uint32_t ciw = (FS_N * (COL_W + FS_GAP) + FS_GAP) * FS_Z, cih = (2u * (COL_H + FS_GAP) + FS_GAP) * FS_Z;
    uint32_t p, row, k;
    for (p = 0; p < NPALETTES; p++) {
        char name[64];
        memset(bimg, 90, sizeof bimg);
        memset(cimg, 90, sizeof cimg);
        state(); song.playing = 0; pal(p);
        song.g[G_BPM] = 129; song.g[G_TUNE] = 19;
        go_title("CLOCK"); page_over = &ROLL_PG;
        snprintf(name, sizeof name, "%s/roll", UI_MACHINE_PALS[p].name);
        cur_name = name;
        draw(-1);
        for (k = 0; k < 8u; k++) { ui.hot_t = 0; ui.bpm_t = 0; ui_draw(); }   /* settled, nothing hot */
        lint();
        for (row = 0; row < 2u; row++) {
            roll_film_put(bimg, biw, row, 0, HX, 0, HW, HH);
            roll_film_put(cimg, ciw, row, 0, CARD_X(3), Y_LABEL, COL_W, COL_H);
            for (k = 1; k < FS_N; k++) {
                if (k == 1u) roll_turns(row ? -1 : 1);
                else ui_draw();
                lint();
                roll_film_put(bimg, biw, row, k, HX, 0, HW, HH);
                roll_film_put(cimg, ciw, row, k, CARD_X(3), Y_LABEL, COL_W, COL_H);
            }
            if (ui.roll[3].from[0] || ui.roll[ROLL_BPM].from[0] || song.g[G_BPM] != (row ? 129 : 130) ||
                song.g[G_TUNE] != (row ? 19 : 20)) {
                fprintf(rep, "ROLL %s: not over after %u frames (BPM %d TUNE %d)\n", name, ROLL_FRAMES, song.g[G_BPM], song.g[G_TUNE]);
                nfind++;
            }
            for (k = 0; k < 8u; k++) ui_draw();
        }
        if (dir) {
            roll_film_save(dir, UI_MACHINE_PALS[p].name, "bpm_roll", bimg, biw, bih);
            roll_film_save(dir, UI_MACHINE_PALS[p].name, "card_roll", cimg, ciw, cih);
        }
    }
    page_over = 0;
}
/* the host cost of a roll frame: the header and the cards (draw_head, draw_columns) while both strips roll,
 * against the same calls on an idle frame (nothing drawn) and a full redraw of the four cards and the header */
static void roll_cost(void)
{
    enum { N = 400 };
    uint32_t i, k;
    double t_roll = 0, t_full = 0;
    uint64_t px_roll = 0, bl_roll = 0, nroll = 0, px_full = 0, bl_full = 0;
    for (i = 0; i < N; i++) {
        clock_t c0;
        state(); song.playing = 0; pal(1);
        song.g[G_BPM] = 129; song.g[G_TUNE] = 19;
        go_title("CLOCK"); page_over = &ROLL_PG;
        draw(-1);
        for (k = 0; k < 8u; k++) { ui.hot_t = 0; ui.bpm_t = 0; ui_draw(); }
        px_visited = 0; blit_px = 0;
        c0 = clock();
        ui.force = 1; ui.frame++; draw_head(); draw_columns(); ui.force = 0;
        t_full += (double)(clock() - c0); px_full += px_visited; bl_full += blit_px;
        nscr = npend = 0;
        roll_turns(i & 1u ? -1 : 1);                /* the change: the card and the header redrawn whole */
        for (k = 2; k < ROLL_FRAMES; k++) {         /* the strip-only frames */
            px_visited = 0; blit_px = 0;
            c0 = clock();
            ui.frame++; draw_head(); draw_columns();
            t_roll += (double)(clock() - c0); px_roll += px_visited; bl_roll += blit_px; nroll++;
            nscr = npend = 0;
        }
    }
    fprintf(rep, "  roll frame (header BPM 129->130 and card TUNE 19->20, strips only): %.2f us, %llu glyph px, %llu px blitted;"
            " header + 4 cards redrawn whole: %.2f us, %llu glyph px, %llu px blitted\n",
            t_roll / CLOCKS_PER_SEC / (double)nroll * 1e6, (unsigned long long)(px_roll / nroll), (unsigned long long)(bl_roll / nroll),
            t_full / CLOCKS_PER_SEC / N * 1e6, (unsigned long long)(px_full / N), (unsigned long long)(bl_full / N));
    page_over = 0;
}

/* the alignment check over every value its places can show, beyond the screens above (FLAT and LINE, GREY): the
 * track cushions 1..4 at 12 16 24 px; every icon in a card's icon slot; the dialogs of every kind; every menu row with
 * each of its values; NAME's every key, letter and cycle in ABC and 123, the field holding every character; the
 * piano roll's C labels C-1 .. C9; the mixer's LEVEL, PAN and REV over their ranges */
static void align_sweeps(void)
{
    uint32_t st, i, k, nf = nfind;
    FILE *r0 = rep, *nul = fopen("/dev/null", "w");
    int32_t v;
    if (nul) rep = nul;                              /* (not lint scenes: their draws pile up) */
    for (st = ST_FLAT; st <= ST_LINE; st++) {
        state(); pal(0);
        ui_style = (uint8_t)st; style_apply();
        cur_name = st ? "sweep LINE: cushions" : "sweep FLAT: cushions";
        for (k = 0; k < NTRK; k++)
            for (i = 0; i < 3u; i++) {
                cv_begin(40, 40, T_SURF);
                cv_icon_on(4, 4, i == 0u ? 12u : i == 1u ? 16u : 24u, trk_icon(k, 1), T_ACCENT, T_SURF);
            }
        cur_name = st ? "sweep LINE: card icons" : "sweep FLAT: card icons";
        for (i = 0; i < ICON_COUNT; i++) {
            ui.force = 1;
            draw_column(i & 3u, "LVL", "64", "", T_THEME, 500, i);
            ui.col[i & 3u][0] = 0;
        }
        cur_name = st ? "sweep LINE: LARGE card icons" : "sweep FLAT: LARGE card icons";
        ui_prefs = PREF_LARGE; ui.home = 1;             /* (tall: HOME) */
        for (i = 0; i < ICON_COUNT; i++) {
            ui.force = 1; ui.hot_col = (uint8_t)(i & 3u); ui.hot_t = (uint8_t)(i & 4u);
            draw_column(i & 3u, "LVL", i & 8u ? "OFF" : "64", i & 16u ? "%" : "", T_THEME, 500, i);
            ui.col[i & 3u][0] = 0;
        }
        ui.layer = LAYER_FX;                            /* (the labels in M: a layer's cards) */
        for (i = 0; i < ICON_COUNT; i++) {
            ui.force = 1; ui.hot_col = (uint8_t)(i & 3u); ui.hot_t = (uint8_t)(i & 4u);
            draw_column(i & 3u, "LVL", "64", "", T_THEME, 500, i);
            ui.col[i & 3u][0] = 0;
        }
        ui.layer = 0; ui.hot_t = 0; ui_prefs = 0;
        cur_name = st ? "sweep LINE: dialogs" : "sweep FLAT: dialogs";
        for (i = CF_CLEAR_SEQ; i <= CF_ERASE_USER; i++)
            for (k = 0; k < 4u; k++) {
                ui.confirm = (uint8_t)i; ui.confirm_trk = (uint8_t)k;
                draw_confirm();
            }
        ui.confirm = 0;
        cur_name = st ? "sweep LINE: menu rows" : "sweep FLAT: menu rows";
        ui.menu = 1;
        for (i = 0; i < MI_COUNT; i++)
            for (k = 0; k < 4u; k++) {
                ui.menu_sel = (uint8_t)i; ui_prefs = k & 1u ? 0xFFu : 0u; settings_hold = (uint8_t)k; settings.lowcut = (uint8_t)(k % 3u);
                ui_rec_prefs = (uint8_t)((k % 3u) | (k % 3u) << 2 | ((k + 1u) % 3u) << 4);   /* (CLICK, LEVEL, COUNT-IN: each value) */
                settings_leds = (uint8_t)(k % LEDS_COUNT); ui.force = 1;   /* (every LEDS name) */
                draw_menu();
            }
        ui_prefs = 0; ui_rec_prefs = 0;
        cur_name = st ? "sweep LINE: menu TUNE / MIDI IN" : "sweep FLAT: menu TUNE / MIDI IN";   /* (1.2: the project's) */
        for (k = 0; k < 2u * TUNE_N; k++) {             /* every TUNE (the cents, signed), selected or not, S and M */
            ui.menu_sel = (uint8_t)(k & 2u ? MI_TUNE : MI_LOWCUT); ui_prefs = k & 1u ? PREF_LARGE : 0u;
            song.g[G_TUNE] = (int16_t)((int32_t)(k / 2u) + TUNE_MIN); ui.force = 1;
            draw_menu();
        }
        for (k = 0; k < 4u * NELEM(N_ROUTE); k++) {     /* every MIDI IN, selected or not, S and M */
            ui.menu_sel = MI_MIDIIN; ui_prefs = k & 1u ? PREF_LARGE : 0u;
            song.g[G_ROUTE] = (int16_t)(k / 4u); ui.force = 1;
            draw_menu();
        }
        cur_name = st ? "sweep LINE: menu T1..T4 CHANNEL" : "sweep FLAT: menu T1..T4 CHANNEL";   /* (1.5.5) */
        for (k = 0; k < 8u * MCH_N; k++) {              /* every channel, OMNI, OFF, selected or not, TRACK or not, S and M */
            ui.menu_sel = (uint8_t)(k & 2u ? MI_TCH1 + (k / 8u) % 4u : MI_MIDIIN); ui_prefs = k & 1u ? PREF_LARGE : 0u;
            song.g[G_ROUTE] = k & 4u ? ROUTE_TRACK : 0;
            mch_set((k / 8u) % 4u, k / 8u); ui.force = 1;
            draw_menu();
        }
        memset(song.mch, 0, sizeof song.mch);
        song.g[G_TUNE] = 0; song.g[G_ROUTE] = 0;
        cur_name = st ? "sweep LINE: INFO" : "sweep FLAT: INFO";   /* (1.2: every USB state, the CPU 0..99 %, S and M) */
        {
            uint8_t up0 = usb.up, cf0 = usb.config;
            uint32_t su0 = usb.setups, sof0 = usb.sof_seen;
            ui.menu = 3;
            for (k = 0; k < 5u * 2u * 10u; k++) {
                uint32_t u = k % 5u;
                usb.up = (uint8_t)(u != 0u); usb.config = (uint8_t)(u == 4u); usb.setups = u >= 3u; usb.sof_seen = u >= 2u;
                song.cpu_q8 = (k / 10u) * 28u; info.t = fm1_ms - INFO_CPU_MS;
                ui_prefs = k / 5u & 1u ? PREF_LARGE : 0u; ui.force = 1;
                draw_menu();
            }
            usb.up = up0; usb.config = cf0; usb.setups = su0; usb.sof_seen = sof0; song.cpu_q8 = 0;
            ui.menu = 1;
        }
        cur_name = st ? "sweep LINE: menu tabs sliding" : "sweep FLAT: menu tabs sliding";
        for (i = 0; i < MI_COUNT; i++)                  /* every tab, from every place of the bar, S and M */
            for (k = 0; k <= (MTAB_COUNT - 1u) * 16u; k++) {
                ui.menu_sel = (uint8_t)i; ui_prefs = k & 1u ? PREF_LARGE : 0u;
                mt.pos = (int16_t)(k * 4u); mt.frame = ui.frame - 1u; ui.force = 1;
                draw_menu();
            }
        ui.menu = 0; ui_prefs = 0; ui_rec_prefs = 0; settings_hold = 0; settings.lowcut = 0; settings_leds = 0;
        cur_name = st ? "sweep LINE: REC layer values" : "sweep FLAT: REC layer values";
        ui.home = 1; ui.layer = LAYER_REC;              /* (the map and the cards: every value of the three, each track) */
        for (k = 0; k < 27u * 2u; k++) {
            rp_put(RP_CLICK, k % 3u); rp_put(RP_COUNTIN, k / 3u % 3u); rp_put(RP_LEVEL, k / 9u % 3u);
            song.sel = (uint8_t)(k % NTRK); ui_prefs = k >= 27u ? PREF_LARGE : 0u;
            ui.force = 1; draw_layer(); ui.force = 0;
        }
        ui.layer = 0; song.sel = 0; ui_prefs = 0; ui_rec_prefs = 0;
        cur_name = st ? "sweep LINE: HELP hints" : "sweep FLAT: HELP hints";   /* (1.5: every page's, HOME's, every layer's) */
        ui_prefs2 |= PREF_HELP >> 16;
        for (i = 0; i <= NPAGES; i++) {
            ui.home = i >= NPAGES; ui.page = (uint8_t)(i < NPAGES ? i : 0u);
            ui.help_t = HELP_FRAMES; ui.force = 1;
            draw_foot();
        }
        for (k = LAYER_FX; k < LAYER_N; k++) {
            ui.layer = (uint8_t)k; ui.force = 1; draw_layer(); ui.force = 0;
        }
        ui.layer = 0; ui.home = 1; ui.help_t = 0; ui_prefs2 &= (uint8_t)~(PREF_HELP >> 16);
        cur_name = st ? "sweep LINE: count-in" : "sweep FLAT: count-in";
        for (i = 1; i <= 2u; i++)                       /* 1 and 2 bars, every beat */
            for (k = 4u * i; k >= 1u; k--) {
                cin_total = (uint8_t)(4u * i); cin_left = (uint8_t)k; ui.force = 1;
                draw_countin();
            }
        cin_total = cin_left = 0;
        state(); pal(0);
        cur_name = st ? "sweep LINE: NAME" : "sweep FLAT: NAME";
        song.playing = 0; go_page(GR_USER); name_open(NK_USER_SAVE, 6);
        for (k = 0; k < 2u; k++) {                       /* ABC, 123: every key cycling, every letter typed next */
            nm.num = (uint8_t)k;
            for (i = 0; i <= 16u; i++) {
                uint32_t n = i ? str_len(nm_group(i - 1u)) : 1u, j;
                for (j = 0; j < n; j++) {
                    nm.key = (uint8_t)i; nm.tap = (uint8_t)j;
                    nm_draw_panel();
                }
            }
        }
        nm.key = 0;
        for (i = 0; i < sizeof NM_SET - 1u; i += NM_LEN) {   /* the field: every character */
            for (k = 0; k < NM_LEN; k++) nm.s[k] = NM_SET[(i + k) % (sizeof NM_SET - 1u)];
            nm.s[NM_LEN] = 0; nm.len = NM_LEN; nm.cur = 0;
            nm_draw_field();
        }
        name_close();
        cur_name = st ? "sweep LINE: piano roll" : "sweep FLAT: piano roll";
        roll_scene(S_ROLL_ACID);
        for (v = 0; v + PR_ROWS <= 128; v++) {
            proll.lo = (uint8_t)v;
            cv_begin(240, H_GRAPH, T_SURF);
            graph_roll(TSEL, T_THEME);
        }
        state(); pal(0);
        cur_name = st ? "sweep LINE: mixer" : "sweep FLAT: mixer";
        go_page(GR_TRK);
        for (v = 0; v < 256; v++) {                    /* (then with LARGE: the strip) */
            ui_prefs = v >= 128 ? PREF_LARGE : 0u;
            trk[0].p[P_LEVEL] = (int16_t)(v & 127); trk[0].p[P_PAN] = (int16_t)((v & 127) - 64); trk[0].p[P_REV] = (int16_t)(v & 127);
            trk[1].p[P_MUTE] = (int16_t)(v & 1); song.rec = v & 2 ? 2u : 0u; ui.hot_t = (uint8_t)(v & 4); ui.hot_col = 3;
            ts.meter[0] = (uint8_t)(v % (TS_MH - 1));
            ui.force = 1;
            draw_tracks();
        }
        ui_prefs = 0; ui.hot_t = 0;
        ui.force = 0;
    }
    ui_style = ST_FLAT; style_apply();
    state();
    nscr = npend = nrules = 0;
    nfind = nf;
    rep = r0;
    if (nul) fclose(nul);
}

/* the keycaps' labels in their pills (tools/gen_aa_keycaps.py), from the data: the label's ink (nibbles 6..15) in the
 * pill (KC_H rows, its width) */
static void al_keycaps(void)
{
    uint32_t id;
    cur_name = "keycap data";
    for (id = 0; id < KC_COUNT; id++) {
        const kc_t *k = &KC[id];
        int32_t x, y, x0 = 999, y0 = 999, x1 = -1, y1 = -1, w = k->w;
        uint32_t i = 0;
        for (y = 0; y < KC_H; y++)
            for (x = 0; x < w; x++, i++)
                if (((KC_DATA[k->off + (i >> 1)] >> ((i & 1u) ? 0 : 4)) & 15u) >= 6u) {
                    if (x < x0) x0 = x;
                    if (y < y0) y0 = y;
                    if (x + 1 > x1) x1 = x + 1;
                    if (y + 1 > y1) y1 = y + 1;
                }
        al_record("keycap label in its pill", (x0 + x1) - w, (y0 + y1) - KC_H, k->label, 0, 0, w, KC_H, x0, y0, x1, y1);
        if (al_rep) fprintf(al_rep, "keycap %-8s w %2d  ink x %2d..%2d y %d..%d  across %+.1f up/down %+.1f\n", k->label, w, x0, x1, y0, y1,
                            ((x0 + x1) - w) / 2.0, ((y0 + y1) - KC_H) / 2.0);
    }
}
static void al_table(void)
{
    uint32_t i;
    if (!al_rep) return;
    fprintf(al_rep, "\n%-30s %8s %5s %5s %5s %6s %6s\n", "place", "measured", "1px+", "-0.5", "+0.5", "across", "up/dn");
    for (i = 0; i < al_nst; i++) {
        const alstat_t *a = &al_st[i];
        if (!strcmp(a->tag, "self-test")) continue;
        fprintf(al_rep, "%-30s %8u %5u %5u %5u %6.1f %6.1f\n", a->tag, a->n, a->nfail, a->half_lo, a->half_hi, a->wh2 / 2.0,
                a->wv2 / 2.0);
        if (a->ex_h[0]) fprintf(al_rep, "    across  worst %s\n", a->ex_h);
        if (a->ex_v[0]) fprintf(al_rep, "    up/down worst %s\n", a->ex_v);
    }
    fprintf(al_rep, "\n%u measurements, %u places, %u off by 1 px or more; %u declared places drawn without their items\n",
            al_count, al_nst, al_fail, al_lost);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/ui_new";
    char path[600];
    uint32_t p, s;
    snprintf(path, sizeof path, "%s/report.txt", out);
    rep = fopen(path, "w");
    if (!rep) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    {
        char ap[600];
        snprintf(ap, sizeof ap, "%s/align.txt", out);
        al_rep = fopen(ap, "w");
        if (al_rep) fprintf(al_rep, "alignment: the ink of each centred text / icon / keycap against its box (gfx.c GFX_HOOK_ALIGN);"
                            " offsets in px, + right / down\n\n");
    }
    {
        char ap[600];
        snprintf(ap, sizeof ap, "%s/text_audit.tsv", out);
        audf = fopen(ap, "w");
    }
    {
        char ap[600];
        snprintf(ap, sizeof ap, "%s/font_audit.tsv", out);
        fontf = fopen(ap, "w");
        if (fontf) fprintf(fontf, "mode\tscreen\tface\tx0\ty0\tx1\ty1\ttext\n");
    }
    if (audf) fprintf(audf, "screen\ttexts\ttext_px\ticons\ticon_px\tkeycaps\tkeycap_px\tellipsised\ttight\twords\ttight_list\n");
    {   /* the lint itself: an overlap, a value too wide for its column, a text past its canvas, one past its cell */
        state(); pal(0); cur_name = "self-test";
        nscr = npend = 0;
        cv_begin(COL_W, COL_H, T_BG);
        cv_text(0, 0, &AF_S, "AAAA", T_TEXT);
        cv_text(10, 4, &AF_S, "BBBB", T_TEXT);
        cv_text_fit(0, 15, &AF_M, "WWWWWWWW", T_THEME, T_BG, COL_W);
        cv_text(40, 30, &AF_M, "CUT", T_THEME);
        cv_rrect(2, 31, 20, 13, 3, T_SURF, T_BG);          /* a cell narrower than its word */
        cv_text(4, 31, &AF_S, "SPILL", T_TEXT);
        cv_blit(4, Y_LABEL);
        lint();
        nrules = 0;                                         /* a word on a divider (STYLE LINE) */
        lcd_rule(0, 120, 240, 1);
        cv_begin(40, 16, T_BG);
        cv_text(2, 0, &AF_S, "RULE", T_TEXT);
        cv_blit(8, 112);
        lint();
        nrules = 0;
        {   /* the alignment check: a word 2 px right of its box's centre, one centred */
            uint32_t f0 = al_fail;
            cv_begin(60, 20, T_BG);
            GFX_HOOK_ALIGN(0, 0, 60, 20, AL_HV, "self-test");
            cv_text_in(4, 0 + CAP_IN(S, 20), 60, &AF_S, "OFF", T_TEXT, T_BG);
            GFX_HOOK_ALIGN(0, 0, 60, 20, AL_HV, "self-test");
            cv_text_in(0, 0 + CAP_IN(S, 20), 60, &AF_S, "MID", T_TEXT, T_BG);
            if (al_fail - f0 != 1u) { fprintf(stderr, "ui_render: the alignment check missed its self-test (%u)\n", al_fail - f0); return 1; }
            al_fail = f0;
            al_count -= 2u;
            npend = 0;
        }
        {   /* the trim check: a trim rectangle with a text drawn over it is a finding, a clear one is not */
            uint32_t f0 = nfind, b0 = n_trim_bad;
            cv_begin(60, 20, T_BG);
            cv_rect(0, 0, 40, 3, 0xF800u);
            hk_trim(0, 0, 40, 3, 0xF800u);
            cv_rect(0, 8, 40, 2, 0x07E0u);
            hk_trim(0, 8, 40, 10, 0x07E0u);
            cv_text(2, 0, &AF_S, "OVER", T_TEXT);                   /* over the first, clear of the second */
            cv_blit(8, 200);
            lint();
            if (n_trim_bad - b0 != 1u) { fprintf(stderr, "ui_render: the trim check missed its self-test (%u)\n", n_trim_bad - b0); return 1; }
            (void)f0;
            npend = 0; nscr = 0;
        }
        if (nfind < 5u || !nspill) { fprintf(stderr, "ui_render: the lint missed its self-test (%u)\n", nfind); return 1; }
        fprintf(rep, "(self-test: %u findings above are expected)\n\n", nfind);
        nfind = 0;
    }
    {   /* the machines' palettes and trims (the one colour scheme): every screen with the selected track set to each machine
         * (its palette and trim), then in MACHINE mode as each screen sets its own track. HOME and EDIT of each machine: OUTDIR/ppm/<MACHINE>_<screen>.ppm. 0 lint findings,
         * and no trim under a text, icon or card (hk_trim) */
        static const char *const SHOT[] = {"home_big", "home_big_playing", "edit_big_obx", "edit_big_wave", "edit_big_acid",
                                           "edit_big_drift", "home", "menu", "presets", "step"};
        uint32_t m, k, n0 = nfind, nm = 0, nshot = 0;
        n_trim_rects = n_trim_checked = 0;
        for (m = 0; m < UI_NMACHINE; m++)
            for (s = 0; s < S_COUNT; s++) {
                char name[64];
                snprintf(name, sizeof name, "TRACK-%s/%s", UI_MACHINE_PALS[m].name, S_NAME[s]);
                cur_name = name;
                setup((int)s);
                eng(m);
                machine_mode();
                if (m == 0u) aud = audf;                  /* (the text and font audits: OB-Z's, the layout is the same in every palette) */
                draw((int)s);
                lint();
                if (m == 0u) { audit_scene(S_NAME[s]); font_scene("FLAT", S_NAME[s]); aud = 0; }
                nm++;
            }
        for (s = 0; s < S_COUNT; s++) {
            char name[64];
            snprintf(name, sizeof name, "MACHINE/%s", S_NAME[s]);
            cur_name = name;
            setup((int)s);
            machine_mode();
            draw((int)s);
            lint();
            nm++;
            for (k = 0; k < sizeof SHOT / sizeof SHOT[0]; k++)
                if (!strcmp(S_NAME[s], SHOT[k])) {
                    write_ppm(out, UI_MACHINE_PALS[TSEL->eng_req % NENGINES].name, S_NAME[s]);
                    nshot++;
                }
        }
        for (m = 0; m < UI_NMACHINE; m++) {          /* HOME of each machine's track in MACHINE mode: the shots */
            setup(S_HOME_BIG);
            eng(m);
            machine_mode();
            draw(S_HOME_BIG);
            write_ppm(out, UI_MACHINE_PALS[m].name, "home_big");
            nshot++;
        }
        sweep_columns();                             /* the value sweeps: every page, engine and column value, each in its machine's palette */
        printf("ui_render: MACHINE mode: %u screens (each machine's palette, on its machine's track, as set up) and the value sweeps; "
               "%u trim rectangles drawn, each still whole at the blit (%u checks); %u wrong palettes\n",
               nm, n_trim_rects, n_trim_checked, mach_bad);
        fprintf(rep, "MACHINE mode (every screen on each machine's track, as set up; the value sweeps): %u screens, "
                "%u trim rectangles drawn, %u checked against the texts and cards of their canvas; %u lint findings; %u wrong palettes\n",
                nm, n_trim_rects, n_trim_checked, nfind - n0, mach_bad);
        if (n_trim_rects < 1000u) { fprintf(stderr, "ui_render: the trim was drawn %u times only\n", n_trim_rects); nfind++; }
        if (mach_bad) { fprintf(stderr, "ui_render: MACHINE mode set the wrong palette %u times\n", mach_bad); nfind++; }
        (void)nshot;
    }
    {   /* STYLE LINE (gfx.c ST_*): every screen in every machine's palette, linted; as OUTDIR/ppm/<STYLE>_<MACHINE>_<screen>.ppm
         * (ui_render.py: sheet_<STYLE>_<MACHINE>.png) */
        static const char *const ST_N[2] = {"FLAT", "LINE"};
        uint32_t st, n0 = nfind, nruled = 0;
        for (st = ST_LINE; st <= ST_LINE; st++)
            for (p = 0; p < NPALETTES; p++) {
                for (s = 0; s < S_COUNT; s++) {
                    char name[64], tag[24];
                    snprintf(tag, sizeof tag, "%s_%s", ST_N[st], UI_MACHINE_PALS[p].name);
                    snprintf(name, sizeof name, "%s/%s", tag, S_NAME[s]);
                    cur_name = name;
                    setup((int)s);
                    eng(p);
                    pal(p);
                    ui_style = (uint8_t)st; style_apply();
                    draw((int)s);
                    if (ux.style != st) { fprintf(stderr, "ui_render: %s drawn in style %u\n", name, ux.style); return 1; }
                    nruled += nrules != 0u;
                    lint();
                        write_ppm(out, tag, S_NAME[s]);
                }
            }
        ui_style = ST_FLAT; style_apply();
        if (nruled < 100u) { fprintf(stderr, "ui_render: dividers on %u STYLE screens only\n", nruled); return 1; }
        fprintf(rep, "STYLE LINE (every machine palette): %u lint findings\n", nfind - n0);
    }
    {   /* MENU > LARGE (ui.c large_kind): every screen in every machine's palette in FLAT and LINE, linted; OB-Z's as
         * OUTDIR/ppm/LARGE_OB-Z_<screen>.ppm (ui_render.py: sheet_LARGE_OB-Z.png) and LARGE-LINE_OB-Z; the tall cards' value faces
         * counted (lg_hook) */
        static const char *const ST_N[2] = {"LARGE", "LARGE-LINE"};
        uint32_t st, n0 = nfind, ntall = 0;
        large_on = 1;
        for (st = ST_FLAT; st <= ST_LINE; st++)
            for (p = 0; p < NPALETTES; p++)
                for (s = 0; s < S_COUNT; s++) {
                    char name[64], tag[24];
                    snprintf(tag, sizeof tag, "%s_%s", ST_N[st], UI_MACHINE_PALS[p].name);
                    snprintf(name, sizeof name, "%s/%s", tag, S_NAME[s]);
                    cur_name = name;
                    setup((int)s);
                    ui_prefs |= PREF_LARGE;
                    eng(p);
                    pal(p);
                    ui_style = (uint8_t)st; style_apply();
                    draw((int)s);
                    ntall += !ui.menu && !ui.confirm && !name_on() && large_kind() == LK_TALL;
                    lint();
                    if (!st && p == 0u) font_scene("LARGE", S_NAME[s]);
                        if (p == 0u)
                        write_ppm(out, tag, S_NAME[s]);
                }
        ui_style = ST_FLAT; style_apply();
        sweep_columns();                                /* every value of every column, tall */
        large_on = 0;
        if (ntall < 100u) { fprintf(stderr, "ui_render: LARGE drew %u screens with tall cards only\n", ntall); return 1; }
        fprintf(rep, "MENU > LARGE (FLAT and LINE, every machine palette, the page / value sweep): %u lint findings; %u screens with tall"
                " cards\n", nfind - n0, ntall);
        fprintf(rep, "  tall card values: %llu in L, %llu in M (a glyph not in L: \"%s\"), %llu in M (L too wide), %llu in S\n",
                (unsigned long long)lg_n[0], (unsigned long long)lg_n[1], lg_miss, (unsigned long long)lg_n[2],
                (unsigned long long)lg_n[3]);
        fprintf(rep, "  too wide for L, e.g.:");
        for (s = 0; s < lg_nwide; s++) fprintf(rep, " '%s'", lg_wide[s]);
        fprintf(rep, "\n  in S, e.g.:");
        for (s = 0; s < lg_nsmall; s++) fprintf(rep, " '%s'", lg_small[s]);
        fprintf(rep, "\n");
    }
    sweep_columns();
    roll_frames(argc > 2 ? argv[2] : 0);
    al_off = 1;                                     /* (the alignment check is not timed) */
    /* draw cost: a full redraw of a screen (all strips), and HOME frame by frame (the scope, every other frame) */
    {
        static const int COST[] = {S_HOME, S_PRESETS, S_STEP, S_ROLL_CHORDS, S_MIXER, S_MENU, S_ABOUT_CREDITS, S_CONFIRM_PROJ};
        enum { N = 200 };
        uint32_t i, k;
        fprintf(rep, "\ndraw cost on the host (cc -O1; only the ratios mean anything on the device):\n");
        fprintf(rep, "  screen            full redraw us   texts+icons   glyph px read\n");
        for (k = 0; k < sizeof COST / sizeof COST[0]; k++) {
            clock_t c0;
            uint64_t px, nt;
            setup(COST[k]); pal(1);
            draw(COST[k]);
            px_visited = n_text = 0;
            c0 = clock();
            for (i = 0; i < N; i++) draw(COST[k]);
            px = px_visited / N; nt = n_text / N;
            fprintf(rep, "  %-16s %15.1f %13llu %15llu\n", S_NAME[COST[k]], (double)(clock() - c0) / CLOCKS_PER_SEC / N * 1e6,
                    (unsigned long long)nt, (unsigned long long)px);
        }
        {   /* UI.4: HOME (the selected track of each machine) and EDIT of each machine in MACHINE mode: a full redraw, all strips */
            static const int C2[] = {S_HOME_BIG, S_HOME_BIG, S_HOME_BIG, S_HOME_BIG, S_EDIT_BIG_OBX, S_EDIT_BIG_WAVE, S_EDIT_BIG_ACID, S_EDIT_BIG_DRIFT};
            static const char *const N2[] = {"HOME on OB-Z", "HOME on WAVE", "HOME on ACID", "HOME on DRIFT", "EDIT OB-Z", "EDIT WAVE", "EDIT ACID", "EDIT DRIFT"};
            for (k = 0; k < sizeof C2 / sizeof C2[0]; k++) {
                clock_t c0;
                setup(C2[k]);
                if (k < 4u) eng(k);
                machine_mode();
                draw(C2[k]);
                c0 = clock();
                for (i = 0; i < N; i++) { ui.force = 1; draw(C2[k]); }
                fprintf(rep, "  cost2 %-14s %9.1f us (a forced full redraw)\n", N2[k], (double)(clock() - c0) / CLOCKS_PER_SEC / N * 1e6);
            }
        }
        {
            clock_t c0;
            setup(S_HOME); pal(1); draw(S_HOME);
            px_visited = n_text = 0;
            c0 = clock();
            for (i = 0; i < N * 4; i++) { ui_draw(); nscr = npend = 0; }
            fprintf(rep, "  HOME, a frame without force (lazy strips): %.1f us, %llu texts\n",
                    (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6, (unsigned long long)(n_text / (N * 4)));
        }
        {   /* the STEP page's piano roll: its panel alone redrawn, and a frame that changes nothing (the signature only) */
            static const int G[] = {S_ROLL_CHORDS, S_ROLL_PLAYING};
            for (k = 0; k < 2u; k++) {
                clock_t c0;
                double tg, ti;
                setup(G[k]); pal(1); draw(G[k]);
                c0 = clock();
                for (i = 0; i < N * 4; i++) { ui.force = 1; draw_graph(); ui.force = 0; nscr = npend = 0; }
                tg = (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6;
                song.playing = 0;                        /* (no playhead moving) */
                ui_draw(); ui_draw(); nscr = npend = 0;
                c0 = clock();
                for (i = 0; i < N * 4; i++) { ui.frame++; draw_graph(); nscr = npend = 0; }
                ti = (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6;
                fprintf(rep, "  %s: the panel redrawn %.1f us; an unchanged frame (signature only) %.2f us\n", S_NAME[G[k]], tg, ti);
            }
        }
        roll_cost();
        {   /* MENU > LARGE: the same full redraws, tall cards and the strip */
            static const int LC[] = {S_HOME, S_EDIT_ANALOG, S_ENV, S_LFO, S_MIXER, S_FX, S_CLOCK};
            fprintf(rep, "  MENU > LARGE:\n");
            large_on = 1;
            for (k = 0; k < sizeof LC / sizeof LC[0]; k++) {
                clock_t c0;
                uint64_t px, nt;
                setup(LC[k]); pal(1);
                draw(LC[k]);
                px_visited = n_text = 0;
                c0 = clock();
                for (i = 0; i < N; i++) draw(LC[k]);
                px = px_visited / N; nt = n_text / N;
                fprintf(rep, "  %-16s %15.1f %13llu %15llu\n", S_NAME[LC[k]], (double)(clock() - c0) / CLOCKS_PER_SEC / N * 1e6,
                        (unsigned long long)nt, (unsigned long long)px);
            }
            {
                clock_t c0;
                setup(S_HOME); pal(1); draw(S_HOME);
                px_visited = n_text = 0;
                c0 = clock();
                for (i = 0; i < N * 4; i++) { ui_draw(); nscr = npend = 0; }
                fprintf(rep, "  LARGE HOME, a frame without force (lazy strips): %.1f us, %llu texts\n",
                        (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6, (unsigned long long)(n_text / (N * 4)));
                setup(S_ENV); pal(1); draw(S_ENV);
                px_visited = n_text = 0;
                c0 = clock();
                for (i = 0; i < N * 4; i++) { ui_draw(); nscr = npend = 0; }
                fprintf(rep, "  LARGE ENV, a frame without force (lazy strips): %.1f us, %llu texts\n",
                        (double)(clock() - c0) / CLOCKS_PER_SEC / (N * 4) * 1e6, (unsigned long long)(n_text / (N * 4)));
            }
            large_on = 0;
        }
    }
    al_off = 0;
    al_keycaps();
    align_sweeps();
    al_table();
    if (al_rep) fclose(al_rep);
    fprintf(rep, "\nalignment: %u measurements of %u places, %u off by 1 px or more (OUTDIR/align.txt)\n", al_count, al_nst - 1u, al_fail);
    if (audf) fclose(audf);
    if (fontf) fclose(fontf);
    {
        uint32_t i;
        fprintf(rep, "\ncard values set in S (M too wide for the card with its unit; FLAT and LINE, the sweep too): %llu draws, "
                "%u distinct%s:", (unsigned long long)cs_count, cs_n + cs_more, cs_more ? " (the first 64)" : "");
        for (i = 0; i < cs_n; i++) fprintf(rep, " '%s'", cs_seen[i]);
        fprintf(rep, "\n");
    }
    fprintf(rep, "\n%u lint findings, %u inks on their own colour, %u ellipsised free texts; "
            "%u column values swept; %u cards linted with MOTION's icon\n", nfind, nink, nfree,
            nsweep, nmotion);
    if (!nmotion) { fprintf(stderr, "ui_render: the MOTION sweep marked no card\n"); nfind++; }
    if (!nsong_txt) { fprintf(stderr, "ui_render: SONG's bands checked no text\n"); nfind++; }
    fclose(rep);
    printf("ui_render: %u screens x %u palettes + the page/value sweep; %u lint findings, %u ellipsised free texts (%u distinct), "
           "%u inks on their own colour; report %s\n",
           (unsigned)S_COUNT, (unsigned)NPALETTES, nfind, nfree, nfree_seen, nink,
           path);
    printf("ui_render: alignment: %u measurements of %u places, %u off by 1 px or more%s\n", al_count, al_nst - 1u, al_fail,
           al_fail ? " (see align.txt)" : "");
    return nfind || nink || al_fail ? 1 : 0;
}
