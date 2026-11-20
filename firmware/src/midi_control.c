/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from MIDI control contribution by ChanceTheMaker (2026).
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Shared USB/TRS channel controls. Included by seq.c after its input helpers.
 * USB and TRS intentionally share channel state, matching the existing routing.
 * A synth part has one live bend/wheel state; channels assigned to the same part
 * share it (last controller wins). Drum hits ignore bend and sustain. CC1 keeps the existing matrix routing. */
typedef struct {
    int16_t bend;                           /* signed 14-bit value, zero = centre */
    uint8_t wheel, pedal, targets;
    uint8_t owned[NTRK];                    /* held/pedal notes per track, at most 128 per channel */
    uint8_t ready, semis, cents;
    uint8_t rpn_msb, rpn_lsb;
    uint8_t bank;                           /* V2: CC0 received + 1, taken by the next Program Change; 0 none */
} midi_channel_t;
static midi_channel_t midi_ch[16];
/* Low bits: track + 1. High bit: key released, held by its channel's pedal. */
static uint8_t midi_sel_on[16][128];
#define midi_notes midi_sel_on
static uint16_t midi_owners[NTRK];           /* avoids rescanning all 2048 entries for CC123 */
#define MIDI_PEDAL_NOTE 0x80u
static uint8_t mout_from_midi;               /* MIDI OUT: set while a note received is played (not sent back out) */

static midi_channel_t *midi_channel(uint32_t ch)
{
    midi_channel_t *c = &midi_ch[ch];
    if (!c->ready) {
        c->semis = 2;
        c->rpn_msb = c->rpn_lsb = 127;
        c->ready = 1;
    }
    return c;
}

static uint32_t midi_targets(uint32_t ch)
{
    return midi_channel(ch)->targets | (1u << trk_index(midi_track(ch)));
}

static void midi_expression(track_t *t, const midi_channel_t *c)
{
    int32_t range = ((int32_t)c->semis * 100 + c->cents) * 256 / 100;
    midi_bend_target[trk_index(t)] = (int32_t)c->bend * range / (c->bend < 0 ? 8192 : 8191);
}

static void midi_expression_channel(uint32_t ch)
{
    uint32_t i, mask = midi_targets(ch);
    for (i = 0; i < NPART; i++)
        if (mask & (1u << i))
            midi_expression(&trk[i], midi_channel(ch));
}

/* a MIDI note of track t sounds note: one played as itself, or a tone of one played as a chord (chord.c) */
static int midi_note_held(const track_t *t, uint32_t note)
{
    uint32_t ch, id = trk_index(t) + 1u;
    for (ch = 0; ch < 16u; ch++)
        if ((midi_notes[ch][note] & 0x7Fu) == id && !mchord_of(ch, note, id))
            return 1;
    return mchord_held(id, note);
}

/* a key held on track t sounds note (the key's own note, or a tone of its chord) */
static int midi_local_held(const track_t *t, uint32_t note)
{
    uint32_t k, i;
    for (k = 0; k < 27u; k++)
        if (kb_chn[k] && kb_trk[k] == trk_index(t))
            for (i = 0; i < kb_chn[k]; i++)
                if (kb_chord[k][i] == note)
                    return 1;
    return 0;
}

static void midi_release(uint32_t ch, uint32_t note)
{
    uint32_t id = midi_notes[ch][note] & 0x7Fu;
    midi_notes[ch][note] = 0;
    if (id) {
        midi_owners[id - 1u]--;
        if (!--midi_ch[ch].owned[id - 1u])
            midi_ch[ch].targets &= (uint8_t)~(1u << (id - 1u));
    }
    if (id) {
        mchord_t *m = mchord_of(ch, note, id);
        uint8_t nn[CHORD_MAX];
        uint32_t n = 1, i;
        nn[0] = (uint8_t)note;
        if (m) {                                  /* a chord: exactly the notes it started */
            n = m->n;
            for (i = 0; i < n; i++)
                nn[i] = m->note[i];
            m->id = 0;
        }
        for (i = 0; i < n; i++)
            if (!midi_local_held(&trk[id - 1u], nn[i]))
                input_off(&trk[id - 1u], nn[i]);  /* input_off also checks other MIDI owners */
    }
}

/* a MIDI note-on (ch, note) of track t: its chord (chord.c) or the note alone. A note another key or MIDI
 * note holds already sounds: not started again */
