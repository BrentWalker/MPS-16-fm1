/* ChordSmith FM: chords and phrases on 16 pads, for the M-VAVE FM-1. Built on fm1-base.
 * Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence of this file: to be chosen (see README).
 *
 * Milestone 1: the 16 white keys are pads holding the key's chords, the first seven black keys are its scale
 * degrees (bass notes), the keys light up (white LEDs: loaded pads dim, a playing pad and its root full), a small
 * polyphonic synth sounds them, and the notes go out over USB-MIDI. ALGORITHM turns the key's root; held with OCT-
 * or OCT+ it turns the scale. OCT- / OCT+ alone step the octave. SAVE keeps the key, scale and octave.
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. */
#include "fastmath.h"

/* ================================================================ settings === */
#define SET_MAGIC 0x43534631u            /* "CSF1" */
typedef struct {
    uint32_t magic;
    uint8_t root;                        /* 0 = C .. 11 = B */
    uint8_t scale;                       /* SCALES[] */
    int8_t octave;                       /* -2 .. +2 */
    uint8_t rsv;
} settings_t;
static settings_t set;
static uint8_t set_dirty;

/* ================================================================== theory === */
#define NSCALES 9u
static const char *const SCALE_NAME[NSCALES] = {"MINOR", "MAJOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN",
                                                "LOCRIAN", "HARM MINOR", "MEL MINOR"};
static const char *const SCALE_SHORT[NSCALES] = {"MIN", "MAJ", "DOR", "PHR", "LYD", "MIX", "LOC", "HMIN", "MMIN"};
static const uint8_t SCALES[NSCALES][7] = {
    {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 1, 3, 5, 6, 8, 10}, {0, 2, 3, 5, 7, 8, 11},
    {0, 2, 3, 5, 7, 9, 11}};

/* chord qualities by their intervals above the root (bit i = i semitones), longest first */
typedef struct {
    uint16_t mask;
    const char *name;
} quality_t;
#define IV(a, b, c, d, e) ((uint16_t)((1u << (a)) | (1u << (b)) | (1u << (c)) | (1u << (d)) | (1u << (e))))
static const quality_t QUAL[] = {
    {IV(0, 4, 7, 11, 2), "maj9"}, {IV(0, 3, 7, 10, 2), "m9"}, {IV(0, 4, 7, 10, 2), "9"},
    {IV(0, 4, 7, 10, 1), "7b9"}, {IV(0, 3, 7, 10, 1), "m7b9"}, {IV(0, 3, 6, 10, 1), "m7b5b9"},
    {IV(0, 3, 6, 10, 2), "m9b5"}, {IV(0, 3, 7, 11, 2), "mMaj9"}, {IV(0, 4, 8, 11, 2), "maj9#5"},
    {IV(0, 3, 6, 9, 1), "dim7b9"}, {IV(0, 4, 6, 11, 2), "maj9#11"}, {IV(0, 4, 7, 11, 11), "maj7"},
    {IV(0, 4, 7, 10, 10), "7"}, {IV(0, 3, 7, 10, 10), "m7"}, {IV(0, 3, 7, 11, 11), "mMaj7"},
    {IV(0, 3, 6, 10, 10), "m7b5"}, {IV(0, 3, 6, 9, 9), "dim7"}, {IV(0, 4, 8, 11, 11), "maj7#5"},
    {IV(0, 4, 6, 11, 11), "maj7b5"}, {IV(0, 5, 7, 10, 10), "7sus4"}, {IV(0, 4, 7, 7, 7), ""},
    {IV(0, 3, 7, 7, 7), "m"}, {IV(0, 3, 6, 6, 6), "dim"}, {IV(0, 4, 8, 8, 8), "aug"},
    {IV(0, 5, 7, 7, 7), "sus4"}, {IV(0, 2, 7, 7, 7), "sus2"}};
#define NQUAL (sizeof QUAL / sizeof QUAL[0])

