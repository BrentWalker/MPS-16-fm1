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
#include "disk.c"                        /* the USB drive: a FAT12 volume in the flash */
#include "smf.c"                         /* standard MIDI files, read from it */

/* ================================================================ settings === */
#define SET_MAGIC 0x43534632u            /* "CSF2" */
typedef struct {
    uint32_t magic;
    uint8_t root;                        /* 0 = C .. 11 = B */
    uint8_t scale;                       /* SCALES[] */
    int8_t octave;                       /* -2 .. +2 */
    uint8_t lock;                        /* scale lock: every note snaps to the key */
    int8_t steps;                        /* scale transpose, in degrees (-7 .. 7), with the lock on */
    uint8_t kit;                         /* the kit in use, 1 .. NKITS */
    uint8_t strum;                       /* chord FX: strum, 2 ms steps (0 .. 100) */
    uint8_t arp;                         /* 0 off, 1 up, 2 down, 3 up-down, 4 random */
    uint8_t arprate;                     /* ARP_RATE[] */
    uint8_t human;                       /* humanize, % (0 .. 50): time and velocity */
    int8_t inv;                          /* inversion, -3 .. 3 */
    uint8_t bass;                        /* a bass note under every chord */
    uint8_t bpm;                         /* 0: the file's tempo; else this */
    uint8_t mode;                        /* the black keys: MODE_ROOT or MODE_LEAD (SEQ switches) */
    uint8_t fmove;                       /* the key change F1 .. F4 in use, 0: the home key */
    uint8_t home;                        /* with one in use, the home key: root << 4 | scale */
    uint8_t om_slice;                    /* ORCHID: how the file is cut (OM_*) */
    uint8_t om_f4, om_play, om_shape;    /* ORCHID: the 4 black keys (FK_*), how pads play (OP_*), the voicing */
    uint16_t om_start;                   /* ORCHID: the slice on pad 1 */
    uint8_t om_amt;                      /* ORCHID: how much of the way they play, 0 .. 10 */
    int8_t om_len;                       /* ORCHID: ALGORITHM, the notes' length, -5 .. 5 (0: as played) */
    int8_t om_slen;                      /* ORCHID: OCT- and ALGORITHM, the slices' length, -2 .. 2 (0: as cut) */
    int8_t om_vcurve;                    /* ORCHID: OCT+ and ALGORITHM, the velocity curve, -5 .. 5 */
    uint8_t om_rsv;
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
#define MAXPH 64u                        /* notes in a phrase pad */
#define PREVIEW NPADS                    /* the extra slot SLICE auditions from */
typedef struct {
    uint8_t type;                        /* PAD_CHORD or PAD_PHRASE */
    uint8_t fromkey;                     /* a chord built from the key: rebuilt when the key changes */
    uint8_t tag;                         /* the slice it came from (1 ..), 0 none */
    uint8_t pn;                          /* a phrase's notes */
    uint16_t len;                        /* a phrase's length, 96ths of a beat */
    uint8_t n;                           /* notes in the chord */
    uint8_t note[MAXN];                  /* MIDI notes, before the octave */
    uint8_t deg;                         /* the root's scale degree, 0..6 */
    uint8_t root_pc;                     /* the root's pitch class */
    char qual[10];                       /* "m7b5" */
    char nm[MAXN][4];                    /* each note spelled in the key ("C#", "Bb") */
    char roman[6];                       /* "ii" */
    uint8_t dim;                         /* the roman numeral takes a degree sign */
    char title[14];                      /* a progression's name ("i VI III VII"), else empty */
    /* TRIM (0 everywhere: as it came) */
    uint16_t ts, te;                     /* a phrase's window, 96ths: from ts to te (0: its length) */
    int8_t tune;                         /* semitones, -12 .. 12 */
    uint8_t cut;                         /* the level: 100 - cut, % */
    uint8_t pmode;                       /* a phrase: PM_ONESHOT, PM_GATE, PM_LOOP */
    uint8_t rsv;
} pad_t;
enum { PM_ONESHOT, PM_GATE, PM_LOOP };
static const char *const PM_NAME[3] = {"ONE-SHOT", "GATE", "LOOP"};
enum { PAD_EMPTY, PAD_CHORD, PAD_PHRASE };
typedef struct {
    uint16_t s, d;                       /* start and length, 96ths of a beat */
    uint8_t p, v;
} pnote_t;
/* the kit: every pad and its phrase, one block, saved whole as a file on the drive (KITnn.CSK) */
#define KIT_MAGIC 0x324B5343u            /* "CSK2" (CSK1: before TRIM) */
#define NKITS 8u
static struct {
    uint32_t magic, size;
    pad_t pad[NPADS + 1];
    pnote_t ph[NPADS + 1][MAXPH];
} K;
#define pads K.pad
#define ph K.ph
static uint32_t pad_end(const pad_t *p) { return p->te && p->te <= p->len ? p->te : p->len; }
static void trim_reset(pad_t *p)
{
    p->ts = p->te = 0;
    p->tune = 0;
    p->cut = 0;
    p->pmode = PM_ONESHOT;
}

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
static uint32_t letter_of(uint32_t root, uint32_t scale)   /* the tonic's letter in a key, 0 = C */
{
    static const uint8_t MAJ[12] = {0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6};   /* C Db D Eb E F F# G Ab A Bb B */
    static const uint8_t MIN[12] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6};   /* C C# D Eb E F F# G G# A Bb B */
    return (SCALES[scale % NSCALES][2] == 3u ? MIN : MAJ)[root % 12u];
}
static uint32_t tonic_letter(void) { return letter_of(set.root, set.scale); }
static void spell(uint32_t pc, uint32_t letter, char *out)   /* pitch class pc on letter (0 = C): "F#", "Bb" */
{
    int32_t d = (int32_t)((pc + 18u - LETTER_PC[letter % 7u]) % 12u) - 6;
    out[0] = "CDEFGAB"[letter % 7u];
    out[1] = d > 0 ? '#' : d < 0 ? 'b' : 0;
    out[2] = (d > 1 || d < -1) ? out[1] : 0;
    out[3] = 0;
}
static void tonic_name(char *out) { spell(set.root, tonic_letter(), out); }

static uint32_t cs_len(const char *s)   /* (the device's libc has no strlen) */
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
/* scale lock (set.lock): a note snaps to the key's scale (a tie goes down), then moves set.steps degrees */
static int32_t keyed(int32_t p)
{
    const uint8_t *sc = SCALES[set.scale];
    int32_t rel, best = 0, bd = 99, i, k, idx = 0, oct, nn;
    if (!set.lock)
        return p;
    rel = ((p - set.root) % 12 + 12) % 12;
    for (i = 0; i < 7; i++)
        for (k = -12; k <= 12; k += 12) {
            int32_t c = sc[i] + k, d = c > rel ? c - rel : rel - c;
            if (d < bd) {
                bd = d;
                best = c;
            }
        }
    p += best - rel;
    if (!set.steps)
        return p;
    rel = ((p - set.root) % 12 + 12) % 12;
    for (i = 0; i < 7; i++)
        if (sc[i] == rel)
            idx = i;
    oct = (p - set.root - rel) / 12;
    if (p - set.root - rel < 0 && (p - set.root - rel) % 12)
        oct--;
    nn = idx + set.steps;
    oct += nn >= 0 ? nn / 7 : -((6 - nn) / 7);
    nn = ((nn % 7) + 7) % 7;
    return set.root + 12 * oct + sc[nn];
}

static void num_text(int32_t v, char *o, int sign)   /* "12", "+3", "-2" */
{
    char t[8];
    uint32_t u = (uint32_t)(v < 0 ? -v : v), n = 0;
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 7u);
    if (v < 0)
        *o++ = '-';
    else if (sign)
        *o++ = '+';
    while (n)
        *o++ = t[--n];
    *o = 0;
}

static void text_cat_n(char *d, const char *s, uint32_t max)   /* append, never past max - 1 characters */
{
    uint32_t n = 0;
    while (n + 1u < max && d[n])
        n++;
    while (n + 1u < max && *s)
        d[n++] = *s++;
    d[n] = 0;
}
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
            str_set(p->roman + cs_len(UP[d]), "+", 2);
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
        if (p->type != PAD_EMPTY && !p->fromkey)
            continue;                    /* a slice dropped here stays */
        p->type = PAD_CHORD;
        p->fromkey = 1;
        p->tag = 0;
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

/* one core: the audio interrupt and the main loop share aligned 32-bit words, read and written whole; the barrier
 * keeps the compiler from reordering a slot's write past its index */
#define BARRIER() __asm__ volatile("" ::: "memory")
#define QN 128u
static volatile uint32_t q_ev[QN];
static volatile uint32_t q_w, q_r;
enum { QE_OFF, QE_ON, QE_PSTART, QE_PSTOP, QE_CSTART, QE_CSTOP };
static void ev_push(uint32_t type, uint32_t a, uint32_t b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                          /* full: drop (never seen at human speed) */
    q_ev[w % QN] = type << 16 | (a & 127u) << 8 | (b & 127u);
    BARRIER();
    q_w = w + 1u;
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

/* the tempo phrases play at (the loaded file's), and the audio clock in samples */
static volatile uint32_t tempo = 90;
static volatile uint32_t clock_smp;

/* REC: MIDI in captured as it comes, with the audio clock (the main loop writes it out as a file when REC stops) */
#define RECMAX 512u
typedef struct {
    uint32_t t;
    uint8_t st, d1, d2, pad;
} recev_t;
static recev_t rec_ev[RECMAX];
static volatile uint32_t rec_n;
static volatile uint8_t rec_on;

static void midi_in(void)                /* audio interrupt: MIDI in plays the synth (a sound module), REC keeps it */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24, hi = st & 0xF0u;
        if (hi == 0x90u && d2)
            voice_on(d1, d2);
        else if (hi == 0x80u || hi == 0x90u)
            voice_off(d1);
        if (rec_on && (hi == 0x80u || hi == 0x90u) && rec_n < RECMAX) {
            recev_t *r = &rec_ev[rec_n];
            r->t = clock_smp;
            r->st = (uint8_t)(hi == 0x90u && d2 ? 0x90u : 0x80u);
            r->d1 = (uint8_t)d1;
            r->d2 = (uint8_t)(hi == 0x90u ? d2 : 0u);
            BARRIER();
            rec_n = rec_n + 1u;
        }
    }
}

/* phrase players: one per pad and one for SLICE's preview. Times in 96ths of a beat; one-shot. Each note's output
 * (octave and scale lock applied) is kept while it sounds, so its note off matches whatever changed meanwhile */
typedef struct {
    uint8_t on;
    int8_t oct;
    float pos;
    uint8_t snd[MAXPH];                  /* the note sounding for phrase note k, + 1; 0 none */
    uint16_t endt[MAXPH];                /* and when it stops (ORCHID's note length) */
} player_t;
static player_t pl[NPADS + 1];

static void out_on(uint32_t m, uint32_t v)
{
    voice_on(m, v);
    plat_midi_send(0x09u | 0x90u << 8 | m << 16 | v << 24, TO_USB);
}
static void out_off(uint32_t m)
{
    voice_off(m);
    plat_midi_send(0x08u | 0x80u << 8 | m << 16, TO_USB);
}

static void pl_release(uint32_t i)
{
    uint32_t k;
    for (k = 0; k < MAXPH; k++)
        if (pl[i].snd[k]) {
            out_off(pl[i].snd[k] - 1u);
            pl[i].snd[k] = 0;
        }
}

static uint32_t human_vel(uint32_t v);
static uint8_t orchid;                   /* ORCHID (1) or FM mode (0): set below */
/* ORCHID's note length (ALGORITHM): % of each note's own length, the last LEGATO (on to the next note) */
static const uint16_t LEN_PCT[11] = {10, 25, 40, 55, 75, 100, 140, 200, 300, 450, 0};
static const char *const LEN_NAME[11] = {"10%", "25%", "40%", "55%", "75%", "AS PLAYED", "140%", "200%", "300%",
                                         "450%", "LEGATO"};
/* ORCHID's velocity curve (OCT+ and ALGORITHM): left softer (down to v squared), right harder */
static uint32_t vcurve(uint32_t v)
{
    int32_t c = orchid ? set.om_vcurve : 0, x = (int32_t)v;
    if (!c)
        return v;
    x += c * x * (127 - x) / 635;        /* 635 = 127 * 5 */
    return (uint32_t)clamp(x, 1, 127);
}
static void phrase_tick(uint32_t n)
{
    float dt = (float)n * (float)tempo * 96.0f / (60.0f * SR);
    uint32_t i, k;
    for (i = 0; i <= NPADS; i++) {
        player_t *P = &pl[i];
        float a, b;
        if (!P->on)
            continue;
        const pad_t *pd = &pads[i];
        float ws = (float)pd->ts, we = (float)pad_end(pd);
        uint32_t lv = 100u - (pd->cut > 100u ? 100u : pd->cut);
        if (P->pos < ws)
            P->pos = ws;                 /* (the start moved past it) */
        a = P->pos;
        b = a + dt;
        for (k = 0; k < pd->pn && k < MAXPH; k++) {
            const pnote_t *x = &ph[i][k];
            float s = (float)x->s, e = fm_minf(P->snd[k] ? (float)P->endt[k] : s + (float)x->d, we);
            int starts = s >= a && s < b && s >= ws && s < we;
            if (starts) {
                uint32_t m = (uint32_t)clamp(keyed((int32_t)x->p + 12 * P->oct + pd->tune), 0, 127);
                uint32_t v = (uint32_t)x->v * lv / 100u;
                if (set.human)
                    v = human_vel(v ? v : 1u);   /* humanize (ORCHID's SLOPPY): phrases too */
                if (P->snd[k])
                    out_off(P->snd[k] - 1u);
                out_on(m, vcurve(v ? v : 1u));
                P->snd[k] = (uint8_t)(m + 1u);
                {                        /* when it stops: its length, or ORCHID's */
                    uint32_t d = x->d, li = (uint32_t)(set.om_len + 5);
                    if (orchid && set.om_len && li < 11u) {
                        if (LEN_PCT[li])
                            d = d * LEN_PCT[li] / 100u;
                        else {           /* LEGATO: on to the next note's start */
                            uint32_t j, nx = (uint32_t)we;
                            for (j = 0; j < pd->pn && j < MAXPH; j++)
                                if (ph[i][j].s > x->s && ph[i][j].s < nx)
                                    nx = ph[i][j].s;
                            d = nx - x->s;
                        }
                        if (d < 2u)
                            d = 2u;
                    }
                    P->endt[k] = (uint16_t)(x->s + d > 65535u ? 65535u : x->s + d);
                    e = fm_minf((float)P->endt[k], we);
                }
            }
            if (P->snd[k] && e < b && !(starts && e >= b)) {
                out_off(P->snd[k] - 1u);
                P->snd[k] = 0;
            }
        }
        P->pos = b;
        if (P->pos >= we) {
            pl_release(i);
            if (pd->pmode == PM_LOOP && i < NPADS && we > ws)
                P->pos = ws + (P->pos - we);   /* again from the start, on the beat */
            else
                P->on = 0;
        }
    }
}

/* chord players: a chord pad while its key is held, through the chord FX: strum (the notes one after another),
 * arp (the notes in turn at a rate, until the key is let go), humanize (time and velocity off by a little). The main
 * loop writes the chord's notes (voiced, octave and scale lock applied) into cbuf, then asks for the start */