static void midi_play(track_t *t, uint32_t ch, uint32_t note, uint32_t vel)
{
    uint8_t nn[CHORD_MAX];
    uint32_t n = chord_build(t, note, nn), i, f = 0;
    if (n > 1u || nn[0] != note)
        for (f = 0; f < MCHORD_N && mchord[f].id; f++)
            ;
    if (f == MCHORD_N) {                          /* no room to keep a chord: the note alone */
        n = 1;
        nn[0] = (uint8_t)note;
    }
    for (i = 0; i < n; i++)
        if (!midi_note_held(t, nn[i]) && !midi_local_held(t, nn[i]))
            input_on(t, nn[i], vel);
    if (n > 1u || nn[0] != note) {
        mchord_t *m = &mchord[f];
        m->ch = (uint8_t)ch;
        m->src = (uint8_t)note;
        m->n = (uint8_t)n;
        for (i = 0; i < n; i++)
            m->note[i] = nn[i];
        m->id = (uint8_t)(trk_index(t) + 1u);
    }
}

static void midi_note_event_in(uint32_t ch, uint32_t note, uint32_t vel);
/* a note received: played as ever, never echoed to MIDI OUT (mout_note: mout_from_midi) */
static void midi_note_event(uint32_t ch, uint32_t note, uint32_t vel)
{
    uint8_t f = mout_from_midi;
    mout_from_midi = 1;
    midi_note_event_in(ch, note, vel);
    mout_from_midi = f;
}
static void midi_note_event_in(uint32_t ch, uint32_t note, uint32_t vel)
{
    midi_channel_t *c = midi_channel(ch);
    uint32_t id = midi_notes[ch][note] & 0x7Fu;
    if (vel) {
        track_t *t = midi_track(ch);
        /* Repeated notes replace the previous press, including a pedal-held one. */
        if (id)
            midi_release(ch, note);
        midi_expression(t, c);
        if (t != TSEL)
            midi_hint = (uint8_t)(trk_index(t) + 1u);
        midi_seen |= (uint8_t)(1u << trk_index(t));   /* (1.5.5: only noted; the UI does the rest) */
        c->targets |= (uint8_t)(1u << trk_index(t));
        midi_play(t, ch, note, vel);
        midi_notes[ch][note] = (uint8_t)(trk_index(t) + 1u);
        midi_owners[trk_index(t)]++;
        c->owned[trk_index(t)]++;
    } else if (id) {
        if (c->pedal)
            midi_notes[ch][note] |= MIDI_PEDAL_NOTE;
        else
            midi_release(ch, note);
    }
}

static void midi_pedal_up(uint32_t ch)
{
    uint32_t note;
    midi_channel(ch)->pedal = 0;
    for (note = 0; note < 128u; note++)
        if (midi_notes[ch][note] & MIDI_PEDAL_NOTE)
            midi_release(ch, note);
}

/* Preset/project panic and CC120 must discard ownership so a later pedal-up
 * or note-off cannot release notes subsequently started on another patch. */
static void __attribute__((noinline)) midi_forget_track(uint32_t track)
{
    uint32_t ch, note;
    for (ch = 0; ch < 16u; ch++) {
        if (!(midi_ch[ch].targets & (1u << track)))
            continue;
        for (note = 0; note < 128u; note++)
            if ((midi_notes[ch][note] & 0x7Fu) == track + 1u)
                midi_notes[ch][note] = 0;
        midi_ch[ch].targets &= (uint8_t)~(1u << track);
        midi_ch[ch].owned[track] = 0;
    }
    midi_owners[track] = 0;
    mchord_forget(track);
    midi_bend_q8[track] = midi_bend_target[track] = 0;
}

static void midi_silence_track(uint32_t track)
{
    track_t *t = &trk[track];
    uint32_t i;
    trk_all_off(t);
    t->nheld = t->arp_phys = t->rh_n = 0;
    arp_forget(t);
    t->seq_n = t->seq_hold = t->slide_glide = 0;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            voice_kill(&t->v[i]);             /* one-block fade, regardless of RELEASE */
    sl[track].rec = sl[track].loop = 0;       /* do not keep replaying captured sound */
    midi_forget_track(track);
}

static int midi_track_held(uint32_t track)
{
    uint32_t k;
    if (midi_owners[track])
        return 1;
    for (k = 0; k < 27u; k++)
        if ((fm1_in.notes & (1u << k)) && kb_trk[k] == track)
            return 1;
    return 0;
}