/* ==================================================================== kit === */
#define NPADS 16u
#define MAXN 6u
typedef struct {
    uint8_t n;                           /* notes in the chord */
    uint8_t note[MAXN];                  /* MIDI notes, before the octave */
    uint8_t deg;                         /* the root's scale degree, 0..6 */
    uint8_t root_pc;                     /* the root's pitch class */
    char qual[10];                       /* "m7b5" */
    char nm[MAXN][4];                    /* each note spelled in the key ("C#", "Bb") */
    char roman[6];                       /* "ii" */
    uint8_t dim;                         /* the roman numeral takes a degree sign */
} pad_t;
static pad_t pads[NPADS];

/* the white keys and black keys by colour, left to right (key 0 = F3) */
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;

static int32_t base_note(void)           /* the key's root, around C4: 54 .. 65 */
{
    int32_t b = 48 + set.root;
    if (b > 53)
        b -= 12;
    return b + 12;
}
static int32_t deg_pitch(int32_t j)      /* scale degree j (any: wraps into octaves) as a MIDI note */
{
    const uint8_t *sc = SCALES[set.scale];
    int32_t o = j >= 0 ? j / 7 : -((6 - j) / 7);
    return base_note() + sc[j - 7 * o] + 12 * o;
}

/* spelling: the key's notes take consecutive letters from the tonic's, so a key never mixes sharps and flats */
static const uint8_t LETTER_PC[7] = {0, 2, 4, 5, 7, 9, 11};
static int minorish(void) { return SCALES[set.scale][2] == 3u; }
static uint32_t tonic_letter(void)
{
    static const uint8_t MAJ[12] = {0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6};   /* C Db D Eb E F F# G Ab A Bb B */
    static const uint8_t MIN[12] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6};   /* C C# D Eb E F F# G G# A Bb B */
    return (minorish() ? MIN : MAJ)[set.root];
}
static void spell(uint32_t pc, uint32_t letter, char *out)   /* pitch class pc on letter (0 = C): "F#", "Bb" */
{
    int32_t d = (int32_t)((pc + 18u - LETTER_PC[letter % 7u]) % 12u) - 6;
    out[0] = "CDEFGAB"[letter % 7u];
    out[1] = d > 0 ? '#' : d < 0 ? 'b' : 0;
    out[2] = (d > 1 || d < -1) ? out[1] : 0;
    out[3] = 0;
}
static void tonic_name(char *out) { spell(set.root, tonic_letter(), out); }

