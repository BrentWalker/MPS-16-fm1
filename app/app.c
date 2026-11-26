/* SEED: MPS-16's chord builder, as firmware for the M-VAVE FM-1. Built on fm1-base.
 * Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence of this file: to be chosen (see README).
 *
 * As MPS-16 ver WEB's SEED, one job a button. The 16 white keys (A pads): 1-8 your key's chords, 9-16 progressions.
 * The first 7 black keys (B pads): the notes of the key, lit by the chord you played (the bass bright). The last 4
 * (C pads): BASS, NO BASS, NO ROOT, TAP. HOME (held: SHIFT), EDIT pick notes, SEL the library, ENV pad edit, LFO
 * LOCK, SYS setup (held: COACH), PLAY stop (SHIFT: clear the pads), ARP UNDO, SEQ LATCH, REC, SAVE (held:
 * LOAD), OCT- / OCT+ the keyboard's octave. FX: free. MIDI out over USB and Bluetooth on four fixed channels: pads
 * 1-8 on 1, pads 9-16 on 2, the B pads on 3, the keyboard on 4. MIDI clock out and in.
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. */
#include "fastmath.h"

/* ================================================================ settings === */
#define SET_MAGIC 0x33444553u            /* "SED3" */
enum { BM_BASS, BM_NOBASS, BM_NOROOT, BM_N };
enum { PL_CHORD, PL_STRUM, PL_ARP, PL_STABS, PL_N };
enum { CO_OFF, CO_FIRST, CO_ALWAYS, CO_N };
/* MIDI out, fixed: pads 1-8 on 1, pads 9-16 on 2, the B pads on 3, the keyboard (PICK NOTES, LIB) on 4 */
#define CH_PADS1 0u
#define CH_PADS2 1u
#define CH_BPADS 2u
#define CH_KEYS 3u
typedef struct {
    uint32_t magic;
    uint8_t root;                        /* 0 = C .. 11 = B */
    uint8_t scale;                       /* SCALES[] */
    int8_t octave;                       /* the keyboard's (B pads, PICK NOTES): -2 .. +2 */
    uint8_t type;                        /* the kit's chords: TRIADS, 7THS, 9THS, SUS2, SUS4, ADD9 */
    uint8_t plays, human;                /* the last "all pads" (home K3, K4): new pads start so */
    int8_t len;
    uint8_t bassm;                       /* BASS, NO BASS, NO ROOT: BM_* */
    uint8_t bpm;                         /* 40 .. 240 */
    uint8_t latch;                       /* LATCH: pads keep playing after you let go */
    uint8_t coach, coach_seen, coach_set;   /* COACH: CO_*, been through once, the choice made */
    uint8_t ble;                         /* Bluetooth MIDI on */
    uint8_t clk_out, clk_in;             /* MIDI clock: sent at the BPM; followed when it comes in */
    uint8_t rsv[3];
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
    set.human = 10;
    set.bassm = BM_BASS;
    set.bpm = 90;
    set.coach = CO_FIRST;
    set.ble = 1;
    set.clk_out = 1;
    set.clk_in = 1;
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
static const char *const PLAYS_NAME[PL_N] = {"CHORD", "STRUM", "ARP", "STABS"};
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
#define MAXPH 96u                        /* notes in a progression or a recorded pad */
#define PREVIEW NPADS                    /* the extra slot LIB plays its pick from */
enum { PAD_EMPTY, PAD_CHORD, PAD_PROG };
typedef struct {
    uint16_t s, d;                       /* start and length, 96ths of a beat */
    uint8_t p, v;
} pnote_t;
typedef struct {
    uint8_t type, n, deg, root_pc;
    uint8_t note[MAXN];                  /* a chord's notes, low to high */
    uint8_t bass_p, dim, pn, bm;         /* bass_p: the written bass (before NO BASS); bm: the bass mode built in */
    uint8_t bars, bar_deg[4];            /* a progression's bars and their degrees */
    uint16_t len;                        /* a progression's length, 96ths */
    char qual[10];
    char nm[MAXN][4];
    char roman[6];
    char title[16];
} pad_t;
/* where a pad came from: rebuilt from this when the key, type or bass mode changes (LOCK: never) */
enum { SRC_KIT, SRC_LIBC, SRC_LIBP, SRC_REC, SRC_EMPTY };
typedef struct {
    uint8_t src, a, t, lock;             /* a: the degree (a chord) or the progression; t: the chord type */
} desc_t;
/* PAD EDIT, each pad's own: how it plays, SPEED, HUMANIZE, LENGTH, INVERSION, VELOCITY, its octave */
typedef struct {
    uint8_t plays, human, speed, vel;    /* speed: SPEED_PCT[]; vel 0: as you play it, else that velocity */
    int8_t len, inv, oct, rsv;
} pfx_t;
static const uint16_t SPEED_PCT[7] = {50, 67, 75, 100, 133, 150, 200};
static PLAT_POOL pad_t pads[NPADS + 1];
static PLAT_POOL pnote_t ph[NPADS + 1][MAXPH];
static desc_t desc[NPADS];
static pfx_t pfx[NPADS + 1];

static void pfx_default(pfx_t *f)
{
    __builtin_memset(f, 0, sizeof *f);
    f->plays = set.plays;
    f->human = set.human;
    f->len = set.len;
    f->speed = 3;
}

static void name_chord(pad_t *p, uint32_t mask)
{
    uint32_t i;
    for (i = 0; i < NQUAL; i++)
        if (QUAL[i].mask == mask) {
            str_set(p->qual, QUAL[i].name, sizeof p->qual);
            return;
        }
    str_set(p->qual, (mask & 1u << 3) ? ((mask & 1u << 6) ? "dim+" : "m+") : "+", sizeof p->qual);
}
static uint32_t chord_of(uint32_t d, uint32_t t, int32_t *v)   /* degree d's chord of type t, low to high */
{
    uint32_t k, n = 0;
    for (k = 0; k < 5u && CTYPE[t][k] != 0xFF; k++)
        v[n++] = deg_pitch((int32_t)(d + CTYPE[t][k]));
    for (k = 1; k < n; k++)
        if (v[k] <= v[k - 1])
            v[k] += 12 * ((v[k - 1] - v[k]) / 12 + 1);
    return n;
}
/* NO BASS drops the bottom note, NO ROOT the root too (two notes stay at least) */
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

static void chord_into(pad_t *p, uint32_t d, uint32_t t)   /* degree d's chord of type t (d 7: an octave up) */
{
    int32_t v[6];
    uint32_t k, n, mask = 0, rp = deg_pc((int32_t)d);
    __builtin_memset(p, 0, sizeof *p);
    n = chord_of(d, t, v);
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
        for (j = 0; j < 5u && CTYPE[t][j] != 0xFF; j++)
            if (deg_pc((int32_t)(d + CTYPE[t][j])) == (uint32_t)(v[k] % 12))
                step = CTYPE[t][j];
        spell((uint32_t)v[k] % 12u, tonic_letter() + d + step, p->nm[k]);
    }
    name_chord(p, mask);
    roman_of(d % 7u, p->roman, &p->dim);
}
static void chord_label(uint32_t d, uint32_t t, char *o)   /* "Am7" */
{
    pad_t tmp;
    char r[4];
    chord_into(&tmp, d, t);
    spell(tmp.root_pc, tonic_letter() + d, r);
    str_set(o, r, 24);
    text_cat(o, tmp.qual);
}

/* progression r into slot dst: four bars, a chord a bar, played as the pad plays, the bass line by the bass mode */
static void prog_into(uint32_t dst, uint32_t r, uint32_t type, uint32_t plays)
{
    pad_t *p = &pads[dst];
    const uint8_t *seq = PROGS[r % NPROGS];
    uint32_t t = type == 2u ? 1u : type, bar, bl = PPQ * 4u, n, k;
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
        if (set.bassm == BM_NOROOT)
            n = chord_bm(v, n, rp, BM_NOROOT);
        nearest(v, n, prev);
        prev = 0.0f;
        for (k = 0; k < n; k++)
            prev += (float)v[k];
        prev /= (float)n;
        if (plays == PL_STABS) {         /* 1, the and of 2, 4; the bass on 1 and 3 */
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
        } else if (plays == PL_ARP) {    /* arp 8ths, the bass held */
            uint32_t j;
            for (j = 0; j < 8u; j++)
                ph_note(dst, v[j % n] + (j >= n ? 12 : 0), o + j * 48u, 43, j % 2u ? 82 : 100);
            if (set.bassm == BM_BASS)
                ph_note(dst, bass, o, bl - 10u, 100);
        } else {                         /* held a bar (STRUM: a 16th apart, low to high) */
            for (k = 0; k < n; k++) {
                uint32_t sk = plays == PL_STRUM ? k * 12u : 0u;
                ph_note(dst, v[k], o + sk, bl - 10u - sk, 96);
            }
            if (set.bassm == BM_BASS)
                ph_note(dst, bass, o, bl - 10u, 104);
        }
        roman_of(d, rn, &dim);
        if (bar)
            text_cat_n(p->title, " ", sizeof p->title);
        text_cat_n(p->title, rn, sizeof p->title);
    }
}