/* The fixed CC map (#103; FM1-Four Task 2.1): always on, these CCs only, on the track the channel plays (ROUT as notes),
 * the same numbers in and out (MIDI OUT: mout_param). A CC sets its parameter as a knob would, 0..127 over the
 * parameter's range (64: the middle of a bipolar one), and AUTOMATION records it as a knob move (motion_capture: the
 * selected track, armed and playing). CC74 71 79 70 are the EDIT screen's four controls of the track's machine
 * (engine_t.knob: CUT, RES, ENV and the machine's own), CC7 its VOLUME (LEVEL); the rest as Felucca had them. CC121
 * resets none of them (RP-015: they are the sound's) */
#define CC_VOL 7u
#define CC_CUT 74u
#define CC_RES 71u
#define CC_ENV 79u
#define CC_MAC 70u
static const uint8_t CC_KNOB[4] = {CC_CUT, CC_RES, CC_ENV, CC_MAC};   /* EDIT K1..K4 */
static const uint8_t MIDI_CC_MAP[][2] = {          /* the common rows, the same on every machine */
    {5, P_GLIDE}, {CC_VOL, P_LEVEL}, {10, P_PAN}, {72, P_REL}, {73, P_ATK}, {75, P_DEC},
    {91, P_REV}, {93, P_CHOR}, {94, P_DLY},
};
/* V2 (Phase 0.5): CC 102..109 are the machine's own eight, P_E0..P_E7, in and out on every machine (undefined in the
 * MIDI spec: they clash with nothing; a Syntakt MIDI track's 8 CC slots map onto them one to one). One of them may
 * also be one of the four EDIT controls (CUT on CC74 too): in, both set it; out, its EDIT control's CC only */
#define CC_E0 102u
/* the parameter CC cc sets on track t, -1 none */
static int32_t cc_param(const track_t *t, uint32_t cc)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (CC_KNOB[i] == cc)
            return e->knob[i];
    for (i = 0; i < NELEM(MIDI_CC_MAP); i++)
        if (MIDI_CC_MAP[i][0] == cc)
            return MIDI_CC_MAP[i][1];
    if (cc - CC_E0 < 8u)
        return (int32_t)(P_E0 + cc - CC_E0);
    return -1;
}
/* .. and the inverse: the CC that parameter id of track t goes out on, -1 none (the four controls first: a machine's
 * K4 could be a common parameter one day) */
static int32_t param_cc(const track_t *t, uint32_t id)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (e->knob[i] == id)
            return CC_KNOB[i];
    for (i = 0; i < NELEM(MIDI_CC_MAP); i++)
        if (MIDI_CC_MAP[i][1] == id)
            return MIDI_CC_MAP[i][0];
    if (id - P_E0 < 8u)
        return (int32_t)(CC_E0 + id - P_E0);
    return -1;
}
/* ---------------------------------------------------------- MIDI OUT --- */
/* FM1-Four (Task 2.2): per track P_MOUT, INT the machine only (as before: nothing sent), EXT external gear only (the
 * voice silent), BOTH. USB only (the TRS jack is an input). Track N sends on its MIDI channel (trk_midi_ch: ROUT's
 * block, TRACK its own).
 * One context: the USB out ring (usb.c midi_out_event) has one producer, the audio ISR's control block. Notes go out
 * from trk_note_on / trk_note_off (voice.c), which only the block calls (sequencer, arp, keys, chords, MIDI in; the one
 * exception, project.c's seq_stop, runs with the interrupts off, so never beside the ISR);
 * a note received is not sent back (mout_from_midi). Every note sent is remembered (mout_held) until its note-off,
 * whatever MOUT says by then, and trk_all_off ends them all, so external gear is never left with a hung note.
 * Knobs, VOLUME, a preset load and the mod wheel are never sent from the UI: mout_block compares, once per control
 * block, each mapped value with the byte last sent (a CC received sets that byte: not sent back) and sends a change;
 * a sound load (ui.c load_begin / load_end: mout_load) holds them, then sends its Program Change (t->preset, a factory
 * preset loaded on the FM-1; none for one a PC received loaded, none for a user preset) and takes the new values as
 * sent, so the external patch the PC chose is not overwritten by the FM-1's. Turning a track to EXT / BOTH sends
 * nothing until a value changes. The clock (G_CLKOUT): F8 at 24 ppqn locked to the sequencer's samples, FA on PLAY
 * (and RESTART), FC on STOP; none while the transport follows an external clock (no loop) */
