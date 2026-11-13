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
    uint8_t rsv[3];
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
} pad_t;
enum { PAD_EMPTY, PAD_CHORD, PAD_PHRASE };
typedef struct {
    uint16_t s, d;                       /* start and length, 96ths of a beat */
    uint8_t p, v;
} pnote_t;
/* the kit: every pad and its phrase, one block, saved whole as a file on the drive (KITnn.CSK) */
#define KIT_MAGIC 0x314B5343u            /* "CSK1" */
#define NKITS 8u
static struct {
    uint32_t magic, size;
    pad_t pad[NPADS + 1];
    pnote_t ph[NPADS + 1][MAXPH];
} K;
#define pads K.pad
#define ph K.ph

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

static void phrase_tick(uint32_t n)
{
    float dt = (float)n * (float)tempo * 96.0f / (60.0f * SR);
    uint32_t i, k;
    for (i = 0; i <= NPADS; i++) {
        player_t *P = &pl[i];
        float a, b;
        if (!P->on)
            continue;
        a = P->pos;
        b = a + dt;
        for (k = 0; k < pads[i].pn && k < MAXPH; k++) {
            const pnote_t *x = &ph[i][k];
            float s = (float)x->s, e = (float)x->s + (float)x->d;
            int starts = s >= a && s < b;
            if (starts) {
                uint32_t m = (uint32_t)clamp(keyed((int32_t)x->p + 12 * P->oct), 0, 127);
                if (P->snd[k])
                    out_off(P->snd[k] - 1u);
                out_on(m, x->v);
                P->snd[k] = (uint8_t)(m + 1u);
            }
            if (P->snd[k] && e < b && !(starts && e >= b)) {
                out_off(P->snd[k] - 1u);
                P->snd[k] = 0;
            }
        }
        P->pos = b;
        if (P->pos >= (float)pads[i].len) {
            pl_release(i);
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
        if (set.arp && c->n) {
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
                out_on(c->note[pick], human_vel(c->vel[0]));
                c->cur = (uint8_t)(c->note[pick] + 1u);
                c->idx++;
                step = (uint32_t)(60.0f * SR / (float)tempo * ARP_RATE[set.arprate % 6u]);
                c->next += step ? step : 1u;
            }
        } else {
            for (k = 0; k < c->n; k++)
                if (!c->sounded[k] && c->at[k] < c->t + n) {
                    out_on(c->note[k], c->vel[k]);
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
            pl[a].pos = 0.0f;
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
#define MAXSL 64u
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
    return 0;
}

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.root > 11u || set.scale >= NSCALES || set.octave < -2 || set.octave > 2 || set.kit < 1u ||
        set.kit > NKITS || set.steps < -7 || set.steps > 7 || set.arp > 4u || set.arprate > 5u || set.human > 50u ||
        set.inv < -3 || set.inv > 3 || set.strum > 100u)
        settings_defaults();
    disk_init();
    kit_load(set.kit);
    if (set.bpm)
        tempo = set.bpm;
    if (nfiles)
        file_load(0);
}

/* ======================================================================= UI === */
enum { SCR_HOME, SCR_SLICE, SCR_FILES, SCR_DISK, SCR_KEY, SCR_FX, SCR_LIB };
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    int32_t pad;                         /* the pad played last (-1 none yet) */
    int32_t root_lit;                    /* the black key lit as the root (-1 none) */
    uint8_t oct_mod;                     /* OCT held and used as a modifier: its release steps nothing */
    uint8_t screen, disk_state, trk_cur, fx_page;
    int32_t file_cur;
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
    if (set.bass && n) {                 /* the root, an octave under the lowest note */
        int32_t r = v[0] - (((v[0] - p->root_pc) % 12 + 12) % 12);   /* the root at or under the lowest note */
        out[no++] = r - 12;
    }
    for (k = 0; k < n; k++)
        out[no++] = v[k];
    cbuf_n[i] = 0;
    for (k = 0; k < no && cbuf_n[i] < MAXC; k++) {
        int32_t m = clamp(keyed(out[k] + 12 * set.octave), 0, 127);
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
    if (p->type == PAD_CHORD) {
        chord_build(i);
        ev_push(QE_CSTART, i, vel);
    }
    else if (p->type == PAD_PHRASE)
        ev_push(QE_PSTART, i, (uint32_t)(set.octave + 8));
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

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), i;
    int32_t e;
    touch_poll();
    keys = touch_keys();
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
        } else if (i == B_EDIT) {        /* KEY */
            ui.screen = SCR_KEY;
        } else if (i == B_FX) {          /* FX: again, the second page */
            ui.fx_page = ui.screen == SCR_FX ? (uint8_t)!ui.fx_page : 0u;
            ui.screen = SCR_FX;
        } else if (i == B_LFO) {         /* LIB */
            ui.screen = SCR_LIB;
            if (lib_sel >= lib_count())
                lib_sel = 0;
        } else if (i == B_GLO) {         /* SYS: the USB drive, for now */
            disk_enter();
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
    if ((e = plat_enc(EN_ALGO)) != 0) {
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
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

/* the lights: loaded pads dim, held or playing pads full, an empty pad dark; in SLICE the pads breathe as drop
 * targets (the mid level); the seven degree keys dim, the root full; REC lit while recording, SAVE while changed */
static void lights(void)
{
    uint32_t i, bright = 0, dim = 0, btn = 0, mid = 0;
    for (i = 0; i < 16u; i++) {
        uint32_t bit = 1u << WK[i];
        if ((ui.keys & bit) || pl[i].on || cp[i].on)
            bright |= bit;
        else if (ui.screen == SCR_SLICE && nsl)
            mid |= bit;
        else if (pads[i].type != PAD_EMPTY)
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
    if (rec_on)
        btn |= 1u << B_REC;
    btn |= 1u << (ui.screen == SCR_SLICE || ui.screen == SCR_FILES ? B_SEL : ui.screen == SCR_DISK ? B_GLO
                  : ui.screen == SCR_KEY ? B_EDIT : ui.screen == SCR_FX ? B_FX : ui.screen == SCR_LIB ? B_LFO : B_HOME);
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
            kx = cv_text(10, 6, &FONT_XS, kt, C_WHITE);
            if (cur_file >= 0)
                cv_text(kx + 10, 6, &FONT_XS, files[cur_file].name, C_GRAY);
        }
        cv_rect(226 - w, 5, w + 10, 17, AMBER);
        cv_text(231 - w, 6, &FONT_XS, t, C_BLACK);
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
    cv_text(6, 30, &FONT_XS, "ROOT", TEAL);
    for (i = 0; i < 7u; i++) {
        int lit = (int32_t)i == ui.root_lit;
        int32_t cx = 46 + (int32_t)i * 27;
        char d[2] = {(char)('1' + i), 0};
        cv_rect(cx, 28, 24, 20, lit ? TEAL : C_LINE);
        cv_text(cx + (24 - text_w(&FONT_XS, d)) / 2, 30, &FONT_XS, d, lit ? C_BLACK : C_GRAY);
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

#include "screens.c"                     /* KEY, FX, LIB */

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
    input();
    lights();
    ann_tick(now);
    for (i = 0; i <= NPADS; i++)         /* a phrase starting or ending lights the screen too */
        on |= (uint32_t)pl[i].on << i;
    if (on != last_on) {
        last_on = on;
        ui.redraw = 1;
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
        else
            draw_home();
    }
}

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