/* pad i made again from where it came from (a LOCKed or recorded pad stays as it is) */
static void pad_make(uint32_t i)
{
    desc_t *d = &desc[i];
    if (d->lock || d->src == SRC_REC)
        return;
    if (d->src == SRC_EMPTY)
        __builtin_memset(&pads[i], 0, sizeof pads[i]);
    else if (d->src == SRC_KIT)
        i < 8u ? chord_into(&pads[i], i, set.type) : prog_into(i, i - 8u, set.type, pfx[i].plays);
    else if (d->src == SRC_LIBC)
        chord_into(&pads[i], d->a, d->t);
    else
        prog_into(i, d->a, d->t, pfx[i].plays);
}
static void kit_build(void)
{
    uint32_t i;
    for (i = 0; i < NPADS; i++)
        pad_make(i);
}
static void kit_fresh(void)              /* every pad the kit's, settings as the last "all pads" */
{
    uint32_t i;
    for (i = 0; i < NPADS; i++) {
        desc[i].src = SRC_KIT;
        desc[i].a = (uint8_t)i;
        desc[i].t = set.type;
        desc[i].lock = 0;
        pfx_default(&pfx[i]);
    }
    pfx_default(&pfx[PREVIEW]);
    kit_build();
}

/* ================================================================== synth === */
#define NVOICES 12u
#define SR 44100.0f
typedef struct {
    int8_t note;
    uint8_t gate;
    uint8_t stage;
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
static volatile uint32_t clock_smp;      /* the audio clock, in samples */

/* MIDI out, over USB and Bluetooth */
static uint32_t midi_to(void) { return TO_USB | (set.ble ? TO_BLE : 0u); }
static uint8_t ch_last;
static void midi_note(uint32_t on, uint32_t ch, uint32_t m, uint32_t v)
{
    ch_last = (uint8_t)ch;
    plat_midi_send((on ? 0x09u | (0x90u | ch) << 8 | m << 16 | v << 24 : 0x08u | (0x80u | ch) << 8 | m << 16),
                   midi_to());
}
static void out_on(uint32_t m, uint32_t v, uint32_t ch)
{
    voice_on(m, v);
    midi_note(1, ch, m, v);
}
static void out_off(uint32_t m, uint32_t ch)
{
    voice_off(m);
    midi_note(0, ch, m, 0);
}
static uint32_t pad_ch(uint32_t i)       /* pads 1-8 on 1, 9-16 on 2; LIB's preview (i = NPADS) on the keyboard's */
{
    return i >= NPADS ? CH_KEYS : i < 8u ? CH_PADS1 : CH_PADS2;
}

/* REC: what you play, from the main loop and from MIDI in, with the audio clock */
#define RECMAX 256u
typedef struct {
    uint32_t t;
    uint8_t on, p, v, pad;
} recev_t;
static PLAT_POOL recev_t rec_ev[RECMAX];
static volatile uint32_t rec_n;
static volatile uint8_t rec_on;
static void rec_push(uint32_t on, uint32_t p, uint32_t v)
{
    uint32_t n = rec_n;
    if (!rec_on || n >= RECMAX)
        return;
    rec_ev[n].t = clock_smp;
    rec_ev[n].on = (uint8_t)on;
    rec_ev[n].p = (uint8_t)p;
    rec_ev[n].v = (uint8_t)v;
    BARRIER();
    rec_n = n + 1u;
}

/* MIDI clock: in (the BPM follows it), out (24 a beat, at the BPM) */
static volatile uint32_t clk_last, clk_avg, clk_seen;
static void midi_in(void)                /* audio interrupt: MIDI in plays the synth and is recorded */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24, hi = st & 0xF0u;
        if (st == 0xF8u) {
            if (set.clk_in) {
                uint32_t d = clock_smp - clk_last;
                if (clk_last && d > 20u && d < 22050u)
                    clk_avg = clk_avg ? (clk_avg * 7u + d * 16u) / 8u : d * 16u;   /* 16ths of a sample, smoothed */
                clk_last = clock_smp;
                clk_seen = clock_smp ? clock_smp : 1u;
            }
            continue;
        }
        if (hi == 0x90u && d2) {
            voice_on(d1, d2);
            rec_push(1, d1, d2);
        } else if (hi == 0x80u || hi == 0x90u) {
            voice_off(d1);
            rec_push(0, d1, 0);
        }
    }
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
static uint32_t human_vel(uint32_t v, uint32_t h)   /* humanize: velocity off by a little, like a player */
{
    int32_t r = (int32_t)(cs_rnd() % 201u) - 100;
    return (uint32_t)clamp((int32_t)v + (int32_t)v * (int32_t)h * r / 25000, 1, 127);
}

static volatile uint8_t pad_held[NPADS + 1], pad_latched[NPADS + 1];

/* progression players: a tap plays it once; held past the end it loops; latched it loops till tapped again */
typedef struct {
    uint8_t on, looped, vel, ch;
    int8_t oct;
    float pos;
    uint8_t snd[MAXPH];
    uint16_t endt[MAXPH];
} player_t;
static PLAT_POOL player_t pl[NPADS + 1];
static void pl_release(uint32_t i)
{
    uint32_t k;
    for (k = 0; k < MAXPH; k++)
        if (pl[i].snd[k]) {
            out_off(pl[i].snd[k] - 1u, pl[i].ch);
            pl[i].snd[k] = 0;
        }
}
static void phrase_tick(uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i <= NPADS; i++) {
        player_t *P = &pl[i];
        const pad_t *pd = &pads[i];
        const pfx_t *f = &pfx[i];
        float a, b, we, dt;
        if (!P->on)
            continue;
        dt = (float)n * (float)tempo * 96.0f / (60.0f * SR) * (float)SPEED_PCT[f->speed % 7u] / 100.0f;
        we = (float)pd->len;
        a = P->pos;
        b = a + dt;
        for (k = 0; k < pd->pn && k < MAXPH; k++) {
            const pnote_t *x = &ph[i][k];
            float s = (float)x->s, e = fm_minf(P->snd[k] ? (float)P->endt[k] : s + (float)x->d, we);
            int starts = s >= a && s < b && s < we;
            if (starts) {
                uint32_t m = (uint32_t)clamp((int32_t)x->p + 12 * P->oct, 0, 127);
                uint32_t v = (uint32_t)x->v * P->vel / 100u, d = x->d, li = (uint32_t)(f->len + 5);
                if (f->vel)
                    v = (uint32_t)x->v * f->vel / 100u;
                if (f->human)
                    v = human_vel(v ? v : 1u, f->human);
                if (P->snd[k])
                    out_off(P->snd[k] - 1u, P->ch);
                out_on(m, clamp((int32_t)v, 1, 127), P->ch);
                P->snd[k] = (uint8_t)(m + 1u);
                if (f->len && li < 11u) {
                    if (LEN_PCT[li])
                        d = d * LEN_PCT[li] / 100u;
                    else {
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
                out_off(P->snd[k] - 1u, P->ch);
                P->snd[k] = 0;
            }
        }
        P->pos = b;
        if (P->pos >= we) {
            pl_release(i);
            if (i < NPADS && (pad_held[i] || pad_latched[i]) && we > 0.0f) {
                P->pos -= we;
                P->looped = 1;
            } else
                P->on = 0;
        }
    }
}

/* chord players: CHORD (held), STRUM (low to high), ARP (in turn), STABS (1, the and of 2, 4, each bar) */
#define MAXC 8u
typedef struct {
    uint8_t on, n, cur, mode, ch, human;
    uint8_t note[MAXC], vel[MAXC], sounded[MAXC];
    uint32_t at[MAXC];
    uint32_t t, next, idx, off, gate;
    float spd;
    int8_t len;
} cplay_t;
static cplay_t cp[NPADS];
static uint8_t cbuf[NPADS][MAXC], cbuf_n[NPADS];
#define STAB_PAT 0x1041u
static void cp_stop(uint32_t i)
{
    cplay_t *c = &cp[i];
    uint32_t k;
    if (c->cur) {
        out_off(c->cur - 1u, c->ch);
        c->cur = 0;
    }
    for (k = 0; k < c->n; k++)
        if (c->sounded[k]) {
            out_off(c->note[k], c->ch);
            c->sounded[k] = 0;
        }
    c->on = 0;
}
static void cp_start(uint32_t i, uint32_t vel)
{
    cplay_t *c = &cp[i];
    const pfx_t *f = &pfx[i];
    uint32_t k, ss;
    cp_stop(i);
    c->spd = (float)SPEED_PCT[f->speed % 7u] / 100.0f;
    ss = f->plays == PL_STRUM ? (uint32_t)(0.035f * SR / c->spd) : 0u;   /* STRUM: 35 ms a note at 100% */
    c->n = cbuf_n[i] < MAXC ? cbuf_n[i] : (uint8_t)MAXC;
    c->mode = f->plays;
    c->ch = (uint8_t)pad_ch(i);
    c->human = f->human;
    c->len = f->len;
    if (f->vel)
        vel = f->vel;
    for (k = 0; k < c->n; k++) {
        c->note[k] = cbuf[i][k];
        c->vel[k] = (uint8_t)(f->human ? human_vel(vel, f->human) : vel);
        c->sounded[k] = 0;
        c->at[k] = k * ss + (f->human ? cs_rnd() % (f->human * 13u + 1u) : 0u);
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
            uint32_t step = (uint32_t)(15.0f * SR / (float)tempo / c->spd);
            if (!step)
                step = 1u;
            if (c->off && c->off < c->t + n) {
                for (k = 0; k < c->n; k++)
                    if (c->sounded[k]) {
                        out_off(c->note[k], c->ch);
                        c->sounded[k] = 0;
                    }
                c->off = 0;
            }
            while (c->next < c->t + n) {
                if (STAB_PAT >> (c->idx % 16u) & 1u) {
                    uint32_t li = (uint32_t)(c->len + 5), g = step * 3u / 2u;
                    for (k = 0; k < c->n; k++) {
                        if (c->sounded[k])
                            out_off(c->note[k], c->ch);
                        out_on(c->note[k], c->human ? human_vel(c->vel[0], c->human) : c->vel[0], c->ch);
                        c->sounded[k] = 1;
                    }
                    if (li < 11u)
                        g = LEN_PCT[li] ? g * LEN_PCT[li] / 100u : step * 5u;
                    c->off = c->next + (g ? g : 1u);
                }
                c->idx++;
                c->next += step;
            }
        } else if (c->mode == PL_ARP && c->n) {
            while (c->next < c->t + n) {
                uint32_t step = (uint32_t)(30.0f * SR / (float)tempo / c->spd);   /* 8ths at 100% */
                if (c->cur)
                    out_off(c->cur - 1u, c->ch);
                out_on(c->note[c->idx % c->n], c->human ? human_vel(c->vel[0], c->human) : c->vel[0], c->ch);
                c->cur = (uint8_t)(c->note[c->idx % c->n] + 1u);
                c->idx++;
                c->next += step ? step : 1u;
            }
        } else {
            for (k = 0; k < c->n; k++)
                if (!c->sounded[k] && c->at[k] < c->t + n) {
                    out_on(c->note[k], c->vel[k], c->ch);
                    c->sounded[k] = 1;
                }
        }
        c->t += n;
    }
}