#define MAXC 8u
typedef struct {
    uint8_t on, n, cur;                  /* cur: the arp's note sounding + 1, 0 none */
    uint8_t note[MAXC], vel[MAXC], sounded[MAXC];
    uint32_t at[MAXC];                   /* each note's start, samples from the press */
    uint32_t t, next, idx;
} cplay_t;
static cplay_t cp[NPADS];
static uint8_t cbuf[NPADS][MAXC], cbuf_n[NPADS];
static uint16_t cpat[NPADS];             /* ORCHID's PERFORM: a rhythm, 16 sixteenths a bar (bit 0 first), 0 none */
static uint32_t cp_off[NPADS];           /* PERFORM: when the hit sounding stops */
static const float ARP_RATE[6] = {1.0f, 0.5f, 0.3333333f, 0.25f, 0.1666667f, 0.125f};   /* of a beat */
static const char *const ARP_RATE_NAME[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const ARP_NAME[5] = {"OFF", "UP", "DOWN", "UP-DN", "RAND"};
static uint32_t cs_rng = 0x2545F491u;
static uint32_t cs_rnd(void)
{
    cs_rng = cs_rng * 1664525u + 1013904223u;
    return cs_rng >> 8;
}
static uint32_t human_vel(uint32_t v)
{
    int32_t h = set.human, r = (int32_t)(cs_rnd() % 201u) - 100;
    return (uint32_t)clamp((int32_t)v + (int32_t)v * h * r / 10000, 1, 127);
}

static void cp_stop(uint32_t i)
{
    cplay_t *c = &cp[i];
    uint32_t k;
    if (c->cur) {
        out_off(c->cur - 1u);
        c->cur = 0;
    }
    for (k = 0; k < c->n; k++)
        if (c->sounded[k]) {
            out_off(c->note[k]);
            c->sounded[k] = 0;
        }
    c->on = 0;
}

static void cp_start(uint32_t i, uint32_t vel)
{
    cplay_t *c = &cp[i];
    uint32_t k, ss = (uint32_t)set.strum * 2u * 441u / 10u;   /* 2 ms steps, in samples */
    cp_stop(i);
    c->n = cbuf_n[i] < MAXC ? cbuf_n[i] : (uint8_t)MAXC;
    for (k = 0; k < c->n; k++) {
        c->note[k] = cbuf[i][k];
        c->vel[k] = (uint8_t)human_vel(vel);
        c->sounded[k] = 0;
        c->at[k] = k * ss + (set.human ? cs_rnd() % (set.human * 26u + 1u) : 0u);   /* up to ~30 ms late */
    }
    c->t = 0;
    c->next = 0;
    c->idx = 0;
    c->cur = 0;
    cp_off[i] = 0;
    c->vel[0] = c->vel[0] ? c->vel[0] : 1;
    c->on = c->n != 0;
}

static void chord_tick(uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < NPADS; i++) {
        cplay_t *c = &cp[i];
        if (!c->on)
            continue;
        if (cpat[i] && c->n) {           /* PERFORM: the chord on the rhythm's sixteenths while held */
            uint32_t step = (uint32_t)(15.0f * SR / (float)tempo);
            if (!step)
                step = 1u;
            if (cp_off[i] && cp_off[i] < c->t + n) {
                for (k = 0; k < c->n; k++)
                    if (c->sounded[k]) {
                        out_off(c->note[k]);
                        c->sounded[k] = 0;
                    }
                cp_off[i] = 0;
            }
            while (c->next < c->t + n) {
                if (cpat[i] >> (c->idx % 16u) & 1u) {
                    for (k = 0; k < c->n; k++) {
                        if (c->sounded[k])
                            out_off(c->note[k]);
                        out_on(c->note[k], vcurve(human_vel(c->vel[0])));
                        c->sounded[k] = 1;
                    }
                    {                    /* short: a stab, as long as ORCHID's note length says */
                        uint32_t li = (uint32_t)(set.om_len + 5), g = step * 3u / 5u;
                        if (li < 11u)
                            g = LEN_PCT[li] ? g * LEN_PCT[li] / 100u : step - 1u;
                        cp_off[i] = c->next + (g ? g : 1u);
                    }
                }
                c->idx++;
                c->next += step;
            }
        } else if (set.arp && c->n) {
            while (c->next < c->t + n) {
                uint32_t len = c->n, j = c->idx, pick, step;
                if (set.arp == 1u)
                    pick = j % len;
                else if (set.arp == 2u)
                    pick = len - 1u - j % len;
                else if (set.arp == 3u) {
                    uint32_t per = len > 1u ? 2u * len - 2u : 1u, q = j % per;
                    pick = q < len ? q : per - q;
                } else
                    pick = cs_rnd() % len;
                if (c->cur)
                    out_off(c->cur - 1u);
                out_on(c->note[pick], vcurve(human_vel(c->vel[0])));
                c->cur = (uint8_t)(c->note[pick] + 1u);
                c->idx++;
                step = (uint32_t)(60.0f * SR / (float)tempo * ARP_RATE[set.arprate % 6u]);
                c->next += step ? step : 1u;
            }
        } else {
            for (k = 0; k < c->n; k++)
                if (!c->sounded[k] && c->at[k] < c->t + n) {
                    out_on(c->note[k], vcurve(c->vel[k]));
                    c->sounded[k] = 1;
                }
        }
        c->t += n;
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, v;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    static float mix[APP_BLOCK];
    while (q_r != q_w) {
        BARRIER();
        uint32_t e = q_ev[q_r % QN], type = e >> 16, a = e >> 8 & 127u, b = e & 127u;
        if (type == QE_ON)
            voice_on(a, b);
        else if (type == QE_OFF)
            voice_off(a);
        else if (type == QE_CSTART && a < NPADS)
            cp_start(a, b);
        else if (type == QE_CSTOP && a < NPADS)
            cp_stop(a);
        else if (a <= NPADS) {           /* a phrase: start (b: the octave + 8) or stop */
            pl_release(a);
            pl[a].on = type == QE_PSTART;
            pl[a].oct = (int8_t)((int32_t)b - 8);
            pl[a].pos = (float)pads[a].ts;
        }
        q_r++;
    }
    phrase_tick(n);
    chord_tick(n);
    clock_smp += n;
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
    for (i = 0; i <= NPADS; i++) {
        uint32_t k;
        pl[i].on = 0;
        for (k = 0; k < MAXPH; k++)
            pl[i].snd[k] = 0;
        if (i < NPADS) {
            cp[i].on = 0;
            cp[i].cur = 0;
            for (k = 0; k < MAXC; k++)
                cp[i].sounded[k] = 0;
        }
    }
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
        int32_t m = clamp(keyed((int32_t)notes[i] + 12 * set.octave), 0, 127);
        h->note[h->n++] = (uint8_t)m;
        ev_push(QE_ON, (uint32_t)m, vel);
        plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)m << 16 | vel << 24, TO_USB);
    }
}
static void notes_off(uint32_t k)
{
    uint32_t i;
    held_t *h = &held[k];
    for (i = 0; i < h->n; i++) {
        ev_push(QE_OFF, h->note[i], 0);
        plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)h->note[i] << 16, TO_USB);
    }
    h->n = 0;
}

/* ================================================================= slicing === */
/* the loaded file cut into slices, by its hits (where notes land together, the louder the likelier) or on a grid */
#define MAXSL 128u
static uint32_t sl_s[MAXSL], sl_e[MAXSL], nsl, sel;
static uint8_t sl_mode;                  /* 0 hits, 1 grid */
static uint8_t sl_sens = 55, sl_grid = 4;
static int32_t cur_file = -1;
static const char *const GRID_NAME[7] = {"1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS"};
static uint32_t grid_len(uint32_t g)
{
    static const uint16_t BEATS96[4] = {24, 48, 96, 192};
    return g < 4u ? BEATS96[g] : PPQ * song_bpb * (g == 4u ? 1u : g == 5u ? 2u : 4u);
}
static int note_used(const note_t *n) { return trk[N_TRK(n) % MAXTRK].on; }

static void slice_auto(void)
{
    uint32_t i;
    nsl = 0;
    if (!song_n)
        return;
    if (sl_mode) {                       /* the grid: every interval with a note starting in it (any track) */
        uint32_t step = grid_len(sl_grid), t, k = 0;
        for (t = 0; t < song_len && nsl < MAXSL; t += step) {
            while (k < song_n && N_START(&song[k]) < t)
                k++;
            if (k < song_n && N_START(&song[k]) < t + step) {
                sl_s[nsl] = t;
                sl_e[nsl] = t + step < song_len ? t + step : song_len;
                nsl++;
            }
        }
    } else {                             /* hits: onsets within 1/16 of a 16th merge, weighted by velocity */
        static uint32_t on_t[MAXSL * 4u], on_w[MAXSL * 4u];
        uint32_t non = 0, mx = 0, thr, gap, last_end = 0, last = 0;
        for (i = 0; i < song_n; i++) {
            const note_t *n = &song[i];
            if (!note_used(n))
                continue;
            if (N_START(n) + n->d > last_end)
                last_end = N_START(n) + n->d;
            if (non && N_START(n) - on_t[non - 1u] < 6u)
                on_w[non - 1u] += n->v;
            else if (non < MAXSL * 4u) {
                on_t[non] = N_START(n);
                on_w[non++] = n->v;
            }
        }
        for (i = 0; i < non; i++)
            if (on_w[i] > mx)
                mx = on_w[i];
        thr = mx * (100u - sl_sens) * 85u / 10000u;
        gap = 12u + (100u - sl_sens) * 180u / 100u;   /* 1/8 of a beat .. 2 beats */
        for (i = 0; i < non && nsl < MAXSL; i++)
            if (on_w[i] >= thr && (!nsl || on_t[i] - last >= gap)) {
                if (nsl)
                    sl_e[nsl - 1u] = on_t[i];
                sl_s[nsl] = last = on_t[i];
                nsl++;
            }
        if (nsl)
            sl_e[nsl - 1u] = last_end > sl_s[nsl - 1u] + 24u ? last_end : sl_s[nsl - 1u] + 24u;
    }
    if (sel >= nsl)
        sel = nsl ? nsl - 1u : 0u;
}

