/* SPDX-License-Identifier: GPL-3.0-only */
/* SCENES (Switch Blade V2, the scenes plan d:/Switchblade/2026-11-20-scenes-and-performance.md rev 6, G.2): GLOBAL's 16
 * slots, each a whole section of a track: the four parts' sounds (engine, preset, every parameter: the machines'
 * extras are parameters too), their patterns (steps, LEN DIV SWING GATE QUANTIZE), the automation and locks, the
 * mutes and the transposes (P_MUTE, P_TRANS: parameters), and the TRANCE GATE pattern. Not the project's globals
 * (tempo, the FX buses, MIDI).
 *
 * A scene's record is a FOR4 project image (project.c proj_pack: P_COUNT and the step layout carried, refused when
 * they differ, the same checks as a project), captured as a project save captures (project_capture). FOR4's song-chain
 * bytes, which nothing reads since SONG left (2026-11-20), hold the scene's own: row 15 slot 0..1 the TRANCE GATE
 * pattern (16 bits, LE; the plan's G.5), rows 0..14 zero; count 0, so the bytes are a valid chain.
 *
 * One bank for the device (rev 6: every storage object is an A/B pair, the free store fits 16, not four projects'
 * 64): storage.c OBJ_SCENE0.. . The bank is mirrored in RAM (scn_img, .pool, read at power-on): a recall never waits
 * on the flash. A save writes the mirror at once and the flash when the transport is stopped (an erase silences the
 * audio ~40 ms): saved while playing, the slot is unsaved (scn.dirty) until STOP (scene_poll).
 *
 * Recall: stopped, at once. Playing, queued to the end of the loop (seq.c scn_tick: the longest track's, in time):
 * every track then starts its new pattern on step 1 together, in time with the clock. Another key meanwhile changes
 * the queued one; the current slot's key cancels it. A recall keeps the state just before it (scn_prev) for PREV,
 * which swaps the two (PREV again: back). NEXT: the next filled slot after the current one, queued as a key is.
 * Main loop only (scene_poll from ui_input), never the audio ISR. */
#define SCN_NONE 0xFFu
#define SCN_GATE_DEF 0xB6DBu                          /* the plan's default TRANCE GATE: x.xx x.x. xx.x x.xx (bit 0 the 1st 1/16) */
static project_store_t scn_img[SCN_SLOTS] __attribute__((section(".pool")));   /* the bank's mirror */
static project_store_t scn_prev __attribute__((section(".pool")));            /* the state before the last recall */
static project_t scn_dec __attribute__((section(".pool")));                   /* a scene decoded (main loop) */
static struct {
    uint16_t used;                                    /* slots holding a scene (mirror) */
    uint16_t dirty;                                   /* .. saved since the flash had it: written at STOP */
    uint8_t cur;                                      /* the scene now (the last recalled or saved), SCN_NONE none */
    uint8_t q;                                        /* the scene queued for the loop's end, SCN_NONE none */
    uint8_t has_prev;                                 /* scn_prev holds a state */
    uint8_t err;                                      /* the last flash write failed (retried at the next STOP) */
    uint16_t gate;                                    /* the TRANCE GATE pattern of the state now (G.5 plays it) */
} scn = {0, 0, SCN_NONE, SCN_NONE, 0, 0, SCN_GATE_DEF};

static uint16_t scn_gate_of(const project_t *p)
{
    uint16_t g = (uint16_t)(p->chain.row[15].slot[0] | p->chain.row[15].slot[1] << 8);
    return g ? g : SCN_GATE_DEF;                      /* (0: none stored, a gate that never opens: the default) */
}
/* the state now -> img (0 failed: a value FOR4 cannot hold) */
static int scn_capture(project_store_t *img)
{
    project_capture(&proj_scratch);
    proj_scratch.chain.row[15].slot[0] = (uint8_t)scn.gate;
    proj_scratch.chain.row[15].slot[1] = (uint8_t)(scn.gate >> 8);
    proj_scratch.sum = proj_sum(&proj_scratch);
    return proj_pack(img, &proj_scratch);
}
static int scene_used(uint32_t s) { return s < SCN_SLOTS && ((scn.used >> s) & 1u); }
/* for the screen (ui_graph.c): the scene now, the one queued (SCN_NONE none), the filled and unsaved slots */
static uint32_t scene_cur(void) { return scn.cur; }
static uint32_t scene_queued(void) { return scn.q; }
static uint32_t scene_mask(void) { return scn.used | (uint32_t)scn.dirty << 16; }

