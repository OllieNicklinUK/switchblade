/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define __attribute__(x)
#define NENGINES 8u                              /* (core.h, V2: OB-Z WAVE ACID DRIFT JP-Z CZ-Z GW-Z MICRO) */
#define UP_SLOTS 32u
static uint8_t fx_lowcut;
static void fm1_led_key(unsigned k, int on) { (void)k; (void)on; }
static int fm1_enc_take(unsigned k) { (void)k; return 0; }
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
static struct { uint8_t eng_req; } sel_stub;     /* (the selected track: favorites.c palette_apply follows its machine) */
#define TSEL (&sel_stub)
#include "../firmware/src/panel.c"
static void settings_save(void) {}
#if __has_include("../firmware/src/favorites.c")
#include "../firmware/src/favorites.c"
#endif
#include "../firmware/src/settings_persist.c"
int main(void)
{
    persist_t original = {0}, p;
    original.magic = PERSIST_MAGIC;
    original.palette = 4; original.bold = original.lowcut = original.zoom = 1;   /* old id 4: MONO, now GREY */
    original.panel = PANEL_DEFAULT; original.panel.enc[0] = 3;
    original.favorites.factory[3][0] = 1;           /* DRIFT (the last engine) preset 0 */
    original.favorites.user = 1u << 31; original.favorites.filter = 1;
    p = original;
    assert(settings_import(&p, sizeof p) == 1); settings_init();
    assert(settings.palette == UI_PAL_MACHINE && fx_lowcut && settings.zoom && panel.enc[0] == 3);
    assert(p.palette == palette_to_stored(UI_PAL_MACHINE));       /* migrated in place: an old id 4 is MACHINE now */
    {   /* Switch Blade: MACHINE is the one colour scheme. Every stored id loads as MACHINE (an older firmware's index below 20,
         * a fixed palette's tagged id, CUSTOM's, MACHINE's own); MACHINE is written as the id older firmware reads as GREY */
        persist_t q = original;
        uint32_t i;
        assert(palette_to_stored(UI_PAL_MACHINE) == UI_PAL_TAG + UI_PAL_STORED_MACHINE && UI_PAL_STORED_MACHINE == 10u);
        assert(palette_stored_ok(UI_PAL_TAG + UI_PAL_STORED_MACHINE) && !palette_stored_ok(UI_PAL_TAG + UI_PAL_STORED_MACHINE + 1u) &&
               !palette_stored_ok(40) && palette_stored_ok(3));
        for (i = 0; i < 20u; i++) assert(palette_from_stored(i) == UI_PAL_MACHINE);
        for (i = 0; i <= UI_PAL_STORED_MACHINE; i++) assert(palette_from_stored(UI_PAL_TAG + i) == UI_PAL_MACHINE);
        assert(palette_from_stored(40) == UI_PAL_MACHINE && palette_from_stored(0xFFFFFFFFu) == UI_PAL_MACHINE);
        settings.magic = 0; settings.palette = 5; settings_init();             /* fresh: no valid record */
        assert(settings.palette == UI_PAL_MACHINE && trim_machine == 0u);       /* (palette_apply: the selected track's machine) */
        assert(UI_NMACHINE == NENGINES && NPALETTES == UI_NMACHINE);
        for (i = 0; i < NENGINES; i++) {                                         /* the palette in use follows the machine */
            sel_stub.eng_req = (uint8_t)i;
            settings.palette = UI_PAL_MACHINE;
            palette_apply();
            assert(ux.trim == 1u + i && T_BG == UI_MACHINE_PALS[i].bg && T_THEME == UI_MACHINE_PALS[i].theme && trim_machine == i);
        }
        sel_stub.eng_req = 0;
        settings.palette = UI_PAL_MACHINE;                                       /* MACHINE saved and restored */
        settings_export(&p);
        assert(p.palette == UI_PAL_TAG + UI_PAL_STORED_MACHINE);
        for (i = 0; i < 20u; i++) {                                              /* an older firmware's ids */
            q.palette = i;
            assert(settings_import(&q, sizeof q) == 1 && settings.palette == UI_PAL_MACHINE && q.palette == UI_PAL_TAG + 10u);
            settings_init(); assert(settings.palette == UI_PAL_MACHINE);
        }
        for (i = 0; i <= UI_PAL_STORED_MACHINE; i++) {                           /* ... a fixed palette's, MACHINE's, ... */
            q.palette = UI_PAL_TAG + i;
            assert(settings_import(&q, sizeof q) == 1 && settings.palette == UI_PAL_MACHINE && q.palette == UI_PAL_TAG + 10u);
            settings_init(); assert(settings.palette == UI_PAL_MACHINE && T_BG == UI_MACHINE_PALS[0].bg && ux.trim == 1u);
        }
        settings.magic = SETTINGS_MAGIC_OLD; settings.palette = 13; settings_init();   /* retained SET3 */
        assert(settings.magic == SETTINGS_MAGIC && settings.palette == UI_PAL_MACHINE);
        settings.palette = 77; settings_init();                                  /* a stray value in RAM */
        assert(settings.palette == UI_PAL_MACHINE);
        {   /* CUSTOM's bytes (favorites.factory[15][0..9, 12]) are kept as saved, nobody reads them */
            q = original;
            q.favorites.factory[15][0] = 0x41; q.favorites.factory[15][1] = 0x08; q.favorites.factory[15][12] = 3;
            assert(settings_import(&q, sizeof q) == 1 && settings.palette == UI_PAL_MACHINE);
            settings_init();
            assert(settings.palette == UI_PAL_MACHINE && ux.trim == 1u);
            settings_export(&q);
            assert(q.favorites.factory[15][0] == 0x41 && q.favorites.factory[15][12] == 3 && q.palette == UI_PAL_TAG + 10u);
        }
        p = original; assert(settings_import(&p, sizeof p) == 1); settings_init();
    }
#ifdef FELUCCA_FAVORITES
    assert(favorite_has(3, 0) && favorite_has(NENGINES, 31) && favorites.filter);
    assert(!favorite_has(NENGINES + 1u, 0) && !favorite_set(NENGINES + 1u, 0, 1));   /* no row past the engines */
#endif
    settings.lowcut = 0;
    settings_export(&p);
    assert(!p.lowcut && p.bold == 1 && p.favorites.user == (1u << 31));   /* bold: kept as saved */
    assert(p.favorites.factory[3][0] == 1 && p.favorites.filter == 1);
    assert(p.panel.enc[0] == 3); /* saving one feature preserves the other */
    p = original; memset(&p.favorites, 0, sizeof p.favorites);
    assert(settings_import(&p, sizeof p) == 1);
    assert(p.bold == 1 && !p.favorites.user && !p.favorites.filter);
    settings_export(&p); assert(p.bold == 1); /* favorites-only preserves PER3 font */
    {   /* every Felucca settings object (PER1..PER4, any size) is refused and leaves the settings untouched */
        static const uint32_t OLD[4] = {0x50455231u, 0x50455232u, 0x50455233u, 0x50455234u};
        uint32_t m;
        for (m = 0; m < 4u; m++) {
            persist_t o = original;
            uint32_t pal = settings.palette;
            o.magic = OLD[m];
            assert(settings_import(&o, sizeof o) == 0 && settings.palette == pal);
            assert(settings_import(&o, sizeof o - sizeof o.favorites) == 0);
            assert(settings_import(&o, sizeof o - sizeof o.favorites - sizeof o.bold) == 0);
            assert(settings_import(&o, 8 + sizeof(panel_t)) == 0);
        }
        assert(PERSIST_MAGIC == 0x34524F46u);
    }
    {   /* HOLD: in the retired bold field; older records (bold 0 / 1) are 0.4 s, and stay as saved */
        p = original; p.bold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF && HOLD_MS[settings_hold] == 400u);
        p = original;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        settings_hold = 3;
        settings_export(&p);
        assert(hold_stored_ok(p.bold) && p.bold != 1u);
        settings_hold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == 3u && HOLD_MS[settings_hold] == 600u);
        settings_hold = HOLD_DEF;
        settings_export(&p);
        assert(p.bold == 0u && settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        assert(!hold_stored_ok(2u) && !hold_stored_ok(HOLD_TAG + 4u) && hold_stored_ok(HOLD_TAG | 2u));
    }
    {   /* LEDS: in the retired zoom field; older records (zoom 0 / 1) are DIM, and stay as saved; idempotent */
        persist_t q;
        p = original;                                   /* zoom 1: an older record's large readout */
        assert(settings_import(&p, sizeof p) == 1 && settings_leds == LEDS_DIM);
        q = p; settings_export(&q); assert(q.zoom == 1u);                  /* DIM: kept as saved */
        settings_leds = LEDS_INV;
        settings_export(&p);
        assert(p.zoom == (LEDS_TAG | LEDS_INV) && leds_stored_ok(p.zoom));
        q = p; settings_export(&q); assert(!memcmp(&q, &p, sizeof q));   /* a second save: the same record */
        settings_leds = LEDS_DIM;
        assert(settings_import(&p, sizeof p) == 1 && settings_leds == LEDS_INV);
        q = p; assert(settings_import(&q, sizeof q) == 1 && !memcmp(&q, &p, sizeof q));   /* import twice: the same */
        settings_leds = LEDS_DIM;
        settings_export(&p);
        assert(p.zoom == 0u && settings_import(&p, sizeof p) == 1 && settings_leds == LEDS_DIM);
        assert(leds_stored_ok(0u) && leds_stored_ok(1u) && !leds_stored_ok(2u) && leds_stored_ok(LEDS_TAG | 2u) &&
               leds_stored_ok(LEDS_TAG | 3u) && !leds_stored_ok(LEDS_TAG + 4u) && leds_from_stored(LEDS_TAG + 4u) == LEDS_DIM &&
               leds_from_stored(7u) == LEDS_DIM && leds_from_stored(2u) == LEDS_DIM);
        /* the modes appended (append-only: DIM 0, INV 1 as before): OFF 2, DIM LO 3, each saved and read back,
         * twice the same */
        {
            uint32_t m;
            static const uint32_t ADDED[2] = {LEDS_OFF, LEDS_DIM_LO};
            assert(LEDS_DIM == 0 && LEDS_INV == 1 && LEDS_OFF == 2 && LEDS_DIM_LO == 3 && LEDS_COUNT == 4);
            for (m = 0; m < 2u; m++) {
                settings_leds = (uint8_t)ADDED[m];
                settings_export(&p);
                assert(p.zoom == (LEDS_TAG | ADDED[m]) && leds_stored_ok(p.zoom));
                q = p; settings_export(&q); assert(!memcmp(&q, &p, sizeof q));
                settings_leds = LEDS_DIM;
                assert(settings_import(&p, sizeof p) == 1 && settings_leds == ADDED[m]);
            }
            settings_leds = LEDS_DIM;
            settings_export(&p);
            assert(p.zoom == 0u);
        }
    }