#define MOUT_NMAP (4u + NELEM(MIDI_CC_MAP))         /* EDIT K1..K4, then the common rows .. */
#define MOUT_NCC (MOUT_NMAP + 8u)                   /* .. then (V2) E0..E7 on CC 102..109: a track's CCs out */
#define MOUT_CC_BLOCK 8u                            /* at most this many CCs a block (all tracks) ... */
#define MOUT_ROOM 16u                               /* .. while the ring keeps this much free for notes and the clock */
static uint8_t mout_held[NTRK][16];                 /* notes sent and not ended yet, a bit per note */
static uint8_t mout_owe[NTRK][16];                  /* .. ended, but the ring was full: their note-offs still to send */
static uint8_t mout_hch[NTRK];                      /* .. the channel they went out on */
static uint8_t mout_last[NTRK][MOUT_NCC];           /* the CC values last sent (or received) */
static uint8_t mout_mode[NTRK], mout_eng[NTRK];     /* MOUT and the machine as the block last saw them */
static volatile uint8_t mout_load[NTRK];            /* a sound load: 1 under way, 2 done (send its PC), 3 done (no PC) */
static uint8_t mout_pcw[NTRK];                      /* the load is a factory preset chosen on the FM-1 (ui.c) */
static volatile uint8_t mout_mw[NTRK];              /* MENU > SELECT MODW turned: CC1 to send (ui_input.c sel_play) */
static uint8_t mout_run;                            /* the transport as the clock last saw it */
static uint8_t mout_crun;                           /* clock out running: FA sent, FC not yet */
static uint32_t mout_cpos;                          /* clock: samples x 6 into the current 1/16 */

static void mout_send(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_out_event(st >> 4 | st << 8 | d1 << 16 | d2 << 24);
}
/* as mout_send; 0: the ring is full, nothing queued (no host: taken as sent) */
static int mout_try(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (usb.config && mo_w - mo_r >= MQ)
        return 0;
    mout_send(st, d1, d2);
    return 1;
}
static uint32_t mout_slot_param(const track_t *t, uint32_t s)
{
    return s < 4u ? ENGINES[eng_idx(t->eng_req)]->knob[s] : s < MOUT_NMAP ? MIDI_CC_MAP[s - 4u][1] : P_E0 + s - MOUT_NMAP;
}
static uint32_t mout_slot_cc(uint32_t s)
{
    return s < 4u ? CC_KNOB[s] : s < MOUT_NMAP ? MIDI_CC_MAP[s - 4u][0] : CC_E0 + s - MOUT_NMAP;
}
/* the next slot from s on of parameter id, -1 none (V2: an E value can have two, its EDIT control's and CC 102..109) */
static int32_t mout_slot_from(const track_t *t, uint32_t id, uint32_t s)
{
    for (; s < MOUT_NCC; s++)
        if (mout_slot_param(t, s) == id)
            return (int32_t)s;
    return -1;
}
static int32_t mout_slot(const track_t *t, uint32_t id) { return mout_slot_from(t, id, 0); }   /* -1 not mapped */
/* slot s sends: the first of its parameter's (an E value one of the four EDIT controls goes out on that control's CC
 * only, CUT on CC74 not also CC106: one CC per parameter out; in, both set it) */