static void str_set(char *d, const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; i + 1u < max && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

static void name_chord(pad_t *p)
{
    uint32_t i, mask = 0;
    for (i = 0; i < p->n; i++)
        mask |= 1u << ((p->note[i] - p->root_pc + 120u) % 12u);
    for (i = 0; i < NQUAL; i++)
        if (QUAL[i].mask == mask) {
            str_set(p->qual, QUAL[i].name, sizeof p->qual);
            return;
        }
    /* none matched: the triad's quality, then "+" for the extensions */
    str_set(p->qual, (mask & 1u << 3) ? ((mask & 1u << 6) ? "dim+" : "m+") : "+", sizeof p->qual);
}

static void roman_for(pad_t *p)
{
    static const char *const UP[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const char *const LO[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    const uint8_t *sc = SCALES[set.scale];
    uint32_t d = p->deg, r = sc[d];
    uint32_t third = (sc[(d + 2u) % 7u] + 12u - r) % 12u, fifth = (sc[(d + 4u) % 7u] + 12u - r) % 12u;
    p->dim = 0;
    if (third == 4u) {
        str_set(p->roman, UP[d], sizeof p->roman);
        if (fifth == 8u)
            str_set(p->roman + __builtin_strlen(UP[d]), "+", 2);
    } else {
        str_set(p->roman, LO[d], sizeof p->roman);
        p->dim = fifth == 6u;
    }
}

/* the pads: 1-7 the seventh chords on degrees 1-7, 8 the first an octave up, 9-15 the ninth chords, 16 sus4 on V */
static void kit_build(void)
{
    static const uint8_t SEVENTH[] = {0, 2, 4, 6}, NINTH[] = {0, 2, 4, 6, 8}, SUS4[] = {0, 3, 4};
    uint32_t i, k;
    for (i = 0; i < NPADS; i++) {
        pad_t *p = &pads[i];
        const uint8_t *shape = SEVENTH;
        uint32_t n = 4, deg = i;
        if (i >= 8u && i < 15u) {
            shape = NINTH;
            n = 5;
            deg = i - 8u;
        } else if (i == 15u) {
            shape = SUS4;
            n = 3;
            deg = 4;
        }
        p->n = (uint8_t)n;
        for (k = 0; k < n; k++) {
            p->note[k] = (uint8_t)deg_pitch((int32_t)(deg + shape[k]));
            spell(p->note[k] % 12u, tonic_letter() + deg + shape[k], p->nm[k]);
        }
        p->deg = (uint8_t)(deg % 7u);
        p->root_pc = (uint8_t)(p->note[0] % 12u);
        name_chord(p);
        roman_for(p);
    }
}

/* ================================================================== synth === */
/* a small polyphonic voice: two detuned saws through two one-pole low-passes, an envelope per voice. The main loop
 * asks for notes through a ring of events; the audio interrupt owns the voices. */
#define NVOICES 12u
#define SR 44100.0f
typedef struct {
    int8_t note;                         /* -1 free */
    uint8_t gate;
    uint8_t stage;                       /* 0 off, 1 attack, 2 decay/sustain, 3 release */
    float env, vel, p1, p2, inc, lp1, lp2;
    uint32_t age;
} voice_t;
static voice_t vo[NVOICES];
static uint32_t vo_clock;
static float gain_now;

#define QN 128u
static volatile uint32_t q_ev[QN];
static volatile uint32_t q_w, q_r;
static void ev_push(uint32_t on, uint32_t note, uint32_t vel)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                          /* full: drop (never seen at human speed) */
    q_ev[w % QN] = on << 16 | (note & 127u) << 8 | (vel & 127u);
    __atomic_store_n(&q_w, w + 1u, __ATOMIC_RELEASE);
}

static void voice_on(uint32_t note, uint32_t vel)
{
    uint32_t i, best = 0, oldest = 0xFFFFFFFFu;
    for (i = 0; i < NVOICES; i++) {      /* a free voice, else the oldest one in release, else the oldest */
        uint32_t score = vo[i].stage == 0 ? 0u : vo[i].stage == 3 ? 1u : 2u;
        uint32_t key = score << 28 | (vo[i].age & 0x0FFFFFFFu);
        if (key < oldest) {
            oldest = key;
            best = i;
        }
    }
    vo[best].note = (int8_t)note;
    vo[best].gate = 1;
    vo[best].stage = 1;
    vo[best].vel = 0.35f + 0.65f * (float)vel / 127.0f;
    vo[best].inc = 440.0f * fm_exp2f((float)((int32_t)note - 69) / 12.0f) / SR;
    vo[best].age = ++vo_clock;
    if (vo[best].env < 0.001f) {
        vo[best].p1 = 0.0f;
        vo[best].p2 = 0.37f;
    }
}
static void voice_off(uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICES; i++)
        if (vo[i].note == (int8_t)note && vo[i].gate) {
            vo[i].gate = 0;
            vo[i].stage = 3;
        }
}

static void midi_in(void)                /* audio interrupt: MIDI in plays the synth as a sound module */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        if ((st & 0xF0u) == 0x90u && d2)
            voice_on(d1, d2);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            voice_off(d1);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, v;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    static float mix[APP_BLOCK];
    while (q_r != __atomic_load_n(&q_w, __ATOMIC_ACQUIRE)) {
        uint32_t e = q_ev[q_r % QN];
        if (e >> 16)
            voice_on(e >> 8 & 127u, e & 127u);
        else
            voice_off(e >> 8 & 127u);
        q_r++;
    }
    midi_in();
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    for (i = 0; i < n; i++)
        mix[i] = 0.0f;
    for (v = 0; v < NVOICES; v++) {
        voice_t *x = &vo[v];
        float fc, c, inc2;
        if (!x->stage)
            continue;
        /* the filter opens with velocity and the envelope; set once a block */
        fc = 700.0f + 2600.0f * x->vel * x->vel + 1800.0f * x->env;
        c = fm_minf(0.95f, 6.2831853f * fc / SR);
        inc2 = x->inc * 1.0058f;
        for (i = 0; i < n; i++) {
            float s;
            switch (x->stage) {
            case 1:
                x->env += 0.0045f;       /* ~5 ms */
                if (x->env >= 1.0f) {
                    x->env = 1.0f;
                    x->stage = 2;
                }
                break;
            case 2:
                x->env += (0.62f - x->env) * 0.00012f;   /* toward the sustain, ~200 ms */
                break;
            default:
                x->env *= 0.99975f;      /* release, ~90 ms to -40 dB ... a soft tail */
                if (x->env < 0.0004f) {
                    x->env = 0.0f;
                    x->stage = 0;
                    x->note = -1;
                }
            }
            x->p1 += x->inc;
            if (x->p1 >= 1.0f)
                x->p1 -= 1.0f;
            x->p2 += inc2;
            if (x->p2 >= 1.0f)
                x->p2 -= 1.0f;
            s = (x->p1 + x->p2) - 1.0f;  /* two saws, -1 .. 1 */
            x->lp1 += c * (s - x->lp1);
            x->lp2 += c * (x->lp1 - x->lp2);
            mix[i] += x->lp2 * x->env * x->vel;
            if (!x->stage)
                break;
        }
    }
    for (i = 0; i < n; i++) {
        float s = mix[i] * 0.16f;
        s = s / (1.0f + fm_fabsf(s));    /* soft limit: a big chord never clips */
        gain_now += (gain - gain_now) * 0.004f;
        s *= gain_now * 3.2f;
        lr[2 * i] = (int32_t)s;
        lr[2 * i + 1] = (int32_t)s;
    }
}

