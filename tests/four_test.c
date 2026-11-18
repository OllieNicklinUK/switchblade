/* four_test.c: the machine contract (V2: eight machines, 50 presets each) */
#define ENGINES_QUAL
#define main hostsim_main
#include "hostsim.c"
#undef main
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void test_engine_table(void)
{
    static const char *const NAME[8] = {"OB-Z", "WAVE", "NANO", "JUNO", "JP-Z", "CZ-Z", "GW-Z", "MICRO"};
    uint32_t e;
    CHECK(NENGINES == 8u && NELEM(NAME) == NENGINES, "NENGINES %u", (unsigned)NENGINES);   /* (V2: the ids 0..7 are stored: never reordered) */
    for (e = 0; e < NENGINES; e++) {
        CHECK(strcmp(ENGINES[e]->name, NAME[e]) == 0, "engine %u is %s", (unsigned)e, ENGINES[e]->name);
        CHECK(ENGINES[e]->knob[0] == P_E4, "%s K1 not CUT", NAME[e]);
        CHECK(ENGINES[e]->knob[1] == P_E5, "%s K2 not RES", NAME[e]);
        CHECK(ENGINES[e]->knob[2] == P_ED_FLT, "%s K3 not ENV", NAME[e]);
    }
    CHECK(ENGINES[ENGI_OBX]->knob[3] == P_E1, "OBX K4");
    CHECK(ENGINES[ENGI_WAVE]->knob[3] == P_E1, "WAVE K4");
    CHECK(ENGINES[ENGI_ACID]->knob[3] == P_E2, "ACID K4");
    CHECK(ENGINES[ENGI_DRIFT]->knob[3] == P_E6, "DRIFT K4");
    CHECK(ENGINES[ENGI_JPZ]->knob[3] == P_E0, "JP-Z K4 (DTN)");
    CHECK(ENGINES[ENGI_CZZ]->knob[3] == P_E7, "CZ-Z K4 (DCW ENV)");
    CHECK(ENGINES[ENGI_GWZ]->knob[3] == P_E7, "GW-Z K4 (ROTOR)");
    CHECK(ENGINES[ENGI_MICRO]->knob[3] == P_E2, "MICRO K4 (ENV -> POS)");
    CHECK(ENGINES[ENGI_ACID]->poly == 1u, "ACID mono");
    {   /* NANO's SUB model (2026-10-21): the module's own envelopes, SUB_POLY voices; the 303 its own envelope, one */
        track_t t;
        memset(&t, 0, sizeof t);
        t.engine = t.eng_req = ENGI_ACID;
        t.p[P_RSV_LN + 1] = 127;
        CHECK(ENGINES[ENGI_ACID]->ownenv_of(&t) == 1 && ENGINES[ENGI_ACID]->poly_of(&t) == 1u, "NANO 303: own envelope, mono");
        t.p[P_RSV_LN + 1] = 1;
        CHECK(ENGINES[ENGI_ACID]->ownenv_of(&t) == 1 && ENGINES[ENGI_ACID]->poly_of(&t) == 4u, "NANO SUB: the module's own envelopes, 4 voices");
        CHECK(NANO_PX[0][0] == 0 && NANO_PX[1][0] == 0 && NANO_PX[2][0] == 1 && NANO_PX[31][0] == 1, "NANO presets 1-2 the 303, 3-32 SUB");
    }
}