static uint32_t clk_acc;                 /* MIDI clock out: samples toward the next tick, in 24ths of a beat x 16 */
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
            pl[a].oct = (int8_t)pfx[a].oct;
            pl[a].ch = (uint8_t)pad_ch(a);
            pl[a].vel = (uint8_t)(b ? b * 100u / 127u + 20u : 100u);
            pl[a].pos = 0.0f;
        }
        q_r++;
    }
    phrase_tick(n);
    chord_tick(n);
    midi_in();
    if (set.clk_out) {                   /* 24 a beat */
        uint32_t per = (uint32_t)(SR * 60.0f * 16.0f / (24.0f * (float)tempo));
        clk_acc += n * 16u;
        while (clk_acc >= per) {
            clk_acc -= per;
            plat_midi_send(0x0Fu | 0xF8u << 8, midi_to());
        }
    }
    clock_smp += n;
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
    h->ch = (uint8_t)ch;
    for (i = 0; i < n && i < MAXN; i++) {
        int32_t m = clamp((int32_t)notes[i] + 12 * set.octave, 0, 127);
        h->note[h->n++] = (uint8_t)m;
        ev_push(QE_ON, (uint32_t)m, vel);
        midi_note(1, ch, (uint32_t)m, vel);
        rec_push(1, (uint32_t)m, vel);
    }
}
static void notes_off(uint32_t k)
{
    uint32_t i;
    held_t *h = &held[k];
    for (i = 0; i < h->n; i++) {
        ev_push(QE_OFF, h->note[i], 0);
        midi_note(0, h->ch, h->note[i], 0);
        rec_push(0, h->note[i], 0);
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
static void all_notes_off(void)          /* PLAY: everything off, and every channel told so */
{
    uint32_t c;
    players_stop();
    for (c = 0; c < 16u; c++)
        plat_midi_send(0x0Bu | (0xB0u | c) << 8 | 123u << 16, midi_to());
}
/* a pad's sound: a chord (its inversion and octave) into its player, a progression started */
static void pad_start(uint32_t i, uint32_t vel)
{
    const pad_t *p = &pads[i];
    const pfx_t *f = &pfx[i];
    if (p->type == PAD_CHORD) {
        int32_t v[MAXC];
        uint32_t k, n = p->n < MAXC ? p->n : MAXC;
        int32_t inv = f->inv;
        for (k = 0; k < n; k++)
            v[k] = p->note[k];
        while (inv > 0 && n > 1u) {      /* up: the bottom note an octave up */
            int32_t lo = v[0] + 12;
            for (k = 0; k + 1u < n; k++)
                v[k] = v[k + 1u];
            v[n - 1u] = lo;
            inv--;
        }
        while (inv < 0 && n > 1u) {      /* down: the top note an octave down */
            int32_t hi = v[n - 1u] - 12;
            for (k = n - 1u; k > 0; k--)
                v[k] = v[k - 1u];
            v[0] = hi;
            inv++;
        }
        for (k = 0; k < n; k++) {
            cbuf[i][k] = (uint8_t)clamp(v[k] + 12 * f->oct, 0, 127);
            rec_push(1, cbuf[i][k], f->vel ? f->vel : vel);
        }
        cbuf_n[i] = (uint8_t)n;
        ev_push(QE_CSTART, i, vel);
    } else if (p->type == PAD_PROG)
        ev_push(QE_PSTART, i, vel);
}
static void pad_release_rec(uint32_t i)   /* REC: a chord pad let go */
{
    uint32_t k;
    if (pads[i].type == PAD_CHORD)
        for (k = 0; k < cbuf_n[i]; k++)
            rec_push(0, cbuf[i][k], 0);
}

/* ======================================================================= UI === */
enum { SCR_HOME, SCR_PICK, SCR_LIB, SCR_PAD, SCR_SETUP };
enum { CS_NONE, CS_CHOOSE, CS_NOTES, CS_SCALE, CS_PADS, CS_BASS, CS_SHAPE, CS_CPADS, CS_DONE };
enum { RS_OFF, RS_PICK, RS_COUNT, RS_REC };
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    int32_t pad;                         /* the A pad played last (-1 none yet) */
    uint8_t screen, redraw;
    uint32_t sys_at, home_at, save_at;   /* SYS / HOME / SAVE went down then (0 up, all ones: held) */
    uint8_t shift_used;                  /* HOME held and used as SHIFT: its release goes nowhere */
    uint8_t coach, co_count, co_pick;
    uint32_t co_at, boot_at;
    uint8_t rs, rpad;                    /* REC: the state, the pad it records into */
    uint32_t r_t0, r_click;              /* REC: the take's start (samples), the next click (samples) */
    uint32_t tap[4];                     /* TAP: the last taps */
    uint8_t tap_n;
    uint8_t lib_t, lib_sel;              /* LIB: the type shown (NCTYPES: the progressions), the pick */
} ui = {.pad = -1, .lib_t = 1};
static uint32_t btn_now;                 /* the buttons held this frame */
static int shift(void) { return (int)(btn_now >> B_HOME & 1u); }   /* HOME held: SHIFT */

#define FD_MAX 12u
static uint8_t fd_seq[FD_MAX], fd_n, fd_list[12u * NSCALES], fd_cnt, fd_sel;
static uint16_t fd_pcs;

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

/* ----------------------------------------------------------------- UNDO --- */
static PLAT_POOL struct {
    uint8_t have;
    pad_t pad[NPADS];
    pnote_t uph[NPADS][MAXPH];
    desc_t d[NPADS];
    pfx_t f[NPADS];
} U;
static void undo_snap(void)
{
    __builtin_memcpy(U.pad, pads, sizeof U.pad);
    __builtin_memcpy(U.uph, ph, sizeof U.uph);
    __builtin_memcpy(U.d, desc, sizeof U.d);
    __builtin_memcpy(U.f, pfx, sizeof U.f);
    U.have = 1;
}
static void undo_swap(void)              /* one step back; again: forward */
{
    uint32_t i, k;
    if (!U.have) {
        app_say("UNDO", "NOTHING TO UNDO");
        return;
    }
    players_stop();
    for (i = 0; i < NPADS; i++) {
        pad_t tp = pads[i];
        desc_t td = desc[i];
        pfx_t tf = pfx[i];
        pads[i] = U.pad[i];
        U.pad[i] = tp;
        desc[i] = U.d[i];
        U.d[i] = td;
        pfx[i] = U.f[i];
        U.f[i] = tf;
        for (k = 0; k < MAXPH; k++) {
            pnote_t t = ph[i][k];
            ph[i][k] = U.uph[i][k];
            U.uph[i][k] = t;
        }
    }
    app_say("UNDO", "AGAIN: REDO");
}

/* ------------------------------------------------------ SAVE and LOAD --- */
/* the session: the settings; every pad's source, its PAD EDIT and (LOCKed or recorded) its notes */
#define SESS_MAGIC 0x31534553u           /* "SES1" */
typedef struct {
    uint32_t magic;
    desc_t d[NPADS];
    pfx_t f[NPADS];
    uint16_t kept;                       /* bit i: pad i's notes are in the record (LOCKed or recorded) */
    uint16_t rsv;
    pad_t p[NPADS];                      /* (only the kept ones matter) */
} sess_t;
static PLAT_POOL sess_t SS;
#define NOTES_MAX (PLAT_STORE_MAX / sizeof(pnote_t))
static PLAT_POOL pnote_t note_buf[NOTES_MAX];
static int session_save(void)
{
    uint32_t i, k, nn = 0;
    __builtin_memset(&SS, 0, sizeof SS);
    SS.magic = SESS_MAGIC;
    for (i = 0; i < NPADS; i++) {
        SS.d[i] = desc[i];
        SS.f[i] = pfx[i];
        if (desc[i].lock || desc[i].src == SRC_REC) {
            SS.kept |= (uint16_t)(1u << i);
            SS.p[i] = pads[i];
            for (k = 0; k < pads[i].pn && pads[i].type == PAD_PROG; k++) {
                if (nn >= NOTES_MAX)
                    return -2;
                note_buf[nn++] = ph[i][k];
            }
        }
    }
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) || plat_store_save(OBJ_SESS, &SS, sizeof SS) ||
        plat_store_save(OBJ_NOTES, note_buf, nn ? nn * sizeof(pnote_t) : sizeof(pnote_t)))
        return -1;
    set_dirty = 0;
    return 0;
}
static int session_load(void)
{
    uint32_t i, k, nn = 0;
    int got = plat_store_load(OBJ_SESS, &SS, sizeof SS), gn;
    if (got != (int)sizeof SS || SS.magic != SESS_MAGIC)
        return -1;
    gn = plat_store_load(OBJ_NOTES, note_buf, sizeof note_buf);
    if (gn < 0)
        gn = 0;
    for (i = 0; i < NPADS; i++)
        if (SS.d[i].src > SRC_EMPTY || SS.d[i].t >= NCTYPES || SS.f[i].plays >= PL_N || SS.f[i].speed > 6u)
            return -1;
    for (i = 0; i < NPADS; i++) {
        desc[i] = SS.d[i];
        pfx[i] = SS.f[i];
        if (SS.kept >> i & 1u) {
            pads[i] = SS.p[i];
            if (pads[i].type == PAD_PROG)
                for (k = 0; k < pads[i].pn && k < MAXPH; k++)
                    ph[i][k] = nn < (uint32_t)gn / sizeof(pnote_t) ? note_buf[nn++] : (pnote_t){0, 1, 60, 1};
        }
    }
    kit_build();
    return 0;
}