void app_panic(void)
{
    uint32_t i;
    for (i = 0; i < NVOICES; i++) {
        vo[i].stage = 0;
        vo[i].gate = 0;
        vo[i].env = 0.0f;
        vo[i].note = -1;
    }
}

/* ============================================================== note out === */
/* what each held key sounds, so its release ends exactly those notes (the key or octave may change meanwhile) */
typedef struct {
    uint8_t n;
    uint8_t note[MAXN];
} held_t;
static held_t held[NKEYS];

static void notes_on(uint32_t k, const uint8_t *notes, uint32_t n, uint32_t vel)
{
    uint32_t i;
    held_t *h = &held[k];
    h->n = 0;
    for (i = 0; i < n && i < MAXN; i++) {
        int32_t m = clamp((int32_t)notes[i] + 12 * set.octave, 0, 127);
        h->note[h->n++] = (uint8_t)m;
        ev_push(1, (uint32_t)m, vel);
        plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)m << 16 | vel << 24, TO_USB);
    }
}
static void notes_off(uint32_t k)
{
    uint32_t i;
    held_t *h = &held[k];
    for (i = 0; i < h->n; i++) {
        ev_push(0, h->note[i], 0);
        plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)h->note[i] << 16, TO_USB);
    }
    h->n = 0;
}

/* ===================================================================== init === */
static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.root = 9;                        /* A minor */
    set.scale = 0;
    set.octave = 0;
}

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.root > 11u || set.scale >= NSCALES || set.octave < -2 || set.octave > 2)
        settings_defaults();
    kit_build();
}

/* ======================================================================= UI === */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    int32_t pad;                         /* the pad played last (-1 none yet) */
    int32_t root_lit;                    /* the black key lit as the root (-1 none) */
    uint8_t oct_mod;                     /* OCT held and used as a modifier: its release steps nothing */
    uint8_t redraw;
} ui = {.pad = -1, .root_lit = -1};

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

