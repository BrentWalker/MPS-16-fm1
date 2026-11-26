/* SEED: MPS-16's chord builder, as firmware for the M-VAVE FM-1. Built on fm1-base.
 * Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence of this file: to be chosen (see README).
 *
 * The 16 white keys (A pads) are your key's chords: 1-8 the chords, 9-16 progressions. The first 7 black keys
 * (B pads) are the notes of the key, and their lights show the chord you played: bright the bass, mid the rest of
 * the chord, dim the key. The last 4 black keys (C pads) are BASS, NO BASS, NO ROOT and LATCH. PICK NOTES (EDIT)
 * turns any keys you play into a key; COACH walks you through it, the buttons and keys it means lit.
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. */
#include "fastmath.h"

/* ================================================================ settings === */
#define SET_MAGIC 0x31444553u            /* "SED1" */
enum { BM_BASS, BM_NOBASS, BM_NOROOT, BM_N };
enum { PL_CHORD, PL_STABS, PL_ARP, PL_N };
enum { CO_OFF, CO_FIRST, CO_ALWAYS, CO_N };
typedef struct {
    uint32_t magic;
    uint8_t root;                        /* 0 = C .. 11 = B */
    uint8_t scale;                       /* SCALES[] */
    int8_t octave;                       /* -2 .. +2 */
    uint8_t type;                        /* the chords: TRIADS, 7THS, 9THS, SUS2, SUS4, ADD9 */
    uint8_t plays;                       /* how the pads play: PL_* */
    uint8_t bassm;                       /* BASS, NO BASS, NO ROOT: BM_* */
    uint8_t human;                       /* humanize, % (0 .. 50): time and velocity */
    uint8_t strum;                       /* strum, 2 ms steps (0 .. 50) */
    int8_t len;                          /* the notes' length, -5 .. 5 (0: as written) */
    uint8_t bpm;                         /* 40 .. 240 */
    uint8_t arprate;                     /* ARP_RATE[] */
    uint8_t latch;                       /* LATCH: pads keep playing after you let go */
    uint8_t ch[3];                       /* MIDI out channels, 0 .. 15: the A pads, the B pads, (the C pads: none) */
    uint8_t coach;                       /* COACH: CO_OFF, CO_FIRST (the first start), CO_ALWAYS (every start) */
    uint8_t coach_seen;                  /* the steps have been through once */
    uint8_t coach_set;                   /* the choice has been made */
    uint8_t rsv[2];
} settings_t;
static settings_t set;
static uint8_t set_dirty;

static void settings_defaults(void)
{
    __builtin_memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.root = 0;                        /* C major */
    set.scale = 1;
    set.type = 1;                        /* 7ths */
    set.plays = PL_CHORD;
    set.bassm = BM_BASS;
    set.human = 10;
    set.bpm = 90;
    set.arprate = 1;                     /* 1/8 */
    set.ch[0] = 0;
    set.ch[1] = 1;
    set.ch[2] = 2;
    set.coach = CO_FIRST;
}

/* ================================================================== theory === */
#define NSCALES 9u
static const char *const SCALE_NAME[NSCALES] = {"MINOR", "MAJOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN",
                                                "LOCRIAN", "HARM MINOR", "MEL MINOR"};
static const char *const SCALE_SHORT[NSCALES] = {"MIN", "MAJ", "DOR", "PHR", "LYD", "MIX", "LOC", "HMIN", "MMIN"};
static const uint8_t SCALES[NSCALES][7] = {
    {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 1, 3, 5, 6, 8, 10}, {0, 2, 3, 5, 7, 8, 11},
    {0, 2, 3, 5, 7, 9, 11}};

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
    {IV(0, 5, 7, 7, 7), "sus4"}, {IV(0, 2, 7, 7, 7), "sus2"}, {IV(0, 4, 7, 2, 2), "add9"},
    {IV(0, 3, 7, 2, 2), "madd9"}, {IV(0, 2, 6, 6, 6), "sus2b5"}, {IV(0, 5, 6, 6, 6), "sus4b5"}};
#define NQUAL (sizeof QUAL / sizeof QUAL[0])

/* the chord types, as scale steps above the root */
#define NCTYPES 6u
static const char *const CTYPE_NAME[NCTYPES] = {"TRIADS", "7THS", "9THS", "SUS2", "SUS4", "ADD9"};
static const uint8_t CTYPE[NCTYPES][5] = {{0, 2, 4, 0xFF, 0xFF}, {0, 2, 4, 6, 0xFF}, {0, 2, 4, 6, 8},
                                          {0, 1, 4, 0xFF, 0xFF}, {0, 3, 4, 0xFF, 0xFF}, {0, 2, 4, 8, 0xFF}};
static const char *const PLAYS_NAME[PL_N] = {"CHORD", "STABS", "ARP"};
static const char *const BM_NAME[BM_N] = {"BASS", "NO BASS", "NO ROOT"};
static const char *const CO_NAME[CO_N] = {"OFF", "FIRST TIME", "EVERY START"};
/* ten four-bar progressions, as scale degrees: pads 9-16 are the first eight */
#define NPROGS 10u
static const uint8_t PROGS[NPROGS][4] = {{0, 4, 5, 3}, {1, 4, 0, 0}, {0, 5, 3, 4}, {5, 3, 0, 4}, {0, 3, 4, 3},
                                         {0, 5, 2, 6}, {0, 3, 4, 0}, {0, 6, 5, 6}, {3, 4, 2, 5}, {0, 2, 3, 4}};

#define PPQ 96u                          /* phrase time: 96ths of a beat */