static int mout_slot_live(const track_t *t, uint32_t s)
{
    return s < MOUT_NMAP || mout_slot(t, mout_slot_param(t, s)) == (int32_t)s;
}
/* parameter id of t as a CC value 0..127, the inverse of midi_cc_set; -1 none (no range) */
static int32_t mout_value(const track_t *t, uint32_t id)
{
    const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), id);
    int32_t v = t->p[id];
    if (d->max <= d->min)
        return -1;
    v = v < d->min ? d->min : v > d->max ? d->max : v;
    return (v - d->min) * 127 / (d->max - d->min);
}
static void mout_resync(const track_t *t)           /* every mapped value taken as sent */
{
    uint32_t s, k = trk_index(t);
    int32_t v;
    for (s = 0; s < MOUT_NCC; s++)
        if ((v = mout_value(t, mout_slot_param(t, s))) >= 0)
            mout_last[k][s] = (uint8_t)v;
}
/* parameter id of t was set by a CC received: its value is the external gear's already */
static void mout_heard(const track_t *t, uint32_t id)
{
    int32_t s, v = mout_value(t, id);
    if (v < 0)
        return;
    for (s = mout_slot(t, id); s >= 0; s = mout_slot_from(t, id, (uint32_t)s + 1u))
        mout_last[trk_index(t)][s] = (uint8_t)v;      /* (every slot of it: none sends it back) */
}
/* every note t has sent ends (note-offs on the channel they went on) */
static void mout_off_all(track_t *t)
{
    uint32_t k = trk_index(t), b, n;
    for (b = 0; b < 16u; b++)                       /* (nothing held, the usual case: 16 tests) */
        for (n = 0; mout_held[k][b] && n < 8u; n++)
            if ((mout_held[k][b] >> n) & 1u) {
                mout_held[k][b] &= (uint8_t)~(1u << n);
                if (!mout_try(0x80u | mout_hch[k], b * 8u + n, 0))
                    mout_owe[k][b] |= (uint8_t)(1u << n);   /* (the ring full: mout_block sends it when there is room) */
            }
}
/* the note-offs owed (the ring was full), as far as there is room now; 0: some still owed */
static int mout_pay(uint32_t k)
{
    uint32_t b, n;
    for (b = 0; b < 16u; b++)
        for (n = 0; mout_owe[k][b] && n < 8u; n++)
            if ((mout_owe[k][b] >> n) & 1u) {
                if (!mout_try(0x80u | mout_hch[k], b * 8u + n, 0))
                    return 0;
                mout_owe[k][b] &= (uint8_t)~(1u << n);
            }
    return 1;
}
/* note on (vel 1..127) / off (vel 0) of track t: sent if MOUT is EXT or BOTH (a note-off: if its note-on was sent) */
static void mout_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t k = trk_index(t), ch, bit = 1u << (note & 7u);
    uint8_t *h = &mout_held[k][(note >> 3) & 15u];
    if (note > 127u)
        return;
    if (!vel) {
        if (*h & bit) {
            *h &= (uint8_t)~bit;
            if (!mout_try(0x80u | mout_hch[k], note, 0))
                mout_owe[k][(note >> 3) & 15u] |= (uint8_t)bit;   /* (owed: mout_block) */
        }
        return;
    }
    if (t->p[P_MOUT] == MOUT_INT || mout_from_midi)
        return;
    ch = trk_midi_ch(k);
    if (ch != mout_hch[k]) {                         /* the channel moved: what went out before ends there */
        mout_off_all(t);
        mout_hch[k] = (uint8_t)ch;
    }
    if (mout_owe[k][(note >> 3) & 15u] & bit) {      /* its owed note-off first */
        mout_owe[k][(note >> 3) & 15u] &= (uint8_t)~bit;
        mout_send(0x80u | mout_hch[k], note, 0);
    }
    if (*h & bit)                                   /* again while held: one note-on per note-off out there */
        mout_send(0x80u | ch, note, 0);
    *h |= (uint8_t)bit;
    mout_send(0x90u | ch, note, vel > 127u ? 127u : vel);
}
/* parameter id of t out on its CC (param_cc), scaled 0..127, if MOUT sends and it is mapped. The immediate-send helper
 * the tests and briefs use; the firmware's path is mout_block (once per control block, on a change, ring room kept) */
static void mout_param(track_t *t, uint32_t id)
{
    int32_t s = mout_slot(t, id), v = s >= 0 ? mout_value(t, id) : -1;
    if (v < 0 || t->p[P_MOUT] == MOUT_INT)
        return;
    mout_heard(t, id);                                /* (every slot of it as sent: it goes out once) */
    mout_send(0xB0u | trk_midi_ch(trk_index(t)), mout_slot_cc((uint32_t)s), (uint32_t)v);
}
/* Program Change: the track's preset number; V2: after CC0 = its machine (the bank select midi_control takes in), so
 * what a DAW records chooses the same sound again */