void app_say(const char *a, const char *b)
{
    str_set(ui.msg[0], a ? a : "", sizeof ui.msg[0]);
    str_set(ui.msg[1], b ? b : "", sizeof ui.msg[1]);
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        char t[52] = "";
        text_cat(t, ui.msg[0]);
        text_cat(t, " ");
        text_cat(t, ui.msg[1]);
        ann_say(t, 0, ANN_NOW);
    }
}

static void key_text(char *t)            /* "A MIN" */
{
    tonic_name(t);
    text_cat(t, " ");
    text_cat(t, SCALE_SHORT[set.scale]);
}

static void rekey(void)
{
    char t[24];
    kit_build();
    set_dirty = 1;
    tonic_name(t);
    text_cat(t, " ");
    text_cat(t, SCALE_NAME[set.scale]);
    app_say("KEY", t);
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), i;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    /* white keys: pads */
    for (i = 0; i < 16u; i++) {
        uint32_t k = WK[i], bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t vel = touch_vel(k);
            notes_on(k, pads[i].note, pads[i].n, vel ? vel : 100u);
            ui.pad = (int32_t)i;
            ui.root_lit = pads[i].deg;
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit)) {
            notes_off(k);
            ui.redraw = 1;
        }
    }
    /* black keys 1-7: the scale degrees, a bass note an octave under the pads */
    for (i = 0; i < 7u; i++) {
        uint32_t k = BK[i], bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint8_t m = (uint8_t)(deg_pitch((int32_t)i) - 12);
            uint32_t vel = touch_vel(k);
            notes_on(k, &m, 1, vel ? vel : 100u);
            ui.root_lit = (int32_t)i;
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit)) {
            notes_off(k);
        }
    }
    /* buttons */
    for (i = 0; i < NB; i++) {
        uint32_t bit = 1u << i;
        int down = (btn & bit) && !(ui.btn & bit), up = !(btn & bit) && (ui.btn & bit);
        if (i == B_OCTDN || i == B_OCTUP) {
            if (down)
                ui.oct_mod = 0;
            if (up && !ui.oct_mod) {
                int32_t o = clamp(set.octave + (i == B_OCTUP ? 1 : -1), -2, 2);
                if (o != set.octave) {
                    char t[4] = {o < 0 ? '-' : '+', (char)('0' + (o < 0 ? -o : o)), 0, 0};
                    set.octave = (int8_t)o;
                    set_dirty = 1;
                    app_say("OCTAVE", t);
                }
            }
            continue;
        }
        if (!down)
            continue;
        if (i == B_SAVE) {
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
        } else if (i == B_PLAY) {
            uint32_t k;
            for (k = 0; k < NKEYS; k++)
                notes_off(k);
            app_say("ALL", "NOTES OFF");
        } else if (i == B_HOME) {
            ui.msg_until = 0;
            ui.redraw = 1;
        } else {
            static const char *const LABEL[NB] = {"FX", "SLICE", "TRIM", "LIB", "KEY", "SYS", "HOME", "SAVE",
                                                  "ARP", "MODE", "PLAY", "REC", "OCT-", "OCT+"};
            app_say(LABEL[i], "COMING SOON");
        }
    }
    /* ALGORITHM (ROOT): the key's root; with OCT held, the scale */
    if ((e = plat_enc(EN_ALGO)) != 0) {
        if (btn & (1u << B_OCTDN | 1u << B_OCTUP)) {
            ui.oct_mod = 1;
            set.scale = (uint8_t)(((int32_t)set.scale + e % (int32_t)NSCALES + (int32_t)NSCALES) % (int32_t)NSCALES);
        } else {
            set.root = (uint8_t)(((int32_t)set.root + e % 12 + 12) % 12);
        }
        rekey();
    }
    for (i = 0; i < NE; i++)             /* the others: drained for now */
        if (i != EN_ALGO)
            (void)plat_enc((int)i);
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