/* a pitch-class spelling for chords found in a file: flats in flat keys, sharps otherwise */
static void pc_name(uint32_t pc, char *out)
{
    static const char *const SH[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *const FL[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    char t[4];
    tonic_name(t);
    str_set(out, (t[1] == 'b' || (t[0] == 'F' && !t[1])) ? FL[pc % 12u] : SH[pc % 12u], 4);
}

/* slice i into pad slot dst: a chord when every note lands together, else a phrase */
static int slice_to(uint32_t i, uint32_t dst)
{
    pad_t *p = &pads[dst];
    uint32_t k, s = sl_s[i], e = sl_e[i], first = 0xFFFFFFFFu, chord = 1;
    uint8_t ps[MAXPH];
    uint32_t nps = 0;
    p->pn = 0;
    trim_reset(p);
    for (k = 0; k < song_n; k++) {
        const note_t *n = &song[k];
        uint32_t t = N_START(n);
        if (t < s || !note_used(n))
            continue;
        if (t >= e)
            break;
        if (first == 0xFFFFFFFFu)
            first = t;
        if (t - first > 10u)
            chord = 0;
        if (p->pn < MAXPH) {
            pnote_t *x = &ph[dst][p->pn++];
            x->s = (uint16_t)(t - s);
            x->d = (uint16_t)(t + n->d > e ? e - t : n->d);
            x->p = n->p;
            x->v = n->v;
        }
    }
    if (!p->pn)
        return -1;
    p->fromkey = 0;
    p->tag = (uint8_t)(i + 1u);
    p->len = (uint16_t)(e - s > 0xFFFFu ? 0xFFFFu : e - s);
    for (k = 0; k < p->pn; k++) {        /* the distinct pitches, low to high */
        uint32_t j, q = ph[dst][k].p, at = nps;
        for (j = 0; j < nps; j++)
            if (ps[j] >= q)
                break;
        if (j < nps && ps[j] == q)
            continue;
        for (at = nps; at > j; at--)
            ps[at] = ps[at - 1u];
        ps[j] = (uint8_t)q;
        nps++;
    }
    if (chord && nps >= 2u) {            /* a chord: its lowest notes, named from the bass if that fits */
        uint32_t best = 0xFFu, j, r;
        p->type = PAD_CHORD;
        p->n = (uint8_t)(nps < MAXN ? nps : MAXN);
        for (k = 0; k < p->n; k++) {
            p->note[k] = ps[k];
            pc_name(ps[k] % 12u, p->nm[k]);
        }
        p->qual[0] = 0;
        for (r = 0; r < 12u && best == 0xFFu; r++) {   /* each pitch class as the root, the bass first */
            uint32_t root = (ps[0] + r) % 12u, mask = 0;
            for (k = 0; k < p->n; k++)
                mask |= 1u << ((p->note[k] + 12u - root) % 12u);
            if (!(mask & 1u))
                continue;
            for (j = 0; j < NQUAL; j++)
                if (QUAL[j].mask == mask) {
                    best = root;
                    str_set(p->qual, QUAL[j].name, sizeof p->qual);
                    break;
                }
        }
        p->root_pc = (uint8_t)(best == 0xFFu ? ps[0] % 12u : best);
        if (best == 0xFFu)
            str_set(p->qual, "?", sizeof p->qual);
        else if (best != ps[0] % 12u) {   /* a slash chord: the bass after the quality */
            char b[4];
            pc_name(ps[0] % 12u, b);
            if (cs_len(p->qual) + 1u + cs_len(b) < sizeof p->qual) {
                char *q = p->qual + cs_len(p->qual);
                *q++ = '/';
                str_set(q, b, 4);
            }
        }
        if (best != ps[0] % 12u)         /* the root name, not the bass, on the first line */
            for (k = 0; k < p->n; k++)
                if (p->note[k] % 12u == p->root_pc) {
                    pc_name(p->root_pc, p->nm[0]);
                    break;
                }
        pc_name(p->root_pc, p->nm[0]);
        p->deg = 0xFF;                   /* its degree in the key, if it has one */
        p->roman[0] = 0;
        p->dim = 0;
        for (j = 0; j < 7u; j++)
            if ((set.root + SCALES[set.scale][j]) % 12u == p->root_pc) {
                p->deg = (uint8_t)j;
                roman_for(p);
                break;
            }
    } else {
        p->type = PAD_PHRASE;
        p->deg = 0xFF;
    }
    return 0;
}

static void fmt_pos(uint32_t t, char *o)   /* bar.beat.16th, from 1 */
{
    uint32_t bar = t / (PPQ * song_bpb) + 1u, beat = t / PPQ % song_bpb + 1u, six = t % PPQ / 24u + 1u;
    char *q = o;
    if (bar >= 10u)
        *q++ = (char)('0' + bar / 10u % 10u);
    *q++ = (char)('0' + bar % 10u);
    *q++ = '.';
    *q++ = (char)('0' + beat);
    *q++ = '.';
    *q++ = (char)('0' + six);
    *q = 0;
}
static void fmt_len(uint32_t t, char *o)
{
    uint32_t bar = PPQ * song_bpb, n;
    char *q = o;
    const char *u;
    if (t % bar == 0u) {
        n = t / bar;
        u = n == 1u ? " BAR" : " BARS";
    } else if (t % PPQ == 0u) {
        n = t / PPQ;
        u = n == 1u ? " BEAT" : " BEATS";
    } else if (t % (PPQ / 2u) == 0u) {   /* half beats: "1.5 BEATS" */
        n = t / PPQ;
        if (n >= 10u)
            *q++ = (char)('0' + n / 10u % 10u);
        *q++ = (char)('0' + n % 10u);
        *q++ = '.';
        *q++ = '5';
        *q = 0;
        u = " BEATS";
        q = o + cs_len(o);
        while (*u)
            *q++ = *u++;
        *q = 0;
        return;
    } else {
        n = (t + 12u) / 24u;
        u = "/16";
    }
    if (n >= 10u)
        *q++ = (char)('0' + n / 10u % 10u);
    *q++ = (char)('0' + n % 10u);
    *q = 0;
    q = o + cs_len(o);
    while (*u)
        *q++ = *u++;
    *q = 0;
}

#include "lib.c"                         /* the chord library, built from the key */

/* ===================================================================== init === */
static void settings_defaults(void)
{
    __builtin_memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.root = 9;                        /* A minor */
    set.kit = 1;
    set.arprate = 3;                     /* 1/16 */
    set.om_slice = 1;                    /* ORCHID: BARS */
    set.om_amt = 4;
}

/* kits: KIT01.CSK .. KIT08.CSK on the drive, each the whole pad block (K) */
static void kit_name(uint32_t n, char *o)
{
    __builtin_memcpy(o, "KIT00   CSK", 12);
    o[3] = (char)('0' + n / 10u);
    o[4] = (char)('0' + n % 10u);
}
static int kit_save(void)
{
    char nm[12];
    kit_name(set.kit, nm);
    K.magic = KIT_MAGIC;
    K.size = sizeof K;
    fs_delete(nm);
    return fs_write(nm, (const uint8_t *)&K, sizeof K);
}
static void players_stop(void)
{
    uint32_t k;
    for (k = 0; k <= NPADS; k++) {
        ev_push(QE_PSTOP, k, 8);
        if (k < NPADS)
            ev_push(QE_CSTOP, k, 0);
    }
}
/* 1: the kit was there; 0: an empty slot, filled with the key's chords */
static int kit_load(uint32_t n)
{
    char nm[12];
    kit_name(n, nm);
    if (fs_read(nm, &K, sizeof K) == (int32_t)sizeof K && K.magic == KIT_MAGIC && K.size == sizeof K) {
        kit_build();                     /* the key's own chords follow the key as it is now */
        return 1;
    }
    __builtin_memset(&K, 0, sizeof K);
    kit_build();
    return 0;
}

static uint8_t orchid = 1;               /* ORCHID (1) or FM mode (0); declared above */
static void orchid_fill(void);
static int32_t deg_of(int32_t m);
enum { MODE_ROOT, MODE_LEAD, MODE_3RD, MODE_5TH, MODE_N };
static const char *const MODE_NAME[MODE_N] = {"ROOT", "LEAD", "3RD", "5TH"};
enum { FK_BRIDGE, FK_SUGGEST, FK_OUTSIDE, FK_BORROW, FK_N };
static const char *const FK_MODE[FK_N] = {"BRIDGE", "SUGGEST", "OUTSIDE", "BORROW"};
enum { OP_ASIS, OP_CHORD, OP_STRUM, OP_ARP, OP_SLOPPY, OP_STABS, OP_OFFBEAT, OP_332, OP_SYNC, OP_N };
static const char *const OP_NAME[OP_N] = {"AS IS", "CHORD", "STRUM", "ARP", "SLOPPY", "STABS", "OFFBT", "3-3-2", "SYNC"};
static const uint16_t OP_PAT[4] = {0x1111u, 0x4444u, 0x4949u, 0x1489u};   /* STABS, OFFBEAT, 3-3-2, SYNC */
static const char *const SHAPE_NAME[6] = {"ROOT POS", "INV 1", "INV 2", "INV 3", "WIDE", "RANDOM"};
/* ORCHID's K4 sets the chord FX: STRUM, ARP, SLOPPY and PERFORM, with OCT- held "how much", OCT+ the voicing */
static void om_apply(void)
{
    uint32_t a = set.om_amt > 10u ? 10u : set.om_amt;
    set.strum = set.om_play == OP_STRUM ? (uint8_t)(5u + 9u * a) : 0u;
    set.arp = set.om_play == OP_ARP ? 1u : 0u;
    set.arprate = 3;
    set.human = set.om_play == OP_SLOPPY || set.om_play >= OP_STABS ? (uint8_t)(5u * a) : 0u;
    set.inv = (int8_t)(set.om_shape >= 1u && set.om_shape <= 3u ? set.om_shape : 0);
}
static int om_collapse(void)             /* ORCHID: a phrase pad played as its chord */
{
    return orchid && set.om_play != OP_ASIS && set.om_play != OP_SLOPPY;
}
/* ORCHID's voicing past the inversions: WIDE (every other note up an octave), RANDOM (notes up or down an
 * octave, new each press); and ARP's octaves. v low to high; returns the new count */
static uint32_t om_voice(int32_t *v, uint32_t n, uint32_t max)
{
    uint32_t k, o, n0 = n;
    if (!orchid)
        return n;
    if (set.om_shape == 4u)
        for (k = 1; k < n; k += 2u)
            v[k] += 12;
    else if (set.om_shape == 5u)
        for (k = 0; k < n; k++)
            v[k] += 12 * ((int32_t)(cs_rnd() % 3u) - 1);
    if (set.om_play == OP_ARP)           /* 1, 2 or 3 octaves */
        for (o = 1; o <= (set.om_amt > 10u ? 10u : set.om_amt) / 4u; o++)
            for (k = 0; k < n0 && n < max; k++)
                v[n++] = v[k] + 12 * (int32_t)o;
    for (k = 1; k < n; k++)              /* low to high again */
        for (o = k; o > 0 && v[o - 1] > v[o]; o--) {
            int32_t t = v[o];
            v[o] = v[o - 1];
            v[o - 1] = t;
        }
    return n;
}

static int file_load(int32_t i)
{
    if (i < 0 || (uint32_t)i >= nfiles || fs_open(&files[i]) || smf_load()) {
        song_n = 0;
        nsl = 0;
        return -1;
    }
    cur_file = i;
    tempo = set.bpm ? set.bpm : song_bpm;
    sel = 0;
    slice_auto();
    if (orchid) {                        /* ORCHID: on the 16 pads at once, from the start */
        set.om_start = 0;
        orchid_fill();
    }
    return 0;
}


/* ======================================================================= ORCHID === */
/* ORCHID, the default: the file loaded is already on the 16 pads. PRESET picks how it's cut, K1 where in it the 16
 * start. Hold HOME for FM mode (everything else), tap HOME to come back, as ORCHID was. */
static void text_cat(char *t, const char *add);
enum { OM_EQUAL, OM_BARS, OM_2BARS, OM_HITS, OM_CHORDS, OM_N };
static const char *const OM_NAME[OM_N] = {"EQUAL", "BARS", "2 BARS", "HITS", "CHORDS"};
static uint8_t om_root[MAXSL], om_qual[MAXSL];   /* CHORDS: each slice's chord (root, 0 major 1 minor) */

/* CHORDS: a cut wherever the harmony changes, beat by beat: the major or minor triad that best fits the notes
 * sounding in each beat (by how long they sound), runs of the same one a slice */
static void chord_slices(void)
{
    uint32_t b, k, nb = (song_len + PPQ - 1u) / PPQ, cur = 0xFFu;
    nsl = 0;
    for (b = 0; b < nb && nsl < MAXSL; b++) {
        uint32_t w[12] = {0}, t0 = b * PPQ, t1 = t0 + PPQ, best = 0xFFu, bs = 0, r, q, any = 0;
        for (k = 0; k < song_n; k++) {
            const note_t *n = &song[k];
            uint32_t s = N_START(n), e = s + n->d;
            if (s >= t1)
                break;
            if (e <= t0 || !note_used(n) || trk[N_TRK(n) % MAXTRK].drums)
                continue;
            w[n->p % 12u] += (e < t1 ? e : t1) - (s > t0 ? s : t0);
            any = 1;
        }
        if (!any)
            continue;                    /* a rest: the chord before carries on */
        for (r = 0; r < 12u; r++)
            for (q = 0; q < 2u; q++) {
                uint32_t sc = 2u * w[r] + w[(r + (q ? 3u : 4u)) % 12u] + w[(r + 7u) % 12u];
                if (sc > bs) {
                    bs = sc;
                    best = r << 1 | q;
                }
            }
        if (best != cur) {
            if (nsl)
                sl_e[nsl - 1u] = t0;
            sl_s[nsl] = t0;
            om_root[nsl] = (uint8_t)(best >> 1);
            om_qual[nsl] = (uint8_t)(best & 1u);
            nsl++;
            cur = best;
        }
    }
    if (nsl)
        sl_e[nsl - 1u] = song_len > sl_s[nsl - 1u] ? song_len : sl_s[nsl - 1u] + PPQ;
}

static uint32_t om_first(void)            /* the slice on pad 1 */
{
    uint32_t mx = nsl > 16u ? nsl - 16u : 0u;
    return set.om_start < mx ? set.om_start : mx;
}

/* the 16 pads from the file, as PRESET and K1 say (no file: the key's chords, as always) */
static void orchid_fill(void)
{
    uint32_t i, first;
    if (!song_n)
        return;
    players_stop();
    if (set.om_slice == OM_EQUAL) {
        nsl = 0;
        for (i = 0; i < 16u; i++) {
            sl_s[nsl] = song_len * i / 16u;
            sl_e[nsl] = song_len * (i + 1u) / 16u;
            if (sl_e[nsl] > sl_s[nsl])
                nsl++;
        }
    } else if (set.om_slice == OM_CHORDS)
        chord_slices();
    else {
        sl_mode = set.om_slice != OM_HITS;
        sl_grid = set.om_slice == OM_2BARS ? 5u : 4u;
        slice_auto();
    }
    if (set.om_slen) {                   /* ORCHID's slice length: each slice x1/4 .. x4 from its start */
        for (i = 0; i < nsl; i++) {
            uint32_t L = sl_e[i] - sl_s[i];
            L = set.om_slen > 0 ? L << set.om_slen : L >> -set.om_slen;
            if (L < 24u)
                L = 24u;
            sl_e[i] = sl_s[i] + L < song_len ? sl_s[i] + L : song_len;
        }
    }
    first = set.om_slice == OM_EQUAL ? 0u : om_first();
    for (i = 0; i < 16u; i++) {
        pad_t *p = &pads[i];
        if (first + i < nsl) {
            slice_to(first + i, i);
            p->title[0] = 0;
            if (set.om_slice == OM_CHORDS) {   /* its chord's name on the pad */
                char t[8];
                pc_name(om_root[first + i], t);
                if (om_qual[first + i])
                    text_cat(t, "m");
                str_set(p->title, t, sizeof p->title);
            }
        } else {
            p->type = PAD_EMPTY;
            p->pn = 0;
            p->n = 0;
            p->title[0] = 0;
        }
    }
    sel = first;
}

static uint8_t first_boot;               /* no settings yet: GUIDE opens by itself */
void app_init(void)
{
    first_boot = plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set;
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.root > 11u || set.scale >= NSCALES || set.octave < -2 || set.octave > 2 || set.kit < 1u ||
        set.kit > NKITS || set.steps < -7 || set.steps > 7 || set.arp > 4u || set.arprate > 5u || set.human > 50u ||
        set.inv < -3 || set.inv > 3 || set.strum > 100u || set.om_slice >= OM_N)
        settings_defaults();
    disk_init();
    kit_load(set.kit);
    if (set.bpm)
        tempo = set.bpm;
    if (set.om_len < -5 || set.om_len > 5 || set.om_slen < -2 || set.om_slen > 2 || set.om_vcurve < -5 ||
        set.om_vcurve > 5)
        settings_defaults();
    if (set.mode >= MODE_N || set.om_f4 >= FK_N || set.om_play >= OP_N || set.om_shape > 5u || set.om_amt > 10u)
        settings_defaults();
    om_apply();                          /* ORCHID first */
    if (nfiles)
        file_load(0);
}

/* ======================================================================= UI === */
enum { SCR_HOME, SCR_SLICE, SCR_FILES, SCR_DISK, SCR_KEY, SCR_FX, SCR_LIB, SCR_TRIM, SCR_FIND, SCR_GUIDE };
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    int32_t pad;                         /* the pad played last (-1 none yet) */
    int32_t root_lit;                    /* the black key lit as the root (-1 none) */
    int32_t lead_now, lead_next;         /* LEAD: the degrees of the top note sounding and the next (-1 none) */
    uint8_t f_hold;                      /* F1 .. F4 held (1 .. 4), 0 none */
    uint32_t f_at;                       /* when it went down */
    uint8_t guide_pg;                    /* GUIDE: the page */
    uint32_t home_at, sys_at;            /* HOME, SYS went down then (0: up; all ones: held) */
    uint8_t oct_mod;                     /* OCT held and used as a modifier: its release steps nothing */
    uint8_t screen, disk_state, trk_cur, fx_page;
    int32_t file_cur;
    uint8_t redraw;
} ui = {.pad = -1, .root_lit = -1, .lead_now = -1, .lead_next = -1};

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
    set.fmove = 0;                       /* a key picked by hand is the new home */
    kit_build();
    set_dirty = 1;
    tonic_name(t);
    text_cat(t, " ");
    text_cat(t, SCALE_NAME[set.scale]);
    app_say("KEY", t);
}

static void pad_title(const pad_t *p, char *t)   /* "S3 Am9", "S5 2 BARS", "Bm7b5" */
{
    t[0] = 0;
    if (p->tag) {
        char d[4] = {'S', (char)('0' + p->tag / 10u), (char)('0' + p->tag % 10u), 0};
        if (p->tag < 10u) {
            d[1] = d[2];
            d[2] = 0;
        }
        text_cat(t, d);
        text_cat(t, " ");
    }
    if (p->title[0]) {
        text_cat(t, p->title);
    } else if (p->type == PAD_CHORD) {
        text_cat(t, p->nm[0]);
        text_cat(t, p->qual);
    } else {
        char l[12];
        fmt_len(p->len, l);
        text_cat(t, l);
    }
}

/* a chord pad's notes as they will sound: inverted, a bass note under it, the octave, the scale lock; into cbuf */
static void chord_build(uint32_t i)
{
    const pad_t *p = &pads[i];
    int32_t v[MAXC + 1], out[MAXC + 1];
    uint32_t n = 0, k, j, no = 0;
    int32_t inv = set.inv;
    for (k = 0; k < p->n && n < MAXC; k++)
        v[n++] = p->note[k];
    for (k = 1; k < n; k++)              /* low to high */
        for (j = k; j > 0 && v[j - 1] > v[j]; j--) {
            int32_t t = v[j];
            v[j] = v[j - 1];
            v[j - 1] = t;
        }
    while (n > 1 && inv != 0) {          /* an inversion: the lowest note up an octave, or the highest down */
        if (inv > 0) {
            int32_t lo = v[0] + 12;
            for (k = 0; k + 1 < n; k++)
                v[k] = v[k + 1];
            v[n - 1] = lo;
            inv--;
        } else {
            int32_t hi = v[n - 1] - 12;
            for (k = n - 1; k > 0; k--)
                v[k] = v[k - 1];
            v[0] = hi;
            inv++;
        }
        for (k = 1; k < n; k++)
            for (j = k; j > 0 && v[j - 1] > v[j]; j--) {
                int32_t t = v[j];
                v[j] = v[j - 1];
                v[j - 1] = t;
            }
    }
    n = om_voice(v, n, MAXC);
    if (set.bass && n) {                 /* the root, an octave under the lowest note */
        int32_t r = v[0] - (((v[0] - p->root_pc) % 12 + 12) % 12);   /* the root at or under the lowest note */
        out[no++] = r - 12;
    }
    for (k = 0; k < n; k++)
        out[no++] = v[k];
    cbuf_n[i] = 0;
    for (k = 0; k < no && cbuf_n[i] < MAXC; k++) {
        int32_t m = clamp(keyed(out[k] + 12 * set.octave + p->tune), 0, 127);
        for (j = 0; j < cbuf_n[i]; j++)
            if (cbuf[i][j] == (uint8_t)m)
                break;
        if (j == cbuf_n[i])
            cbuf[i][cbuf_n[i]++] = (uint8_t)m;
    }
}