/* a decoded scene becomes the four parts (not the globals). Playing, called while the tracks wait at their top (seq.c
 * scn_due), so no step is half old, half new. A part that keeps its machine keeps its voices (they finish on the new
 * values); one that changes it fades through the engine switch */
static void scn_apply(const project_t *p)
{
    uint32_t k, i;
    mom_back(0);                                      /* (MOMENTARY: the values as they were, then the load) */
    fm1_irq_off();
    motion = p->motion;
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = 0;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = s->engine % NENGINES;
        int16_t voice = t->p[P_VOICE];
        t->eng_req = (uint8_t)e;
        t->user = 0;
        for (i = 0; i < P_COUNT; i++)
            t->p[i] = (int16_t)param_fit(param_desc_of(e, i), s->p[i]);
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset == PROJ_DEF_SOUND ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        memcpy(t->step, s->step, sizeof t->step);
        proj_steps(t->step);
        if (t->p[P_VOICE] != voice)
            panic_req |= (uint8_t)(1u << k);          /* (MONO / POLY changed: what sounds lets go) */
        mout_load[k] = (uint8_t)(t->p[P_MOUT] != MOUT_INT ? 2u : 3u);   /* MIDI OUT: its machine's PC, as a load's */
    }
    fm1_irq_on();
    scn.gate = scn_gate_of(p);
    pat_from_set(0);                                  /* (the header's PROJECT letter: none) */
    undo.trk = 0;                                     /* (ui.c: the undo copy was another scene's) */
    for (k = 0; k < NTRK; k++)
        pat_sig[k] = ~steps_sig(&trk[k]);
    sync_reload = 1;
    ui.force = 1;
}
/* img (a scene, or the PREV state) -> the parts; 0 = not one */
static int scn_load_img(const project_store_t *img)
{
    if (!proj_import(&scn_dec, img, sizeof *img))
        return 0;
    proj_bound(&scn_dec);
    scn_apply(&scn_dec);
    return 1;
}
static void scn_say(const char *what, uint32_t s)
{
    char b[12] = "SCENE 00";
    b[6] = (char)('0' + (s + 1u) / 10u);
    b[7] = (char)('0' + (s + 1u) % 10u);
    ui_say(b, what);
}
/* slot s becomes the music now (stopped, or the tracks waiting at their top); the state before it kept for PREV */
static void scn_switch(uint32_t s)
{
    if (scn_capture(&scn_prev))
        scn.has_prev = 1;
    if (scn_load_img(&scn_img[s])) {
        scn.cur = (uint8_t)s;
        scn_say("", s);
    }
}

/* GLOBAL: a white key: recall slot s. Stopped, at once; playing, queued to the loop's end (another key changes the
 * queued one; the current slot, with one queued, cancels it). An empty slot: nothing */
static void scene_recall(uint32_t s)
{
    if (s >= SCN_SLOTS)
        return;
    if (!scene_used(s)) {
        scn_say(" EMPTY", s);
        return;
    }
    if (song.playing || seq_counting()) {
        if (s == scn.cur && scn.q != SCN_NONE) {
            scn.q = SCN_NONE;
            scn_arm = 0;
            ui_message("CANCELLED");
        } else if (s != scn.cur || scn.q != SCN_NONE) {
            scn.q = (uint8_t)s;
            scn_arm = 1;
            ui.force = 1;
        }
        return;
    }
    scn.q = SCN_NONE;
    scn_switch(s);
}
/* .. at once while playing too (a Program Change: it arrives on the controller's pattern boundary; G.3) */
static void scene_recall_now(uint32_t s)
{
    if (!scene_used(s))
        return;
    if (song.playing) {
        scn.q = (uint8_t)s;
        scn_arm = 2;
    } else
        scn_switch(s);
}
/* NEXT: the next filled slot after the current one (wrapping), as a key */
static void scene_next(void)
{
    uint32_t i, from = scn.q != SCN_NONE ? scn.q : scn.cur != SCN_NONE ? scn.cur : SCN_SLOTS - 1u;
    for (i = 1; i <= SCN_SLOTS; i++) {
        uint32_t s = (from + i) % SCN_SLOTS;
        if (scene_used(s)) {
            scene_recall(s);
            return;
        }
    }
    ui_message("NO SCENES");
}
/* PREV: the state before the last recall, and the one now kept in its place (PREV again: back) */
static void scene_prev(void)
{
    if (!scn.has_prev) {
        ui_message("NO PREVIOUS");
        return;
    }
    if (!scn_capture(&proj_wire))                     /* (proj_wire: a free 3840 B while the swap is made) */
        return;
    proj_wire_gen++;
    if (scn_load_img(&scn_prev)) {
        memcpy(&scn_prev, &proj_wire, sizeof scn_prev);
        scn.q = SCN_NONE;
        scn_arm = 0;
        scn.cur = SCN_NONE;
        ui_message("PREVIOUS");
    }
}