/* the lights: loaded pads dim, held pads full; the seven degree keys dim, the root full; SAVE lit while changed */
static void lights(void)
{
    uint32_t i, bright = 0, dim = 0, btn = 0;
    for (i = 0; i < 16u; i++) {
        uint32_t bit = 1u << WK[i];
        if (ui.keys & bit)
            bright |= bit;
        else
            dim |= bit;
    }
    for (i = 0; i < 7u; i++) {
        uint32_t bit = 1u << BK[i];
        if ((int32_t)i == ui.root_lit || (ui.keys & bit))
            bright |= bit;
        else
            dim |= bit;
    }
    if (set_dirty)
        btn |= 1u << B_SAVE;
    plat_lights(btn | ui.btn, bright, 0, dim, 1);
}

/* ------------------------------------------------------------------ drawing --- */
static void deg_sign(int32_t x, int32_t y, uint16_t c)   /* a small degree sign: a 4 px ring */
{
    cv_rect(x + 1, y, 2, 1, c);
    cv_rect(x + 1, y + 3, 2, 1, c);
    cv_rect(x, y + 1, 1, 2, c);
    cv_rect(x + 3, y + 1, 1, 2, c);
}

static void draw(void)
{
    char t[32];
    uint32_t i, now = plat_ms();
    int32_t x;
    const pad_t *p = ui.pad >= 0 ? &pads[ui.pad] : 0;
    int playing = p && (ui.keys >> WK[ui.pad] & 1u);
    static const uint16_t AMBER = RGB(242, 181, 82), AMBER_DIM = RGB(122, 90, 46), TEAL = RGB(79, 179, 166);

    /* band 1 (0 .. 120): the top bar, the chord */
    cv_begin(240, 120, C_BLACK);
    if (now < ui.msg_until) {
        x = cv_text(10, 5, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 5, &FONT_B, ui.msg[1], AMBER);
    } else {
        cv_text(10, 6, &FONT_XS, "KIT 01  DIATONIC", C_GRAY);
        key_text(t);
        x = 230 - text_w(&FONT_XS, t) - 8;
        cv_rect(x - 4, 5, text_w(&FONT_XS, t) + 16, 17, AMBER);
        cv_text(x + 4, 6, &FONT_XS, t, C_BLACK);
    }
    cv_rect(0, 27, 240, 1, C_LINE);
    if (p) {
        int32_t w, rw, qw;
        /* "PAD 2  ii" with a degree sign when diminished */
        t[0] = 0;
        text_cat(t, "PAD ");
        {
            char d[4] = {(char)('0' + (ui.pad + 1) / 10), (char)('0' + (ui.pad + 1) % 10), 0, 0};
            text_cat(t, ui.pad + 1 >= 10 ? d : d + 1);
        }
        x = cv_text(10, 36, &FONT_XS, t, C_GRAY);
        x = cv_text(x + 10, 36, &FONT_XS, p->roman, C_GRAY);
        if (p->dim)
            deg_sign(x + 1, 38, C_GRAY);
        /* the chord: its root in the big font, the quality beside it (the big font has capitals only) */
        {
            char r[2] = {p->nm[0][0], 0};
            const char *acc = p->nm[0] + 1;
            rw = text_w(&FONT_L, r) + text_w(&FONT_B, acc);
            qw = text_w(&FONT_B, p->qual);
            w = rw + qw + 4;
            x = (240 - w) / 2;
            x = cv_text(x, 58, &FONT_L, r, playing ? AMBER : AMBER_DIM);
            if (*acc)
                x = cv_text(x, 58, &FONT_B, acc, playing ? AMBER : AMBER_DIM);
            cv_text(x + 3, 72, &FONT_B, p->qual, playing ? AMBER : AMBER_DIM);
        }
        /* the chord tones */
        {
            int32_t tw = 0;
            for (i = 0; i < p->n; i++)
                tw += text_w(&FONT_XS, p->nm[i]) + 14;
            x = (240 - tw + 14) / 2;
            for (i = 0; i < p->n; i++) {
                const char *nm = p->nm[i];
                int32_t nw = text_w(&FONT_XS, nm);
                int root = i == 0u;
                cv_rect(x - 4, 97, nw + 8, 16, root ? TEAL : C_LINE);
                cv_text(x, 97, &FONT_XS, nm, root ? C_BLACK : C_GRAY);
                x += nw + 14;
            }
        }
    } else {
        cv_text((240 - text_w(&FONT_M, "PLAY A PAD")) / 2, 58, &FONT_M, "PLAY A PAD", C_GRAY);
        cv_text((240 - text_w(&FONT_XS, "white keys: chords   black keys: bass")) / 2, 92, &FONT_XS,
                "white keys: chords   black keys: bass", C_DIM);
    }
    cv_blit(0, 0);

    /* band 2 (120 .. 240): the pad strip, the degree strip, the bottom bar */
    cv_begin(240, 120, C_BLACK);
    for (i = 0; i < 16u; i++) {          /* the 16 pads as the white keys run */
        int held = (int)(ui.keys >> WK[i] & 1u);
        uint16_t c = held ? AMBER : (i < 8u ? AMBER_DIM : RGB(31, 74, 69));
        cv_rect(6 + (int32_t)i * 14, 8, 12, 14, c);
    }
    cv_text(6, 30, &FONT_XS, "ROOT", TEAL);
    for (i = 0; i < 7u; i++) {           /* the seven degree keys */
        int lit = (int32_t)i == ui.root_lit;
        int32_t cx = 46 + (int32_t)i * 27;
        char d[2] = {(char)('1' + i), 0};
        cv_rect(cx, 28, 24, 20, lit ? TEAL : C_LINE);
        cv_text(cx + (24 - text_w(&FONT_XS, d)) / 2, 30, &FONT_XS, d, lit ? C_BLACK : C_GRAY);
    }
    cv_rect(0, 68, 240, 1, C_LINE);
    {
        static const char *const LBL[4] = {"OCT", "KEY", "SCALE", "VOL"};
        char v[4][12];
        int32_t o = set.octave;
        v[0][0] = o < 0 ? '-' : '+';
        v[0][1] = (char)('0' + (o < 0 ? -o : o));
        v[0][2] = 0;
        tonic_name(v[1]);
        str_set(v[2], SCALE_SHORT[set.scale], sizeof v[2]);
        {
            uint32_t pct = plat_master() * 100u / 4096u;
            char *q = v[3];
            if (pct >= 100u)
                *q++ = '1';
            if (pct >= 10u)
                *q++ = (char)('0' + (pct / 10u) % 10u);
            *q++ = (char)('0' + pct % 10u);
            *q = 0;
        }
        for (i = 0; i < 4u; i++) {
            int32_t cx = (int32_t)i * 60;
            cv_text(cx + (60 - text_w(&FONT_XS, LBL[i])) / 2, 76, &FONT_XS, LBL[i], C_GRAY);
            cv_text(cx + (60 - text_w(&FONT_B, v[i])) / 2, 93, &FONT_B, v[i], C_WHITE);
        }
    }
    cv_blit(0, 120);
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_pct;
    uint32_t now = plat_ms(), pct = plat_master() * 100u / 4096u;
    input();
    lights();
    ann_tick(now);
    if (pct != last_pct || (ui.msg_until && now >= ui.msg_until)) {
        last_pct = pct;
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    uint32_t i;
    if (!strcmp(what, "voices")) {       /* voices sounding (attack or sustain) */
        *got = 0;
        for (i = 0; i < NVOICES; i++)
            *got += vo[i].stage == 1 || vo[i].stage == 2;
    } else if (!strcmp(what, "pad"))
        *got = ui.pad;
    else if (!strcmp(what, "root"))
        *got = set.root;
    else if (!strcmp(what, "scale"))
        *got = set.scale;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "rootkey"))
        *got = ui.root_lit;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strncmp(what, "padnote", 7))  /* padnoteN: pad 1's note N */
        *got = ui.pad >= 0 ? pads[ui.pad].note[atoi(what + 7) % MAXN] : -1;
    else
        return 0;
    return 1;
}
#endif