static void pad_play(uint32_t i, uint32_t k, uint32_t vel)
{
    const pad_t *p = &pads[i];
    (void)k;
    vel = vel * (100u - (p->cut > 100u ? 100u : p->cut)) / 100u;
    if (!vel)
        vel = 1;
    cpat[i] = orchid && set.om_play >= OP_STABS ? OP_PAT[set.om_play - OP_STABS] : 0u;
    if (p->type == PAD_CHORD) {
        chord_build(i);
        ev_push(QE_CSTART, i, vel);
    } else if (p->type == PAD_PHRASE && om_collapse()) {   /* ORCHID: the slice as its chord */
        int32_t v[MAXC * 2u];
        uint32_t n = 0, k, j, e = pad_end(p);
        for (k = 0; k < p->pn && k < MAXPH; k++) {
            const pnote_t *x = &ph[i][k];
            int32_t m = (int32_t)x->p;
            if (x->s < p->ts || x->s >= e)
                continue;
            for (j = 0; j < n && (v[j] - m) % 12; j++)
                ;
            if (j < n) {                 /* a pitch class again: the lower one */
                if (m < v[j])
                    v[j] = m;
            } else if (n < MAXC)
                v[n++] = m;
        }
        for (k = 1; k < n; k++)
            for (j = k; j > 0 && v[j - 1] > v[j]; j--) {
                int32_t t = v[j];
                v[j] = v[j - 1];
                v[j - 1] = t;
            }
        for (k = 0; set.inv > 0 && n > 1 && k < (uint32_t)set.inv; k++) {   /* the inversions */
            int32_t lo = v[0] + 12;
            for (j = 0; j + 1 < n; j++)
                v[j] = v[j + 1];
            v[n - 1] = lo;
        }
        n = om_voice(v, n, MAXC);
        cbuf_n[i] = 0;
        for (k = 0; k < n && cbuf_n[i] < MAXC; k++)
            cbuf[i][cbuf_n[i]++] = (uint8_t)clamp(keyed(v[k] + 12 * set.octave + p->tune), 0, 127);
        ev_push(QE_CSTART, i, vel);
    } else if (p->type == PAD_PHRASE) {
        if (p->pmode == PM_LOOP && pl[i].on)   /* LOOP: a second press stops it */
            ev_push(QE_PSTOP, i, 8);
        else
            ev_push(QE_PSTART, i, (uint32_t)(set.octave + 8));
    }
    ui.pad = (int32_t)i;
    ui.root_lit = p->deg < 7u ? p->deg : -1;
}

static void preview_pad(void);
static void preview_slice(void)
{
    if (!nsl || slice_to(sel, PREVIEW))
        return;
    preview_pad();
}
static void preview_lib(void)
{
    lib_make(lib_sel, PREVIEW);
    preview_pad();
}
static void preview_pad(void)            /* the PREVIEW slot played once; a chord as a held beat */
{
    if (pads[PREVIEW].type == PAD_CHORD) {
        uint32_t k;
        if (pads[PREVIEW].len < 96u)
            pads[PREVIEW].len = 96;
        pads[PREVIEW].pn = 0;
        for (k = 0; k < pads[PREVIEW].n; k++) {
            ph[PREVIEW][k].s = 0;
            ph[PREVIEW][k].d = (uint16_t)(pads[PREVIEW].len - 8u);
            ph[PREVIEW][k].p = pads[PREVIEW].note[k];
            ph[PREVIEW][k].v = 100;
            pads[PREVIEW].pn++;
        }
    }
    ev_push(QE_PSTART, PREVIEW, 8);
}

/* REC stopped: the capture written out as RECnn.MID, then loaded and sliced */
static uint8_t smfbuf[RECMAX * 8u + 64u];
static void rec_save(void)
{
    uint32_t i, n = rec_n, w = 0, mtrk, last = 0, t0 = 0, nn;
    char name[12] = "REC01   MID";
    if (!n) {
        app_say("REC", "NOTHING CAME IN");
        return;
    }
    for (i = 0; i < n; i++)              /* the first note on starts the file */
        if (rec_ev[i].st == 0x90u) {
            t0 = rec_ev[i].t;
            break;
        }
    for (nn = 1; nn < 100u; nn++) {      /* the first free RECnn */
        uint32_t f, used = 0;
        name[3] = (char)('0' + nn / 10u);
        name[4] = (char)('0' + nn % 10u);
        for (f = 0; f < nfiles; f++)
            if (files[f].name[0] == 'R' && files[f].name[1] == 'E' && files[f].name[2] == 'C' &&
                files[f].name[3] == name[3] && files[f].name[4] == name[4])
                used = 1;
        if (!used)
            break;
    }
    {
        static const uint8_t HDR[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, PPQ,
                                      'M', 'T', 'r', 'k', 0, 0, 0, 0};
        uint32_t us = 60000000u / tempo;
        __builtin_memcpy(smfbuf, HDR, sizeof HDR);
        w = sizeof HDR;
        mtrk = w;
        smfbuf[w++] = 0;                 /* the tempo */
        smfbuf[w++] = 0xFF;
        smfbuf[w++] = 0x51;
        smfbuf[w++] = 3;
        smfbuf[w++] = (uint8_t)(us >> 16);
        smfbuf[w++] = (uint8_t)(us >> 8);
        smfbuf[w++] = (uint8_t)us;
    }
    for (i = 0; i < n; i++) {
        const recev_t *r = &rec_ev[i];
        uint32_t t, d, v, k, bytes[4], nb = 0;
        if (r->t < t0)
            continue;
        t = (uint32_t)((float)(r->t - t0) * (float)(tempo * PPQ) / (60.0f * 44100.0f));
        d = t >= last ? t - last : 0u;
        last = t;
        v = d;
        bytes[nb++] = v & 0x7Fu;
        while ((v >>= 7) && nb < 4u)
            bytes[nb++] = (v & 0x7Fu) | 0x80u;
        for (k = nb; k-- > 0;)
            smfbuf[w++] = (uint8_t)bytes[k];
        smfbuf[w++] = r->st;
        smfbuf[w++] = r->d1;
        smfbuf[w++] = r->d2;
    }
    smfbuf[w++] = 0;                     /* the end of the track */
    smfbuf[w++] = 0xFF;
    smfbuf[w++] = 0x2F;
    smfbuf[w++] = 0;
    {
        uint32_t len = w - mtrk;
        smfbuf[mtrk - 4u] = (uint8_t)(len >> 24);
        smfbuf[mtrk - 3u] = (uint8_t)(len >> 16);
        smfbuf[mtrk - 2u] = (uint8_t)(len >> 8);
        smfbuf[mtrk - 1u] = (uint8_t)len;
    }
    if (fs_write(name, smfbuf, w)) {
        app_say("REC", "DRIVE FULL");
        return;
    }
    fs_scan();
    for (i = 0; i < nfiles; i++)
        if (files[i].name[0] == 'R' && files[i].name[3] == name[3] && files[i].name[4] == name[4]) {
            file_load((int32_t)i);
            ui.file_cur = (int32_t)i;
            break;
        }
    ui.screen = SCR_SLICE;
    {
        char t[12] = "REC00.MID";
        t[3] = name[3];
        t[4] = name[4];
        app_say("SAVED", t);
    }
}

static void disk_enter(void)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        notes_off(k);
    dc_flush();
    plat_disk_mode(&DISK);
    ui.screen = SCR_DISK;
    ui.disk_state = PLAT_DISK_WAITING;
    ui.redraw = 1;
}
static void disk_leave(void)
{
    plat_disk_mode(0);
    dc_sec = -1;                         /* the computer may have written anything */
    fs_scan();
    ui.file_cur = 0;
    file_load(nfiles ? 0 : -1);
    ui.screen = nfiles ? SCR_FILES : SCR_HOME;
    ui.redraw = 1;
}

void app_loop(void)
{
    if (ui.screen == SCR_DISK) {
        int st = plat_disk_poll();
        if (st != ui.disk_state) {
            ui.disk_state = (uint8_t)st;
            ui.redraw = 1;
        }
        if (st == PLAT_DISK_EJECTED)
            disk_leave();
    }
}

/* FIND (REC and PLAY together): tap a few notes on any keys and pick a key that has them all; it opens LIB */
#define FD_MAX 12u
static uint8_t fd_seq[FD_MAX], fd_n;     /* the pitch classes tapped, in order, each once */
static uint16_t fd_pcs;
static uint8_t fd_list[12u * NSCALES], fd_cnt, fd_sel;   /* the keys that fit: root << 4 | scale, best first */
static uint16_t scale_mask(uint32_t root, uint32_t scale)
{
    uint32_t j;
    uint16_t m = 0;
    for (j = 0; j < 7u; j++)
        m |= (uint16_t)(1u << (root + SCALES[scale][j]) % 12u);
    return m;
}
static int32_t fd_score(uint32_t root, uint32_t scale)
{
    /* the first note tapped as the tonic, then any note tapped, then major and minor before the modes */
    return (fd_n && root == fd_seq[0] ? 40 : 0) + ((fd_pcs >> root) & 1u ? 20 : 0) +
           (scale <= 1u ? 10 : scale <= 5u ? 5 : 0);
}
static void fd_rank(void)
{
    uint32_t r, sc, i, j;
    fd_cnt = 0;
    fd_sel = 0;
    if (!fd_n)
        return;
    for (r = 0; r < 12u; r++) {
        uint32_t root = (fd_seq[0] + r) % 12u;   /* from the first note up, so ties read from there */
        for (sc = 0; sc < NSCALES; sc++)
            if ((scale_mask(root, sc) & fd_pcs) == fd_pcs)
                fd_list[fd_cnt++] = (uint8_t)(root << 4 | sc);
    }
    for (i = 1; i < fd_cnt; i++) {       /* best first; equal scores keep their order */
        uint8_t x = fd_list[i];
        int32_t sx = fd_score(x >> 4, x & 15u);
        for (j = i; j > 0 && fd_score(fd_list[j - 1] >> 4, fd_list[j - 1] & 15u) < sx; j--)
            fd_list[j] = fd_list[j - 1];
        fd_list[j] = x;
    }
}
static void fd_name(uint32_t e, char *t)   /* "D MAJOR" */
{
    spell(e >> 4, letter_of(e >> 4, e & 15u), t);
    text_cat(t, " ");
    text_cat(t, SCALE_NAME[e & 15u]);
}
static void find_enter(void)
{
    uint32_t k;
    if (rec_on) {                        /* REC went down first: no take */
        rec_on = 0;
        rec_n = 0;
    }
    players_stop();
    for (k = 0; k < NKEYS; k++)
        notes_off(k);
    fd_n = 0;
    fd_pcs = 0;
    fd_rank();
    ui.screen = SCR_FIND;
    app_say("FIND KEY", "TAP A FEW NOTES");
}
static void find_keys(uint32_t keys)     /* every key a note, from F3 */
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint8_t m = (uint8_t)clamp(53 + (int32_t)k + 12 * set.octave, 0, 127);
            uint32_t vel = touch_vel(k), pc = m % 12u;
            notes_on(k, &m, 1, vel ? vel : 100u);
            if (!((fd_pcs >> pc) & 1u) && fd_n < FD_MAX) {
                fd_seq[fd_n++] = (uint8_t)pc;
                fd_pcs |= (uint16_t)(1u << pc);
                fd_rank();
            }
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit))
            notes_off(k);
    }
}
static void find_use(void)
{
    char t[24];
    if (!fd_cnt)
        return;
    set.root = (uint8_t)(fd_list[fd_sel] >> 4);
    set.scale = (uint8_t)(fd_list[fd_sel] & 15u);
    set.fmove = 0;
    kit_build();
    set_dirty = 1;
    lib_sel = 0;
    ui.screen = SCR_LIB;
    fd_name(fd_list[fd_sel], t);
    app_say("KEY", t);
}

/* F1-F4: the key changes, each from the home key */
static const char *const FK_NAME[4] = {"F1 RELATIVE", "F2 PARALLEL", "F3 UP A 4TH", "F4 UP A STEP"};
static uint32_t fk_target(uint32_t mv)   /* the key move mv (0: home) goes to, root << 4 | scale */
{
    uint32_t h = set.fmove ? set.home : (uint32_t)(set.root << 4 | set.scale), r = h >> 4, sc = h & 15u;
    int minor = SCALES[sc][2] == 3u;
    if (mv == 1u)                        /* relative: the same notes */
        return minor ? ((r + 3u) % 12u) << 4 | 1u : ((r + 9u) % 12u) << 4 | 0u;
    if (mv == 2u)                        /* parallel: the same tonic */
        return r << 4 | (minor ? 1u : 0u);
    if (mv == 3u)
        return ((r + 5u) % 12u) << 4 | sc;
    if (mv == 4u)
        return ((r + 2u) % 12u) << 4 | sc;
    return h;
}
static void fk_go(uint32_t mv)           /* a tap: there, or home again */
{
    uint32_t e;
    if (set.fmove == mv)
        mv = 0;
    if (!set.fmove)
        set.home = (uint8_t)(set.root << 4 | set.scale);
    e = fk_target(mv);
    set.root = (uint8_t)(e >> 4);
    set.scale = (uint8_t)(e & 15u);
    set.fmove = (uint8_t)mv;
    kit_build();
    set_dirty = 1;
}
/* SUGGEST, OUTSIDE, BORROW: F-key i's notes (up to 3) and name */
static uint32_t fk_notes(uint32_t i, uint8_t *m, char *name)
{
    static const uint8_t SUCC[7][4] = {{3, 4, 5, 1}, {4, 6, 3, 0}, {5, 3, 1, 4}, {4, 0, 1, 5},
                                       {0, 5, 3, 2}, {1, 3, 4, 2}, {0, 2, 5, 4}};
    static const uint8_t OUT_ORDER[11] = {3, 10, 6, 8, 1, 11, 4, 9, 2, 5, 7};
    static const uint8_t BOR_MAJ[4][3] = {{5, 8, 12}, {8, 12, 15}, {10, 14, 17}, {3, 7, 10}};   /* iv bVI bVII bIII */
    static const uint8_t BOR_MIN[4][3] = {{5, 9, 12}, {7, 11, 14}, {1, 5, 8}, {0, 4, 7}};      /* IV V bII I */
    int32_t b = base_note() + 12 * set.octave, k;
    uint16_t sm = scale_mask(set.root, set.scale);
    if (set.om_f4 == FK_SUGGEST) {       /* what tends to come after the pad played last: the key's triads */
        int32_t d = 0, r;
        if (ui.pad >= 0) {
            const pad_t *p = &pads[ui.pad];
            if (p->type == PAD_CHORD)
                d = deg_of((int32_t)p->root_pc + 60);
            else if (p->type == PAD_PHRASE && p->pn) {
                int32_t lo = 127;
                for (k = 0; k < (int32_t)p->pn && k < (int32_t)MAXPH; k++)
                    if (ph[ui.pad][k].p < lo)
                        lo = ph[ui.pad][k].p;
                d = deg_of(lo);
            }
        }
        d = SUCC[d % 7][i];
        for (k = 0; k < 3; k++)
            m[k] = (uint8_t)clamp(deg_pitch(d + 2 * k) + 12 * set.octave, 0, 127);
        r = m[0] % 12;
        pc_name((uint32_t)r, name);
        if ((m[1] - m[0]) % 12 == 3)
            text_cat(name, (m[2] - m[0]) % 12 == 6 ? "DIM" : "M");
        return 3;
    }
    if (set.om_f4 == FK_OUTSIDE) {       /* notes off the scale, the bluest first */
        uint32_t c = 0, j;
        for (j = 0; j < 11u; j++)
            if (!(sm >> ((set.root + OUT_ORDER[j]) % 12u) & 1u) && c++ == i) {
                m[0] = (uint8_t)clamp(b + OUT_ORDER[j], 0, 127);
                pc_name((set.root + OUT_ORDER[j]) % 12u, name);
                return 1;
            }
        m[0] = (uint8_t)clamp(b, 0, 127);
        pc_name(set.root, name);
        return 1;
    }
    {                                    /* BORROW: from the parallel key */
        const uint8_t *c = (minorish() ? BOR_MIN : BOR_MAJ)[i];
        for (k = 0; k < 3; k++)
            m[k] = (uint8_t)clamp(b + c[k], 0, 127);
        pc_name((set.root + c[0]) % 12u, name);
        if (c[1] - c[0] == 3)
            text_cat(name, "M");
        return 3;
    }
}
static uint32_t fk_pivots(void)          /* held: the chord pads whose notes are all in the key it goes to */
{
    uint32_t e = fk_target(set.fmove == ui.f_hold ? 0u : ui.f_hold), i, k, out = 0;
    uint16_t m = scale_mask(e >> 4, e & 15u);
    for (i = 0; i < NPADS; i++) {
        const pad_t *p = &pads[i];
        uint32_t ok = p->type == PAD_CHORD && p->n;
        for (k = 0; ok && k < p->n; k++)
            ok = (m >> (p->note[k] % 12u)) & 1u;
        out |= ok << i;
    }
    return out;
}