static uint32_t cs_len(const char *s)    /* (the device's libc has no strlen) */
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static void str_set(char *d, const char *s, uint32_t max)
{
    uint32_t i;
    for (i = 0; i + 1u < max && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
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
static uint32_t deg_pc(int32_t j) { return (uint32_t)(deg_pitch(j) % 12); }

/* spelling: the key's notes take consecutive letters from the tonic's, so a key never mixes sharps and flats */
static const uint8_t LETTER_PC[7] = {0, 2, 4, 5, 7, 9, 11};
static uint32_t letter_of(uint32_t root, uint32_t scale)   /* the tonic's letter in a key, 0 = C */
{
    static const uint8_t MAJ[12] = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};   /* C Db D Eb E F F# G Ab A Bb B */
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
static void key_name(uint32_t root, uint32_t scale, char *t)   /* "Eb MINOR" */
{
    spell(root, letter_of(root, scale), t);
    text_cat(t, " ");
    text_cat(t, SCALE_NAME[scale % NSCALES]);
}
static void deg_name(uint32_t d, char *t) { spell(deg_pc((int32_t)d), tonic_letter() + d, t); }
static void pc_name(uint32_t pc, char *t)   /* any pitch class, as sharps */
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    str_set(t, N[pc % 12u], 4);
}

static void roman_of(uint32_t d, char *o, uint8_t *dim)
{
    static const char *const UP[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const char *const LO[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    const uint8_t *sc = SCALES[set.scale];
    uint32_t r = sc[d % 7u];
    uint32_t third = (sc[(d + 2u) % 7u] + 12u - r) % 12u, fifth = (sc[(d + 4u) % 7u] + 12u - r) % 12u;
    *dim = 0;
    if (third == 4u) {
        str_set(o, UP[d % 7u], 6);
        if (fifth == 8u)
            text_cat_n(o, "+", 6);
    } else {
        str_set(o, LO[d % 7u], 6);
        *dim = fifth == 6u;
    }
}

/* ===================================================================== pads === */
#define NPADS 16u
#define MAXN 6u
#define MAXPH 96u                        /* notes in a progression pad */
#define PREVIEW NPADS                    /* the extra slot LIB plays its pick from */
enum { PAD_EMPTY, PAD_CHORD, PAD_PROG };
typedef struct {
    uint16_t s, d;                       /* start and length, 96ths of a beat */
    uint8_t p, v;
} pnote_t;
typedef struct {
    uint8_t type, n, lock, deg;          /* lock: a LIB pick dropped here, kept when the key changes */
    uint8_t note[MAXN];                  /* a chord's notes, low to high (as they sound) */
    uint8_t root_pc, bass_p, dim, pn;    /* bass_p: the chord's written bass (its lowest note before NO BASS) */
    uint8_t bm;                          /* the bass mode it was built in */
    uint8_t bars;                        /* a progression's bars */
    uint8_t bar_deg[4];                  /* and the degree of each */
    uint16_t len;                        /* a progression's length, 96ths */
    char qual[10];                       /* "m7b5" */
    char nm[MAXN][4];                    /* each note spelled in the key */
    char roman[6];                       /* "ii" */
    char title[16];                      /* a progression: "I V vi IV" */
} pad_t;
static pad_t pads[NPADS + 1];
static pnote_t ph[NPADS + 1][MAXPH];

static void name_chord(pad_t *p, uint32_t mask)   /* the quality from the intervals above the root */
{
    uint32_t i;
    for (i = 0; i < NQUAL; i++)
        if (QUAL[i].mask == mask) {
            str_set(p->qual, QUAL[i].name, sizeof p->qual);
            return;
        }
    str_set(p->qual, (mask & 1u << 3) ? ((mask & 1u << 6) ? "dim+" : "m+") : "+", sizeof p->qual);
}

/* degree d's chord of type t, low to high, as MIDI notes into v: how many */
static uint32_t chord_of(uint32_t d, uint32_t t, int32_t *v)
{
    uint32_t k, n = 0;
    for (k = 0; k < 5u && CTYPE[t][k] != 0xFF; k++)
        v[n++] = deg_pitch((int32_t)(d + CTYPE[t][k]));
    for (k = 1; k < n; k++)              /* low to high */
        if (v[k] <= v[k - 1])
            v[k] += 12 * ((v[k - 1] - v[k]) / 12 + 1);
    return n;
}
/* the bass mode on a chord's notes: NO BASS drops the bottom note, NO ROOT the root too (two notes stay at least) */
static uint32_t chord_bm(int32_t *v, uint32_t n, uint32_t root_pc, uint32_t bm)
{
    uint32_t i, k = 0;
    if (bm == BM_BASS || n <= 2u)
        return n;
    for (i = 1; i < n; i++)
        v[k++] = v[i];
    if (bm == BM_NOROOT) {
        uint32_t left = 0;
        for (i = 0; i < k; i++)
            left += (uint32_t)(v[i] % 12) != root_pc;
        if (left >= 2u) {
            uint32_t j = 0;
            for (i = 0; i < k; i++)
                if ((uint32_t)(v[i] % 12) != root_pc)
                    v[j++] = v[i];
            k = j;
        }
    }
    return k;
}

/* the inversion of v closest to the previous chord's middle: chords that move as little as they can */
static void nearest(int32_t *v, uint32_t n, float prev)
{
    int32_t best[6], w[6];
    float bd = 1e9f;
    int32_t k;
    uint32_t i, j;
    for (k = -3; k <= 3; k++) {
        float avg = 0.0f, d;
        for (i = 0; i < n; i++)
            w[i] = v[i];
        for (j = 0; j < (uint32_t)(k < 0 ? -k : k); j++) {
            if (k > 0) {
                int32_t lo = w[0] + 12;
                for (i = 0; i + 1 < n; i++)
                    w[i] = w[i + 1];
                w[n - 1] = lo;
            } else {
                int32_t hi = w[n - 1] - 12;
                for (i = n - 1; i > 0; i--)
                    w[i] = w[i - 1];
                w[0] = hi;
            }
        }
        for (i = 0; i < n; i++)
            avg += (float)w[i];
        avg /= (float)n;
        d = avg > prev ? avg - prev : prev - avg;
        if (d < bd) {
            bd = d;
            for (i = 0; i < n; i++)
                best[i] = w[i];
        }
    }
    for (i = 0; i < n; i++)
        v[i] = best[i];
}

static void ph_note(uint32_t dst, int32_t p, uint32_t s, uint32_t d, uint32_t v)
{
    pad_t *P = &pads[dst];
    if (P->pn < MAXPH && p >= 0 && p < 128) {
        pnote_t *x = &ph[dst][P->pn++];
        x->p = (uint8_t)p;
        x->s = (uint16_t)s;
        x->d = (uint16_t)d;
        x->v = (uint8_t)v;
    }
}

/* degree d's chord into pad slot dst (d 7: the first an octave up) */
static void chord_into(pad_t *p, uint32_t d)
{
    int32_t v[6];
    uint32_t k, n, mask = 0, rp = deg_pc((int32_t)d);
    __builtin_memset(p, 0, sizeof *p);
    n = chord_of(d, set.type, v);
    p->bass_p = (uint8_t)v[0];
    for (k = 0; k < n; k++)
        mask |= 1u << (((uint32_t)v[k] + 12u - rp) % 12u);
    n = chord_bm(v, n, rp, set.bassm);
    p->type = PAD_CHORD;
    p->n = (uint8_t)n;
    p->deg = (uint8_t)(d % 7u);
    p->root_pc = (uint8_t)rp;
    p->bm = set.bassm;
    for (k = 0; k < n; k++) {
        uint32_t step = 0, j;
        p->note[k] = (uint8_t)v[k];
        for (j = 0; j < 5u && CTYPE[set.type][j] != 0xFF; j++)   /* its letter: the type's step it came from */
            if (deg_pc((int32_t)(d + CTYPE[set.type][j])) == (uint32_t)(v[k] % 12))
                step = CTYPE[set.type][j];
        spell((uint32_t)v[k] % 12u, tonic_letter() + d + step, p->nm[k]);
    }
    name_chord(p, mask);
    roman_of(d % 7u, p->roman, &p->dim);
}
static void make_chord(uint32_t d, uint32_t dst) { chord_into(&pads[dst], d); }
static void chord_label(uint32_t d, char *t)   /* "Am7" */
{
    pad_t tmp;
    char r[4];
    chord_into(&tmp, d);
    spell(tmp.root_pc, tonic_letter() + d, r);
    str_set(t, r, 24);
    text_cat(t, tmp.qual);
}

/* progression r into pad slot dst: four bars, a chord a bar, played as the pads play, the bass line by the bass mode */
static void make_prog(uint32_t r, uint32_t dst)
{
    pad_t *p = &pads[dst];
    const uint8_t *seq = PROGS[r % NPROGS];
    uint32_t t = set.type == 2u ? 1u : set.type, bar, bl = PPQ * 4u, n, k;
    float prev = (float)base_note() + 5.0f;
    int32_t v[6];
    __builtin_memset(p, 0, sizeof *p);
    p->type = PAD_PROG;
    p->bars = 4;
    p->bm = set.bassm;
    p->len = (uint16_t)(4u * bl);
    p->deg = seq[0];
    for (bar = 0; bar < 4u; bar++) {
        uint32_t o = bar * bl, d = seq[bar], rp = deg_pc((int32_t)d);
        int32_t bass = deg_pitch((int32_t)d) - 12;
        char rn[6];
        uint8_t dim;
        p->bar_deg[bar] = (uint8_t)d;
        n = chord_of(d, t, v);
        if (set.bassm == BM_NOROOT)      /* the root out of the chord (the bass is out already) */
            n = chord_bm(v, n, rp, BM_NOROOT) + 0u;
        nearest(v, n, prev);
        prev = 0.0f;
        for (k = 0; k < n; k++)
            prev += (float)v[k];
        prev /= (float)n;
        if (set.plays == PL_CHORD) {    /* held a bar */
            for (k = 0; k < n; k++)
                ph_note(dst, v[k], o, bl - 10u, 96);
            if (set.bassm == BM_BASS)
                ph_note(dst, bass, o, bl - 10u, 104);
        } else if (set.plays == PL_STABS) {   /* 1, the and of 2, 4; the bass on 1 and 3 */
            static const uint16_t S[3] = {0, 144, 288}, D[3] = {72, 48, 72};
            static const uint8_t V[3] = {108, 86, 96};
            uint32_t j;
            for (j = 0; j < 3u; j++)
                for (k = 0; k < n; k++)
                    ph_note(dst, v[k], o + S[j], D[j], V[j]);
            if (set.bassm == BM_BASS) {
                ph_note(dst, bass, o, 58, 112);
                ph_note(dst, bass, o + 192u, 72, 100);
            }
        } else {                         /* arp 8ths, the bass held */
            uint32_t j;
            for (j = 0; j < 8u; j++)
                ph_note(dst, v[j % n] + (j >= n ? 12 : 0), o + j * 48u, 43, j % 2u ? 82 : 100);
            if (set.bassm == BM_BASS)
                ph_note(dst, bass, o, bl - 10u, 100);
        }
        roman_of(d, rn, &dim);
        if (bar)
            text_cat_n(p->title, " ", sizeof p->title);
        text_cat_n(p->title, rn, sizeof p->title);
    }
}

/* the kit: pads 1-7 the chords on degrees 1-7, 8 the first an octave up, 9-16 progressions 1-8 (a LIB pick stays) */
static void kit_build(void)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        if (!pads[i].lock)
            make_chord(i, i);
    for (i = 0; i < 8u; i++)
        if (!pads[8u + i].lock)
            make_prog(i, 8u + i);
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

#define BARRIER() __asm__ volatile("" ::: "memory")
#define QN 128u
static volatile uint32_t q_ev[QN];
static volatile uint32_t q_w, q_r;
enum { QE_OFF, QE_ON, QE_PSTART, QE_PSTOP, QE_CSTART, QE_CSTOP };
static void ev_push(uint32_t type, uint32_t a, uint32_t b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;
    q_ev[w % QN] = type << 16 | (a & 127u) << 8 | (b & 127u);
    BARRIER();
    q_w = w + 1u;
}

static void voice_on(uint32_t note, uint32_t vel)
{
    uint32_t i, best = 0, oldest = 0xFFFFFFFFu;
    for (i = 0; i < NVOICES; i++) {
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

static volatile uint32_t tempo = 90;

static void midi_in(void)                /* audio interrupt: MIDI in plays the synth (a sound module) */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24, hi = st & 0xF0u;
        if (hi == 0x90u && d2)
            voice_on(d1, d2);
        else if (hi == 0x80u || hi == 0x90u)
            voice_off(d1);
    }
}

/* the A pads' notes out: the synth and MIDI channel A */
static uint8_t ch_a_on, ch_last;
static void out_on(uint32_t m, uint32_t v)
{
    voice_on(m, v);
    ch_a_on = ch_last = set.ch[0] & 15u;
    plat_midi_send(0x09u | (0x90u | ch_a_on) << 8 | m << 16 | v << 24, TO_USB);
}
static void out_off(uint32_t m)
{
    voice_off(m);
    plat_midi_send(0x08u | (0x80u | ch_a_on) << 8 | m << 16, TO_USB);
}

static const uint16_t LEN_PCT[11] = {10, 25, 40, 55, 75, 100, 140, 200, 300, 450, 0};
static const char *const LEN_NAME[11] = {"10%", "25%", "40%", "55%", "75%", "AS WRITTEN", "140%", "200%", "300%",
                                         "450%", "LEGATO"};
static uint32_t cs_rng = 0x2545F491u;
static uint32_t cs_rnd(void)
{
    cs_rng = cs_rng * 1664525u + 1013904223u;
    return cs_rng >> 8;
}
static uint32_t human_vel(uint32_t v)    /* humanize: velocity off by a little, like a player */
{
    int32_t h = set.human, r = (int32_t)(cs_rnd() % 201u) - 100;
    return (uint32_t)clamp((int32_t)v + (int32_t)v * h * r / 25000, 1, 127);
}

/* held and LATCH: a pad's key held, or latched on (the main loop writes them, the audio interrupt reads them) */
static volatile uint8_t pad_held[NPADS + 1], pad_latched[NPADS + 1];

/* progression players: one per pad and one for LIB's preview. A tap plays it once; held past the end it loops
 * (until let go); latched it loops until tapped again */
typedef struct {
    uint8_t on, looped;
    int8_t oct;
    float pos;
    uint8_t vel;
    uint8_t snd[MAXPH];                  /* the note sounding for phrase note k, + 1; 0 none */
    uint16_t endt[MAXPH];                /* and when it stops */
} player_t;
static player_t pl[NPADS + 1];

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
        float a, b, we;
        const pad_t *pd = &pads[i];
        if (!P->on)
            continue;
        we = (float)pd->len;
        a = P->pos;
        b = a + dt;
        for (k = 0; k < pd->pn && k < MAXPH; k++) {
            const pnote_t *x = &ph[i][k];
            float s = (float)x->s, e = fm_minf(P->snd[k] ? (float)P->endt[k] : s + (float)x->d, we);
            int starts = s >= a && s < b && s < we;
            if (starts) {
                uint32_t m = (uint32_t)clamp((int32_t)x->p + 12 * P->oct, 0, 127);
                uint32_t v = (uint32_t)x->v * P->vel / 100u, d = x->d, li = (uint32_t)(set.len + 5);
                if (set.human)
                    v = human_vel(v ? v : 1u);
                if (P->snd[k])
                    out_off(P->snd[k] - 1u);
                out_on(m, v ? v : 1u);
                P->snd[k] = (uint8_t)(m + 1u);
                if (set.len && li < 11u) {   /* ALGORITHM: the notes' length */
                    if (LEN_PCT[li])
                        d = d * LEN_PCT[li] / 100u;
                    else {               /* LEGATO: on to the next note's start */
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
            if (P->snd[k] && e < b && !(starts && e >= b)) {
                out_off(P->snd[k] - 1u);
                P->snd[k] = 0;
            }
        }
        P->pos = b;
        if (P->pos >= we) {
            pl_release(i);
            if (i < NPADS && (pad_held[i] || pad_latched[i]) && we > 0.0f) {
                P->pos -= we;            /* again from the start, on the beat */
                P->looped = 1;
            } else
                P->on = 0;
        }
    }
}

/* chord players: a chord pad while its key is held (or latched), as the pads play: CHORD (held, strummed),
 * STABS (on 1, the and of 2 and 4, again each bar), ARP (the notes in turn) */
#define MAXC 8u
typedef struct {
    uint8_t on, n, cur, mode;            /* cur: the arp's note sounding + 1, 0 none */
    uint8_t note[MAXC], vel[MAXC], sounded[MAXC];
    uint32_t at[MAXC];                   /* each note's start, samples from the press */
    uint32_t t, next, idx, off;
} cplay_t;
static cplay_t cp[NPADS];
static uint8_t cbuf[NPADS][MAXC], cbuf_n[NPADS];
static const float ARP_RATE[6] = {1.0f, 0.5f, 0.3333333f, 0.25f, 0.1666667f, 0.125f};   /* of a beat */
static const char *const ARP_RATE_NAME[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
#define STAB_PAT 0x1041u                 /* sixteenths 0, 6, 12: 1, the and of 2, 4 */

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
    c->mode = set.plays;
    for (k = 0; k < c->n; k++) {
        c->note[k] = cbuf[i][k];
        c->vel[k] = (uint8_t)human_vel(vel);
        c->sounded[k] = 0;
        c->at[k] = k * ss + (set.human ? cs_rnd() % (set.human * 13u + 1u) : 0u);   /* up to ~15 ms late */
    }
    c->t = c->next = c->idx = c->off = 0;
    c->cur = 0;
    c->on = c->n != 0;
}
static void chord_tick(uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < NPADS; i++) {
        cplay_t *c = &cp[i];
        if (!c->on)
            continue;
        if (c->mode == PL_STABS && c->n) {
            uint32_t step = (uint32_t)(15.0f * SR / (float)tempo);
            if (!step)
                step = 1u;
            if (c->off && c->off < c->t + n) {
                for (k = 0; k < c->n; k++)
                    if (c->sounded[k]) {
                        out_off(c->note[k]);
                        c->sounded[k] = 0;
                    }
                c->off = 0;
            }
            while (c->next < c->t + n) {
                if (STAB_PAT >> (c->idx % 16u) & 1u) {
                    for (k = 0; k < c->n; k++) {
                        if (c->sounded[k])
                            out_off(c->note[k]);
                        out_on(c->note[k], human_vel(c->vel[0]));
                        c->sounded[k] = 1;
                    }
                    {
                        uint32_t li = (uint32_t)(set.len + 5), g = step * 3u / 2u;
                        if (li < 11u)
                            g = LEN_PCT[li] ? g * LEN_PCT[li] / 100u : step * 5u;
                        c->off = c->next + (g ? g : 1u);
                    }
                }
                c->idx++;
                c->next += step;
            }
        } else if (c->mode == PL_ARP && c->n) {
            while (c->next < c->t + n) {
                uint32_t step = (uint32_t)(60.0f * SR / (float)tempo * ARP_RATE[set.arprate % 6u]);
                if (c->cur)
                    out_off(c->cur - 1u);
                out_on(c->note[c->idx % c->n], human_vel(c->vel[0]));
                c->cur = (uint8_t)(c->note[c->idx % c->n] + 1u);
                c->idx++;
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
        else if (a <= NPADS) {           /* a progression: start (b: the velocity) or stop */
            pl_release(a);
            pl[a].on = type == QE_PSTART;
            pl[a].looped = 0;
            pl[a].oct = (int8_t)set.octave;
            pl[a].vel = (uint8_t)(b ? b * 100u / 127u + 20u : 100u);
            pl[a].pos = 0.0f;
        }
        q_r++;
    }
    phrase_tick(n);
    chord_tick(n);
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
        fc = 700.0f + 2600.0f * x->vel * x->vel + 1800.0f * x->env;
        c = fm_minf(0.95f, 6.2831853f * fc / SR);
        inc2 = x->inc * 1.0058f;
        for (i = 0; i < n; i++) {
            float s;
            switch (x->stage) {
            case 1:
                x->env += 0.0045f;
                if (x->env >= 1.0f) {
                    x->env = 1.0f;
                    x->stage = 2;
                }
                break;
            case 2:
                x->env += (0.62f - x->env) * 0.00012f;
                break;
            default:
                x->env *= 0.99975f;
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
            s = (x->p1 + x->p2) - 1.0f;
            x->lp1 += c * (s - x->lp1);
            x->lp2 += c * (x->lp1 - x->lp2);
            mix[i] += x->lp2 * x->env * x->vel;
            if (!x->stage)
                break;
        }
    }
    for (i = 0; i < n; i++) {
        float s = mix[i] * 0.16f;
        s = s / (1.0f + fm_fabsf(s));
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
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
typedef struct {
    uint8_t n, ch;
    uint8_t note[MAXN];
} held_t;
static held_t held[NKEYS];
static void notes_on(uint32_t k, const uint8_t *notes, uint32_t n, uint32_t vel, uint32_t ch)
{
    uint32_t i;
    held_t *h = &held[k];
    h->n = 0;
    h->ch = ch_last = (uint8_t)(ch & 15u);
    for (i = 0; i < n && i < MAXN; i++) {
        int32_t m = clamp((int32_t)notes[i] + 12 * set.octave, 0, 127);
        h->note[h->n++] = (uint8_t)m;
        ev_push(QE_ON, (uint32_t)m, vel);
        plat_midi_send(0x09u | (0x90u | h->ch) << 8 | (uint32_t)m << 16 | vel << 24, TO_USB);
    }
}
static void notes_off(uint32_t k)
{
    uint32_t i;
    held_t *h = &held[k];
    for (i = 0; i < h->n; i++) {
        ev_push(QE_OFF, h->note[i], 0);
        plat_midi_send(0x08u | (0x80u | h->ch) << 8 | (uint32_t)h->note[i] << 16, TO_USB);
    }
    h->n = 0;
}

static void pad_stop(uint32_t i)
{
    pad_latched[i] = 0;
    ev_push(pads[i].type == PAD_PROG ? QE_PSTOP : QE_CSTOP, i, 0);
}
static void players_stop(void)
{
    uint32_t k;
    for (k = 0; k <= NPADS; k++) {
        pad_latched[k] = 0;
        ev_push(QE_PSTOP, k, 0);
        if (k < NPADS)
            ev_push(QE_CSTOP, k, 0);
    }
    for (k = 0; k < NKEYS; k++)
        notes_off(k);
}

/* a pad's sound: a chord into its player (octave applied), a progression started */
static void pad_start(uint32_t i, uint32_t vel)
{
    const pad_t *p = &pads[i];
    if (p->type == PAD_CHORD) {
        uint32_t k;
        for (k = 0; k < p->n && k < MAXC; k++)
            cbuf[i][k] = (uint8_t)clamp((int32_t)p->note[k] + 12 * set.octave, 0, 127);
        cbuf_n[i] = (uint8_t)(p->n < MAXC ? p->n : MAXC);
        ev_push(QE_CSTART, i, vel);
    } else if (p->type == PAD_PROG)
        ev_push(QE_PSTART, i, vel);
}

/* ======================================================================= UI === */
enum { SCR_HOME, SCR_PICK, SCR_LIB, SCR_FX, SCR_MIDI };
/* COACH: the steps; done by doing them (the screen moves on by itself) */
enum { CS_NONE, CS_CHOOSE, CS_NOTES, CS_SCALE, CS_PADS, CS_BASS, CS_SHAPE, CS_CPADS, CS_DONE };
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    int32_t pad;                         /* the A pad played last (-1 none yet) */
    uint8_t screen, redraw, oct_mod;
    uint32_t sys_at, home_at;            /* SYS / HOME went down then (0 up, all ones: held) */
    uint8_t coach, co_count, co_pick;    /* COACH: the step (CS_*), a count toward it, the choice shown */
    uint32_t co_at;                      /* when the step began */
    uint32_t boot_at;
} ui = {.pad = -1};
static uint8_t first_boot;

/* PICK NOTES: the notes you played, in order, and the keys that hold them all, best first */
#define FD_MAX 12u
static uint8_t fd_seq[FD_MAX], fd_n, fd_list[12u * NSCALES], fd_cnt, fd_sel;
static uint16_t fd_pcs;
/* LIB: the pick: 0-6 the chords, 7-16 the progressions */
static uint8_t lib_sel;
#define LIB_N (7u + NPROGS)

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
    /* the first note played as the tonic, then any note played, then major and minor before the modes */
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
        uint32_t root = (fd_seq[0] + r) % 12u;
        for (sc = 0; sc < NSCALES; sc++)
            if ((scale_mask(root, sc) & fd_pcs) == fd_pcs)
                fd_list[fd_cnt++] = (uint8_t)(root << 4 | sc);
    }
    for (i = 1; i < fd_cnt; i++) {
        uint8_t x = fd_list[i];
        int32_t sx = fd_score(x >> 4, x & 15u);
        for (j = i; j > 0 && fd_score(fd_list[j - 1] >> 4, fd_list[j - 1] & 15u) < sx; j--)
            fd_list[j] = fd_list[j - 1];
        fd_list[j] = x;
    }
}
static void fd_toggle(uint32_t pc)       /* a note in, or out again */
{
    uint32_t i, j;
    if ((fd_pcs >> pc) & 1u) {
        for (i = j = 0; i < fd_n; i++)
            if (fd_seq[i] != pc)
                fd_seq[j++] = fd_seq[i];
        fd_n = (uint8_t)j;
        fd_pcs &= (uint16_t)~(1u << pc);
    } else if (fd_n < FD_MAX) {
        fd_seq[fd_n++] = (uint8_t)pc;
        fd_pcs |= (uint16_t)(1u << pc);
    }
    fd_rank();
}
static void pick_enter(void)
{
    players_stop();
    fd_n = 0;
    fd_pcs = 0;
    fd_rank();
    ui.screen = SCR_PICK;
}
static void rekey(void)                  /* the key changed: the kit again (a LIB pick stays) */
{
    uint32_t i;
    for (i = 0; i < NPADS; i++)
        if (!pads[i].lock && (pl[i].on || cp[i].on))
            pad_stop(i);
    kit_build();
    set_dirty = 1;
    ui.redraw = 1;
}
static void pick_use(void)
{
    char t[24];
    if (!fd_cnt) {
        app_say("PICK NOTES", "PLAY A FEW NOTES FIRST");
        return;
    }
    set.root = (uint8_t)(fd_list[fd_sel] >> 4);
    set.scale = (uint8_t)(fd_list[fd_sel] & 15u);
    rekey();
    key_name(set.root, set.scale, t);
    ui.screen = SCR_HOME;
    app_say("KEY", t);
}

/* the chord shown and lit: the A pad played last, a progression's bar under its playhead */
typedef struct {
    uint8_t ok, bass, root, bass_on, root_on;
    uint16_t pcs;                        /* every note of the chord as written */
} cur_t;
static cur_t cur_chord(void)
{
    cur_t c = {0};
    const pad_t *p;
    if (ui.pad < 0)
        return c;
    p = &pads[ui.pad];
    if (p->type == PAD_CHORD) {
        uint32_t k;
        c.ok = 1;
        c.root = p->root_pc;
        c.bass = (uint8_t)(p->bass_p % 12u);
        c.bass_on = p->bm == BM_BASS;
        c.root_on = 0;
        for (k = 0; k < p->n; k++) {
            c.pcs |= (uint16_t)(1u << (p->note[k] % 12u));
            c.root_on |= p->note[k] % 12u == p->root_pc;
        }
        c.pcs |= (uint16_t)(1u << c.bass);
        if (c.bass == c.root && c.bass_on)
            c.root_on = 1;
    } else if (p->type == PAD_PROG) {
        uint32_t bar = pl[ui.pad].on ? (uint32_t)(pl[ui.pad].pos / (float)(PPQ * 4u)) : 0u, d, k;
        int32_t v[6];
        uint32_t t = set.type == 2u ? 1u : set.type, n;
        d = p->bar_deg[bar < p->bars ? bar : 0u];
        n = chord_of(d, t, v);
        c.ok = 1;
        c.root = c.bass = (uint8_t)deg_pc((int32_t)d);
        c.bass_on = p->bm == BM_BASS;
        c.root_on = p->bm != BM_NOROOT;
        for (k = 0; k < n; k++)
            c.pcs |= (uint16_t)(1u << (v[k] % 12));
    }
    return c;
}

/* ---------------------------------------------------------------- COACH --- */
static void coach_go(uint32_t step)
{
    ui.coach = (uint8_t)step;
    ui.co_count = 0;
    ui.co_at = plat_ms();
    if (step == CS_NOTES)
        pick_enter();
    if (step == CS_PADS || step == CS_BASS || step == CS_SHAPE || step == CS_CPADS || step == CS_DONE)
        ui.screen = SCR_HOME;
    ui.redraw = 1;
}
static void coach_start(void) { coach_go(CS_NOTES); }
static void coach_end(void)
{
    if (ui.coach && ui.coach != CS_CHOOSE) {
        set.coach_seen = 1;
        set_dirty = 1;
    }
    ui.coach = CS_NONE;
    ui.redraw = 1;
}
/* something happened: the step done moves the coach on. ev: what */
enum { CE_NOTE, CE_KEYSET, CE_APAD, CE_BPAD, CE_SHAPE, CE_CPAD };
static void coach_event(uint32_t ev)
{
    switch (ui.coach) {
    case CS_NOTES:
        if (ev == CE_NOTE && fd_n >= 3u)
            coach_go(CS_SCALE);
        break;
    case CS_SCALE:
        if (ev == CE_KEYSET)
            coach_go(CS_PADS);
        break;
    case CS_PADS:
        if (ev == CE_APAD && ++ui.co_count >= 3u)
            coach_go(CS_BASS);
        break;
    case CS_BASS:
        if (ev == CE_BPAD && ++ui.co_count >= 2u)
            coach_go(CS_SHAPE);
        break;
    case CS_SHAPE:
        if (ev == CE_SHAPE)
            coach_go(CS_CPADS);
        break;
    case CS_CPADS:
        if (ev == CE_CPAD)
            coach_go(CS_DONE);
        break;
    default:
        break;
    }
}
static const char *coach_title(uint32_t s)
{
    static const char *const T[] = {"", "WELCOME TO SEED", "PICK YOUR NOTES", "PICK A SCALE", "PLAY THE PADS",
                                    "PLAY THE BASS", "SHAPE IT", "BASS OR NOT", "YOU'RE SET"};
    return T[s % 9u];
}
static const char *coach_text(uint32_t s)
{
    static const char *const T[] = {"",
                                    "SHOW THESE STEPS? TURN SELECT TO CHOOSE, PRESS PLAY.",
                                    "PLAY A FEW NOTES YOU LIKE, ON ANY KEYS. PLAY ONE AGAIN TO TAKE IT OUT.",
                                    "TURN SELECT TO PICK A SCALE THAT FITS. PRESS PLAY TO USE IT.",
                                    "THE WHITE KEYS: 8 CHORDS, THEN 8 PROGRESSIONS. PLAY A FEW.",
                                    "THE BRIGHT BLACK KEY IS THE BASS OF YOUR CHORD. PLAY ALONG.",
                                    "TURN K2 FOR 7THS AND 9THS, K3 FOR HOW THE PADS PLAY.",
                                    "THE LAST 4 BLACK KEYS: BASS, NO BASS, NO ROOT, LATCH. TRY ONE.",
                                    "THAT'S SEED. HOLD SYS ANY TIME FOR THESE STEPS."};
    return T[s % 9u];
}

/* ================================================================== input === */
static void input(void)
{
    uint32_t keys, btn = plat_buttons(), i;
    int32_t e;
    touch_poll();
    keys = touch_keys();

    if (ui.coach == CS_DONE && ((keys & ~ui.keys) || plat_ms() - ui.co_at > 4000u))
        coach_end();

    if (ui.coach == CS_CHOOSE)
        goto buttons;                    /* the choice first: the keys wait */
    if (ui.screen == SCR_PICK) {         /* PICK NOTES: every key a note, from F3; played again: out */
        uint32_t k;
        for (k = 0; k < NKEYS; k++) {
            uint32_t bit = 1u << k;
            if ((keys & bit) && !(ui.keys & bit)) {
                uint8_t m = (uint8_t)clamp(53 + (int32_t)k, 0, 127);
                uint32_t vel = touch_vel(k);
                notes_on(k, &m, 1, vel ? vel : 100u, set.ch[1]);
                fd_toggle(m % 12u);
                coach_event(CE_NOTE);
                ui.redraw = 1;
            } else if (!(keys & bit) && (ui.keys & bit))
                notes_off(k);
        }
        goto buttons;
    }
    /* the A pads: a chord while held (or latched), a progression once (held: again and again) */
    for (i = 0; i < 16u; i++) {
        uint32_t k = WK[i], bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t vel = touch_vel(k);
            if (ui.screen == SCR_LIB) {  /* LIB: the pick goes onto the pad */
                char t[24];
                pad_stop(i);
                if (lib_sel < 7u)
                    make_chord(lib_sel, i);
                else
                    make_prog(lib_sel - 7u, i);
                pads[i].lock = 1;
                str_set(t, "PAD ", sizeof t);
                num_text((int32_t)i + 1, t + 4, 0);
                app_say(t, lib_sel < 7u ? "A CHORD, KEPT" : "A PROGRESSION, KEPT");
            }
            if (pad_latched[i] && (pl[i].on || cp[i].on)) {   /* latched and playing: tapped again, it stops */
                pad_stop(i);
                pad_held[i] = 0;
            } else {
                pad_held[i] = 1;
                pad_start(i, vel ? vel : 100u);
                if (set.latch)
                    pad_latched[i] = 1;
            }
            ui.pad = (int32_t)i;
            coach_event(CE_APAD);
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit)) {
            pad_held[i] = 0;
            if (!pad_latched[i]) {
                if (pads[i].type == PAD_CHORD)
                    ev_push(QE_CSTOP, i, 0);
                else if (pl[i].looped)   /* let go after it looped: it stops */
                    ev_push(QE_PSTOP, i, 0);
            }
            ui.redraw = 1;
        }
    }
    /* the B pads: the notes of the key */
    for (i = 0; i < 7u; i++) {
        uint32_t k = BK[i], bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint8_t m = (uint8_t)clamp(deg_pitch((int32_t)i), 0, 127);
            uint32_t vel = touch_vel(k);
            notes_on(k, &m, 1, vel ? vel : 100u, set.ch[1]);
            coach_event(CE_BPAD);
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit))
            notes_off(k);
    }
    /* the C pads: BASS, NO BASS, NO ROOT, LATCH */
    for (i = 0; i < 4u; i++) {
        uint32_t bit = 1u << BK[7u + i];
        if ((keys & bit) && !(ui.keys & bit)) {
            if (i < 3u) {
                set.bassm = (uint8_t)i;
                rekey();
                app_say("TYPE", BM_NAME[i]);
            } else {
                set.latch = (uint8_t)!set.latch;
                set_dirty = 1;
                if (!set.latch) {        /* LATCH off: what it kept going stops */
                    uint32_t q;
                    for (q = 0; q < NPADS; q++)
                        if (pad_latched[q]) {
                            pad_latched[q] = 0;
                            if (!pad_held[q])
                                ev_push(pads[q].type == PAD_PROG ? QE_PSTOP : QE_CSTOP, q, 0);
                        }
                }
                app_say("LATCH", set.latch ? "ON: PADS KEEP PLAYING" : "OFF");
            }
            coach_event(CE_CPAD);
            ui.redraw = 1;
        }
    }
buttons:
    for (i = 0; i < NB; i++) {
        uint32_t bit = 1u << i;
        int down = (btn & bit) && !(ui.btn & bit), up = !(btn & bit) && (ui.btn & bit);
        if (i == B_OCTDN || i == B_OCTUP) {
            if (down && !(ui.btn & (1u << (i == B_OCTUP ? B_OCTDN : B_OCTUP))))
                ui.oct_mod = 0;
            if (up && !ui.oct_mod) {
                int32_t o = clamp(set.octave + (i == B_OCTUP ? 1 : -1), -2, 2);
                if (o != set.octave) {
                    char t[4];
                    num_text(o, t, 1);
                    set.octave = (int8_t)o;
                    set_dirty = 1;
                    app_say("OCTAVE", t);
                }
            }
            continue;
        }
        if (!down)
            continue;
        if (ui.coach == CS_CHOOSE && i != B_PLAY && i != B_HOME)
            continue;                    /* the choice first */
        if (i == B_SAVE) {
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "KEY AND SETTINGS");
            } else
                app_say("SAVE", "FAILED");
        } else if (i == B_PLAY) {
            if (ui.coach == CS_CHOOSE) { /* the choice made */
                set.coach = ui.co_pick;
                set.coach_set = 1;
                set_dirty = 1;
                plat_store_save(OBJ_SETTINGS, &set, sizeof set);
                set_dirty = 0;
                if (set.coach == CO_OFF) {
                    ui.coach = CS_NONE;
                    app_say("STEPS OFF", "HOLD SYS FOR THEM");
                } else
                    coach_start();
            } else if (ui.screen == SCR_PICK) {
                pick_use();
                coach_event(CE_KEYSET);
            } else if (ui.screen == SCR_LIB) {   /* hear the pick */
                pad_stop(PREVIEW);
                if (lib_sel < 7u)
                    make_chord(lib_sel, PREVIEW);
                else
                    make_prog(lib_sel - 7u, PREVIEW);
                if (pads[PREVIEW].type == PAD_CHORD) {   /* a chord: a beat of it, as a phrase */
                    pad_t *p = &pads[PREVIEW];
                    uint32_t k, nn = p->n;
                    uint8_t nt[MAXN];
                    for (k = 0; k < nn; k++)
                        nt[k] = p->note[k];
                    p->type = PAD_PROG;
                    p->pn = 0;
                    p->len = (uint16_t)(PPQ * 2u);
                    for (k = 0; k < nn; k++)
                        ph_note(PREVIEW, nt[k], 0, PPQ * 2u - 10u, 100);
                }
                ev_push(QE_PSTART, PREVIEW, 100);
            } else {
                players_stop();
                app_say("ALL", "NOTES OFF");
            }
        } else if (i == B_HOME) {
            if (ui.coach && ui.coach != CS_CHOOSE) {
                coach_end();
                app_say("STEPS", "HOLD SYS FOR THEM AGAIN");
            } else if (ui.coach == CS_CHOOSE) {
                ui.coach = CS_NONE;
                set.coach_set = 1;
                set_dirty = 1;
            }
            ui.screen = SCR_HOME;
        } else if (i == B_EDIT) {        /* PICK NOTES */
            if (ui.screen == SCR_PICK)
                ui.screen = SCR_HOME;
            else {
                pick_enter();
                app_say("PICK NOTES", "PLAY A FEW YOU LIKE");
            }
        } else if (i == B_SEL || i == B_LFO) {   /* LIB */
            ui.screen = ui.screen == SCR_LIB ? SCR_HOME : SCR_LIB;
        } else if (i == B_FX) {
            ui.screen = ui.screen == SCR_FX ? SCR_HOME : SCR_FX;
        } else if (i == B_GLO) {         /* SYS: tapped, the MIDI page; held, COACH (below) */
        } else {
            static const char *const LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "SYS", "HOME", "SAVE",
                                                  "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
            app_say(LABEL[i], "COMING SOON");
        }
        ui.redraw = 1;
    }
    /* ALGORITHM: the notes' length */
    if ((e = plat_enc(EN_ALGO)) != 0) {
        set.len = (int8_t)clamp(set.len + e, -5, 5);
        app_say("LENGTH", LEN_NAME[set.len + 5]);
        set_dirty = 1;
    }
    {
        int32_t es = plat_enc(EN_SELECT), k1 = plat_enc(EN_K1), k2 = plat_enc(EN_K2), k3 = plat_enc(EN_K3),
                k4 = plat_enc(EN_K4), ek = plat_enc(EN_PRESET);
        if ((es || k1 || k2 || k3 || k4 || ek) && (btn & (1u << B_OCTDN | 1u << B_OCTUP)))
            ui.oct_mod = 1;
        if (ui.coach == CS_CHOOSE) {
            if (es)
                ui.co_pick = (uint8_t)clamp(ui.co_pick + es, CO_OFF, CO_ALWAYS);
            es = k1 = k2 = k3 = k4 = ek = 0;
        }
        if (ui.screen == SCR_PICK) {
            if (es && fd_cnt)
                fd_sel = (uint8_t)clamp((int32_t)fd_sel + es, 0, (int32_t)fd_cnt - 1);
            if (k1) {                    /* CLEAR */
                fd_n = 0;
                fd_pcs = 0;
                fd_rank();
            }
            es = k1 = k2 = k3 = k4 = ek = 0;
        } else if (ui.screen == SCR_LIB) {
            if (es)
                lib_sel = (uint8_t)clamp((int32_t)lib_sel + es, 0, (int32_t)LIB_N - 1);
            es = 0;
        } else if (ui.screen == SCR_FX) {
            if (k1)
                set.strum = (uint8_t)clamp(set.strum + 2 * k1, 0, 50);
            if (k2)
                set.human = (uint8_t)clamp(set.human + 5 * k2, 0, 50);
            if (k3)
                set.arprate = (uint8_t)clamp(set.arprate + k3, 0, 5);
            if (k1 || k2 || k3)
                set_dirty = 1;
            k1 = k2 = k3 = k4 = 0;
        } else if (ui.screen == SCR_MIDI) {
            int32_t kk[2] = {k1, k2};
            uint32_t g;
            for (g = 0; g < 2u; g++)
                if (kk[g]) {
                    players_stop();
                    set.ch[g] = (uint8_t)clamp((int32_t)set.ch[g] + kk[g], 0, 15);
                    set_dirty = 1;
                }
            if (k4) {
                set.coach = (uint8_t)clamp(set.coach + k4, CO_OFF, CO_ALWAYS);
                set.coach_set = 1;
                set_dirty = 1;
            }
            k1 = k2 = k3 = k4 = 0;
        }
        /* everywhere else: the home knobs */
        if (ek) {                        /* PRESET: the key's root */
            char t[24];
            set.root = (uint8_t)(((int32_t)set.root + ek % 12 + 12) % 12);
            rekey();
            key_name(set.root, set.scale, t);
            app_say("KEY", t);
        }
        if (k1) {                        /* K1: the scale */
            char t[24];
            set.scale = (uint8_t)(((int32_t)set.scale + k1 % (int32_t)NSCALES + (int32_t)NSCALES) % (int32_t)NSCALES);
            rekey();
            key_name(set.root, set.scale, t);
            app_say("KEY", t);
        }
        if (k2) {                        /* K2: the chords' type */
            set.type = (uint8_t)clamp(set.type + k2, 0, (int32_t)NCTYPES - 1);
            rekey();
            app_say("TYPE", CTYPE_NAME[set.type]);
            coach_event(CE_SHAPE);
        }
        if (k3) {                        /* K3: how the pads play */
            set.plays = (uint8_t)clamp(set.plays + k3, 0, PL_N - 1);
            rekey();
            app_say("PLAYS", PLAYS_NAME[set.plays]);
            coach_event(CE_SHAPE);
        }
        if (k4) {                        /* K4: humanize */
            char t[8];
            set.human = (uint8_t)clamp(set.human + 5 * k4, 0, 50);
            num_text(set.human * 2, t, 0);
            text_cat(t, "%");
            app_say("HUMANIZE", t);
            set_dirty = 1;
        }
        if (es) {                        /* SELECT: BPM */
            char b[8];
            set.bpm = (uint8_t)clamp((int32_t)set.bpm + es, 40, 240);
            tempo = set.bpm;
            num_text(set.bpm, b, 0);
            app_say("BPM", b);
            set_dirty = 1;
        }
        if (es || k1 || k2 || k3 || k4 || ek)
            ui.redraw = 1;
    }
    {                                    /* SYS: a tap, the MIDI page; held 0.8 s, COACH */
        uint32_t now = plat_ms();
        if (btn & 1u << B_GLO) {
            if (!ui.sys_at)
                ui.sys_at = now ? now : 1u;
            else if (ui.sys_at != 0xFFFFFFFFu && now - ui.sys_at >= 800u) {
                ui.sys_at = 0xFFFFFFFFu;
                coach_start();
            }
        } else {
            if (ui.sys_at && ui.sys_at != 0xFFFFFFFFu && ui.coach != CS_CHOOSE) {
                ui.screen = ui.screen == SCR_MIDI ? SCR_HOME : SCR_MIDI;
                ui.redraw = 1;
            }
            ui.sys_at = 0;
        }
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

/* ================================================================= lights === */
/* home: the A pads dim, held or playing full; the B pads show the chord you played: the bass full (blinking when
 * NO BASS or NO ROOT takes it out), the rest of the chord mid, the key dim; the C pad in use full, LATCH full when
 * on. COACH: what the step means blinks */
static void lights(void)
{
    uint32_t i, bright = 0, dim = 0, mid = 0, btn = 0, now = plat_ms();
    int blink = (now / 260u) & 1u;
    if (ui.coach == CS_CHOOSE) {
        plat_lights(blink ? 1u << B_PLAY : 0u, 0, 0, 0, 1);
        plat_lights_mid(0, 0);
        return;
    }
    if (ui.screen == SCR_PICK) {         /* the notes picked full, the rest of the scale shown dim */
        uint16_t sm = fd_cnt ? scale_mask(fd_list[fd_sel] >> 4, fd_list[fd_sel] & 15u) : 0;
        for (i = 0; i < NKEYS; i++) {
            uint32_t pc = (uint32_t)(53 + (int32_t)i) % 12u;
            if ((ui.keys >> i & 1u) || (fd_pcs >> pc & 1u))
                bright |= 1u << i;
            else if (sm >> pc & 1u)
                dim |= 1u << i;
        }
        if (ui.coach == CS_NOTES && !fd_n && blink)   /* every key: play me */
            for (i = 0; i < NKEYS; i++)
                dim |= 1u << i;
        btn = 1u << B_EDIT;
        if (fd_cnt && (ui.coach != CS_SCALE || blink))
            btn |= 1u << B_PLAY;
        plat_lights(btn | ui.btn, bright, 0, dim, 1);
        plat_lights_mid(0, 0);
        return;
    }
    for (i = 0; i < 16u; i++) {
        uint32_t bit = 1u << WK[i];
        if ((ui.keys & bit) || pl[i].on || cp[i].on)
            bright |= bit;
        else if (ui.screen == SCR_LIB)
            mid |= bit;                  /* drop targets */
        else if (ui.coach == CS_PADS) {
            if (blink)
                bright |= bit;
            else
                dim |= bit;
        }
        else if (pads[i].type != PAD_EMPTY)
            dim |= bit;
    }
    {
        cur_t c = cur_chord();
        for (i = 0; i < 7u; i++) {
            uint32_t bit = 1u << BK[i], pc = deg_pc((int32_t)i);
            if (ui.keys & bit)
                bright |= bit;
            else if (c.ok && pc == c.bass) {
                if (c.bass_on || blink)
                    bright |= bit;
                else
                    dim |= bit;
            } else if (c.ok && (c.pcs >> pc & 1u) && (pc != c.root || c.root_on))
                mid |= bit;
            else if (ui.coach == CS_BASS && blink)
                mid |= bit;
            else
                dim |= bit;
        }
    }
    for (i = 0; i < 4u; i++) {
        uint32_t bit = 1u << BK[7u + i];
        int on = i < 3u ? set.bassm == i : set.latch;
        if (on || (ui.keys & bit) || (ui.coach == CS_CPADS && blink))
            bright |= bit;
        else
            dim |= bit;
    }
    if (set_dirty)
        btn |= 1u << B_SAVE;
    btn |= 1u << (ui.screen == SCR_LIB ? B_SEL : ui.screen == SCR_FX ? B_FX : ui.screen == SCR_MIDI ? B_GLO : B_HOME);
    if (ui.screen == SCR_LIB)
        btn |= 1u << B_LFO;
    plat_lights(btn | ui.btn, bright, 0, dim, 1);
    plat_lights_mid(0, mid);
}

/* ================================================================ drawing === */
/* the Earth Science palette: Earth, Air, Fire, Water; ink, slate, purple and white for the rest */
static const uint16_t EARTH_C = RGB(0x7C, 0x5D, 0x48), AIR_C = RGB(0xAD, 0xCF, 0xDE), FIRE_C = RGB(0xDC, 0x7B, 0x78),
                      WATER_C = RGB(0x7E, 0xB3, 0xD2);
static const uint16_t ES_INK = RGB(0x31, 0x30, 0x43), ES_SLATE = RGB(0x42, 0x4C, 0x60), ES_PURPLE = RGB(0x57, 0x55, 0x6F);
static const uint16_t LAB_GRID = RGB(0x3A, 0x3D, 0x54), LAB_LABEL = RGB(0xA9, 0xA9, 0xB4);
static void lab_begin(void)              /* a band of the screen: ink, graph paper */
{
    int32_t k;
    cv_begin(240, 120, ES_INK);
    for (k = 12; k < 240; k += 12)
        cv_rect(k, 0, 1, 120, LAB_GRID);
    for (k = 12; k < 120; k += 12)
        cv_rect(0, k, 240, 1, LAB_GRID);
}
static void deg_sign(int32_t x, int32_t y, uint16_t c)   /* a small degree sign: a 4 px ring */
{
    cv_rect(x + 1, y, 2, 1, c);
    cv_rect(x + 1, y + 3, 2, 1, c);
    cv_rect(x, y + 1, 1, 2, c);
    cv_rect(x + 3, y + 1, 1, 2, c);
}
static int has_lower(const char *t)
{
    for (; *t; t++)
        if (*t >= 'a' && *t <= 'z')
            return 1;
    return 0;
}
static const felucca_font_t *fit_font(const char *t, int32_t w, const felucca_font_t *big)
{
    if (has_lower(t))
        big = &FONT_B;
    if (big == &FONT_L && text_w(&FONT_L, t) <= w)
        return &FONT_L;
    if ((big == &FONT_L || big == &FONT_M) && text_w(&FONT_M, t) <= w)
        return &FONT_M;
    if (text_w(&FONT_B, t) <= w)
        return &FONT_B;
    return &FONT_XS;
}
static void text_c(int32_t y, const felucca_font_t *f, const char *t, uint16_t c)   /* centred */
{
    cv_text((240 - text_w(f, t)) / 2, y, f, t, c);
}
static void pill(int32_t x, int32_t y, const char *t, uint16_t bg, uint16_t fg)   /* returns nothing; 21 px tall */
{
    int32_t w = text_w(&FONT_B, t);
    cv_rect(x, y, w + 12, 21, bg);
    cv_text(x + 6, y + 2, &FONT_B, t, fg);
}
/* words wrapped to w pixels, from y, at most lines lines: the next y */
static int32_t wrap(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, int32_t lh, const char *t, uint16_t c,
                    uint32_t lines)
{
    char line[48];
    uint32_t n = 0;
    while (*t && lines) {
        uint32_t take = 0, last_sp = 0, i;
        char tryl[48];
        for (i = 0; t[i] && i < 46u; i++) {
            str_set(tryl, t, i + 2u);
            if (text_w(f, tryl) > w)
                break;
            if (t[i] == ' ')
                last_sp = i;
            take = i + 1u;
        }
        if (t[take] && last_sp && take < cs_len(t))
            take = last_sp;
        str_set(line, t, take + 1u);
        cv_text(x, y, f, line, c);
        y += lh;
        t += take;
        while (*t == ' ')
            t++;
        lines--;
        n++;
    }
    return y;
}

static void top_bar(const char *tag, uint16_t tag_bg)
{
    char t[24];
    if (plat_ms() < ui.msg_until) {      /* a message: big */
        const felucca_font_t *f = !has_lower(ui.msg[0]) && !has_lower(ui.msg[1]) &&
                                          text_w(&FONT_M, ui.msg[0]) + text_w(&FONT_M, ui.msg[1]) + 10 <= 230
                                      ? &FONT_M
                                      : &FONT_B;
        int32_t x = cv_text(6, f == &FONT_M ? 1 : 5, f, ui.msg[0], C_WHITE);
        cv_text(x + 8, f == &FONT_M ? 1 : 5, f, ui.msg[1], AIR_C);
    } else {
        int32_t w;
        pill(4, 3, tag, tag_bg, C_WHITE);
        spell(set.root, tonic_letter(), t);
        text_cat(t, " ");
        text_cat(t, SCALE_SHORT[set.scale]);
        w = text_w(&FONT_B, t);
        pill(222 - w, 3, t, AIR_C, ES_INK);
        {
            char b[12];
            num_text((int32_t)tempo, b, 0);
            text_cat(b, " BPM");
            cv_text(222 - w - 12 - text_w(&FONT_XS, b), 9, &FONT_XS, b, LAB_LABEL);
        }
    }
    cv_rect(0, 27, 240, 1, ES_SLATE);
}
static void bottom_bar(int32_t y, const char *const lbl[4], char val[4][12])
{
    uint32_t i;
    cv_rect(0, y, 240, 1, ES_SLATE);
    for (i = 0; i < 4u; i++) {
        int32_t cx = (int32_t)i * 60;
        const felucca_font_t *f = fit_font(val[i], 58, &FONT_M);
        cv_text(cx + (60 - text_w(&FONT_XS, lbl[i])) / 2, y + 3, &FONT_XS, lbl[i], LAB_LABEL);
        cv_text(cx + 1 + (58 - text_w(f, val[i])) / 2, y + 18 + (f == &FONT_M ? 0 : 4), f, val[i], C_WHITE);
    }
}

/* the 7 B pads as the web's keyboard shows them: brown the bass, red the root, blue the chord, an outline: muted */
static void draw_bpads(int32_t y)
{
    cur_t c = cur_chord();
    uint32_t i;
    for (i = 0; i < 7u; i++) {
        uint32_t pc = deg_pc((int32_t)i);
        int32_t x = 4 + (int32_t)i * 33;
        char nm[4];
        uint16_t fill = ES_SLATE, fg = C_WHITE, ring = 0;
        int mute = 0;
        if (c.ok && pc == c.bass) {
            fill = EARTH_C;
            mute = !c.bass_on;
            if (pc == c.root)
                ring = FIRE_C;
        } else if (c.ok && pc == c.root) {
            fill = FIRE_C;
            fg = ES_INK;
            mute = !c.root_on;
        } else if (c.ok && (c.pcs >> pc & 1u)) {
            fill = WATER_C;
            fg = ES_INK;
        }
        if (mute) {                      /* muted: an outline, the note to play still shown */
            cv_rect(x, y, 31, 30, fill);
            cv_rect(x + 3, y + 3, 25, 24, ES_INK);
            fg = C_WHITE;
        } else
            cv_rect(x, y, 31, 30, fill);
        if (ring)
            cv_rect(x, y + 26, 31, 4, ring);
        deg_name(i, nm);
        cv_text(x + (31 - text_w(&FONT_B, nm)) / 2, y + 6, &FONT_B, nm, fg);
    }
}

/* the bottom half at home: the 16 A pads, the C pads, the knobs */
static void draw_home_bottom(void)
{
    uint32_t i;
    lab_begin();
    for (i = 0; i < 16u; i++) {          /* two rows of 8: the chords, the progressions */
        int32_t x = 4 + (int32_t)(i % 8u) * 29, y = 2 + (int32_t)(i / 8u) * 23;
        int on = (int)(ui.keys >> WK[i] & 1u) || pl[i].on || cp[i].on;
        const pad_t *p = &pads[i];
        uint16_t bg = p->type == PAD_PROG ? (on ? WATER_C : ES_PURPLE) : (on ? EARTH_C : ES_SLATE);
        char t[8];
        cv_rect(x, y, 27, 21, bg);
        if ((int32_t)i == ui.pad)
            cv_rect(x, y + 18, 27, 3, AIR_C);
        if (p->lock)
            cv_rect(x + 22, y, 5, 5, AIR_C);
        if (p->type == PAD_CHORD)
            str_set(t, p->roman, sizeof t);
        else {
            t[0] = 'P';
            num_text((int32_t)i - 7, t + 1, 0);
        }
        cv_text(x + (27 - text_w(&FONT_XS, t)) / 2, y + 4, &FONT_XS, t, on ? ES_INK : C_WHITE);
    }
    for (i = 0; i < 4u; i++) {           /* the C pads */
        static const char *const CN[4] = {"BASS", "NO BASS", "NO ROOT", "LATCH"};
        int32_t x = 4 + (int32_t)i * 58;
        int on = i < 3u ? set.bassm == i : set.latch;
        cv_rect(x, 50, 56, 16, on ? (i < 3u ? EARTH_C : FIRE_C) : ES_SLATE);
        cv_text(x + (56 - text_w(&FONT_XS, CN[i])) / 2, 52, &FONT_XS, CN[i], C_WHITE);
    }
    {
        static const char *const LBL[4] = {"K1 SCALE", "K2 TYPE", "K3 PLAYS", "K4 HUMAN"};
        char v[4][12];
        str_set(v[0], SCALE_SHORT[set.scale], sizeof v[0]);
        str_set(v[1], CTYPE_NAME[set.type], sizeof v[1]);
        str_set(v[2], PLAYS_NAME[set.plays], sizeof v[2]);
        num_text(set.human * 2, v[3], 0);
        text_cat(v[3], "%");
        bottom_bar(72, LBL, v);
    }
    cv_blit(0, 120);
}

static void draw_home_top(void)
{
    const pad_t *p = ui.pad >= 0 ? &pads[ui.pad] : 0;
    int32_t x;
    lab_begin();
    top_bar("SEED", EARTH_C);
    if (p && p->type == PAD_CHORD) {
        char t[24], r[2] = {p->nm[0][0], 0};
        const char *acc;
        int32_t w;
        /* the chord's name from its root (as written, the bass mode aside) */
        char rn[4];
        spell(p->root_pc, tonic_letter() + p->deg, rn);
        r[0] = rn[0];
        acc = rn + 1;
        str_set(t, "PAD ", sizeof t);
        num_text(ui.pad + 1, t + 4, 0);
        x = cv_text(8, 32, &FONT_B, t, LAB_LABEL);
        x = cv_text(x + 10, 32, &FONT_B, p->roman, LAB_LABEL);
        if (p->dim)
            deg_sign(x + 1, 34, LAB_LABEL);
        w = text_w(&FONT_L, r) + text_w(&FONT_B, acc) + text_w(&FONT_B, p->qual) + 4;
        x = cv_text((240 - w) / 2, 44, &FONT_L, r, C_WHITE);
        if (*acc)
            x = cv_text(x, 44, &FONT_B, acc, C_WHITE);
        cv_text(x + 3, 58, &FONT_B, p->qual, C_WHITE);
    } else if (p && p->type == PAD_PROG) {
        char t[24] = "PAD ";
        num_text(ui.pad + 1, t + 4, 0);
        text_cat(t, ", PROGRESSION");
        cv_text(8, 32, &FONT_B, t, LAB_LABEL);
        text_c(56, &FONT_B, p->title, C_WHITE);
    } else {
        text_c(40, &FONT_M, "PLAY A PAD", LAB_LABEL);
        text_c(64, &FONT_XS, "WHITE KEYS: CHORDS, THEN PROGRESSIONS", LAB_LABEL);
    }
    draw_bpads(86);
    cv_blit(0, 0);
}

static void draw_pick_top(void)
{
    uint32_t i;
    int32_t x = 6;
    lab_begin();
    top_bar("PICK NOTES", WATER_C);
    if (!fd_n) {
        text_c(44, &FONT_M, "PLAY NOTES", C_WHITE);
        text_c(72, &FONT_B, "ANY KEYS, AS MANY AS YOU LIKE", LAB_LABEL);
    } else {
        cv_text(6, 34, &FONT_XS, "YOUR NOTES (PLAY ONE AGAIN: OUT)", LAB_LABEL);
        for (i = 0; i < fd_n; i++) {
            char t[4];
            int32_t w;
            pc_name(fd_seq[i], t);
            w = text_w(&FONT_B, t) + 10;
            if (x + w > 236)
                break;
            cv_rect(x, 50, w, 22, i ? WATER_C : FIRE_C);
            cv_text(x + 5, 52, &FONT_B, t, ES_INK);
            x += w + 4;
        }
        cv_text(6, 84, &FONT_XS, fd_cnt ? "K1: CLEAR" : "NO SCALE HOLDS ALL OF THEM", LAB_LABEL);
    }
    cv_blit(0, 0);
}
static void draw_pick_bottom(void)
{
    uint32_t i, first;
    lab_begin();
    cv_text(6, 4, &FONT_XS, "SCALES THAT FIT (SELECT, THEN PLAY)", LAB_LABEL);
    first = fd_sel > 2u ? fd_sel - 2u : 0u;
    for (i = 0; i < 4u && first + i < fd_cnt; i++) {
        char t[24];
        uint32_t k = first + i;
        int32_t y = 20 + (int32_t)i * 24;
        key_name(fd_list[k] >> 4, fd_list[k] & 15u, t);
        if (k == fd_sel)
            cv_rect(2, y - 2, 236, 22, AIR_C);
        cv_text(10, y, &FONT_B, t, k == fd_sel ? ES_INK : C_WHITE);
    }
    if (!fd_cnt)
        text_c(50, &FONT_B, fd_n ? "TAKE A NOTE OUT" : "THEY SHOW UP HERE", LAB_LABEL);
    cv_blit(0, 120);
}

static void lib_label(uint32_t s, char *t)
{
    if (s < 7u) {
        uint8_t dim;
        char r[6];
        chord_label(s, t);
        roman_of(s, r, &dim);
        text_cat(t, "  ");
        text_cat(t, r);
    } else {
        uint32_t k;
        uint8_t dim;
        str_set(t, "", 24);
        for (k = 0; k < 4u; k++) {
            char r[6];
            roman_of(PROGS[s - 7u][k], r, &dim);
            if (k)
                text_cat(t, " ");
            text_cat(t, r);
        }
    }
}
static void draw_lib(void)
{
    uint32_t i, first;
    lab_begin();
    top_bar("LIB", ES_PURPLE);
    text_c(40, &FONT_B, lib_sel < 7u ? "YOUR KEY'S CHORDS" : "PROGRESSIONS", LAB_LABEL);
    {
        char t[24];
        lib_label(lib_sel, t);
        text_c(62, fit_font(t, 230, &FONT_M), t, C_WHITE);
    }
    text_c(96, &FONT_XS, "PLAY: HEAR IT. A WHITE KEY: PUT IT THERE", LAB_LABEL);
    cv_blit(0, 0);
    lab_begin();
    first = lib_sel > 1u ? lib_sel - 1u : 0u;
    for (i = 0; i < 4u && first + i < LIB_N; i++) {
        char t[24];
        uint32_t k = first + i;
        int32_t y = 8 + (int32_t)i * 26;
        lib_label(k, t);
        if (k == lib_sel)
            cv_rect(2, y - 2, 236, 22, AIR_C);
        cv_text(10, y, &FONT_B, t, k == lib_sel ? ES_INK : C_WHITE);
    }
    cv_blit(0, 120);
}

static void draw_fx(void)
{
    static const char *const LBL[4] = {"K1 STRUM", "K2 HUMAN", "K3 ARP", ""};
    char v[4][12];
    lab_begin();
    top_bar("FX", WATER_C);
    text_c(44, &FONT_B, "HOW THE PADS PLAY", C_WHITE);
    text_c(66, &FONT_XS, "STRUM: CHORD. ARP: THE ARP'S SPEED", LAB_LABEL);
    cv_blit(0, 0);
    lab_begin();
    num_text(set.strum * 2, v[0], 0);
    text_cat(v[0], "MS");
    num_text(set.human * 2, v[1], 0);
    text_cat(v[1], "%");
    str_set(v[2], ARP_RATE_NAME[set.arprate % 6u], sizeof v[2]);
    v[3][0] = 0;
    bottom_bar(40, LBL, v);
    cv_blit(0, 120);
}

static void draw_midi(void)
{
    static const char *const LBL[4] = {"K1 A PADS", "K2 B PADS", "", "K4 STEPS"};
    char v[4][12];
    lab_begin();
    top_bar("SYS", ES_PURPLE);
    text_c(40, &FONT_B, "MIDI OUT CHANNELS", C_WHITE);
    text_c(64, &FONT_XS, "HOLD SYS FOR THE STEPS", LAB_LABEL);
    cv_blit(0, 0);
    lab_begin();
    str_set(v[0], "CH ", sizeof v[0]);
    num_text(set.ch[0] + 1, v[0] + 3, 0);
    str_set(v[1], "CH ", sizeof v[1]);
    num_text(set.ch[1] + 1, v[1] + 3, 0);
    v[2][0] = 0;
    str_set(v[3], set.coach == CO_OFF ? "OFF" : set.coach == CO_FIRST ? "FIRST" : "ALWAYS", sizeof v[3]);
    bottom_bar(40, LBL, v);
    cv_blit(0, 120);
}

/* COACH: the step's card over the top half, the screen it means under it */
static void draw_coach_top(void)
{
    char t[24];
    lab_begin();
    if (ui.coach == CS_CHOOSE) {
        text_c(6, &FONT_M, "SEED", EARTH_C);
        text_c(32, &FONT_XS, "AN MPS-16 FUNCTION FOR FM-1", LAB_LABEL);
        wrap(8, 52, 224, &FONT_B, 18, coach_text(CS_CHOOSE), C_WHITE, 3);
    } else {
        if (ui.coach < CS_DONE) {
            str_set(t, "STEP ", sizeof t);
            num_text(ui.coach - 1, t + 5, 0);
            text_cat(t, " OF 6");
        } else
            str_set(t, "STEPS DONE", sizeof t);
        pill(4, 3, t, AIR_C, ES_INK);
        cv_text(234 - text_w(&FONT_XS, "HOME: SKIP"), 9, &FONT_XS, "HOME: SKIP", LAB_LABEL);
        cv_text(8, 30, fit_font(coach_title(ui.coach), 224, &FONT_M), coach_title(ui.coach), FIRE_C);
        if (ui.coach >= CS_PADS) {       /* the B pads under it, as at home */
            wrap(8, 52, 224, &FONT_B, 17, coach_text(ui.coach), C_WHITE, 2);
            draw_bpads(88);
        } else
            wrap(8, 56, 224, &FONT_B, 18, coach_text(ui.coach), C_WHITE, 3);
    }
    cv_blit(0, 0);
}
static void draw_coach_choice(void)
{
    uint32_t i;
    lab_begin();
    for (i = 0; i < CO_N; i++) {
        int32_t y = 10 + (int32_t)i * 30;
        if (i == ui.co_pick)
            cv_rect(2, y - 3, 236, 26, AIR_C);
        cv_text(14, y, &FONT_B, i == CO_OFF ? "NO, NOT NOW" : i == CO_FIRST ? "THIS FIRST TIME" : "EVERY START",
                i == ui.co_pick ? ES_INK : C_WHITE);
    }
    text_c(102, &FONT_XS, "CHANGE IT LATER: SYS, K4", LAB_LABEL);
    cv_blit(0, 120);
}

#ifndef PLAT_HOST
static uint8_t splash_done;
#endif
static void draw_splash(void)
{
    lcd_fill(0, 0, 240, 240, ES_INK);
    cv_begin(240, 120, ES_INK);
    text_c(30, &FONT_L, "SEED", EARTH_C);
    text_c(70, &FONT_B, "AN MPS-16 FUNCTION", AIR_C);
    text_c(90, &FONT_B, "FOR FM-1", AIR_C);
    cv_blit(0, 60);
}

void app_init(void)
{
    pal[0] = RGB(0x42, 0x4C, 0x60);
    pal[1] = RGB(0x57, 0x55, 0x6F);
    pal[2] = RGB(0xA9, 0xA9, 0xB4);
    pal[3] = RGB(0xAD, 0xCF, 0xDE);
    pal[4] = RGB(0xFF, 0xFF, 0xFF);
    {
        int got = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
        if (got != (int)sizeof set || set.magic != SET_MAGIC || set.root > 11u || set.scale >= NSCALES ||
            set.octave < -2 || set.octave > 2 || set.type >= NCTYPES || set.plays >= PL_N || set.bassm >= BM_N ||
            set.human > 50u || set.strum > 50u || set.len < -5 || set.len > 5 || set.bpm < 40u || set.bpm > 240u ||
            set.arprate > 5u || set.ch[0] > 15u || set.ch[1] > 15u || set.ch[2] > 15u || set.coach >= CO_N) {
            settings_defaults();
            first_boot = 1;
        }
    }
    tempo = set.bpm;
    kit_build();
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.boot_at = plat_ms();
    ui.redraw = 1;
    if (!set.coach_set)
        ui.coach = CS_CHOOSE, ui.co_pick = CO_FIRST;
    else if (set.coach == CO_ALWAYS || (set.coach == CO_FIRST && !set.coach_seen))
        coach_start();
}

void app_frame(void)
{
    static uint32_t last_on, last_blink;
    uint32_t now = plat_ms(), i, on = 0;
    (void)first_boot;
    input();
    lights();
    ann_tick(now);
    for (i = 0; i <= NPADS; i++)
        on |= (uint32_t)(pl[i].on | (i < NPADS ? cp[i].on : 0)) << i;
    if (on != last_on) {
        last_on = on;
        ui.redraw = 1;
    }
    if (ui.pad >= 0 && pl[ui.pad].on) {  /* a progression playing: its bar moves the B pads */
        static uint32_t bar_at;
        if (now - bar_at >= 100u) {
            bar_at = now;
            ui.redraw = 1;
        }
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (now / 260u != last_blink) {      /* (the screen's muted keys don't blink: only the lights) */
        last_blink = now / 260u;
    }
#ifndef PLAT_HOST
    if (now - ui.boot_at < 2600u) {      /* SEED's splash, about 2.5 s */
        if (!splash_done) {
            splash_done = 1;
            draw_splash();
        }
        return;
    }
#endif
    if (ui.redraw) {
        ui.redraw = 0;
        if (ui.coach == CS_CHOOSE) {
            draw_coach_top();
            draw_coach_choice();
            return;
        }
        if (ui.coach)
            draw_coach_top();
        if (ui.screen == SCR_PICK) {
            if (!ui.coach)
                draw_pick_top();
            draw_pick_bottom();
        } else if (ui.screen == SCR_LIB)
            draw_lib();
        else if (ui.screen == SCR_FX)
            draw_fx();
        else if (ui.screen == SCR_MIDI)
            draw_midi();
        else {
            if (!ui.coach)
                draw_home_top();
            draw_home_bottom();
        }
    }
}

#ifdef PLAT_HOST
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a, (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "splash")) {
        draw_splash();
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
    else if (!strcmp(what, "type"))
        *got = set.type;
    else if (!strcmp(what, "plays"))
        *got = set.plays;
    else if (!strcmp(what, "bassm"))
        *got = set.bassm;
    else if (!strcmp(what, "latch"))
        *got = set.latch;
    else if (!strcmp(what, "coach"))
        *got = ui.coach;
    else if (!strcmp(what, "coachmode"))
        *got = set.coach;
    else if (!strcmp(what, "screen"))
        *got = ui.screen;
    else if (!strcmp(what, "picked"))
        *got = fd_n;
    else if (!strcmp(what, "midi_ch"))
        *got = ch_last;
    else if (!strncmp(what, "padnote", 7)) {   /* padnoteK: the last pad's K-th note */
        int k = atoi(what + 7);
        *got = ui.pad >= 0 && k < pads[ui.pad].n ? pads[ui.pad].note[k] : -1;
    } else if (!strncmp(what, "padn", 4))   /* padnN: pad N's notes (a chord) or phrase notes */
        *got = pads[atoi(what + 4) % NPADS].type == PAD_CHORD ? pads[atoi(what + 4) % NPADS].n
                                                               : pads[atoi(what + 4) % NPADS].pn; else if (!strncmp(what, "playing", 7)) {
        int k = atoi(what + 7) % (NPADS + 1);
        *got = pl[k].on || (k < (int)NPADS && cp[k].on);
    } else if (!strncmp(what, "lock", 4))
        *got = pads[atoi(what + 4) % NPADS].lock;
    else
        return 0;
    return 1;
}
#endif