static void test_presets(void)
{
    uint32_t e, k;
    for (e = 0; e < NENGINES; e++) {
        CHECK(ENGINES[e]->npresets == NPRESET && NPRESET == 50u, "%s has %u presets", ENGINES[e]->name, ENGINES[e]->npresets);
        for (k = 0; k < ENGINES[e]->npresets; k++) {
            uint32_t c = preset_cat(e, k);
            CHECK(c >= CAT_BASS && c < NCAT, "%s/%u no category", ENGINES[e]->name, (unsigned)k);
            CHECK((c == CAT_ACID) == (e == ENGI_ACID && !NANO_PX[k][0]), "%s/%s: ACID only on NANO's 303 presets",
                  ENGINES[e]->name, ENGINES[e]->presets[k].name);
            CHECK(strlen(ENGINES[e]->presets[k].name) <= 12u, "name too long");
        }
    }
    CHECK(NELEM(WAVE_PX) == NPRESET, "WAVE_PX %u", (unsigned)NELEM(WAVE_PX));
    {   /* DRIFT's chorus (Task 6.2): DRIFT_PX[k] is preset k's CHOR (0 OFF, 1 I, 2 II, 3 I+II); a load sets it, any other engine's load resets it */
        uint32_t nb = 0, nk = 0, non = 0;
        CHECK(NELEM(DRIFT_PX) == NPRESET, "DRIFT_PX %u", (unsigned)NELEM(DRIFT_PX));
        for (k = 0; k < 32u && k < NELEM(DRIFT_PX); k++) {
            uint32_t c = preset_cat(ENGI_DRIFT, k);
            CHECK(DRIFT_PX[k] <= 3u, "DRIFT_PX[%u] = %u", (unsigned)k, (unsigned)DRIFT_PX[k]);
            non += DRIFT_PX[k] != 0u;
            nb += c == CAT_BASS && DRIFT_PX[k] == 0u;
            nk += c == CAT_KEYS && (DRIFT_PX[k] == 1u || DRIFT_PX[k] == 2u);
            host_tracks_init();
            host_preset(&trk[0], ENGI_DRIFT, k);
            CHECK(trk[0].p[P_RSV_LN] == DRIFT_PX[k], "DRIFT %s loads CHOR %d, wants %u", ENGINES[ENGI_DRIFT]->presets[k].name, trk[0].p[P_RSV_LN], (unsigned)DRIFT_PX[k]);
            host_preset(&trk[0], ENGI_OBX, 0);
            CHECK(trk[0].p[P_RSV_LN] == 0, "an OB-Z load leaves CHOR %d", trk[0].p[P_RSV_LN]);
        }
        CHECK(nb >= 9u, "basses mostly OFF: %u of 12", (unsigned)nb);
        CHECK(nk == 10u, "every DRIFT keys preset has I or II: %u of 10", (unsigned)nk);
        CHECK(non >= 14u && non <= 24u, "%u DRIFT presets with the chorus on", (unsigned)non);
    }
}

/* an engine that runs its own envelope (ACID): amp is constant, the voice ends when done says so, not when the ADSR would */
static int own_done, own_renders;
static int32_t own_amp0, own_amp1;
static int32_t own_amp(const track_t *t, const voice_t *v) { (void)t; (void)v; return 0x4000; }
static int own_isdone(const track_t *t, const voice_t *v) { (void)t; (void)v; return own_done; }
static void own_note_on(track_t *t, voice_t *v) { (void)t; (void)v; }
static int32_t own_stream[4096];                        /* what the stub rendered: a DC level ramped amp0 -> amp1 per block */
static uint32_t own_nstream;
static int32_t own_level = 0x4000;
static int32_t own_amp_level(const track_t *t, const voice_t *v) { (void)t; (void)v; return own_level; }
static void own_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t i;
    (void)t; (void)v;
    own_renders++;
    own_amp0 = m->amp0;
    own_amp1 = m->amp1;
    for (i = 0; i < n; i++)
        out[i] += m->amp0 + (m->amp1 - m->amp0) * (int32_t)(i + 1u) / (int32_t)n;
}