static void guide_open(void);           /* (guide.c) */
static void guide_lights(void);
static uint32_t guide_count(void);

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), i;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    if (ui.screen == SCR_FIND) {
        find_keys(keys);
        goto buttons;
    }
    /* white keys: the pads; in SLICE, a key drops the selected slice on its pad */
    for (i = 0; i < 16u; i++) {
        uint32_t k = WK[i], bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t vel = touch_vel(k);
            if (ui.screen == SCR_LIB) {
                char t[20];
                ev_push(QE_PSTOP, i, 8);
                lib_make(lib_sel, i);
                pad_title(&pads[i], t);
                app_say("PAD", t);
            }
            if (ui.screen == SCR_SLICE && nsl) {
                char t[16], d[4] = {'P', 'A', 'D', 0};
                ev_push(QE_PSTOP, i, 8);
                if (!slice_to(sel, i)) {
                    char n[8] = " ";
                    pad_title(&pads[i], t);
                    n[1] = (char)('0' + (i + 1u) / 10u);
                    n[2] = (char)('0' + (i + 1u) % 10u);
                    n[3] = 0;
                    if (i + 1u < 10u) {
                        n[1] = n[2];
                        n[2] = 0;
                    }
                    text_cat(d, n);
                    app_say(d, t);
                }
            }
            pad_play(i, k, vel ? vel : 100u);
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit)) {
            ev_push(QE_CSTOP, i, 0);
            if (pads[i].type == PAD_PHRASE && pads[i].pmode == PM_GATE)
                ev_push(QE_PSTOP, i, 8);  /* GATE: only while held */
            ui.redraw = 1;
        }
    }
    /* black keys 1-7: the scale degrees. ROOT: a bass note an octave under the pads; LEAD: a lead note an octave
     * over them, to play along with the phrase the keys light up */
    for (i = 0; i < 7u; i++) {
        uint32_t k = BK[i], bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            int32_t lead = set.mode == MODE_LEAD;
            uint8_t m[2];
            uint32_t vel = touch_vel(k), nm = 1;
            if (set.mode == MODE_3RD || set.mode == MODE_5TH) {   /* the note and its 3rd or 5th, in the key */
                m[0] = (uint8_t)clamp(deg_pitch((int32_t)i) + 12 * set.octave, 0, 127);
                m[1] = (uint8_t)clamp(deg_pitch((int32_t)i + (set.mode == MODE_3RD ? 2 : 4)) + 12 * set.octave, 0, 127);
                nm = 2;
            } else
                m[0] = (uint8_t)clamp(deg_pitch((int32_t)i) + (lead ? 12 + 12 * set.octave : -12), 0, 127);
            notes_on(k, m, nm, vel ? vel : 100u);
            if (!lead)
                ui.root_lit = (int32_t)i;
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit)) {
            notes_off(k);
        }
    }
    /* black keys F1-F4: key changes from the home key, for a bridge. A tap goes there (and lights the key), a tap on
     * the lit one comes home; held, nothing changes yet: the screen names the key and the pads that fit it light */
    for (i = 0; i < 4u && set.om_f4 != FK_BRIDGE; i++) {   /* or SUGGEST, OUTSIDE, BORROW: they play */
        uint32_t k = BK[7u + i], bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint8_t m[3];
            char t[12];
            uint32_t n = fk_notes(i, m, t), vel = touch_vel(k);
            notes_on(k, m, n, vel ? vel : 100u);
            app_say(FK_MODE[set.om_f4 % FK_N], t);
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit))
            notes_off(k);
    }
    for (i = 0; i < 4u && set.om_f4 == FK_BRIDGE; i++) {
        uint32_t k = BK[7u + i], bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            char t[24];
            uint32_t mv = i + 1u, e = fk_target(set.fmove == mv ? 0u : mv);
            ui.f_hold = (uint8_t)mv;
            ui.f_at = plat_ms();
            fd_name(e, t);
            app_say(set.fmove == mv ? "HOME" : FK_NAME[i], t);
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (ui.f_hold == i + 1u && plat_ms() - ui.f_at < 500u)
                fk_go(i + 1u);
            if (ui.f_hold == i + 1u)
                ui.f_hold = 0;
            ui.redraw = 1;
        }
    }