/* ------------------------------------------------------------ PICK NOTES --- */
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
static void fd_toggle(uint32_t pc)
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
static void rekey(void)                  /* the key, type or bass mode changed: the pads made again */
{
    uint32_t i;
    for (i = 0; i < NPADS; i++)
        if (!desc[i].lock && desc[i].src != SRC_REC && (pl[i].on || cp[i].on))
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
    uint16_t pcs;
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
        for (k = 0; k < p->n; k++) {
            c.pcs |= (uint16_t)(1u << (p->note[k] % 12u));
            c.root_on |= p->note[k] % 12u == p->root_pc;
        }
        c.pcs |= (uint16_t)(1u << c.bass);
        if (c.bass == c.root && c.bass_on)
            c.root_on = 1;
    } else if (p->type == PAD_PROG && p->bars) {
        uint32_t bar = pl[ui.pad].on ? (uint32_t)(pl[ui.pad].pos / (float)(PPQ * 4u)) : 0u, d, k, n;
        int32_t v[6];
        uint32_t t = desc[ui.pad].src == SRC_LIBP ? desc[ui.pad].t : set.type;
        d = p->bar_deg[bar < p->bars ? bar : 0u];
        n = chord_of(d, t == 2u ? 1u : t, v);
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
    if (step >= CS_PADS)
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
                                    "THE LAST 4 BLACK KEYS: BASS, NO BASS, NO ROOT, TAP. TRY ONE.",
                                    "THAT'S SEED. HOLD SYS ANY TIME FOR THESE STEPS."};
    return T[s % 9u];
}

/* ------------------------------------------------------------------ REC --- */
static uint32_t bpb_smp(void) { return (uint32_t)(SR * 60.0f / (float)tempo); }   /* a beat, in samples */
static void rec_arm(void)
{
    players_stop();
    ui.rs = RS_PICK;
    app_say("REC", "TAP A PAD TO RECORD INTO");
}
static void rec_pick(uint32_t i)         /* the pad: a bar's count-in, then recording */
{
    if (desc[i].lock) {
        app_say("LOCKED", "PICK ANOTHER PAD");
        return;
    }
    ui.rpad = (uint8_t)i;
    ui.rs = RS_COUNT;
    ui.r_t0 = clock_smp + 4u * bpb_smp() + 2000u;
    ui.r_click = clock_smp + 2000u;
    rec_n = 0;
    rec_on = 1;
    app_say("REC", "COUNT IN");
}
static void rec_stop(void)               /* the take onto the pad: rounded to whole bars */
{
    uint32_t now = clock_smp, i, j, n = rec_n, bar = 4u * bpb_smp(), bars, len;
    pad_t *p;
    rec_on = 0;
    if (ui.rs != RS_REC) {
        ui.rs = RS_OFF;
        app_say("REC", "CANCELLED");
        return;
    }
    ui.rs = RS_OFF;
    bars = (now - ui.r_t0 + bar / 2u) / bar;
    if (bars < 1u)
        bars = 1u;
    if (bars > 8u)
        bars = 8u;
    len = bars * 4u * PPQ;
    undo_snap();
    p = &pads[ui.rpad];
    __builtin_memset(p, 0, sizeof *p);
    p->type = PAD_PROG;
    p->len = (uint16_t)len;
    str_set(p->title, "RECORDED", sizeof p->title);
    for (i = 0; i < n && p->pn < MAXPH; i++) {
        const recev_t *e = &rec_ev[i];
        uint32_t s, d = 0;
        int32_t dt;
        if (!e->on)
            continue;
        dt = (int32_t)(e->t - ui.r_t0);
        if (dt < -(int32_t)(bpb_smp() / 4u))
            continue;                    /* well before the 1 */
        s = dt < 0 ? 0u : (uint32_t)((float)dt / (float)bpb_smp() * (float)PPQ);
        if (s >= len)
            continue;
        for (j = i + 1u; j < n; j++)     /* its note off */
            if (!rec_ev[j].on && rec_ev[j].p == e->p) {
                d = (uint32_t)((float)(rec_ev[j].t - e->t) / (float)bpb_smp() * (float)PPQ);
                break;
            }
        if (j == n)
            d = (uint32_t)((float)(now - e->t) / (float)bpb_smp() * (float)PPQ);
        if (d < 4u)
            d = 4u;
        if (s + d > len)
            d = len - s;
        ph[ui.rpad][p->pn].s = (uint16_t)s;
        ph[ui.rpad][p->pn].d = (uint16_t)d;
        ph[ui.rpad][p->pn].p = e->p;
        ph[ui.rpad][p->pn].v = e->v;
        p->pn++;
    }
    if (!p->pn) {
        undo_swap();
        app_say("REC", "NOTHING WAS PLAYED");
        return;
    }
    desc[ui.rpad].src = SRC_REC;
    pfx_default(&pfx[ui.rpad]);
    pfx[ui.rpad].plays = PL_CHORD;
    pfx[ui.rpad].len = 0;
    pfx[ui.rpad].human = 0;
    ui.pad = ui.rpad;
    {
        char t[24] = "PAD ", b[16];
        num_text(ui.rpad + 1, t + 4, 0);
        num_text((int32_t)bars, b, 0);
        text_cat(b, bars == 1u ? " BAR RECORDED" : " BARS RECORDED");
        app_say(t, b);
    }
}
static void rec_tick(void)               /* the count-in's clicks, then the take starts */
{
    if (ui.rs == RS_COUNT) {
        uint32_t now = clock_smp;
        if ((int32_t)(now - ui.r_click) >= 0 && (int32_t)(ui.r_t0 - now) > 0) {
            uint32_t first = (ui.r_t0 - ui.r_click) / bpb_smp() == 3u;
            ev_push(QE_ON, first ? 96u : 84u, 90);
            ev_push(QE_OFF, first ? 96u : 84u, 0);
            ui.r_click += bpb_smp();
        }
        if ((int32_t)(now - ui.r_t0) >= 0) {
            ui.rs = RS_REC;
            app_say("REC", "PLAYING IN: REC AGAIN STOPS");
        }
    }
}