static void mout_program(track_t *t)
{
    uint32_t ch = trk_midi_ch(trk_index(t));
    if (t->p[P_MOUT] == MOUT_INT)
        return;
    mout_send(0xB0u | ch, 0, t->eng_req % NENGINES);
    mout_send(0xC0u | ch, t->preset & 127u, 0);
}
static void mout_clock(uint32_t rt)                 /* 0xF8 / 0xFA / 0xFC (mout_block: CLOCK OUT on, INT clock) */
{
    midi_out_event(0x0Fu | rt << 8);
}
static int mout_room(void) { return MQ - (mo_w - mo_r) > MOUT_ROOM; }
/* the control block's MIDI OUT (events_block, after the sequencer): the CC map's changes, a load's PC, CC1, the clock */
static __attribute__((noinline)) void mout_block(uint32_t seq_n, uint32_t clock_mode)
{
    uint32_t k, s, sent = 0, p, on, start;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        uint32_t m = (uint32_t)t->p[P_MOUT], ld = mout_load[k];
        (void)mout_pay(k);                           /* note-offs a full ring owed (whatever MOUT is now) */
        if (m == MOUT_INT) {                         /* (the default: next to nothing each block) */
            mout_mode[k] = MOUT_INT;
            mout_mw[k] = 0;
            if (ld >= 2u)
                mout_load[k] = 0;
            continue;
        }
        if (ld == 1u)                                /* a sound load under way: its values are not changes */
            continue;
        if (mout_mw[k] && (!usb.config || mout_room())) {   /* (no room: the next block) */
            mout_mw[k] = 0;
            mout_send(0xB0u | trk_midi_ch(k), 1, t->mw & 127u);
        }
        if (ld || mout_mode[k] != m || mout_eng[k] != t->eng_req) {
            if (ld == 2u && mout_mode[k] == m) {     /* (a load before EXT / BOTH came on: its PC is not news) */
                if (usb.config && !mout_room())
                    continue;                        /* (no room: the PC stays pending, the next block) */
                mout_program(t);
            }
            mout_resync(t);                          /* (EXT / BOTH just on, another machine, a load: nothing sent) */
            mout_mode[k] = (uint8_t)m;
            mout_eng[k] = t->eng_req;
            mout_load[k] = 0;
            continue;
        }
        for (s = 0; s < MOUT_NCC && sent < MOUT_CC_BLOCK; s++) {
            int32_t v = mout_slot_live(t, s) ? mout_value(t, mout_slot_param(t, s)) : -1;
            if (v < 0 || v == mout_last[k][s])
                continue;
            if (!usb.config)                         /* (nobody listens: taken as sent) */
                mout_last[k][s] = (uint8_t)v;
            else if (mout_room()) {
                mout_last[k][s] = (uint8_t)v;
                mout_send(0xB0u | trk_midi_ch(k), mout_slot_cc(s), (uint32_t)v);
                sent++;
            }                                        /* (else: the next block) */
        }
    }
    on = song.g[G_CLKOUT] && !clock_mode;
    start = song.playing && (!mout_run || !song.tick);   /* PLAY, or RESTART (song.tick back to 0) */
    if (mout_crun && (!on || !song.playing || start)) {
        mout_clock(0xFCu);                           /* STOP; CLOCK OUT off or CLK external while playing; RESTART */
        mout_crun = 0;
    }
    if (on && song.playing) {
        if (start) {                                 /* Start, then the first pulse with step 1 */
            mout_clock(0xFAu);
            mout_clock(0xF8u);
            mout_cpos = 0;
            mout_crun = 1;
        } else if (mout_crun) {
            p = div_samples(2);                      /* a 1/16 is 6 pulses: exactly the sequencer's samples */
            mout_cpos += seq_n * 6u;
            for (s = 0; mout_cpos >= p && s < 4u; s++) {
                mout_cpos -= p;
                mout_clock(0xF8u);
            }
            if (mout_cpos >= p)                      /* (a tempo jump: no burst) */
                mout_cpos = 0;
        }                                            /* (CLOCK OUT on mid-song: from the next PLAY, with its Start) */
    }
    mout_run = song.playing;
}

/* a CC sets parameter id of track t as a knob would: 0..127 over its range */
static void midi_cc_set(track_t *t, uint32_t id, uint32_t value)
{
    const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), id);
    int32_t v = d->min + ((int32_t)value * (d->max - d->min) + 63) / 127;
    if (d->max <= d->min || t->p[id] == v)
        return;
    t->p[id] = (int16_t)v;
    (void)motion_capture(t, id, (int16_t)v);
    mout_heard(t, id);                         /* (received: not sent back out) */
}
/* MIDI LEARN (1.5, Discussion #170): up to ML_N CCs, each set to one parameter of one track, whatever channel it
 * comes on (a channel ROUT ignores stays ignored) and whichever track that channel plays. Kept with the settings in
 * the 32 bytes of favorites.factory[14] (no engine 14: favorites.c; all 0 in every older setting = nothing learned),
 * an entry two bytes, as a 16-bit value LSB first: bits 0..6 the CC, 7..8 the track, 9..15 the parameter's code, 0 =
 * an empty entry: a common parameter its P_* id + 1 (1 .. ML_E0 - 1: their ids never move, new ones go before P_E0),
 * an engine parameter ML_E0 + k for P_E0 + k (P_E0 moves when common ones are added). A learned CC no longer reaches
 * the standard map (#103) on any channel: the controller is the user's now. The CCs with a meaning of their own
 * (midi_control's cases, CC0 / 32 bank select, CC11 EXPR, CC96 / 97 data increment, CC120..127) are never learned. The UI
 * (midi_learn.c) writes the entries with the interrupts off; ml_arm / ml_heard hand a CC heard while learning to it */