buttons:
    for (i = 0; i < NB; i++) {
        uint32_t bit = 1u << i;
        int down = (btn & bit) && !(ui.btn & bit), up = !(btn & bit) && (ui.btn & bit);
        if (i == B_OCTDN || i == B_OCTUP) {
            uint32_t other = 1u << (i == B_OCTUP ? B_OCTDN : B_OCTUP);
            if (down && (btn & other)) { /* both: the scale lock on or off */
                set.lock ^= 1u;
                set_dirty = 1;
                ui.oct_mod = 1;
                kit_build();
                app_say("SCALE LOCK", set.lock ? "ON" : "OFF");
                continue;
            }
            if (down && !(ui.btn & other))
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
        if (ui.screen == SCR_DISK && i != B_HOME)
            continue;                    /* the drive is the computer's until it ejects (or HOME) */
        if (ui.screen == SCR_GUIDE)
            ui.screen = SCR_HOME;        /* a button leaves the guide and does what it does */
        if (orchid && (i == B_SEL || i == B_ENV || i == B_LFO || i == B_EDIT || i == B_FX))
            orchid = 0;                  /* FM mode's own screens: FM mode */
        if ((i == B_PLAY && (btn & 1u << B_REC)) || (i == B_REC && (btn & 1u << B_PLAY))) {
            find_enter();                /* REC and PLAY together: FIND */
            ui.redraw = 1;
            continue;
        }
        if (ui.screen == SCR_FIND && (i == B_PLAY || i == B_REC))
            continue;
        if (i == B_SAVE) {
            char t[8] = "KIT 0";
            t[5] = (char)('0' + set.kit);
            t[6] = 0;
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0 && kit_save() == 0) {
                set_dirty = 0;
                app_say("SAVED", t);
            } else {
                app_say("SAVE", "FAILED");
            }
        } else if (i == B_PLAY) {
            if (ui.screen == SCR_SLICE)
                preview_slice();
            else if (ui.screen == SCR_LIB)
                preview_lib();
            else if (ui.screen == SCR_KEY && song_n) {
                char t[24];
                key_detect();
                rekey();
                tonic_name(t);
                text_cat(t, " ");
                text_cat(t, SCALE_NAME[set.scale]);
                app_say("DETECTED", t);
            } else {
                uint32_t k;
                for (k = 0; k < NKEYS; k++)
                    notes_off(k);
                for (k = 0; k <= NPADS; k++) {
                    ev_push(QE_PSTOP, k, 8);
                    if (k < NPADS)
                        ev_push(QE_CSTOP, k, 0);
                }
                app_say("ALL", "NOTES OFF");
            }
        } else if (i == B_HOME) {
            if (ui.screen == SCR_DISK)
                disk_leave();
            ui.screen = SCR_HOME;
            ui.msg_until = 0;
        } else if (i == B_SEL) {         /* SLICE: the slices, again the files */
            if (ui.screen == SCR_SLICE)
                ui.screen = SCR_FILES;
            else
                ui.screen = song_n ? SCR_SLICE : SCR_FILES;
            if (ui.file_cur < 0 || (uint32_t)ui.file_cur >= nfiles)
                ui.file_cur = cur_file < 0 ? 0 : cur_file;
        } else if (i == B_SEQ) {         /* MODE: the black keys as roots or as the lead */
            set.mode = (uint8_t)((set.mode + 1u) % MODE_N);
            set_dirty = 1;
            app_say("MODE", MODE_NAME[set.mode % MODE_N]);
        } else if (i == B_ENV) {         /* TRIM: the pad played last (a white key there picks another) */
            if (ui.pad < 0 || pads[ui.pad].type == PAD_EMPTY)
                app_say("TRIM", "PLAY A PAD FIRST");
            else
                ui.screen = SCR_TRIM;
        } else if (i == B_EDIT) {        /* KEY */
            ui.screen = SCR_KEY;
        } else if (i == B_FX) {          /* FX: again, the second page */
            ui.fx_page = ui.screen == SCR_FX ? (uint8_t)!ui.fx_page : 0u;
            ui.screen = SCR_FX;
        } else if (i == B_LFO) {         /* LIB */
            ui.screen = SCR_LIB;
            if (lib_sel >= lib_count())
                lib_sel = 0;
        } else if (i == B_GLO) {         /* SYS: let go, the USB drive; held, GUIDE (below) */
        } else if (i == B_REC) {
            if (!rec_on) {
                rec_n = 0;
                rec_on = 1;
                app_say("REC", "PLAY MIDI IN");
            } else {
                rec_on = 0;
                rec_save();
            }
        } else {
            static const char *const LABEL[NB] = {"FX", "SLICE", "TRIM", "LIB", "KEY", "SYS", "HOME", "SAVE",
                                                  "ARP", "MODE", "PLAY", "REC", "OCT-", "OCT+"};
            app_say(LABEL[i], "COMING SOON");
        }
        ui.redraw = 1;
    }
    /* ALGORITHM (ROOT): the key's root; with OCT held, the scale */
    if ((e = plat_enc(EN_ALGO)) != 0 && orchid && ui.screen == SCR_HOME) {
        /* ORCHID: the notes' length; OCT- held: the slices' length; OCT+ held: the velocity curve */
        char t[8];
        if (btn & 1u << B_OCTDN) {
            static const char *const SL[5] = {"1/4", "1/2", "AS CUT", "X2", "X4"};
            ui.oct_mod = 1;
            set.om_slen = (int8_t)clamp(set.om_slen + e, -2, 2);
            orchid_fill();
            app_say("SLICE LENGTH", SL[set.om_slen + 2]);
        } else if (btn & 1u << B_OCTUP) {
            ui.oct_mod = 1;
            set.om_vcurve = (int8_t)clamp(set.om_vcurve + e, -5, 5);
            num_text(set.om_vcurve, t, 1);
            app_say("VELOCITY CURVE", set.om_vcurve ? t : "AS PLAYED");
        } else {
            set.om_len = (int8_t)clamp(set.om_len + e, -5, 5);
            app_say("NOTE LENGTH", LEN_NAME[set.om_len + 5]);
        }
        set_dirty = 1;
        ui.redraw = 1;
    } else if (e) {
        if (btn & (1u << B_OCTDN | 1u << B_OCTUP)) {
            ui.oct_mod = 1;
            set.scale = (uint8_t)(((int32_t)set.scale + e % (int32_t)NSCALES + (int32_t)NSCALES) % (int32_t)NSCALES);
        } else {
            set.root = (uint8_t)(((int32_t)set.root + e % 12 + 12) % 12);
        }
        rekey();
    }
    /* SELECT and K1 .. K4: what the screen's bottom bar says */
    {
        int32_t es = plat_enc(EN_SELECT), k1 = plat_enc(EN_K1), k2 = plat_enc(EN_K2), k3 = plat_enc(EN_K3),
                k4 = plat_enc(EN_K4);
        int32_t ek = plat_enc(EN_PRESET);
        if (orchid && ui.screen == SCR_HOME) {   /* ORCHID's knobs */
            if (ek) {                    /* PRESET: how the file is cut */
                set.om_slice = (uint8_t)clamp(set.om_slice + ek, 0, OM_N - 1);
                set.om_start = 0;
                set_dirty = 1;
                orchid_fill();
                app_say("SLICE", OM_NAME[set.om_slice]);
            }
            if (k1 && set.om_slice != OM_EQUAL) {   /* K1: where in the file the 16 start */
                int32_t mx = nsl > 16u ? (int32_t)nsl - 16 : 0;
                int32_t st = clamp((int32_t)om_first() + k1, 0, mx);
                if (st != (int32_t)set.om_start) {
                    set.om_start = (uint16_t)st;
                    set_dirty = 1;
                    orchid_fill();
                }
            }
            if (k2) {                    /* K2: the 7 black keys */
                set.mode = (uint8_t)clamp(set.mode + k2, MODE_ROOT, MODE_N - 1);
                set_dirty = 1;
                app_say("BLACK 7", MODE_NAME[set.mode]);
            }
            if (k3) {                    /* K3: the 4 extra black keys */
                set.om_f4 = (uint8_t)clamp(set.om_f4 + k3, 0, FK_N - 1);
                set_dirty = 1;
                app_say("BLACK 4", FK_MODE[set.om_f4]);
            }
            if (k4) {                    /* K4: how the pads play; OCT- held: how much; OCT+ held: the voicing */
                char t[8];
                if (btn & 1u << B_OCTDN) {
                    ui.oct_mod = 1;
                    set.om_amt = (uint8_t)clamp(set.om_amt + k4, 0, 10);
                    num_text(set.om_amt, t, 0);
                    app_say(set.om_play == OP_STRUM ? "STRUM WIDTH" : set.om_play == OP_ARP ? "ARP OCTAVES"
                            : "HUMANIZE", set.om_play == OP_ARP ? (set.om_amt < 4u ? "1" : set.om_amt < 8u ? "2" : "3") : t);
                } else if (btn & 1u << B_OCTUP) {
                    ui.oct_mod = 1;
                    set.om_shape = (uint8_t)clamp(set.om_shape + k4, 0, 5);
                    app_say("VOICING", SHAPE_NAME[set.om_shape]);
                } else {
                    set.om_play = (uint8_t)clamp(set.om_play + k4, 0, OP_N - 1);
                    app_say("PLAY", OP_NAME[set.om_play]);
                }
                om_apply();
                set_dirty = 1;
            }
            if (ek || k1 || k2 || k3 || k4)
                ui.redraw = 1;
            ek = k1 = k2 = k3 = k4 = 0;
        }
        if (ek) {                        /* KIT: the next kit, loaded at once (SAVE keeps changes first) */
            uint32_t nk = (uint32_t)clamp((int32_t)set.kit + ek, 1, (int32_t)NKITS);
            if (nk != set.kit) {
                char t[8] = "KIT 0";
                players_stop();
                set.kit = (uint8_t)nk;
                set_dirty = 1;
                t[5] = (char)('0' + nk);
                t[6] = 0;
                app_say(t, kit_load(nk) ? "" : "EMPTY: KEY CHORDS");
                ui.pad = -1;
            }
        }
        if (ui.screen == SCR_SLICE && nsl) {
            if (es) {
                sel = (uint32_t)clamp((int32_t)sel + es, 0, (int32_t)nsl - 1);
                preview_slice();
            }
            if (k1) {
                sl_mode ^= 1u;
                slice_auto();
            }
            if (k2) {
                if (sl_mode)
                    sl_grid = (uint8_t)clamp(sl_grid + k2, 0, 6);
                else
                    sl_sens = (uint8_t)clamp(sl_sens + 5 * k2, 0, 100);
                slice_auto();
            }
            if (k3 || k4) {              /* trim by 16ths */
                int32_t s = (int32_t)sl_s[sel] + 24 * k3, en = (int32_t)sl_e[sel] + 24 * k4;
                s = clamp(s, 0, (int32_t)sl_e[sel] - 24);
                en = clamp(en, s + 24, (int32_t)song_len);
                sl_s[sel] = (uint32_t)s;
                sl_e[sel] = (uint32_t)en;
                preview_slice();
            }
            if (es || k1 || k2 || k3 || k4)
                ui.redraw = 1;
        } else if (ui.screen == SCR_HOME || ui.screen == SCR_FX) {
            int page2 = ui.screen == SCR_FX && ui.fx_page;
            if (!page2) {
                if (k1)
                    set.strum = (uint8_t)clamp(set.strum + 5 * k1, 0, 100);
                if (k2)
                    set.arp = (uint8_t)clamp(set.arp + k2, 0, 4);
                if (k3)
                    set.human = (uint8_t)clamp(set.human + 5 * k3, 0, 50);
                if (k4 && ui.screen == SCR_FX)
                    set.inv = (int8_t)clamp(set.inv + k4, -3, 3);
                if (k4 && ui.screen == SCR_HOME) {   /* HOME's K4: the tempo */
                    set.bpm = (uint8_t)clamp((int32_t)tempo + k4, 40, 240);
                    tempo = set.bpm;
                }
            } else {
                if (k1)
                    set.arprate = (uint8_t)clamp(set.arprate + k1, 0, 5);
                if (k2)
                    set.bass = (uint8_t)(k2 > 0);
            }
            if (k1 || k2 || k3 || k4) {
                set_dirty = 1;
                ui.redraw = 1;
            }
        } else if (ui.screen == SCR_GUIDE) {
            if (es) {
                ui.guide_pg = (uint8_t)clamp((int32_t)ui.guide_pg + es, 0, (int32_t)guide_count() - 1);
                ui.redraw = 1;
            }
        } else if (ui.screen == SCR_FIND) {
            if (es && fd_cnt)
                fd_sel = (uint8_t)clamp((int32_t)fd_sel + es, 0, (int32_t)fd_cnt - 1);
            if (k1 && fd_n) {            /* UNDO: the last note */
                fd_n--;
                fd_pcs &= (uint16_t)~(1u << fd_seq[fd_n]);
                fd_rank();
            }
            if (k2) {                    /* CLEAR */
                fd_n = 0;
                fd_pcs = 0;
                fd_rank();
            }
            if (k4 > 0)
                find_use();
            if (es || k1 || k2 || k4)
                ui.redraw = 1;
        } else if (ui.screen == SCR_TRIM && ui.pad >= 0) {
            pad_t *p = &pads[ui.pad];
            if (p->type == PAD_PHRASE) {
                if (k1 || k2) {          /* by 16ths, at least one between them */
                    int32_t st = (int32_t)p->ts + 24 * k1, en = (int32_t)pad_end(p) + 24 * k2, len = p->len;
                    st = clamp(st, 0, len - 24 > 0 ? len - 24 : 0);
                    en = clamp(en, st + 24 < len ? st + 24 : len, len);
                    if (st >= en)
                        st = en > 24 ? en - 24 : 0;
                    p->ts = (uint16_t)st;
                    p->te = (uint16_t)(en >= len ? 0 : en);
                }
                if (es)
                    p->pmode = (uint8_t)clamp(p->pmode + es, PM_ONESHOT, PM_LOOP);
            }
            if (k3)
                p->tune = (int8_t)clamp(p->tune + k3, -12, 12);
            if (k4)
                p->cut = (uint8_t)(100 - clamp(100 - (int32_t)p->cut + 5 * k4, 0, 100));
            if (k3 && p->type == PAD_CHORD && cp[ui.pad].on) {
                chord_build((uint32_t)ui.pad);   /* heard at once while held */
            }
            if (es || k1 || k2 || k3 || k4)
                ui.redraw = 1;
        } else if (ui.screen == SCR_KEY) {
            if (k1)
                set.root = (uint8_t)(((int32_t)set.root + k1 % 12 + 12) % 12);
            if (k2)
                set.scale = (uint8_t)(((int32_t)set.scale + k2 % (int32_t)NSCALES + (int32_t)NSCALES) % (int32_t)NSCALES);
            if (k3)
                set.steps = (int8_t)clamp(set.steps + k3, -7, 7);
            if (k4)
                set.lock = (uint8_t)(k4 > 0);
            if (k1 || k2 || k3 || k4) {
                if (k1 || k2)
                    set.fmove = 0;
                kit_build();
                set_dirty = 1;
                ui.redraw = 1;
            }
        } else if (ui.screen == SCR_LIB) {
            if (es) {
                lib_sel = (uint8_t)clamp((int32_t)lib_sel + es, 0, (int32_t)lib_count() - 1);
                preview_lib();
            }
            if (k1)
                lib_type = (uint8_t)clamp(lib_type + k1, 0, (int32_t)NCTYPES - 1);
            if (k2)
                lib_rhy = (uint8_t)clamp(lib_rhy + k2, 0, (int32_t)NRHY - 1);
            if (k3) {
                lib_grp = (uint8_t)(k3 > 0);
                lib_sel = 0;
            }
            if (k4 > 0) {
                players_stop();
                lib_build_kit();
                app_say("KIT BUILT", "8 CHORDS, 8 PROGS");
            }
            if (es || k1 || k2 || k3 || k4)
                ui.redraw = 1;
        } else if (ui.screen == SCR_FILES) {
            if (es && nfiles) {
                ui.file_cur = clamp(ui.file_cur + es, 0, (int32_t)nfiles - 1);
                if (file_load(ui.file_cur))
                    app_say("CAN'T READ", files[ui.file_cur].name);
                ui.trk_cur = 0;
            }
            if (k1 && ntrk)
                ui.trk_cur = (uint8_t)clamp(ui.trk_cur + k1, 0, (int32_t)ntrk - 1);
            if (k2 && ntrk) {
                trk[ui.trk_cur].on ^= 1u;
                if (!sl_mode)            /* the grid's cuts stay; the hits move with the notes */
                    slice_auto();
            }
            if (es || k1 || k2)
                ui.redraw = 1;
        }
    }
    {                                    /* HOME and SYS: a tap, or held 0.8 s */
        uint32_t now = plat_ms(), j;
        for (j = 0; j < 2u; j++) {
            uint32_t b = j ? B_GLO : B_HOME, *at = j ? &ui.sys_at : &ui.home_at;
            if (btn & 1u << b) {
                if (!*at)
                    *at = now ? now : 1u;
                else if (*at != 0xFFFFFFFFu && now - *at >= 800u) {
                    *at = 0xFFFFFFFFu;   /* once a hold */
                    if (j)
                        guide_open();    /* SYS held: GUIDE */
                    else {               /* HOME held: FM mode's HOME (from ORCHID: as ORCHID left it) */
                        if (orchid)
                            app_say("FM MODE", "TAP HOME: ORCHID");
                        orchid = 0;
                        ui.screen = SCR_HOME;
                    }
                }
            } else {
                if (*at && *at != 0xFFFFFFFFu && ui.screen != SCR_GUIDE) {   /* a tap */
                    if (j)
                        disk_enter();
                    else if (!orchid) {  /* HOME tapped in FM mode: ORCHID, as it was */
                        orchid = 1;
                        ui.screen = SCR_HOME;
                        om_apply();
                        orchid_fill();
                        app_say("ORCHID", OM_NAME[set.om_slice]);
                    }
                }
                *at = 0;
            }
        }
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

/* the lights: loaded pads dim, held or playing pads full, an empty pad dark; in SLICE the pads breathe as drop
 * targets (the mid level); the seven degree keys dim, the root full; REC lit while recording, SAVE while changed */
/* the scale degree (0 .. 6) of a MIDI note in the key: its own, else the nearest (a tie goes down) */
static int32_t deg_of(int32_t m)
{
    const uint8_t *sc = SCALES[set.scale];
    int32_t rel = ((m - (int32_t)set.root) % 12 + 12) % 12, best = 0, bd = 99, j;
    for (j = 0; j < 7; j++) {
        int32_t d = rel - (int32_t)sc[j];
        if (d < -6)
            d += 12;
        if (d > 6)
            d -= 12;
        if (d < 0)
            d = -d * 2 + 1;              /* above the note: loses a tie */
        else
            d *= 2;
        if (d < bd) {
            bd = d;
            best = j;
        }
    }
    return best;
}

/* LEAD: what the black keys follow. A phrase playing (the pad played last first, then any, then SLICE's preview):
 * the top note sounding now and the top note of the next onset; else a chord held: its top note */
static void lead_update(void)
{
    int32_t now = -1, next = -1, src = -1;
    uint32_t i, k;
    if (ui.pad >= 0 && pl[ui.pad].on)
        src = ui.pad;
    for (i = 0; src < 0 && i <= NPADS; i++)
        if (pl[i].on)
            src = (int32_t)i;
    if (src >= 0) {
        const player_t *P = &pl[src];
        float pos = P->pos;
        int32_t top = -1, ntop = -1;
        uint32_t nat = 0xFFFFFFFFu;
        for (k = 0; k < pads[src].pn && k < MAXPH; k++) {
            const pnote_t *x = &ph[src][k];
            int32_t m = keyed((int32_t)x->p + 12 * P->oct + pads[src].tune);
            if (x->s < pads[src].ts || x->s >= pad_end(&pads[src]))
                continue;
            if ((float)x->s <= pos && pos < (float)x->s + (float)x->d) {
                if (m > top)
                    top = m;
            } else if ((float)x->s > pos) {
                if (x->s < nat) {
                    nat = x->s;
                    ntop = m;
                } else if (x->s == nat && m > ntop)
                    ntop = m;
            }
        }
        if (top >= 0)
            now = deg_of(top);
        if (ntop >= 0)
            next = deg_of(ntop);
    } else {
        for (i = 0; i < NPADS; i++)
            if (cp[i].on && cbuf_n[i]) {
                int32_t top = 0;
                for (k = 0; k < cbuf_n[i]; k++)
                    if (cbuf[i][k] > top)
                        top = cbuf[i][k];
                now = deg_of(top);
                break;
            }
    }
    if (next == now)
        next = -1;                       /* the same key: it stays full */
    if (now != ui.lead_now || next != ui.lead_next) {
        ui.lead_now = now;
        ui.lead_next = next;
        if (set.mode == MODE_LEAD)
            ui.redraw = 1;
    }
}

static void lights(void)
{
    uint32_t i, bright = 0, dim = 0, btn = 0, mid = 0;
    if (ui.screen == SCR_GUIDE) {
        guide_lights();
        return;
    }
    if (ui.screen == SCR_FIND) {         /* the notes tapped full, the rest of the key picked dim */
        uint16_t sm = fd_cnt ? scale_mask(fd_list[fd_sel] >> 4, fd_list[fd_sel] & 15u) : 0;
        for (i = 0; i < NKEYS; i++) {
            uint32_t pc = (uint32_t)(53 + (int32_t)i + 12 * set.octave) % 12u;
            if ((ui.keys >> i & 1u) || (fd_pcs >> pc & 1u))
                bright |= 1u << i;
            else if (sm >> pc & 1u)
                dim |= 1u << i;
        }
        plat_lights(ui.btn | 1u << B_PLAY | 1u << B_REC, bright, 0, dim, 1);
        plat_lights_mid(0, 0);
        return;
    }
    uint32_t piv = ui.f_hold ? fk_pivots() : 0;
    for (i = 0; i < 16u; i++) {
        uint32_t bit = 1u << WK[i];
        if (ui.f_hold) {                 /* F held: the pivot chords */
            if (piv >> i & 1u)
                bright |= bit;
            continue;
        }
        if ((ui.keys & bit) || pl[i].on || cp[i].on)
            bright |= bit;
        else if (ui.screen == SCR_SLICE && nsl)
            mid |= bit;
        else if (pads[i].type != PAD_EMPTY)
            dim |= bit;
    }
    lead_update();
    for (i = 0; i < 7u; i++) {
        uint32_t bit = 1u << BK[i];
        int lead = set.mode == MODE_LEAD;
        if ((ui.keys & bit) || (lead ? (int32_t)i == ui.lead_now : (int32_t)i == ui.root_lit))
            bright |= bit;
        else if (lead && (int32_t)i == ui.lead_next && (plat_ms() / 300u & 1u))
            mid |= bit;                  /* the next lead note breathes: mid and dim in turn */
        else
            dim |= bit;
    }
    for (i = 0; i < 4u; i++) {           /* F1-F4: the one in use full */
        uint32_t bit = 1u << BK[7u + i];
        if ((set.om_f4 == FK_BRIDGE && set.fmove == i + 1u) || (ui.keys & bit))
            bright |= bit;
        else
            dim |= bit;
    }
    if (set_dirty)
        btn |= 1u << B_SAVE;
    if (rec_on)
        btn |= 1u << B_REC;
    btn |= 1u << (ui.screen == SCR_SLICE || ui.screen == SCR_FILES ? B_SEL : ui.screen == SCR_DISK ? B_GLO
                  : ui.screen == SCR_KEY ? B_EDIT : ui.screen == SCR_FX ? B_FX : ui.screen == SCR_LIB ? B_LFO : ui.screen == SCR_TRIM ? B_ENV : B_HOME);
    plat_lights(btn | ui.btn, bright, 0, dim, 1);
    plat_lights_mid(0, mid);
}

/* ------------------------------------------------------------------ drawing --- */
static const uint16_t AMBER = RGB(242, 181, 82), AMBER_DIM = RGB(122, 90, 46), TEAL = RGB(79, 179, 166),
                      TEAL_DIM = RGB(31, 74, 69), REDC = RGB(224, 68, 60);

static void deg_sign(int32_t x, int32_t y, uint16_t c)   /* a small degree sign: a 4 px ring */
{
    cv_rect(x + 1, y, 2, 1, c);
    cv_rect(x + 1, y + 3, 2, 1, c);
    cv_rect(x, y + 1, 1, 2, c);
    cv_rect(x + 3, y + 1, 1, 2, c);
}

static void top_bar(const char *pill, const char *right, uint16_t right_c)
{
    uint32_t now = plat_ms();
    int32_t x;
    if (now < ui.msg_until) {
        x = cv_text(10, 5, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 5, &FONT_B, ui.msg[1], AMBER);
    } else {
        x = 8;
        if (pill) {
            int32_t w = text_w(&FONT_XS, pill);
            cv_rect(6, 5, w + 10, 17, C_WHITE);
            cv_text(11, 6, &FONT_XS, pill, C_BLACK);
            x = w + 24;
        }
        if (rec_on) {
            cv_rect(x, 9, 9, 9, REDC);
            x = cv_text(x + 13, 6, &FONT_XS, "REC", REDC) + 10;
        }
        if (right)
            cv_text(232 - text_w(&FONT_XS, right), 6, &FONT_XS, right, right_c);
    }
    cv_rect(0, 27, 240, 1, C_LINE);
}

static void bottom_bar(int32_t y, const char *const lbl[4], char val[4][12])
{
    uint32_t i;
    cv_rect(0, y, 240, 1, C_LINE);
    for (i = 0; i < 4u; i++) {
        int32_t cx = (int32_t)i * 60;
        cv_text(cx + (60 - text_w(&FONT_XS, lbl[i])) / 2, y + 7, &FONT_XS, lbl[i], C_GRAY);
        cv_text(cx + (60 - text_w(&FONT_B, val[i])) / 2, y + 24, &FONT_B, val[i], C_WHITE);
    }
}

static void draw_home(void)
{
    char t[32];
    uint32_t i;
    int32_t x;
    const pad_t *p = ui.pad >= 0 ? &pads[ui.pad] : 0;
    int playing = p && ((ui.keys >> WK[ui.pad] & 1u) || pl[ui.pad].on);

    cv_begin(240, 120, C_BLACK);
    key_text(t);
    if (plat_ms() < ui.msg_until)
        top_bar(0, 0, 0);
    else {
        int32_t w = text_w(&FONT_XS, t);
        top_bar(0, 0, 0);
        {
            char kt[8] = "KIT 0";
            int32_t kx;
            kt[5] = (char)('0' + set.kit);
            kt[6] = 0;
            cv_rect(4, 5, 24, 17, TEAL);   /* FM mode */
            cv_text(8, 6, &FONT_XS, "FM", C_BLACK);
            kx = cv_text(34, 6, &FONT_XS, kt, C_WHITE);
            if (cur_file >= 0)
                cv_text(kx + 10, 6, &FONT_XS, files[cur_file].name, C_GRAY);
        }
        cv_rect(226 - w, 5, w + 10, 17, AMBER);
        cv_text(231 - w, 6, &FONT_XS, t, C_BLACK);
        if (set.fmove) {                 /* a key change in use: which, beside the key */
            char f[3] = {'F', (char)('0' + set.fmove), 0};
            int32_t fw = text_w(&FONT_XS, f);
            cv_rect(218 - w - fw, 5, fw + 6, 17, TEAL);
            cv_text(221 - w - fw, 6, &FONT_XS, f, C_BLACK);
        }
    }
    if (p && p->type == PAD_CHORD) {
        int32_t rw, qw, w;
        char r[2] = {p->nm[0][0], 0};
        const char *acc = p->nm[0] + 1;
        t[0] = 0;
        pad_title(p, t);
        {
            char h[24] = "PAD ";
            char d[4] = {(char)('0' + (ui.pad + 1) / 10), (char)('0' + (ui.pad + 1) % 10), 0, 0};
            text_cat(h, ui.pad + 1 >= 10 ? d : d + 1);
            if (p->tag) {
                char sn[6] = "  S";
                sn[3] = (char)('0' + p->tag / 10u);
                sn[4] = (char)('0' + p->tag % 10u);
                if (p->tag < 10u) {
                    sn[3] = sn[4];
                    sn[4] = 0;
                }
                text_cat(h, sn);
            }
            x = cv_text(10, 36, &FONT_XS, h, C_GRAY);
        }
        x = cv_text(x + 10, 36, &FONT_XS, p->roman, C_GRAY);
        if (p->dim)
            deg_sign(x + 1, 38, C_GRAY);
        rw = text_w(&FONT_L, r) + text_w(&FONT_B, acc);
        qw = text_w(&FONT_B, p->qual);
        w = rw + qw + 4;
        x = (240 - w) / 2;
        x = cv_text(x, 58, &FONT_L, r, playing ? AMBER : AMBER_DIM);
        if (*acc)
            x = cv_text(x, 58, &FONT_B, acc, playing ? AMBER : AMBER_DIM);
        cv_text(x + 3, 72, &FONT_B, p->qual, playing ? AMBER : AMBER_DIM);
        {
            int32_t tw = 0;
            for (i = 0; i < p->n; i++)
                tw += text_w(&FONT_XS, p->nm[i]) + 14;
            x = (240 - tw + 14) / 2;
            for (i = 0; i < p->n; i++) {
                const char *nm = i ? p->nm[i] : p->nm[0];
                char tmp[4];
                int32_t nw;
                int root = p->note[i] % 12u == p->root_pc;
                if (i)
                    nm = p->nm[i];
                else {
                    pc_name(p->note[0] % 12u, tmp);
                    nm = p->fromkey ? p->nm[0] : tmp;
                }
                nw = text_w(&FONT_XS, nm);
                cv_rect(x - 4, 97, nw + 8, 16, root ? TEAL : C_LINE);
                cv_text(x, 97, &FONT_XS, nm, root ? C_BLACK : C_GRAY);
                x += nw + 14;
            }
        }
    } else if (p && p->type == PAD_PHRASE) {
        char h[16] = "PAD ";
        char d[4] = {(char)('0' + (ui.pad + 1) / 10), (char)('0' + (ui.pad + 1) % 10), 0, 0};
        text_cat(h, ui.pad + 1 >= 10 ? d : d + 1);
        cv_text(10, 36, &FONT_XS, h, C_GRAY);
        pad_title(p, t);
        if (p->title[0])                 /* numerals need lower case: the big font has none */
            cv_text((240 - text_w(&FONT_B, t)) / 2, 64, &FONT_B, t, playing ? TEAL : TEAL_DIM);
        else
            cv_text((240 - text_w(&FONT_M, t)) / 2, 58, &FONT_M, t, playing ? TEAL : TEAL_DIM);
        {
            char n[16];
            uint32_t k = p->pn, q = 0;
            char *o = n;
            if (k >= 10u)
                *o++ = (char)('0' + k / 10u);
            *o++ = (char)('0' + k % 10u);
            *o = 0;
            str_set(t, "PHRASE  ", sizeof t);
            text_cat(t, n);
            text_cat(t, " NOTES");
            (void)q;
            cv_text((240 - text_w(&FONT_XS, t)) / 2, 97, &FONT_XS, t, C_GRAY);
        }
    } else {
        cv_text((240 - text_w(&FONT_M, "PLAY A PAD")) / 2, 58, &FONT_M, "PLAY A PAD", C_GRAY);
        cv_text((240 - text_w(&FONT_XS, "SLICE: chop MIDI files onto pads")) / 2, 92, &FONT_XS,
                "SLICE: chop MIDI files onto pads", C_DIM);
    }
    cv_blit(0, 0);

    cv_begin(240, 120, C_BLACK);
    for (i = 0; i < 16u; i++) {
        int on = (int)(ui.keys >> WK[i] & 1u) || pl[i].on;
        uint16_t c = pads[i].type == PAD_PHRASE ? (on ? TEAL : TEAL_DIM)
                     : pads[i].type == PAD_CHORD ? (on ? AMBER : AMBER_DIM) : C_LINE;
        cv_rect(6 + (int32_t)i * 14, 8, 12, 14, c);
    }
    {
        int lead = set.mode == MODE_LEAD;
        cv_text(6, 30, &FONT_XS, MODE_NAME[set.mode % MODE_N], lead ? AMBER : TEAL);
        for (i = 0; i < 7u; i++) {
            int lit = (int32_t)i == (lead ? ui.lead_now : ui.root_lit);
            int nx = lead && (int32_t)i == ui.lead_next;
            int32_t cx = 46 + (int32_t)i * 27;
            char d[2] = {(char)('1' + i), 0};
            uint16_t on = lead ? AMBER : TEAL;
            cv_rect(cx, 28, 24, 20, lit ? on : nx ? on : C_LINE);
            if (nx)
                cv_rect(cx + 2, 30, 20, 16, C_BLACK);   /* next: an outline */
            cv_text(cx + (24 - text_w(&FONT_XS, d)) / 2, 30, &FONT_XS, d, lit ? C_BLACK : nx ? on : C_GRAY);
        }
    }
    {
        static const char *const LBL[4] = {"STRUM", "ARP", "HUMAN", "BPM"};
        char v[4][12];
        uint32_t b = tempo;
        num_text(set.strum * 2, v[0], 0);
        str_set(v[1], ARP_NAME[set.arp], sizeof v[1]);
        num_text(set.human, v[2], 0);
        text_cat(v[2], "%");
        v[3][0] = (char)(b >= 100u ? '0' + b / 100u : ' ');
        v[3][1] = (char)('0' + b / 10u % 10u);
        v[3][2] = (char)('0' + b % 10u);
        v[3][3] = 0;
        bottom_bar(68, LBL, v);
    }
    cv_blit(0, 120);
}

/* the file as a waveform: each note a struck sound (attack, decay, a short tail), louder the harder it's played */
static uint32_t wv_lo, wv_len;
static float amp_at(uint32_t t)
{
    uint32_t i;
    float sum = 0.0f;
    for (i = 0; i < song_n; i++) {
        const note_t *n = &song[i];
        uint32_t s = N_START(n);
        float dt, env;
        if (s > t)
            break;
        if (!note_used(n))
            continue;
        dt = (float)(t - s) / (float)PPQ;
        if (dt > (float)n->d / (float)PPQ + 0.6f)
            continue;
        env = dt < (float)n->d / (float)PPQ ? fm_minf(1.0f, dt * 40.0f) * (fm_expf(-dt * 1.4f) * 0.75f + 0.25f)
                                            : 0.25f * fm_expf(-(dt - (float)n->d / (float)PPQ) * 7.0f);
        sum += (float)n->v / 127.0f * env * 0.55f;
    }
    return fm_tanhf(sum);
}

static void draw_slice(void)
{
    char t[32];
    uint32_t i;
    int32_t x;
    cv_begin(240, 120, C_BLACK);
    top_bar("SLICE", cur_file >= 0 ? files[cur_file].name : "NO FILE", C_GRAY);
    if (!nsl) {
        cv_text(10, 50, &FONT_B, "NO SLICES", C_GRAY);
        cv_text(10, 72, &FONT_XS, "SLICE again: pick a file", C_DIM);
        cv_text(10, 88, &FONT_XS, "SYS: copy files over USB", C_DIM);
        cv_blit(0, 0);
        cv_begin(240, 120, C_BLACK);
        cv_blit(0, 120);
        return;
    }
    {                                    /* the view: 4 bars around the selection at most */
        uint32_t bar = PPQ * song_bpb, vb = 4u * bar;
        wv_len = song_len < vb ? song_len : vb;
        wv_lo = sl_s[sel] > bar ? (sl_s[sel] - bar) / bar * bar : 0u;
        if (wv_lo + wv_len > song_len)
            wv_lo = song_len - wv_len;
    }
    /* the waveform, rows 34 .. 119 here and on into the next band: drawn per band */
    for (x = 0; x < 240; x++) {
        uint32_t tt = wv_lo + (uint32_t)((uint32_t)wv_len * (uint32_t)x / 240u);
        int in = tt >= sl_s[sel] && tt < sl_e[sel];
        float a = amp_at(tt) * (0.6f + 0.4f * (float)((tt * 2654435761u) >> 24) / 255.0f);
        int32_t h = (int32_t)(a * 46.0f), cy = 80;
        cv_rect(x, cy - h, 1, 2 * h + 1, in ? AMBER : AMBER_DIM);
    }
    for (i = 0; i < nsl; i++) {          /* the cuts */
        int32_t sx;
        if (sl_s[i] < wv_lo || sl_s[i] >= wv_lo + wv_len)
            continue;
        sx = (int32_t)((uint32_t)(sl_s[i] - wv_lo) * 240u / wv_len);
        cv_rect(sx, 32, 1, 96, i == sel ? RGB(92, 207, 122) : C_DIM);
    }
    if (sl_e[sel] > wv_lo && sl_e[sel] <= wv_lo + wv_len)
        cv_rect((int32_t)((uint32_t)(sl_e[sel] - wv_lo) * 240u / wv_len) - 1, 32, 2, 96, RGB(224, 98, 92));
    for (i = 0; i <= wv_len / (PPQ * song_bpb); i++) {   /* bar numbers */
        char b[4];
        uint32_t bn = (wv_lo + i * PPQ * song_bpb) / (PPQ * song_bpb) + 1u;
        int32_t bx = (int32_t)((uint32_t)i * PPQ * song_bpb * 240u / wv_len);
        b[0] = (char)(bn >= 10u ? '0' + bn / 10u : '0' + bn);
        b[1] = (char)(bn >= 10u ? '0' + bn % 10u : 0);
        b[2] = 0;
        if (bx < 232)
            cv_text(bx + 2, 30, &FONT_T, b, C_GRAY);
    }
    cv_blit(0, 0);

    cv_begin(240, 120, C_BLACK);
    for (x = 0; x < 240; x++) {          /* the waveform's lower half (rows 120 .. 127 of it) */
        uint32_t tt = wv_lo + (uint32_t)((uint32_t)wv_len * (uint32_t)x / 240u);
        int in = tt >= sl_s[sel] && tt < sl_e[sel];
        float a = amp_at(tt) * (0.6f + 0.4f * (float)((tt * 2654435761u) >> 24) / 255.0f);
        int32_t h = (int32_t)(a * 46.0f), bot = 80 + h - 120;
        if (bot >= 0)
            cv_rect(x, 0, 1, bot + 1, in ? AMBER : AMBER_DIM);
    }
    {
        uint32_t i2;
        for (i2 = 0; i2 < nsl; i2++) {
            int32_t sx;
            if (sl_s[i2] < wv_lo || sl_s[i2] >= wv_lo + wv_len)
                continue;
            sx = (int32_t)((uint32_t)(sl_s[i2] - wv_lo) * 240u / wv_len);
            cv_rect(sx, 0, 1, 8, i2 == sel ? RGB(92, 207, 122) : C_DIM);
        }
        if (sl_e[sel] > wv_lo && sl_e[sel] <= wv_lo + wv_len)
            cv_rect((int32_t)((uint32_t)(sl_e[sel] - wv_lo) * 240u / wv_len) - 1, 0, 2, 8, RGB(224, 98, 92));
    }
    if (!slice_to(sel, PREVIEW)) {       /* the slice: its number and what it is */
        pad_title(&pads[PREVIEW], t);
        cv_text(10, 14, &FONT_B, t, pads[PREVIEW].type == PAD_CHORD ? AMBER : TEAL);
    }
    {
        char a[10], b[10];
        fmt_pos(sl_s[sel], a);
        fmt_pos(sl_e[sel], b);
        str_set(t, a, sizeof t);
        text_cat(t, " - ");
        text_cat(t, b);
        cv_text(230 - text_w(&FONT_XS, t), 16, &FONT_XS, t, C_GRAY);
    }
    cv_text(10, 40, &FONT_T, "WHITE KEY: DROP ON PAD    PLAY: HEAR IT", C_DIM);
    {
        static const char *const LBL[4] = {"MODE", "SENS", "START", "END"};
        static const char *const LBLG[4] = {"MODE", "GRID", "START", "END"};
        char v[4][12];
        str_set(v[0], sl_mode ? "GRID" : "HITS", 12);
        if (sl_mode)
            str_set(v[1], GRID_NAME[sl_grid], 12);
        else {
            v[1][0] = (char)(sl_sens >= 100u ? '1' : '0' + sl_sens / 10u);
            v[1][1] = (char)(sl_sens >= 100u ? '0' : '0' + sl_sens % 10u);
            v[1][2] = (char)(sl_sens >= 100u ? '0' : 0);
            v[1][3] = 0;
        }
        fmt_pos(sl_s[sel], v[2]);
        fmt_pos(sl_e[sel], v[3]);
        bottom_bar(68, sl_mode ? LBLG : LBL, v);
    }
    cv_blit(0, 120);
}

static void draw_files(void)
{
    char t[32];
    uint32_t i;
    cv_begin(240, 120, C_BLACK);
    {
        char kb[12];
        uint32_t f = fs_free_kb(), n = 0;
        if (f >= 100u)
            kb[n++] = (char)('0' + f / 100u);
        if (f >= 10u)
            kb[n++] = (char)('0' + f / 10u % 10u);
        kb[n++] = (char)('0' + f % 10u);
        kb[n] = 0;
        text_cat(kb, " KB FREE");
        top_bar("FILES", kb, C_GRAY);
    }
    if (!nfiles) {
        cv_text(10, 44, &FONT_B, "NO MIDI FILES YET", C_GRAY);
        cv_text(10, 66, &FONT_XS, "SYS: the FM-1 becomes a USB drive.", C_DIM);
        cv_text(10, 82, &FONT_XS, "Copy .mid files on, then eject.", C_DIM);
        cv_text(10, 98, &FONT_XS, "Or REC: record MIDI coming in.", C_DIM);
    } else {
        int32_t first = ui.file_cur > 3 ? ui.file_cur - 3 : 0;
        for (i = 0; i < 5u && (uint32_t)(first + (int32_t)i) < nfiles; i++) {
            int32_t f = first + (int32_t)i, y = 32 + (int32_t)i * 17;
            int cur = f == ui.file_cur;
            if (cur)
                cv_rect(4, y, 232, 16, AMBER);
            cv_text(10, y, &FONT_XS, files[f].name, cur ? C_BLACK : (f == cur_file ? C_WHITE : C_GRAY));
        }
    }
    cv_blit(0, 0);

    cv_begin(240, 120, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    for (i = 0; i < ntrk && i < 4u; i++) {
        int32_t y = 4 + (int32_t)i * 15;
        int cur = i == ui.trk_cur;
        cv_rect(8, y + 3, 9, 9, trk[i].on ? TEAL : C_LINE);
        cv_text(24, y, &FONT_XS, trk[i].name, cur ? C_WHITE : C_GRAY);
        {
            char c[8];
            uint32_t n = trk[i].count, k = 0;
            if (n >= 100u)
                c[k++] = (char)('0' + n / 100u % 10u);
            if (n >= 10u)
                c[k++] = (char)('0' + n / 10u % 10u);
            c[k++] = (char)('0' + n % 10u);
            c[k] = 0;
            cv_text(200 - text_w(&FONT_XS, c), y, &FONT_XS, c, C_DIM);
        }
        if (cur)
            cv_rect(4, y + 4, 2, 8, C_WHITE);
    }
    (void)t;
    {
        static const char *const LBL[4] = {"FILE", "TRACK", "ON/OFF", "SLICES"};
        char v[4][12];
        uint32_t n = nsl;
        str_set(v[0], "SELECT", 12);
        str_set(v[1], ntrk ? "K1" : "-", 12);
        str_set(v[2], ntrk ? (trk[ui.trk_cur].on ? "ON" : "OFF") : "-", 12);
        v[3][0] = (char)(n >= 10u ? '0' + n / 10u : '0' + n);
        v[3][1] = (char)(n >= 10u ? '0' + n % 10u : 0);
        v[3][2] = 0;
        bottom_bar(68, LBL, v);
    }
    cv_blit(0, 120);
}

static void draw_disk(void)
{
    const char *st = ui.disk_state == PLAT_DISK_MOUNTED ? "CONNECTED" : "WAITING FOR A COMPUTER";
    cv_begin(240, 120, C_BLACK);
    top_bar("USB DRIVE", 0, 0);
    cv_text((240 - text_w(&FONT_M, "USB DRIVE")) / 2, 44, &FONT_M, "USB DRIVE", C_WHITE);
    cv_text((240 - text_w(&FONT_XS, st)) / 2, 82, &FONT_XS, st, ui.disk_state == PLAT_DISK_MOUNTED ? TEAL : AMBER);
    cv_blit(0, 0);
    cv_begin(240, 120, C_BLACK);
    cv_text(12, 4, &FONT_XS, "1  Connect USB to a computer.", C_GRAY);
    cv_text(12, 22, &FONT_XS, "2  Copy .mid files onto CHORDSMITH.", C_GRAY);
    cv_text(12, 40, &FONT_XS, "3  Eject it. Files show in SLICE.", C_GRAY);
    cv_text(12, 76, &FONT_XS, "No USB MIDI while the drive is on.", C_DIM);
    cv_text(12, 94, &FONT_XS, "HOME: leave drive mode.", C_DIM);
    cv_blit(0, 120);
}

#include "screens.c"                     /* KEY, FX, LIB, TRIM */
#include "guide.c"                       /* GUIDE */

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.file_cur = cur_file;
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_on;
    uint32_t now = plat_ms(), i, on = 0;
    if (first_boot) {
        first_boot = 0;
        guide_open();
    }
    input();
    lights();
    ann_tick(now);
    for (i = 0; i <= NPADS; i++)         /* a phrase starting or ending lights the screen too */
        on |= (uint32_t)pl[i].on << i;
    if (on != last_on) {
        last_on = on;
        ui.redraw = 1;
    }
    if (ui.screen == SCR_TRIM && ui.pad >= 0 && pl[ui.pad].on) {   /* the playhead, about 30 times a second */
        static uint32_t ph_at;
        if (now - ph_at >= 33u) {
            ph_at = now;
            ui.redraw = 1;
        }
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        if (ui.screen == SCR_SLICE)
            draw_slice();
        else if (ui.screen == SCR_FILES)
            draw_files();
        else if (ui.screen == SCR_DISK)
            draw_disk();
        else if (ui.screen == SCR_KEY)
            draw_key();
        else if (ui.screen == SCR_FX)
            draw_fx();
        else if (ui.screen == SCR_LIB)
            draw_lib();
        else if (ui.screen == SCR_TRIM)
            draw_trim();
        else if (ui.screen == SCR_FIND)
            draw_find();
        else if (ui.screen == SCR_GUIDE)
            draw_guide();
        else if (orchid)
            draw_orchid();
        else
            draw_home();
    }
}

#ifdef PLAT_WEB
/* the browser emulator: a .mid from the page (web/emu/app.js) onto the drive, then opened in SLICE */
static uint8_t web_buf[MAXFBLK * 512u];
static char web_name[12];
static int web_import(uint32_t n)       /* (exported by web.c: the host blanks __attribute__ here) */
{
    char shown[13];
    uint32_t i, k = 0;
    int r;
    web_name[11] = 0;
    if (n > sizeof web_buf)
        return -9;
    fs_delete(web_name);
    r = fs_write(web_name, web_buf, n);
    if (r) {
        app_say("DRIVE", r == -2 ? "FULL" : "NO ROOM");
        return r;
    }
    for (i = 0; i < 8u && web_name[i] != ' '; i++)   /* "NAME.MID", as the drive lists it */
        shown[k++] = web_name[i];
    shown[k++] = '.';
    for (i = 8; i < 11u; i++)
        shown[k++] = web_name[i];
    shown[k] = 0;
    fs_scan();
    for (i = 0; i < nfiles; i++)
        if (!__builtin_memcmp(files[i].name, shown, k + 1u))
            break;
    if (i == nfiles || file_load((int32_t)i)) {
        app_say("CAN'T READ", shown);
        ui.screen = SCR_FILES;
    } else {
        ui.file_cur = (int32_t)i;
        ui.screen = orchid ? SCR_HOME : SCR_SLICE;   /* ORCHID: already on the pads */
        app_say("LOADED", shown);
    }
    ui.redraw = 1;
    return 0;
}
#endif

#ifdef PLAT_HOST
/* the simulator's own commands: "import PATH NAME83" puts a file on the drive as a computer would; "dumpdisk PATH"
 * writes the drive's image out (checked with a FAT library) */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)c, (void)d;
    if (!strcmp(cmd, "import")) {
        static uint8_t buf[96 * 1024];
        char n83[12] = "           ";
        FILE *f = fopen(a, "rb");
        size_t n;
        uint32_t i, k = 0;
        if (!f) {
            printf("  import: no %s\n", a);
            return 1;
        }
        n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        for (i = 0; b[i] && b[i] != '.' && k < 8u; i++)
            n83[k++] = (char)(b[i] >= 'a' && b[i] <= 'z' ? b[i] - 32 : b[i]);
        n83[8] = 'M';
        n83[9] = 'I';
        n83[10] = 'D';
        printf("  import %s: %d\n", b, fs_write(n83, buf, (uint32_t)n));
        fs_scan();
        return 1;
    }
    if (!strcmp(cmd, "dumpdisk")) {
        FILE *f = fopen(a, "wb");
        uint32_t i;
        dc_flush();
        for (i = 0; i < DISK_BLOCKS; i++)
            fwrite(plat_data(DISK_LO + i * 512u), 1, 512, f);
        fclose(f);
        return 1;
    }
    if (!strcmp(cmd, "loadimg")) {      /* "loadimg PATH": the drive as a computer left it */
        static uint8_t img[DISK_BLOCKS * 512u];
        FILE *f = fopen(a, "rb");
        uint32_t i;
        if (!f || fread(img, 1, sizeof img, f) != sizeof img) {
            printf("  loadimg: can't read %s\n", a);
            if (f)
                fclose(f);
            return 1;
        }
        fclose(f);
        dc_sec = -1;
        for (i = 0; i < DISK_BLOCKS / 8u; i++) {
            plat_data_erase(DISK_LO + i * 4096u);
            plat_data_write(DISK_LO + i * 4096u, img + i * 4096u, 4096u);
        }
        fs_scan();
        for (i = 0; i < nfiles; i++)
            printf("  file %u: %s (%u bytes)\n", i, files[i].name, files[i].size);
        return 1;
    }
    if (!strcmp(cmd, "load")) {          /* "load N": as SELECT on FILES */
        ui.file_cur = atoi(a);
        printf("  load %d: %d\n", ui.file_cur, file_load(ui.file_cur));
        return 1;
    }
    return 0;
}