#ifdef FELUCCA_FAVORITES
    {   /* 1.1 CLICK / CLICK LEVEL / COUNT-IN (ui.c ui_rec_prefs, favorites.factory[15][28]): 0 in every older record
         * (the defaults: OFF / MID / OFF); each field 0..2 kept, 3 (no value) back to 0, bits 6-7 kept; twice the same */
        persist_t q;
        uint32_t b;
        p = original; memset(&p.favorites, 0, sizeof p.favorites);            /* PER3 (no favorites): 0 */
        assert(settings_import(&p, sizeof p) == 1 && favorites.factory[15][28] == 0u);
        for (b = 0; b < 256u; b++) {
            uint32_t f, want = b;
            for (f = 0; f < 3u; f++)
                if (((b >> (2u * f)) & 3u) == 3u) want &= ~(3u << (2u * f));
            p = original; p.favorites.factory[15][28] = (uint8_t)b;
            assert(settings_import(&p, sizeof p) == 1 && favorites.factory[15][28] == want && p.favorites.factory[15][28] == want);
            q = p; assert(settings_import(&q, sizeof q) == 1 && !memcmp(&q, &p, sizeof q));
            settings_export(&q); assert(q.favorites.factory[15][28] == want);
        }
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
    {   /* 1.2 RESTORE LAST (ui.c PREF_RESTORE_OFF, bit 7 of MENU's flags, favorites.factory[15][30]): clear in every
         * older record = ON; set = OFF, kept as saved with the other flags */
        persist_t q;
        p = original; memset(&p.favorites, 0, sizeof p.favorites);
        assert(settings_import(&p, sizeof p) == 1 && !(favorites.factory[15][30] & 128u));
        p = original; p.favorites.factory[15][30] = 128u | 64u | 1u;
        assert(settings_import(&p, sizeof p) == 1 && favorites.factory[15][30] == (128u | 64u | 1u));
        q = p; settings_export(&q); assert(q.favorites.factory[15][30] == (128u | 64u | 1u));
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
    {   /* 1.2 the FX layer's key map (ui_layer.c fx_keys, favorites.factory[15][14..23], 16 x 5 bits, 0 = the key's
         * default): 0 in every older record; any bytes kept as saved (codes of a later firmware too), the bytes around
         * them untouched; twice the same */
        persist_t q;
        uint32_t i;
        p = original; memset(&p.favorites, 0, sizeof p.favorites);
        assert(settings_import(&p, sizeof p) == 1);
        for (i = 14; i < 24u; i++) assert(favorites.factory[15][i] == 0u);
        p = original;
        for (i = 14; i < 24u; i++) p.favorites.factory[15][i] = (uint8_t)(i * 37u + 5u);
        assert(settings_import(&p, sizeof p) == 1);
        for (i = 14; i < 24u; i++) assert(favorites.factory[15][i] == (uint8_t)(i * 37u + 5u));
        assert(favorites.factory[15][13] == original.favorites.factory[15][13] && favorites.factory[15][24] == original.favorites.factory[15][24]);
        memset(&q, 0, sizeof q); settings_export(&q); assert(!memcmp(&q.favorites, &p.favorites, sizeof q.favorites));
        assert(settings_import(&q, sizeof q) == 1);
        for (i = 14; i < 24u; i++) assert(favorites.factory[15][i] == (uint8_t)(i * 37u + 5u));
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
    {   /* 1.5 MIDI LEARN's map (midi_control.c, favorites.factory[14], no engine's): 0 = nothing learned in every older
         * record; kept as saved, byte for byte (an entry of a later firmware too), the rows around it untouched; an
         * export / import round trip and importing twice change nothing */
        persist_t q;
        uint32_t i;
        p = original; memset(&p.favorites, 0, sizeof p.favorites);
        assert(settings_import(&p, sizeof p) == 1);
        for (i = 0; i < 32u; i++) assert(favorites.factory[14][i] == 0u);
        p = original;
        for (i = 0; i < 32u; i++) p.favorites.factory[14][i] = (uint8_t)(i * 29u + 3u);
        assert(settings_import(&p, sizeof p) == 1);
        for (i = 0; i < 32u; i++) assert(favorites.factory[14][i] == (uint8_t)(i * 29u + 3u));
        assert(!memcmp(favorites.factory[13], original.favorites.factory[13], 32) &&
               !memcmp(favorites.factory[15], original.favorites.factory[15], 32));
        memset(&q, 0, sizeof q); settings_export(&q); assert(!memcmp(&q.favorites, &p.favorites, sizeof q.favorites));
        assert(settings_import(&q, sizeof q) == 1 && settings_import(&q, sizeof q) == 1);
        for (i = 0; i < 32u; i++) assert(favorites.factory[14][i] == (uint8_t)(i * 29u + 3u));
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
#endif
    assert(settings_import(&p, 3) == 0 && settings_import(&p, -1) == 0);
    assert(settings_import(&p, sizeof p - 1) == 0);
    puts("Settings: Felucca PER1..PER4 refused, palette ids, calibration, HOLD, LEDS, CLICK / COUNT-IN, the FX key map, MIDI LEARN's map and independent feature preservation passed.");
}