#define ML_N 16u
#define ML_E0 120u
typedef char ml_ids_fit[P_E0 < ML_E0 && P_COUNT - P_E0 == 8 ? 1 : -1];
static uint8_t *const ml_tab;               /* favorites.factory[14] (midi_learn.c: a build without the UI, none) */
static volatile uint8_t *const ml_io;       /* ui.ml.io (midi_learn.c; not globals here: the ISR's data keeps its layout) */
#define ml_arm (ml_io[0])                   /* the UI waits for a CC (MIDI LEARN with a parameter picked) */
#define ml_heard (ml_io[1])                 /* .. the CC that came meanwhile, + 1 (0 none) */
static uint32_t ml_id(uint32_t code)        /* an entry's code -> P_* id, P_COUNT: none this firmware knows */
{
    return code >= ML_E0 ? P_E0 + code - ML_E0 : code && code <= P_E0 ? code - 1u : P_COUNT;
}
static int ml_free_cc(uint32_t cc)          /* (CC1 6 38 64 98..101 120..127: midi_control's own) */
{
    return cc && cc < 120u && cc != 1u && cc != 6u && cc != 11u && cc != 32u && cc != 38u && cc != 64u &&
           (cc < 96u || cc > 101u);
}
/* 1: cc is learned (set here) or taken by MIDI LEARN waiting for one */
static int __attribute__((noinline)) midi_learned(uint32_t cc, uint32_t value)
{
    uint32_t i, e, id, hit = 0;
    if (!ml_tab || !ml_free_cc(cc))
        return 0;
    if (ml_arm) {
        if (!ml_heard)
            ml_heard = (uint8_t)(cc + 1u);
        return 1;
    }
    for (i = 0; i < ML_N; i++) {
        e = ml_tab[2u * i] | (uint32_t)ml_tab[2u * i + 1u] << 8;
        if ((e >> 9) && (e & 127u) == cc) {
            if ((id = ml_id(e >> 9)) < P_COUNT)
                midi_cc_set(&trk[(e >> 7) & 3u], id, value);
            hit = 1;
        }
    }
    return hit;
}

static void __attribute__((noinline)) midi_cc_map(track_t *t, uint32_t cc, uint32_t value)
{
    int32_t id;
    if (midi_learned(cc, value))                /* (a learned CC is the user's: not the map's) */
        return;
    if ((id = cc_param(t, cc)) >= 0)
        midi_cc_set(t, (uint32_t)id, value);
}

static void midi_control(uint32_t ch, uint32_t cc, uint32_t value)
{
    midi_channel_t *c = midi_channel(ch);
    uint32_t i, mask;
    switch (cc) {
    case 0:                                        /* V2: Bank Select, the machine (0..7) or 8 USER: the next PC's */
        c->bank = (uint8_t)(value + 1u);
        break;
    case 32:                                       /* (Bank Select LSB: accepted, no meaning) */
        break;
    case 1:
        c->wheel = (uint8_t)value;
        break;
    case 120:                                      /* All Sound Off: ignores the pedal */
        mask = midi_targets(ch);
        for (i = 0; i < NTRK; i++)
            if (mask & (1u << i))
                midi_silence_track(i);
        break;
    case 123:                                      /* All Notes Off: normal releases, honours pedal */
        mask = midi_targets(ch);
        for (i = 0; i < 128u; i++)
            if (midi_notes[ch][i])
                midi_note_event(ch, i, 0);
        for (i = 0; i < NTRK; i++)
            if ((mask & (1u << i)) && !midi_track_held(i)) {
                trk_all_off(&trk[i]);
                trk[i].nheld = trk[i].arp_phys = trk[i].rh_n = 0;
                arp_forget(&trk[i]);
            }
        break;
    case 121:                                      /* Reset All Controllers, keep bend sensitivity */
        c->bend = 0;
        c->wheel = 0;
        c->rpn_msb = c->rpn_lsb = 127;
        midi_expression_channel(ch);
        midi_pedal_up(ch);
        break;
    case 64:
        if (value >= 64u)
            c->pedal = 1;
        else
            midi_pedal_up(ch);
        break;
    case 101: c->rpn_msb = (uint8_t)value; break;
    case 100: c->rpn_lsb = (uint8_t)value; break;
    case 99: case 98:                              /* NRPN selection cancels RPN data entry */
        c->rpn_msb = c->rpn_lsb = 127;
        break;
    case 6: case 38:
        if (!c->rpn_msb && !c->rpn_lsb) {           /* RPN 0: +/-0..24 semitones, 0..99 cents */
            if (cc == 6u)
                c->semis = (uint8_t)(value > 24u ? 24u : value);
            else
                c->cents = (uint8_t)(value > 99u ? 99u : value);
            midi_expression_channel(ch);
        }
        break;
    default:
        midi_cc_map(midi_track(ch), cc, value);
        break;
    }
}