static void test_ownenv(void)
{
    static engine_t stub;
    const engine_t *saved = ENGINES[ENGI_ACID];
    track_t *t = &trk[0];
    int32_t o[CTL];
    uint32_t b, r0;
    stub = *ENGINES[ENGI_OBX];
    stub.ownenv = 1;
    stub.amp = own_amp;
    stub.done = own_isdone;
    stub.note_on = own_note_on;
    stub.render = own_render;
    stub.mem = 0;
    ENGINES[ENGI_ACID] = &stub;
    host_tracks_init();
    host_preset(t, ENGI_OBX, 5);
    t->p[P_VOICE] = V_LEGATO;                          /* one voice, v[0] (OB-Z leads are POLY since 2026-10-20) */
    t->engine = t->eng_req = ENGI_ACID;
    t->p[P_ATK] = 0;
    t->p[P_REL] = 0;                                    /* the fastest release: the ADSR would end it in a few blocks */
    t->p[P_LD_AMP] = 0;
    own_done = own_renders = 0;
    trk_note_on(t, 60, 100);
    for (b = 0; b < 8; b++)
        track_render(t, o, CTL);
    CHECK(own_renders == 8 && own_amp1 == 0x4000 && own_amp0 == 0x4000, "ownenv: amp %d/%d renders %d", (int)own_amp0, (int)own_amp1, own_renders);
    trk_note_off(t, 60);
    for (b = 0; b < 2000; b++)                          /* ~1.4 s past the note-off */
        track_render(t, o, CTL);
    CHECK(t->v[0].active && own_amp1 == 0x4000, "ownenv: ended with the track ADSR (active %d amp %d)", t->v[0].active, (int)own_amp1);
    own_done = 1;
    r0 = (uint32_t)own_renders;
    track_render(t, o, CTL);
    CHECK(!t->v[0].active && (uint32_t)own_renders == r0 + 1u, "ownenv: still active when done said so");
    ENGINES[ENGI_ACID] = saved;
}

/* render one block and append it to the stream (the stub's DC level: a cut from full amplitude is a step of 0x4000) */
static void own_block(track_t *t)
{
    int32_t o[CTL];
    uint32_t i;
    track_render(t, o, CTL);
    for (i = 0; i < CTL && own_nstream < NELEM(own_stream); i++)
        own_stream[own_nstream++] = o[i];
}

static int32_t own_max_step(void)
{
    uint32_t i;
    int32_t m = 0;
    for (i = 1; i < own_nstream; i++) {
        int32_t d = own_stream[i] - own_stream[i - 1];
        m = d < 0 ? (d < -m ? -d : m) : (d > m ? d : m);
    }
    return m;
}

static void own_setup(track_t *t, engine_t *stub)
{
    *stub = *ENGINES[ENGI_OBX];
    stub->ownenv = 1;
    stub->amp = own_amp_level;
    stub->done = own_isdone;
    stub->note_on = own_note_on;
    stub->render = own_render;
    stub->mem = 0;
    ENGINES[ENGI_ACID] = stub;
    host_tracks_init();
    host_preset(t, ENGI_OBX, 5);
    t->p[P_VOICE] = V_LEGATO;                          /* one voice, v[0] (OB-Z leads are POLY since 2026-10-20) */
    t->engine = t->eng_req = ENGI_ACID;
    t->p[P_ATK] = 0;
    t->p[P_REL] = 0;
    t->p[P_LD_AMP] = 0;
    own_done = own_renders = 0;
    own_nstream = 0;
    own_level = 0x4000;
}