/* ================================================================== input === */
static void all_pads(void (*fn)(uint32_t))   /* home's "all pads" knobs */
{
    uint32_t i;
    for (i = 0; i < NPADS; i++)
        if (desc[i].src != SRC_REC)
            fn(i);
}
static int32_t all_d;
static void ap_plays(uint32_t i) { pfx[i].plays = set.plays; (void)all_d; }
static void ap_human(uint32_t i) { pfx[i].human = set.human; }
static void ap_len(uint32_t i) { pfx[i].len = set.len; }
static void ap_oct(uint32_t i) { pfx[i].oct = (int8_t)clamp(pfx[i].oct + all_d, -2, 2); }

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), i, now = plat_ms();
    btn_now = btn;
    touch_poll();
    keys = touch_keys();

    if (ui.coach == CS_DONE && ((keys & ~ui.keys) || now - ui.co_at > 4000u))
        coach_end();
    rec_tick();
    if ((btn >> B_HOME & 1u) && ((keys & ~ui.keys) || (btn & ~ui.btn & ~(1u << B_HOME))))
        ui.shift_used = 1;               /* HOME held with something else: SHIFT */

    if (ui.coach == CS_CHOOSE)
        goto buttons;
    if (ui.screen == SCR_PICK) {         /* PICK NOTES: every key a note, from F3; played again: out */
        uint32_t k;
        for (k = 0; k < NKEYS; k++) {
            uint32_t bit = 1u << k;
            if ((keys & bit) && !(ui.keys & bit)) {
                uint8_t m = (uint8_t)clamp(53 + (int32_t)k, 0, 127);
                uint32_t vel = touch_vel(k);
                notes_on(k, &m, 1, vel ? vel : 100u, CH_KEYS);
                fd_toggle(m % 12u);
                coach_event(CE_NOTE);
                ui.redraw = 1;
            } else if (!(keys & bit) && (ui.keys & bit))
                notes_off(k);
        }
        goto buttons;
    }
    /* the A pads */
    for (i = 0; i < 16u; i++) {
        uint32_t k = WK[i], bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t vel = touch_vel(k);
            if (ui.rs == RS_PICK) {      /* REC: the pad to record into */
                rec_pick(i);
                ui.redraw = 1;
                continue;
            }
            if (ui.screen == SCR_LIB) {  /* LIB: the pick goes onto the pad */
                char t[24];
                if (desc[i].lock) {
                    app_say("LOCKED", "LFO UNLOCKS IT");
                    continue;
                }
                undo_snap();
                pad_stop(i);
                desc[i].src = ui.lib_t < NCTYPES ? SRC_LIBC : SRC_LIBP;
                desc[i].a = ui.lib_sel;
                desc[i].t = ui.lib_t < NCTYPES ? ui.lib_t : set.type;
                pad_make(i);
                str_set(t, "PAD ", sizeof t);
                num_text((int32_t)i + 1, t + 4, 0);
                app_say(t, ui.lib_t < NCTYPES ? "A CHORD FROM LIB" : "A PROGRESSION FROM LIB");
            }
            if (pad_latched[i] && (pl[i].on || cp[i].on)) {
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
                pad_release_rec(i);
                if (pads[i].type == PAD_CHORD)
                    ev_push(QE_CSTOP, i, 0);
                else if (pl[i].looped)
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
            notes_on(k, &m, 1, vel ? vel : 100u, CH_BPADS);
            coach_event(CE_BPAD);
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit))
            notes_off(k);
    }
    /* the C pads: BASS, NO BASS, NO ROOT, TAP */
    for (i = 0; i < 4u; i++) {
        uint32_t bit = 1u << BK[7u + i];
        if ((keys & bit) && !(ui.keys & bit)) {
            if (i < 3u) {
                set.bassm = (uint8_t)i;
                rekey();
                app_say("TYPE", BM_NAME[i]);
            } else {                     /* TAP: the tempo from the last taps */
                if (ui.tap_n && now - ui.tap[(ui.tap_n - 1u) % 4u] > 2000u)
                    ui.tap_n = 0;
                ui.tap[ui.tap_n % 4u] = now;
                ui.tap_n++;
                if (ui.tap_n >= 2u) {
                    uint32_t n = ui.tap_n < 4u ? ui.tap_n : 4u, first = ui.tap[(ui.tap_n - n) % 4u];
                    uint32_t avg = (now - first) / (n - 1u);
                    if (avg) {
                        char b[8];
                        set.bpm = (uint8_t)clamp((int32_t)((60000u + avg / 2u) / avg), 40, 240);
                        tempo = set.bpm;
                        set_dirty = 1;
                        num_text(set.bpm, b, 0);
                        app_say("TAP", b);
                    }
                } else
                    app_say("TAP", "KEEP TAPPING");
            }
            coach_event(CE_CPAD);
            ui.redraw = 1;
        }
    }