/* ROUT CH1-4 listens to channels 1..4 only (1.2: CH5-8, CH9-12, CH13-16 to their four). A switch to a block from
 * SEL or another block (events_block, before the queue) lets go of what the other channels hold: their notes
 * released (also pedal-held ones), their pedal, bend, wheel and RPN selection reset, so no note can hang on a
 * channel that is no longer heard. Each part's bend then follows its own channel of the block. */
static void __attribute__((noinline)) midi_route_block(void)
{
    uint32_t ch, note;
    for (ch = 0; ch < 16u; ch++) {
        midi_channel_t *c = midi_channel(ch);
        if (midi_in_block(ch)) {
            midi_expression(midi_track(ch), c);
            continue;
        }
        for (note = 0; note < 128u; note++)
            if (midi_notes[ch][note])
                midi_release(ch, note);
        c->pedal = c->wheel = c->bank = 0;
        c->bend = 0;
        c->rpn_msb = c->rpn_lsb = 127;
    }
}

/* Program Change (1.5.1, #179 by renebohne): the track the channel plays (ROUT as for notes) takes program d1. Only
 * noted here, 0x80 | d1 per track (a later one before the main loop wins): a sound load is the main loop's work
 * (undo, motion, FM6's patch), never the audio ISR's. ui.mpc (ui.c; a build without the UI: none). V2: with it the
 * channel's bank (CC0 + 1, 0 none: the part's machine as it is), ui.mbank; the bank is the one PC's, not kept for the
 * next. MENU > MIDI > PC IN OFF: the main loop discards both (ui.c midi_pc_poll) */
static volatile uint8_t *const mpc_io;
static volatile uint8_t *const mbank_io;
/* SCENES' channel (the scenes plan rev 6, G.3): on channel 16, before ROUT's block filter, a Program Change selects scene
 * PC mod 16 (an Elektron's PROG CH SEND on CH 16: pattern A01 .. A16, B.. wrapping; at once, it comes on the pattern's
 * boundary), CC 85 a scene 0..15 (mod 16; as a GLOBAL key: playing, at the loop's end) and CC 86 the MORPH (G.6). Only
 * these three: channel 16's notes and its other messages go on as ROUT says (with CH13-16 or TRACK part 4 still plays
 * there; its Program Change there is a scene's now). Noted here, 0x80 | value, for the main loop (scene.c scene_poll) */
#define SCN_MIDI_CH 15u
#define CC_SCENE 85u
#define CC_MORPH 86u
static volatile uint8_t scn_mpc, scn_mcc, scn_mmorph;

/* Keep the occasional controller/panic dispatch outside the hot rendering loop. Channel voice messages only
 * (realtime and clock are handled in events_block, SysEx never reaches here). With ROUT CH1-4 channels
 * 5..16 are ignored entirely (with CH5-8 .. CH13-16 every channel outside the block): notes, bend, CCs (CC1/11/64, RPN, and the CC120/121/123 panic and reset),
 * channel aftertouch, Program Change, so they stay free for other instruments. */
static void __attribute__((noinline)) midi_event(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    if (ch == SCN_MIDI_CH && (st == 0xC0u || (st == 0xB0u && (d1 == CC_SCENE || d1 == CC_MORPH)))) {
        if (st == 0xC0u)
            scn_mpc = (uint8_t)(0x80u | d1);
        else if (d1 == CC_SCENE)
            scn_mcc = (uint8_t)(0x80u | d2);
        else
            scn_mmorph = (uint8_t)(0x80u | d2);
        return;
    }
    if (song.g[G_ROUTE] != 1 && !midi_in_block(ch))
        return;
    if (st == 0x90u || st == 0x80u)
        midi_note_event(ch, d1, st == 0x90u ? d2 : 0);
    else if (st == 0xE0u) {
        midi_channel(ch)->bend = (int16_t)((int32_t)(d1 | (d2 << 7)) - 8192);
        midi_expression_channel(ch);
    } else if (st == 0xB0u) {
        mod_midi(midi_track(ch), st, d1, d2);
        midi_control(ch, d1, d2);
    } else if (st == 0xD0u)
        mod_midi(midi_track(ch), st, d1, d2);
    else if (st == 0xC0u && mpc_io) {              /* Program Change: the main loop loads it (ui.c midi_pc_poll) */
        uint32_t k = trk_index(midi_track(ch));
        mbank_io[k] = midi_channel(ch)->bank;
        midi_channel(ch)->bank = 0;
        mpc_io[k] = (uint8_t)(0x80u | d1);
    }
}