/* a voice killed (steal, shed, all-off: voice_kill) or finished (done) gets one last block that ramps to 0 */
static void test_ownenv_ramp_out(void)
{
    static engine_t stub;
    const engine_t *saved = ENGINES[ENGI_ACID];
    track_t *t = &trk[0];
    uint32_t b;
    int32_t ramp = 0x4000 / (int32_t)CTL * 2;          /* a ramp over one block is 0x4000 / CTL per sample */

    own_setup(t, &stub);                               /* (a) kill mid-note */
    trk_note_on(t, 60, 100);
    for (b = 0; b < 8; b++)
        own_block(t);
    voice_kill(&t->v[0]);
    own_block(t);
    CHECK(own_amp0 == 0x4000 && own_amp1 == 0, "ownenv kill: last block %d -> %d", (int)own_amp0, (int)own_amp1);
    CHECK(!t->v[0].active, "ownenv kill: not freed after its last block");
    own_block(t);
    own_block(t);
    CHECK(own_max_step() <= ramp, "ownenv kill: max step %d (cut from full amplitude)", (int)own_max_step());

    own_setup(t, &stub);                               /* (b) done turns 1 on the tick the engine's amp reaches 0 */
    trk_note_on(t, 60, 100);
    for (b = 0; b < 8; b++)
        own_block(t);
    trk_note_off(t, 60);
    own_done = 1;                                      /* amp() would still say 0x4000 */
    own_block(t);
    CHECK(own_amp0 == 0x4000 && own_amp1 == 0, "ownenv done: last block %d -> %d", (int)own_amp0, (int)own_amp1);
    CHECK(!t->v[0].active, "ownenv done: not freed after its last block");
    own_block(t);
    own_block(t);
    CHECK(own_max_step() <= ramp, "ownenv done: max step %d (cut from full amplitude)", (int)own_max_step());

    own_setup(t, &stub);                               /* (c) done on an already silent voice: freed, nothing rendered */
    own_level = 0;
    trk_note_on(t, 60, 100);
    for (b = 0; b < 4; b++)
        own_block(t);
    own_done = 1;
    b = (uint32_t)own_renders;
    own_block(t);
    CHECK(!t->v[0].active && (uint32_t)own_renders == b, "ownenv silent done: active %d renders +%d", t->v[0].active,
          own_renders - (int)b);
    ENGINES[ENGI_ACID] = saved;
}

#ifndef NO_SHED_TEST
/* shed_pick ranks an own-envelope voice by its real level, not by a v->env nothing updates */
static void test_ownenv_shed(void)
{
    static engine_t stub;
    const engine_t *saved = ENGINES[ENGI_ACID];
    track_t *t = &trk[0], *t1 = &trk[1];
    voice_t *adsr;
    uint32_t b;

    own_setup(t, &stub);
    host_preset(t1, ENGI_OBX, 5);
    t1->p[P_VOICE] = V_LEGATO;                         /* its note on v[0] (OB-Z leads are POLY since 2026-10-20) */
    t1->engine = t1->eng_req = ENGI_OBX;
    t1->p[P_ATK] = 0;
    t1->p[P_SUS] = 32767;
    t1->p[P_REL] = 100;                                /* a long release: a loud tail */
    own_level = 0x6000;                                /* loud 303 tail */
    trk_note_on(t, 60, 100);
    trk_note_on(t1, 64, 100);
    for (b = 0; b < 40; b++) {
        own_block(t);
        own_block(t1);
    }
    adsr = &t1->v[0];
    trk_note_off(t, 60);                               /* both released */
    trk_note_off(t1, 64);
    own_block(t);
    own_block(t1);
    adsr->env = 1 << 20;                               /* the ADSR tail is quieter than the loud 303 */
    CHECK(shed_pick() == adsr, "shed: loud ownenv voice shed before a quieter ADSR tail (env %d)", (int)t->v[0].env);
    own_level = 0x0100;                                /* the 303 tail dies away */
    own_block(t);
    adsr->env = 1 << 22;
    CHECK(shed_pick() == &t->v[0], "shed: quiet ownenv tail not shed first (env %d)", (int)t->v[0].env);
    ENGINES[ENGI_ACID] = saved;
}

#endif

int main(void)
{
    test_engine_table();
    test_presets();
    test_ownenv();
    test_ownenv_ramp_out();
#ifndef NO_SHED_TEST
    test_ownenv_shed();
#endif
    printf(fails ? "four_test: %d FAIL\n" : "four_test: ok\n", fails);
    return fails != 0;
}