buttons:
    for (i = 0; i < NB; i++) {
        uint32_t bit = 1u << i;
        int down = (btn & bit) && !(ui.btn & bit), up = !(btn & bit) && (ui.btn & bit);
        if (i == B_OCTDN || i == B_OCTUP) {   /* the keyboard's octave */
            if (down) {
                int32_t o = clamp(set.octave + (i == B_OCTUP ? 1 : -1), -2, 2);
                char t[16] = "KEYS ";
                if (o != set.octave) {
                    set.octave = (int8_t)o;
                    set_dirty = 1;
                }
                num_text(o, t + 5, 1);
                app_say("OCTAVE", t);
            }
            continue;
        }
        if (i == B_HOME) {               /* HOME: a tap home; held, SHIFT */
            if (down) {
                ui.home_at = now ? now : 1u;
                ui.shift_used = 0;
            }
            if (up) {
                if (!ui.shift_used) {
                    if (ui.coach == CS_CHOOSE) {
                        ui.coach = CS_NONE;
                        set.coach_set = 1;
                        set_dirty = 1;
                    } else if (ui.coach) {
                        coach_end();
                        app_say("STEPS", "HOLD SYS FOR THEM AGAIN");
                    }
                    if (ui.rs != RS_OFF && ui.rs != RS_REC) {
                        ui.rs = RS_OFF;
                        rec_on = 0;
                    }
                    ui.screen = SCR_HOME;
                }
                ui.home_at = 0;
            }
            ui.redraw = 1;
            continue;
        }
        if (i == B_SAVE) {               /* SAVE: a tap saves; held, LOAD (below) */
            if (down)
                ui.save_at = now ? now : 1u;
            if (up) {
                if (ui.save_at && ui.save_at != 0xFFFFFFFFu) {
                    int r = session_save();
                    app_say(r ? "SAVE" : "SAVED", r == -2 ? "TOO MANY NOTES" : r ? "FAILED" : "THE PADS AND SETTINGS");
                }
                ui.save_at = 0;
            }
            continue;
        }
        if (!down)
            continue;
        if (ui.coach == CS_CHOOSE && i != B_PLAY)
            continue;
        if (i == B_PLAY) {
            if (ui.coach == CS_CHOOSE) {
                set.coach = ui.co_pick;
                set.coach_set = 1;
                plat_store_save(OBJ_SETTINGS, &set, sizeof set);
                if (set.coach == CO_OFF) {
                    ui.coach = CS_NONE;
                    app_say("STEPS OFF", "HOLD SYS FOR THEM");
                } else
                    coach_start();
            } else if (shift()) {        /* SHIFT + PLAY: every pad cleared (LOCKed ones stay) */
                uint32_t q;
                undo_snap();
                players_stop();
                for (q = 0; q < NPADS; q++)
                    if (!desc[q].lock) {
                        desc[q].src = SRC_EMPTY;
                        __builtin_memset(&pads[q], 0, sizeof pads[q]);
                    }
                ui.pad = -1;
                app_say("PADS CLEARED", "ARP UNDOES IT. PRESET REFILLS");
            } else if (ui.screen == SCR_PICK) {
                pick_use();
                coach_event(CE_KEYSET);
            } else if (ui.screen == SCR_LIB) {   /* hear the pick */
                pad_stop(PREVIEW);
                if (ui.lib_t < NCTYPES) {
                    uint32_t k;
                    pad_t *p = &pads[PREVIEW];
                    chord_into(p, ui.lib_sel, ui.lib_t);
                    p->type = PAD_PROG;
                    p->pn = 0;
                    p->len = (uint16_t)(PPQ * 2u);
                    for (k = 0; k < p->n; k++)
                        ph_note(PREVIEW, p->note[k], 0, PPQ * 2u - 10u, 100);
                } else
                    prog_into(PREVIEW, ui.lib_sel, set.type, set.plays);
                ev_push(QE_PSTART, PREVIEW, 100);
            } else {
                all_notes_off();
                app_say("STOP", "ALL NOTES OFF");
            }
        } else if (i == B_EDIT) {
            if (ui.screen == SCR_PICK)
                ui.screen = SCR_HOME;
            else {
                pick_enter();
                app_say("PICK NOTES", "PLAY A FEW YOU LIKE");
            }
        } else if (i == B_SEL) {         /* LIB */
            ui.screen = ui.screen == SCR_LIB ? SCR_HOME : SCR_LIB;
        } else if (i == B_ENV) {         /* PAD EDIT: the pad played last */
            if (ui.screen == SCR_PAD)
                ui.screen = SCR_HOME;
            else if (ui.pad < 0 || pads[ui.pad].type == PAD_EMPTY)
                app_say("PAD EDIT", "PLAY A PAD FIRST");
            else
                ui.screen = SCR_PAD;
        } else if (i == B_LFO) {         /* LOCK the pad played last */
            if (ui.pad < 0 || pads[ui.pad].type == PAD_EMPTY)
                app_say("LOCK", "PLAY A PAD FIRST");
            else {
                char t[24] = "PAD ";
                desc[ui.pad].lock = (uint8_t)!desc[ui.pad].lock;
                if (!desc[ui.pad].lock)
                    pad_make((uint32_t)ui.pad);
                num_text(ui.pad + 1, t + 4, 0);
                app_say(t, desc[ui.pad].lock ? "LOCKED: KEPT AS IT IS" : "UNLOCKED");
                set_dirty = 1;
            }
        } else if (i == B_ARP) {         /* UNDO */
            undo_swap();
        } else if (i == B_SEQ) {         /* LATCH */
            set.latch = (uint8_t)!set.latch;
            set_dirty = 1;
            if (!set.latch) {
                uint32_t q;
                for (q = 0; q < NPADS; q++)
                    if (pad_latched[q]) {
                        pad_latched[q] = 0;
                        if (!pad_held[q])
                            ev_push(pads[q].type == PAD_PROG ? QE_PSTOP : QE_CSTOP, q, 0);
                    }
            }
            app_say("LATCH", set.latch ? "ON: PADS KEEP PLAYING" : "OFF");
        } else if (i == B_REC) {
            if (ui.rs == RS_OFF)
                rec_arm();
            else if (ui.rs == RS_REC)
                rec_stop();
            else {
                ui.rs = RS_OFF;
                rec_on = 0;
                app_say("REC", "CANCELLED");
            }
        }
        ui.redraw = 1;
    }
    {                                    /* SAVE held 0.8 s: LOAD */
        if (ui.save_at && ui.save_at != 0xFFFFFFFFu && now - ui.save_at >= 800u) {
            ui.save_at = 0xFFFFFFFFu;
            players_stop();
            undo_snap();
            if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC)
                settings_defaults();
            tempo = set.bpm;
            app_say("LOAD", session_load() ? "NOTHING SAVED YET" : "THE SAVED PADS");
            if (set.coach_set == 0)
                set.coach_set = 1;
        }
    }
    {
        int32_t es = plat_enc(EN_SELECT), k1 = plat_enc(EN_K1), k2 = plat_enc(EN_K2), k3 = plat_enc(EN_K3),
                k4 = plat_enc(EN_K4), ek = plat_enc(EN_PRESET), ea = plat_enc(EN_ALGO);
        if ((es || k1 || k2 || k3 || k4 || ek || ea) && (btn >> B_HOME & 1u))
            ui.shift_used = 1;
        if (ui.coach == CS_CHOOSE) {
            if (es)
                ui.co_pick = (uint8_t)clamp(ui.co_pick + es, CO_OFF, CO_ALWAYS);
            es = k1 = k2 = k3 = k4 = ek = ea = 0;
        }
        if (ui.screen == SCR_PICK) {
            if (es && fd_cnt)
                fd_sel = (uint8_t)clamp((int32_t)fd_sel + es, 0, (int32_t)fd_cnt - 1);
            if (k1) {
                fd_n = 0;
                fd_pcs = 0;
                fd_rank();
            }
            es = k1 = 0;
        } else if (ui.screen == SCR_LIB) {
            if (k1) {                    /* the type shown: TRIADS .. ADD9, PROGRESSIONS */
                ui.lib_t = (uint8_t)clamp(ui.lib_t + k1, 0, (int32_t)NCTYPES);
                ui.lib_sel = 0;
            }
            if (es)
                ui.lib_sel = (uint8_t)clamp((int32_t)ui.lib_sel + es, 0, ui.lib_t < NCTYPES ? 6 : (int32_t)NPROGS - 1);
            es = k1 = 0;
        } else if (ui.screen == SCR_PAD && ui.pad >= 0) {   /* PAD EDIT */
            pfx_t *f = &pfx[ui.pad];
            if (ek) {
                f->plays = (uint8_t)clamp(f->plays + ek, 0, PL_N - 1);
                if (pads[ui.pad].type == PAD_PROG)
                    pad_make((uint32_t)ui.pad);
            }
            if (k1)
                f->speed = (uint8_t)clamp(f->speed + k1, 0, 6);
            if (k2)
                f->human = (uint8_t)clamp(f->human + 5 * k2, 0, 50);
            if (k3)
                f->len = (int8_t)clamp(f->len + k3, -5, 5);
            if (k4)
                f->inv = (int8_t)clamp(f->inv + k4, -3, 3);
            if (es)
                f->vel = (uint8_t)(f->vel ? clamp((int32_t)f->vel + 8 * es, 0, 127) : (es > 0 ? 40 : 0));
            if (ea) {                    /* the pad's octave; SHIFT: every pad's */
                if (shift()) {
                    all_d = ea > 0 ? 1 : -1;
                    all_pads(ap_oct);
                    app_say("OCTAVE", "ALL PADS");
                } else
                    f->oct = (int8_t)clamp(f->oct + ea, -2, 2);
            }
            if (ek || k1 || k2 || k3 || k4 || es || ea)
                set_dirty = 1;
            es = k1 = k2 = k3 = k4 = ek = ea = 0;
        } else if (ui.screen == SCR_SETUP) {
            if (k1) {
                set.ble = (uint8_t)(k1 > 0);
                plat_ble_enable(set.ble);
            }
            if (k2)
                set.clk_out = (uint8_t)(k2 > 0);
            if (k3)
                set.clk_in = (uint8_t)(k3 > 0);
            if (k4) {
                set.coach = (uint8_t)clamp(set.coach + k4, CO_OFF, CO_ALWAYS);
                set.coach_set = 1;
            }
            if (k1 || k2 || k3 || k4)
                set_dirty = 1;
            k1 = k2 = k3 = k4 = 0;
        }
        /* home's knobs, everywhere they're not used */
        if (ek) {                        /* PRESET: the key's root (cleared pads come back) */
            char t[24];
            uint32_t q;
            set.root = (uint8_t)(((int32_t)set.root + ek % 12 + 12) % 12);
            for (q = 0; q < NPADS; q++)
                if (desc[q].src == SRC_EMPTY) {
                    desc[q].src = SRC_KIT;
                    desc[q].a = (uint8_t)q;
                }
            rekey();
            key_name(set.root, set.scale, t);
            app_say("KEY", t);
        }
        if (k1) {
            char t[24];
            set.scale = (uint8_t)(((int32_t)set.scale + k1 % (int32_t)NSCALES + (int32_t)NSCALES) % (int32_t)NSCALES);
            rekey();
            key_name(set.root, set.scale, t);
            app_say("KEY", t);
        }
        if (k2) {
            set.type = (uint8_t)clamp(set.type + k2, 0, (int32_t)NCTYPES - 1);
            rekey();
            app_say("TYPE", CTYPE_NAME[set.type]);
            coach_event(CE_SHAPE);
        }
        if (k3) {                        /* every pad plays so */
            set.plays = (uint8_t)clamp(set.plays + k3, 0, PL_N - 1);
            all_pads(ap_plays);
            rekey();
            app_say("PLAYS, ALL PADS", PLAYS_NAME[set.plays]);
            coach_event(CE_SHAPE);
        }
        if (k4) {
            char t[8];
            set.human = (uint8_t)clamp(set.human + 5 * k4, 0, 50);
            all_pads(ap_human);
            num_text(set.human * 2, t, 0);
            text_cat(t, "%");
            app_say("HUMANIZE, ALL PADS", t);
            set_dirty = 1;
        }
        if (ea) {
            set.len = (int8_t)clamp(set.len + ea, -5, 5);
            all_pads(ap_len);
            app_say("LENGTH, ALL PADS", LEN_NAME[set.len + 5]);
            set_dirty = 1;
        }
        if (es) {
            char b[8];
            set.bpm = (uint8_t)clamp((int32_t)set.bpm + es, 40, 240);
            tempo = set.bpm;
            num_text(set.bpm, b, 0);
            app_say("BPM", b);
            set_dirty = 1;
        }
        if (es || k1 || k2 || k3 || k4 || ek || ea)
            ui.redraw = 1;
    }
    {                                    /* SYS: a tap, SETUP; held 0.8 s, COACH */
        if (btn & 1u << B_GLO) {
            if (!ui.sys_at)
                ui.sys_at = now ? now : 1u;
            else if (ui.sys_at != 0xFFFFFFFFu && now - ui.sys_at >= 800u) {
                ui.sys_at = 0xFFFFFFFFu;
                coach_start();
            }
        } else {
            if (ui.sys_at && ui.sys_at != 0xFFFFFFFFu && ui.coach != CS_CHOOSE) {
                ui.screen = ui.screen == SCR_SETUP ? SCR_HOME : SCR_SETUP;
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
static void lights(void)
{
    uint32_t i, bright = 0, dim = 0, mid = 0, btn = 0, now = plat_ms();
    int blink = (now / 260u) & 1u, fast = (now / 130u) & 1u;
    if (ui.coach == CS_CHOOSE) {
        plat_lights(blink ? 1u << B_PLAY : 0u, 0, 0, 0, 1);
        plat_lights_mid(0, 0);
        return;
    }
    if (ui.screen == SCR_PICK) {
        uint16_t sm = fd_cnt ? scale_mask(fd_list[fd_sel] >> 4, fd_list[fd_sel] & 15u) : 0;
        for (i = 0; i < NKEYS; i++) {
            uint32_t pc = (uint32_t)(53 + (int32_t)i) % 12u;
            if ((ui.keys >> i & 1u) || (fd_pcs >> pc & 1u))
                bright |= 1u << i;
            else if (sm >> pc & 1u)
                dim |= 1u << i;
        }
        if (ui.coach == CS_NOTES && !fd_n && blink)
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
        if (ui.rs == RS_PICK) {          /* REC: the pads you can record into blink */
            if (!desc[i].lock && blink)
                bright |= bit;
            continue;
        }
        if ((ui.rs == RS_COUNT || ui.rs == RS_REC) && i == ui.rpad) {
            if (ui.rs == RS_REC || fast)
                bright |= bit;
            continue;
        }
        if ((ui.keys & bit) || pl[i].on || cp[i].on)
            bright |= bit;
        else if (ui.screen == SCR_LIB)
            mid |= bit;
        else if (ui.coach == CS_PADS) {
            if (blink)
                bright |= bit;
            else
                dim |= bit;
        } else if (pads[i].type != PAD_EMPTY)
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
        int on = i < 3u ? set.bassm == i : (int)((now * (uint32_t)tempo / 60u) % 1000u < 120u);   /* TAP: the beat */
        if (on || (ui.keys & bit) || (ui.coach == CS_CPADS && blink))
            bright |= bit;
        else
            dim |= bit;
    }
    if (set_dirty)
        btn |= 1u << B_SAVE;
    if (set.latch)
        btn |= 1u << B_SEQ;
    if (ui.pad >= 0 && desc[ui.pad].lock)
        btn |= 1u << B_LFO;
    if (U.have)
        btn |= 1u << B_ARP;
    if (ui.rs == RS_REC || ((ui.rs == RS_PICK || ui.rs == RS_COUNT) && blink))
        btn |= 1u << B_REC;
    btn |= 1u << (ui.screen == SCR_LIB ? B_SEL : ui.screen == SCR_PAD ? B_ENV
                  : ui.screen == SCR_SETUP ? B_GLO : B_HOME);
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
    for (i = 0; i < 16u; i++) {
        int32_t x = 4 + (int32_t)(i % 8u) * 29, y = 2 + (int32_t)(i / 8u) * 23;
        int on = (int)(ui.keys >> WK[i] & 1u) || pl[i].on || cp[i].on;
        const pad_t *p = &pads[i];
        uint16_t bg = p->type == PAD_EMPTY ? ES_INK : p->type == PAD_PROG ? (on ? WATER_C : ES_PURPLE) : (on ? EARTH_C : ES_SLATE);
        char t[8];
        cv_rect(x, y, 27, 21, bg);
        if (p->type == PAD_EMPTY)
            cv_rect(x, y + 20, 27, 1, ES_SLATE);
        if ((int32_t)i == ui.pad)
            cv_rect(x, y + 18, 27, 3, AIR_C);
        if (desc[i].lock)
            cv_rect(x + 22, y, 5, 5, AIR_C);
        if (p->type == PAD_CHORD)
            str_set(t, p->roman, sizeof t);
        else if (desc[i].src == SRC_REC)
            str_set(t, "REC", sizeof t);
        else if (p->type == PAD_PROG) {
            t[0] = 'P';
            num_text(desc[i].src == SRC_LIBP ? desc[i].a + 1 : (int32_t)i - 7, t + 1, 0);
        } else
            t[0] = 0;
        cv_text(x + (27 - text_w(&FONT_XS, t)) / 2, y + 4, &FONT_XS, t, on ? ES_INK : C_WHITE);
    }
    for (i = 0; i < 4u; i++) {           /* the C pads */
        static const char *const CN[4] = {"BASS", "NO BASS", "NO ROOT", "TAP"};
        int32_t x = 4 + (int32_t)i * 58;
        int on = i < 3u && set.bassm == i;
        cv_rect(x, 50, 56, 16, on ? EARTH_C : ES_SLATE);
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

static void pad_heading(char *t)         /* "PAD 3  Em7" */
{
    const pad_t *p = &pads[ui.pad];
    str_set(t, "PAD ", 24);
    num_text(ui.pad + 1, t + 4, 0);
    text_cat(t, "  ");
    if (p->type == PAD_CHORD) {
        char rn[4];
        spell(p->root_pc, tonic_letter() + p->deg, rn);
        text_cat(t, rn);
        text_cat(t, p->qual);
    } else
        text_cat_n(t, p->title, 24);
}
static void draw_home_top(void)
{
    const pad_t *p = ui.pad >= 0 ? &pads[ui.pad] : 0;
    int32_t x;
    lab_begin();
    top_bar(ui.rs == RS_REC ? "REC" : ui.rs ? "REC?" : "SEED", ui.rs ? FIRE_C : EARTH_C);
    if (set.latch)
        cv_text(234 - text_w(&FONT_XS, "LATCH"), 30, &FONT_XS, "LATCH", FIRE_C);
    if (ui.rs == RS_PICK) {
        text_c(40, &FONT_M, "TAP A PAD", C_WHITE);
        text_c(64, &FONT_XS, "TO RECORD INTO. REC AGAIN: CANCEL", LAB_LABEL);
    } else if (ui.rs == RS_COUNT || ui.rs == RS_REC) {
        char t[24] = "PAD ";
        num_text(ui.rpad + 1, t + 4, 0);
        text_c(40, &FONT_M, ui.rs == RS_COUNT ? "COUNT IN" : "RECORDING", ui.rs == RS_COUNT ? AIR_C : FIRE_C);
        text_c(64, &FONT_B, t, C_WHITE);
    } else if (p && p->type == PAD_CHORD) {
        char t[24], r[2], rn[4];
        const char *acc;
        int32_t w;
        spell(p->root_pc, tonic_letter() + p->deg, rn);
        r[0] = rn[0];
        r[1] = 0;
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
        text_cat(t, desc[ui.pad].src == SRC_REC ? ", RECORDED" : ", PROGRESSION");
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
    uint8_t dim;
    char r[6];
    if (ui.lib_t < NCTYPES) {
        chord_label(s, ui.lib_t, t);
        roman_of(s, r, &dim);
        text_cat(t, "  ");
        text_cat(t, r);
    } else {
        uint32_t k;
        str_set(t, "", 24);
        for (k = 0; k < 4u; k++) {
            roman_of(PROGS[s % NPROGS][k], r, &dim);
            if (k)
                text_cat(t, " ");
            text_cat(t, r);
        }
    }
}
static void draw_lib(void)
{
    uint32_t i, first, n = ui.lib_t < NCTYPES ? 7u : NPROGS;
    lab_begin();
    top_bar("LIB", ES_PURPLE);
    {
        char t[32] = "K1: ";
        text_cat(t, ui.lib_t < NCTYPES ? CTYPE_NAME[ui.lib_t] : "PROGRESSIONS");
        text_c(34, &FONT_B, t, AIR_C);
        lib_label(ui.lib_sel, t);
        text_c(58, fit_font(t, 230, &FONT_M), t, C_WHITE);
    }
    text_c(96, &FONT_XS, "PLAY: HEAR IT. A WHITE KEY: PUT IT THERE", LAB_LABEL);
    cv_blit(0, 0);
    lab_begin();
    first = ui.lib_sel > 1u ? ui.lib_sel - 1u : 0u;
    for (i = 0; i < 4u && first + i < n; i++) {
        char t[24];
        uint32_t k = first + i;
        int32_t y = 8 + (int32_t)i * 26;
        lib_label(k, t);
        if (k == ui.lib_sel)
            cv_rect(2, y - 2, 236, 22, AIR_C);
        cv_text(10, y, &FONT_B, t, k == ui.lib_sel ? ES_INK : C_WHITE);
    }
    cv_blit(0, 120);
}

static void draw_pad(void)               /* PAD EDIT, as the web's pad edit window */
{
    const pfx_t *f = &pfx[ui.pad];
    char t[32], v[4][12];
    static const char *const LBL[4] = {"K1 SPEED", "K2 HUMAN", "K3 LENGTH", "K4 INVERT"};
    lab_begin();
    top_bar("PAD EDIT", AIR_C);
    pad_heading(t);
    text_c(32, fit_font(t, 230, &FONT_B), t, C_WHITE);
    cv_text(8, 56, &FONT_XS, "PRESET PLAYS", LAB_LABEL);
    cv_text(8, 68, &FONT_M, PLAYS_NAME[f->plays % PL_N], FIRE_C);
    cv_text(130, 56, &FONT_XS, "SELECT VELOCITY", LAB_LABEL);
    if (f->vel)
        num_text(f->vel, t, 0);
    else
        str_set(t, "AS PLAYED", sizeof t);
    cv_text(130, 70, &FONT_B, t, C_WHITE);
    cv_text(130, 92, &FONT_XS, "ALGO OCTAVE", LAB_LABEL);
    num_text(f->oct, t, 1);
    cv_text(200, 90, &FONT_B, t, C_WHITE);
    cv_text(8, 96, &FONT_XS, desc[ui.pad].lock ? "LOCKED (LFO)" : "LFO: LOCK IT", LAB_LABEL);
    cv_blit(0, 0);
    lab_begin();
    num_text(SPEED_PCT[f->speed % 7u], v[0], 0);
    text_cat(v[0], "%");
    num_text(f->human * 2, v[1], 0);
    text_cat(v[1], "%");
    str_set(v[2], f->len ? LEN_NAME[f->len + 5] : "AS IS", sizeof v[2]);
    num_text(f->inv, v[3], f->inv != 0);
    bottom_bar(8, LBL, v);
    text_c(66, &FONT_XS, "SHIFT (HOLD HOME) + ALGO: EVERY PAD'S OCTAVE", LAB_LABEL);
    cv_blit(0, 120);
}

static int clock_ext(void) { return set.clk_in && clk_seen && clock_smp - clk_seen < 22050u; }
static void draw_setup(void)
{
    static const char *const LBL[4] = {"K1 BT MIDI", "K2 CLK OUT", "K3 CLK IN", "K4 STEPS"};
    char v[4][12], t[24];
    lab_begin();
    top_bar("SETUP", ES_PURPLE);
    num_text((int32_t)tempo, t, 0);
    text_cat(t, clock_ext() ? " BPM, FROM CLOCK IN" : " BPM (SELECT, OR TAP)");
    text_c(40, &FONT_B, t, C_WHITE);
    text_c(66, &FONT_XS, "HOLD SYS: THE STEPS. HOLD SAVE: LOAD", LAB_LABEL);
    text_c(86, &FONT_XS, "MIDI: PADS 1-8 CH 1, PADS 9-16 CH 2", WATER_C);
    text_c(100, &FONT_XS, "B PADS CH 3, KEYBOARD CH 4", WATER_C);
    cv_blit(0, 0);
    lab_begin();
    str_set(v[0], set.ble ? "ON" : "OFF", sizeof v[0]);
    str_set(v[1], set.clk_out ? "ON" : "OFF", sizeof v[1]);
    str_set(v[2], set.clk_in ? "ON" : "OFF", sizeof v[2]);
    str_set(v[3], set.coach == CO_OFF ? "OFF" : set.coach == CO_FIRST ? "FIRST" : "ALWAYS", sizeof v[3]);
    bottom_bar(30, LBL, v);
    cv_blit(0, 120);
}

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
        if (ui.coach >= CS_PADS) {
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
            set.human > 50u || set.len < -5 || set.len > 5 || set.bpm < 40u || set.bpm > 240u || set.coach >= CO_N)
            settings_defaults();
    }
    tempo = set.bpm;
    kit_fresh();
    session_load();                      /* the saved pads, if any */
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
    static uint32_t last_on;
    uint32_t now = plat_ms(), i, on = 0;
    input();
    lights();
    ann_tick(now);
    if (clock_ext() && clk_avg) {        /* MIDI clock in: the BPM follows it */
        uint32_t b = (uint32_t)(SR * 60.0f * 16.0f / (24.0f * (float)clk_avg) + 0.5f);
        if (b >= 30u && b <= 300u && b != tempo) {
            tempo = b;
            ui.redraw = 1;
        }
    } else if (tempo != set.bpm && !clock_ext())
        tempo = set.bpm;
    for (i = 0; i <= NPADS; i++)
        on |= (uint32_t)(pl[i].on | (i < NPADS ? cp[i].on : 0)) << i;
    if (on != last_on) {
        last_on = on;
        ui.redraw = 1;
    }
    if ((ui.pad >= 0 && pl[ui.pad].on) || ui.rs) {
        static uint32_t at;
        if (now - at >= 100u) {
            at = now;
            ui.redraw = 1;
        }
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
#ifndef PLAT_HOST
    if (now - ui.boot_at < 2600u) {
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
        else if (ui.screen == SCR_PAD && ui.pad >= 0)
            draw_pad();
        else if (ui.screen == SCR_SETUP)
            draw_setup();
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
    else if (!strcmp(what, "bpm"))
        *got = (int)tempo;
    else if (!strcmp(what, "rec"))
        *got = ui.rs;
    else if (!strcmp(what, "ble"))
        *got = set.ble;
    else if (!strncmp(what, "padnote", 7)) {
        int k = atoi(what + 7);
        *got = ui.pad >= 0 && k < pads[ui.pad].n ? pads[ui.pad].note[k] : -1;
    } else if (!strncmp(what, "padn", 4))
        *got = pads[atoi(what + 4) % NPADS].type == PAD_CHORD ? pads[atoi(what + 4) % NPADS].n
                                                               : pads[atoi(what + 4) % NPADS].pn;
    else if (!strncmp(what, "padtype", 7))
        *got = pads[atoi(what + 7) % NPADS].type;
    else if (!strncmp(what, "playing", 7)) {
        int k = atoi(what + 7) % (NPADS + 1);
        *got = pl[k].on || (k < (int)NPADS && cp[k].on);
    } else if (!strncmp(what, "lock", 4))
        *got = desc[atoi(what + 4) % NPADS].lock;
    else if (!strncmp(what, "src", 3))
        *got = desc[atoi(what + 3) % NPADS].src;
    else if (!strncmp(what, "inv", 3))
        *got = pfx[atoi(what + 3) % NPADS].inv;
    else
        return 0;
    return 1;
}
#endif

int seed_ble_on(void)                    /* (app.h APP_BLE_AT_BOOT: before app_init has the settings, read them) */
{
    settings_t s;
    if (plat_store_load(OBJ_SETTINGS, &s, sizeof s) != (int)sizeof s || s.magic != SET_MAGIC)
        return 1;
    return s.ble != 0;
}