int app_expect(const char *what, int *got)
{
    uint32_t i;
    if (!strcmp(what, "voices")) {
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
    else if (!strncmp(what, "trim", 4) && ui.pad >= 0) {   /* trims, trime, trimtune, trimlevel, trimmode */
        const pad_t *p = &pads[ui.pad];
        *got = !strcmp(what, "trims") ? p->ts : !strcmp(what, "trime") ? (int)pad_end(p) : !strcmp(what, "trimtune")
               ? p->tune : !strcmp(what, "trimlevel") ? 100 - p->cut : p->pmode;
    } else if (!strcmp(what, "omlen"))
        *got = set.om_len;
    else if (!strcmp(what, "omslen"))
        *got = set.om_slen;
    else if (!strcmp(what, "omvcurve"))
        *got = set.om_vcurve;
    else if (!strncmp(what, "slicelen", 8))   /* slicelenN: slice N's length, 96ths */
        *got = (int)(sl_e[atoi(what + 8) % MAXSL] - sl_s[atoi(what + 8) % MAXSL]);
    else if (!strncmp(what, "vc", 2))   /* vcN: velocity N through the curve */
        *got = (int)vcurve((uint32_t)atoi(what + 2));
    else if (!strcmp(what, "omf4"))
        *got = set.om_f4;
    else if (!strcmp(what, "omplay"))
        *got = set.om_play;
    else if (!strcmp(what, "omamt"))
        *got = set.om_amt;
    else if (!strcmp(what, "omshape"))
        *got = set.om_shape;
    else if (!strcmp(what, "strum"))
        *got = set.strum;
    else if (!strcmp(what, "human"))
        *got = set.human;
    else if (!strncmp(what, "cbufn", 5))   /* cbufnN: the notes pad N's chord sounds */
        *got = cbuf_n[atoi(what + 5) % NPADS];
    else if (!strcmp(what, "orchid"))
        *got = orchid;
    else if (!strcmp(what, "omslice"))
        *got = set.om_slice;
    else if (!strcmp(what, "omstart"))
        *got = (int)om_first();
    else if (!strcmp(what, "nsl"))
        *got = (int)nsl;
    else if (!strcmp(what, "guidepg"))
        *got = ui.guide_pg;
    else if (!strcmp(what, "fmove"))
        *got = set.fmove;
    else if (!strcmp(what, "pivots"))
        *got = ui.f_hold ? (int)fk_pivots() : -1;
    else if (!strcmp(what, "fdn"))
        *got = fd_n;
    else if (!strcmp(what, "fdcount"))
        *got = fd_cnt;
    else if (!strcmp(what, "fdtop"))     /* the key picked: root * 16 + scale */
        *got = fd_cnt ? fd_list[fd_sel] : -1;
    else if (!strcmp(what, "mode"))
        *got = set.mode;
    else if (!strcmp(what, "leadnow"))
        *got = ui.lead_now;
    else if (!strcmp(what, "leadnext"))
        *got = ui.lead_next;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strncmp(what, "padnote", 7))
        *got = ui.pad >= 0 ? pads[ui.pad].note[atoi(what + 7) % MAXN] : -1;
    else if (!strcmp(what, "files"))
        *got = (int)nfiles;
    else if (!strcmp(what, "notes"))
        *got = (int)song_n;
    else if (!strcmp(what, "tracks"))
        *got = (int)ntrk;
    else if (!strcmp(what, "slices"))
        *got = (int)nsl;
    else if (!strcmp(what, "bpm"))
        *got = (int)tempo;
    else if (!strcmp(what, "screen"))
        *got = ui.screen;
    else if (!strncmp(what, "padtype", 7))
        *got = pads[atoi(what + 7) % NPADS].type;
    else if (!strncmp(what, "padtag", 6))   /* padtagN: the slice pad N holds (0: the key's chord) */
        *got = pads[atoi(what + 6) % NPADS].tag;
    else if (!strncmp(what, "padn", 4))   /* padnN: pad N's phrase notes */
        *got = pads[atoi(what + 4) % NPADS].pn;
    else if (!strncmp(what, "playing", 7))
        *got = pl[atoi(what + 7) % (NPADS + 1)].on;
    else if (!strcmp(what, "kit"))
        *got = set.kit;
    else if (!strcmp(what, "lock"))
        *got = set.lock;
    else if (!strcmp(what, "steps"))
        *got = set.steps;
    else if (!strcmp(what, "strum"))
        *got = set.strum;
    else if (!strcmp(what, "arp"))
        *got = set.arp;
    else if (!strcmp(what, "inv"))
        *got = set.inv;
    else if (!strcmp(what, "bass"))
        *got = set.bass;
    else if (!strncmp(what, "cbuf", 4)) {    /* cbufP_K: the note pad P's chord sounds as, K-th */
        int p = atoi(what + 4), k = atoi(strchr(what, '_') ? strchr(what, '_') + 1 : "0");
        *got = k < cbuf_n[p % NPADS] ? cbuf[p % NPADS][k % MAXC] : -1;
    } else if (!strncmp(what, "cbn", 3))
        *got = cbuf_n[atoi(what + 3) % NPADS];
    else if (!strcmp(what, "recn"))
        *got = (int)rec_n;
    else
        return 0;
    return 1;
}
#endif