#if FELUCCA_FLASH
static int scn_flash(uint32_t s)                      /* the mirror of slot s -> flash (stopped only): 0 done */
{
    if (!flash_ok)
        return 0;
    return st_save(OBJ_SCENE0 + s, &scn_img[s], scene_used(s) ? sizeof scn_img[s] : 0u) != 0;
}
#endif
/* GLOBAL: SHIFT (OCT+) + a white key: the four parts -> slot s (the mirror now; the flash now, or at STOP) */
static void scene_save(uint32_t s)
{
    if (s >= SCN_SLOTS)
        return;
    if (!scn_capture(&scn_img[s])) {
        ui_message("SAVE ERROR");
        return;
    }
    scn.used |= (uint16_t)(1u << s);
    scn.cur = (uint8_t)s;
    scn.dirty |= (uint16_t)(1u << s);
#if FELUCCA_FLASH
    if (!transport_busy()) {
        if (scn_flash(s)) {
            scn.err = 1;
            ui_message("SAVE ERROR");
            return;
        }
        scn.dirty &= (uint16_t)~(1u << s);
    }
#else
    scn.dirty = 0;
#endif
    scn_say(" SAVED", s);
    if (scn.dirty & (1u << s))
        str_cpy(ui.msg2, "TO FLASH AT STOP", sizeof ui.msg2);   /* (playing: the mirror now, the flash at STOP) */
}

/* main loop, once a pass: a queued scene at the loop's end (or at once, stopped meanwhile), then one unsaved slot to
 * the flash when stopped */
static uint8_t scn_morph;                             /* CC 86 on channel 16 (0..127): MORPH's amount (G.6 plays it) */
static void scene_poll(void)
{
    uint32_t v;
    fm1_irq_off();                                    /* channel 16 (midi_control.c): a PC, CC 85, CC 86 */
    v = (uint32_t)scn_mpc << 16 | (uint32_t)scn_mcc << 8 | scn_mmorph;
    scn_mpc = scn_mcc = scn_mmorph = 0;
    fm1_irq_on();
    if (v & 0x80u)
        scn_morph = (uint8_t)(v & 127u);
    if (!(ui_prefs2 & (PREF_PCIN_OFF >> 16))) {       /* (MENU > MIDI > PC IN OFF: scenes' too) */
        if ((v >> 16) & 0x80u)
            scene_recall_now((v >> 16) & 15u);        /* Program Change: at once (on the controller's boundary) */
        if ((v >> 8) & 0x80u)
            scene_recall((v >> 8) & 15u);             /* CC 85: as a key */
    }
    if (scn.q != SCN_NONE) {
        if (scn_due == 1u) {                          /* the tracks wait at their top: load it, hand them back */
            uint32_t s = scn.q;
            scn.q = SCN_NONE;
            scn_switch(s);
            scn_due = 2;
        } else if (!song.playing && !seq_counting()) {   /* stopped meanwhile: at once */
            uint32_t s = scn.q;
            scn.q = SCN_NONE;
            scn_arm = 0;
            scn_switch(s);
        }
    }
#if FELUCCA_FLASH
    if (scn.dirty && !transport_busy()) {
        uint32_t s;
        for (s = 0; s < SCN_SLOTS && !((scn.dirty >> s) & 1u); s++) ;
        if (!scn_flash(s)) {
            scn.dirty &= (uint16_t)~(1u << s);
            scn.err = 0;
        } else
            scn.err = 1, scn.dirty &= (uint16_t)~(1u << s);   /* (failed: kept in RAM; the next save tries again) */
    }
#endif
}

/* power-on (main.c, after the flash is up): the bank from the flash into the mirror */
static void scene_boot(void)
{
    uint32_t s;
    scn.used = scn.dirty = 0;
    scn.cur = scn.q = SCN_NONE;
    scn.has_prev = 0;
    scn.gate = SCN_GATE_DEF;
    for (s = 0; s < SCN_SLOTS; s++) {
        int n = -1;
#if FELUCCA_FLASH
        if (flash_ok)
            n = st_load(OBJ_SCENE0 + s, &scn_img[s], sizeof scn_img[s]);
#endif
        if (n == (int)sizeof scn_img[s] && proj_import(&scn_dec, &scn_img[s], n))
            scn.used |= (uint16_t)(1u << s);
        else
            memset(&scn_img[s], 0, sizeof scn_img[s]);
    }
}
