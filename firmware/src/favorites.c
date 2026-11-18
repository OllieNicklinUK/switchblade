/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define FELUCCA_FAVORITES 1
#define FAV_ROWS 8u              /* the engines with a row of favourites: the eight machines (V2) */
/* Stable engine/preset references; fixed capacity independent of optional engines. */
typedef struct {
    uint8_t factory[16][32]; /* engine 0..7, preset 0..255 (rows 8..13 unused; 14: the MIDI LEARN map, 15: settings) */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites __attribute__((section(".pool")));
/* where engine e's preset k is starred: its row and bit, 0 = none */
static uint8_t *favorite_at(uint32_t e, uint32_t *k)
{
    return e < FAV_ROWS && *k < 256u ? favorites.factory[e] : 0;
}
static int favorite_has(uint32_t engine, uint32_t preset)
{
    if (engine == NENGINES)
        return preset < UP_SLOTS && ((favorites.user >> preset) & 1u);
    {
        uint8_t *r = engine < NENGINES ? favorite_at(engine, &preset) : 0;
        return r && ((r[preset / 8u] >> (preset % 8u)) & 1u);
    }
}
static int favorite_set(uint32_t engine, uint32_t preset, int on)
{
    uint32_t k = preset;
    if (engine > NENGINES || (engine == NENGINES ? preset >= UP_SLOTS : !favorite_at(engine, &k)))
        return 0;
    if (favorite_has(engine, preset) == !!on) return 0;
    if (engine == NENGINES) {
        if (on) favorites.user |= 1u << preset;
        else favorites.user &= ~(1u << preset);
    } else {
        uint8_t *b = &favorite_at(engine, &preset)[preset / 8u];
        if (on) *b |= (uint8_t)(1u << (preset % 8u));
        else *b &= (uint8_t)~(1u << (preset % 8u));
    }
    return 1;
}

/* Switch Blade has one colour scheme: the palette follows the machine of the selected track (gfx.c UI_MACHINE_PALS, with
 * its trim: ui_trim.c). MENU > COLOR, the fixed palettes and CUSTOM are gone; favorites.factory[15][0..9, 12] (CUSTOM's
 * five colours and flags, 1.6 #244) stay in the settings as bytes nobody reads, so a restore or an older firmware keeps them */
#define ui_cpal (&favorites.factory[15][0])
#define ui_cpal_flags (favorites.factory[15][12])
static uint32_t palette_n(void) { return 1u; }
static uint32_t palette_cur(void) { return 0u; }
static const char *palette_name(uint32_t v) { (void)v; return "MACHINE"; }
static uint8_t trim_machine = 0xFF;                    /* the machine the palette was set for (palette_follow) */
/* the palette in use into gfx.c (the settings changed under it: a start, an import, a restore, a STYLE change, a new
 * machine on the selected track) */
static void palette_apply(void)
{
    trim_machine = (uint8_t)(TSEL->eng_req % NENGINES);
    palette_set(PAL_MACHINE_ID(trim_machine));
}
/* another machine on the selected track (a track change, a machine change, a project or sound load) sets its palette and
 * trim; one full redraw */
static int palette_follow(void)
{
    if (trim_machine == (uint8_t)(TSEL->eng_req % NENGINES))
        return 0;
    palette_apply();
    return 1;
}
