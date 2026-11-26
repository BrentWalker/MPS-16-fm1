/* SEED: MPS-16's chord builder, as firmware for the M-VAVE FM-1. Built on fm1-base.
 * Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence of this file: to be chosen (see README).
 *
 * As MPS-16 ver WEB's SEED. CHORD KEYS (white 1-8), PROGRESSION KEYS (white 9-16), NOTE KEYS (black 1-7), the C pads
 * (BASS, NO BASS, NO ROOT, TAP). A change goes to the group played last; hold a key: that key (SELECT, PRESET, ALGO:
 * its group: sound or MIDI 1-4, the window's START, END). The buttons as they're named: HOME (held: SHIFT), EDIT your
 * pad edit, the window (SHIFT: LOCK); SEQ your notes, scale, root, type; SEL the library; ARP the rhythm (SHIFT: UNDO); ENV the
 * notes' shape; LFO the groove; FX SOUND (held: SETUP; SHIFT: COACH); SAVE (held: LOAD); PLAY and REC the looper;
 * GLO STOP ALL; OCT- / OCT+ octave. MIDI over USB and Bluetooth, clock out and in.
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. */
#include "fastmath.h"

/* ================================================================ settings === */
#define SET_MAGIC 0x36444553u            /* "SED6" */
enum { BM_BASS, BM_NOBASS, BM_NOROOT, BM_N };
/* RHYTHM, how a chord or a progression plays: held, a rhythm (16ths), or an arp (8ths) by its direction */
enum { RH_HOLD, RH_STABS, RH_OFFBEAT, RH_332, RH_SYNC, RH_ARPUP, RH_ARPDN, RH_ARPUD, RH_ARPRND, PL_N };
enum { CO_OFF, CO_FIRST, CO_ALWAYS, CO_N };
/* the keys: CHORD KEYS (white 1-8), PROGRESSION KEYS (white 9-16), NOTE KEYS (black 1-7), the C pads (black 8-11).
 * MIDI out, fixed: chord keys on 1, progression keys on 2, note keys on 3, the keyboard (PICK NOTES, LIB) on 4 */
#define CH_PADS1 0u
#define CH_PADS2 1u
#define CH_BPADS 2u
#define CH_KEYS 3u
#define NGRP 4u                          /* those four: each plays its own FM-1 sound */
typedef struct {
    uint32_t magic;
    uint8_t root;                        /* 0 = C .. 11 = B */
    uint8_t scale;                       /* SCALES[] */
    int8_t octave;                       /* the keyboard's (PICK NOTES): -2 .. +2 */
    uint8_t type;                        /* the kit's chords: TRIADS, 7THS, 9THS, SUS2, SUS4, ADD9 */
    uint8_t plays, human;                /* new pads start so */
    int8_t len;
    uint8_t bassm;                       /* BASS, NO BASS, NO ROOT: BM_* */
    uint8_t bpm;                         /* 40 .. 240 */
    uint8_t nhum;                        /* the note keys' HUMANIZE */
    uint8_t coach, coach_seen, coach_set;   /* COACH: CO_*, been through once, the choice made */
    uint8_t ble;                         /* Bluetooth MIDI on */
    uint8_t clk_out, clk_in;             /* MIDI clock: sent at the BPM; followed when it comes in */
    uint8_t out_rsv;
    uint8_t snd[NGRP];                   /* each group's: a sound (SOUNDS[]), or MIDI 1-4 (NSOUNDS + 0 .. 3), not both */
    int8_t boct;                         /* the note keys' octave, -2 .. 2 */
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
    set.plays = RH_HOLD;
    set.human = 10;
    set.bassm = BM_BASS;
    set.bpm = 90;
    set.coach = CO_FIRST;
    set.ble = 1;
    set.clk_out = 1;
    set.clk_in = 1;
    set.snd[CH_PADS1] = 1;               /* KEYS, STRINGS, BASS, WARM */
    set.snd[CH_PADS2] = 3;
    set.snd[CH_BPADS] = 5;
    set.snd[CH_KEYS] = 0;
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
static const char *const PLAYS_NAME[PL_N] = {"HOLD", "STABS", "OFFBEAT", "3-3-2", "SYNC", "ARP UP", "ARP DOWN",
                                             "ARP UP-DN", "ARP RANDOM"};
static const uint16_t RH_PAT[5] = {0, 0x1041u, 0x4444u, 0x4949u, 0x1489u};   /* the rhythms, 16ths of a bar */
#define RH_ARP(r) ((r) >= RH_ARPUP)
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
static void pos_text(uint32_t tk, char *t, int at)   /* ticks as BAR.BEAT.32ND: a place (at: from 1.1.1) or a length */
{
    uint32_t b = tk / (4u * PPQ), q = tk % (4u * PPQ) / PPQ, r = tk % PPQ / 3u;
    num_text((int32_t)(b + (uint32_t)at), t, 0);
    text_cat(t, ".");
    num_text((int32_t)(q + (uint32_t)at), t + cs_len(t), 0);
    text_cat(t, ".");
    num_text((int32_t)(r + (uint32_t)at), t + cs_len(t), 0);
}
static void key_label(uint32_t i, char *t)   /* "CHORD 3" (white keys 1-8), "PROG 6" (9-16) */
{
    str_set(t, i < 8u ? "CHORD " : "PROG ", 24);
    num_text((int32_t)(i < 8u ? i + 1u : i - 7u), t + cs_len(t), 0);
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
/* EDIT, each key's own: RHYTHM, STRUM, SPEED, HUMANIZE, LENGTH, SWING, INVERSION; its octave (OCT), its bass (C pads) */
enum { AR_UP, AR_DOWN, AR_UPDOWN, AR_RANDOM, AR_N };
typedef struct {
    uint8_t plays, human, speed, vel;    /* speed: SPEED_PCT[]; vel 0: as you play it, else that velocity */
    int8_t len, inv, oct, rsv;
    uint8_t strum, swing, bm, rsv2;      /* strum: 5 ms steps (0 off); swing: 50 .. 75 %; bm: BASS, NO BASS, NO ROOT */
    int16_t st, sl;                      /* the window: START, END (ticks; END 0: the sequence's end). Up to a quarter
                                          * of the sequence past either edge, silent there */
} pfx_t;
static const uint16_t SPEED_PCT[7] = {50, 67, 75, 100, 133, 150, 200};
static PLAT_POOL pad_t pads[NPADS + 1];
static PLAT_POOL pnote_t ph[NPADS + 1][MAXPH];
static desc_t desc[NPADS];
static pfx_t pfx[NPADS + 1];
static volatile uint32_t tempo = 90;

static void pfx_default(pfx_t *f)
{
    __builtin_memset(f, 0, sizeof *f);
    f->plays = set.plays;
    f->human = set.human;
    f->len = set.len;
    f->speed = 3;
    f->swing = 50;
    f->bm = set.bassm;
}
static uint32_t strum_ms(const pfx_t *f) { return f->strum * 5u; }   /* a chord's notes this far apart (ms) */
static uint32_t arp_pick(uint32_t dir, uint32_t j, uint32_t n)   /* ARP's j-th note of n, by its direction */
{
    uint32_t per;
    if (n < 2u)
        return 0;
    if (dir == AR_DOWN)
        return n - 1u - j % n;
    if (dir == AR_UPDOWN) {
        per = 2u * n - 2u;
        j %= per;
        return j < n ? j : per - j;
    }
    if (dir == AR_RANDOM) {
        static uint32_t r = 0x9E3779B9u;
        r = r * 1664525u + 1013904223u;
        return (r >> 16) % n;
    }
    return j % n;
}
/* SWING: a beat's second 8th late (t in 96ths of a beat) */
static float swing_t(float t, uint32_t sw)
{
    float beat, b, m;
    if (sw <= 50u)
        return t;
    beat = (float)(int32_t)(t / 96.0f) * 96.0f;
    b = t - beat;
    m = 96.0f * (float)sw / 100.0f;
    return beat + (b < 48.0f ? b * m / 48.0f : m + (b - 48.0f) * (96.0f - m) / 48.0f);
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

static void chord_into(pad_t *p, uint32_t d, uint32_t t, uint32_t bm)   /* degree d's chord of type t (d 7: up an octave) */
{
    int32_t v[6];
    uint32_t k, n, mask = 0, rp = deg_pc((int32_t)d);
    __builtin_memset(p, 0, sizeof *p);
    n = chord_of(d, t, v);
    p->bass_p = (uint8_t)v[0];
    for (k = 0; k < n; k++)
        mask |= 1u << (((uint32_t)v[k] + 12u - rp) % 12u);
    n = chord_bm(v, n, rp, bm);
    p->type = PAD_CHORD;
    p->n = (uint8_t)n;
    p->deg = (uint8_t)(d % 7u);
    p->root_pc = (uint8_t)rp;
    p->bm = (uint8_t)bm;
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
    chord_into(&tmp, d, t, BM_BASS);
    spell(tmp.root_pc, tonic_letter() + d, r);
    str_set(o, r, 24);
    text_cat(o, tmp.qual);
}

/* progression r into slot dst: four bars, a chord a bar, played by its RHYTHM, the bass line by its bass mode */
static void prog_into(uint32_t dst, uint32_t r, uint32_t type, uint32_t plays)
{
    pad_t *p = &pads[dst];
    const uint8_t *seq = PROGS[r % NPROGS];
    uint32_t t = type == 2u ? 1u : type, bar, bl = PPQ * 4u, n, k, bm = pfx[dst].bm % BM_N;
    float prev = (float)base_note() + 5.0f;
    int32_t v[6];
    __builtin_memset(p, 0, sizeof *p);
    p->type = PAD_PROG;
    p->bars = 4;
    p->bm = (uint8_t)bm;
    p->len = (uint16_t)(4u * bl);
    p->deg = seq[0];
    for (bar = 0; bar < 4u; bar++) {
        uint32_t o = bar * bl, d = seq[bar], rp = deg_pc((int32_t)d);
        int32_t bass = deg_pitch((int32_t)d) - 12;
        char rn[6];
        uint8_t dim;
        p->bar_deg[bar] = (uint8_t)d;
        n = chord_of(d, t, v);
        if (bm == BM_NOROOT)
            n = chord_bm(v, n, rp, BM_NOROOT);
        nearest(v, n, prev);
        prev = 0.0f;
        for (k = 0; k < n; k++)
            prev += (float)v[k];
        prev /= (float)n;
        if (plays >= RH_STABS && plays <= RH_SYNC) {   /* a rhythm in 16ths; the bass on 1 and 3 */
            uint32_t pat = RH_PAT[plays], j, j2;
            for (j = 0; j < 16u; j++)
                if (pat >> j & 1u) {
                    uint32_t gap;
                    for (j2 = j + 1u; j2 < 16u && !(pat >> j2 & 1u); j2++)
                        ;
                    gap = (j2 - j) * 24u;
                    for (k = 0; k < n; k++)
                        ph_note(dst, v[k], o + j * 24u, gap > 48u ? 60u : gap * 3u / 4u, j % 4u ? 88 : 108);
                }
            if (bm == BM_BASS) {
                ph_note(dst, bass, o, 58, 112);
                ph_note(dst, bass, o + 192u, 72, 100);
            }
        } else if (RH_ARP(plays)) {      /* arp 8ths by its direction, the bass held */
            uint32_t j, dir = plays - RH_ARPUP;
            int32_t e[8];
            for (j = 0; j < 8u; j++)
                e[j] = v[j % n] + (j >= n ? 12 : 0);
            for (j = 0; j < 8u; j++) {
                uint32_t q = dir == AR_DOWN ? 7u - j : dir == AR_UPDOWN ? (j <= 4u ? j : 8u - j)
                           : dir == AR_RANDOM ? arp_pick(AR_RANDOM, j, 8u) : j;
                ph_note(dst, e[q], o + j * 48u, 43, j % 2u ? 82 : 100);
            }
            if (bm == BM_BASS)
                ph_note(dst, bass, o, bl - 10u, 100);
        } else {                         /* held a bar, STRUM its time apart, low to high */
            uint32_t ms = strum_ms(&pfx[dst]), gap = (uint32_t)((float)ms * (float)tempo * 96.0f / 60000.0f + 0.5f);
            for (k = 0; k < n; k++) {
                uint32_t sk = k * gap > 96u ? 96u : k * gap;
                ph_note(dst, v[k], o + sk, bl - 10u - sk, 96);
            }
            if (bm == BM_BASS)
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
        i < 8u ? chord_into(&pads[i], i, set.type, pfx[i].bm) : prog_into(i, i - 8u, set.type, pfx[i].plays);
    else if (d->src == SRC_LIBC)
        chord_into(&pads[i], d->a, d->t, pfx[i].bm);
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
/* the sounds: one oscillator shape, a filter, an envelope. All cheap enough for 12 voices */
enum { W_SAW2, W_SQR2, W_FM, W_ORGAN, W_BASS };
typedef struct {
    const char *name;
    uint8_t wave;
    float det;                           /* the second oscillator's ratio (FM: the modulator's) */
    float fm;                            /* FM: the index, by the envelope */
    float fc, fc_vel, fc_env;            /* the filter: Hz, more with velocity squared, more with the envelope */
    float att, sus, dec, rel;            /* attack a sample; sustain level, the rate toward it; release */
    float gain;
} sound_t;
#define NSOUNDS 10u
#define NSEL (NSOUNDS + 4u)              /* SELECT's list: the sounds, then MIDI 1-4 */
#define SND_CLICK NSOUNDS                /* REC's count-in */
static const sound_t SOUNDS[NSOUNDS + 1u] = {
    {"WARM", W_SAW2, 1.0058f, 0, 700, 2600, 1800, 0.0045f, 0.62f, 0.00012f, 0.99975f, 1.0f},
    {"KEYS", W_FM, 1.0f, 1.6f, 9000, 0, 0, 0.02f, 0.0f, 0.00005f, 0.9993f, 1.4f},
    {"ORGAN", W_ORGAN, 1.0f, 0, 9000, 0, 0, 0.01f, 1.0f, 0.001f, 0.9993f, 0.75f},
    {"STRINGS", W_SAW2, 1.0071f, 0, 1400, 1500, 600, 0.00007f, 0.85f, 0.0002f, 0.99993f, 1.2f},
    {"PLUCK", W_SAW2, 1.003f, 0, 300, 1500, 5200, 0.03f, 0.0f, 0.00009f, 0.9995f, 1.7f},
    {"BASS", W_BASS, 0.5f, 0, 350, 900, 1700, 0.01f, 0.7f, 0.0002f, 0.9994f, 1.1f},
    {"BELL", W_FM, 3.5f, 2.4f, 9000, 0, 0, 0.03f, 0.0f, 0.000025f, 0.99985f, 1.3f},
    {"LEAD", W_SQR2, 1.004f, 0, 1300, 3000, 1500, 0.01f, 0.8f, 0.0002f, 0.9995f, 0.8f},
    {"PAD", W_SAW2, 1.009f, 0, 700, 900, 500, 0.00004f, 0.9f, 0.0002f, 0.99995f, 1.0f},
    {"BRASS", W_SAW2, 1.002f, 0, 450, 2400, 2600, 0.004f, 0.75f, 0.0003f, 0.9993f, 0.9f},
    {"CLICK", W_FM, 2.0f, 0.5f, 9000, 0, 0, 0.1f, 0.0f, 0.0008f, 0.999f, 1.0f},
};
typedef struct {
    int8_t note;
    uint8_t gate, grp, snd;              /* grp: the group (0 .. 3, 4 the click) it was played in */
    uint8_t stage;
    float env, vel, p1, p2, inc, lp1, lp2;
    uint32_t age;
} voice_t;
static voice_t vo[NVOICES];
static uint32_t vo_clock;
static float gain_now;
static float sinp(float p)               /* sin(2 pi p), p 0 .. 1, close enough (and its sign flipped) */
{
    float u = 2.0f * p - 1.0f, y = 4.0f * u * (1.0f - fm_fabsf(u));
    return 0.225f * (y * fm_fabsf(y) - y) + y;
}
static float frac1(float x) { return x - (float)(int32_t)x; }   /* x >= 0 */

#define BARRIER() __asm__ volatile("" ::: "memory")
#define QN 128u
static volatile uint32_t q_ev[QN];
static volatile uint32_t q_w, q_r;
enum { QE_OFF, QE_ON, QE_PSTART, QE_PSTOP, QE_CSTART, QE_CSTOP, QE_LOOP };
static void ev_push_g(uint32_t type, uint32_t a, uint32_t b, uint32_t g)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;
    q_ev[w % QN] = g << 24 | type << 16 | (a & 127u) << 8 | (b & 127u);
    BARRIER();
    q_w = w + 1u;
}
static void ev_push(uint32_t type, uint32_t a, uint32_t b) { ev_push_g(type, a, b, 0); }
static uint8_t grp_snd[NGRP];
static void voice_on(uint32_t note, uint32_t vel, uint32_t grp)
{
    uint32_t i, best = 0, oldest = 0xFFFFFFFFu;
    if (grp < NGRP && grp_snd[grp] >= NSOUNDS)   /* this group goes out as MIDI */
        return;
    for (i = 0; i < NVOICES; i++) {
        uint32_t score = vo[i].stage == 0 ? 0u : vo[i].stage == 3 ? 1u : 2u;
        uint32_t key = score << 28 | (vo[i].age & 0x0FFFFFFFu);
        if (key < oldest) {
            oldest = key;
            best = i;
        }
    }
    vo[best].note = (int8_t)note;
    vo[best].grp = (uint8_t)grp;
    vo[best].snd = (uint8_t)(grp < NGRP ? grp_snd[grp] : SND_CLICK);
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
static void voice_off(uint32_t note, uint32_t grp)
{
    uint32_t i;
    for (i = 0; i < NVOICES; i++)
        if (vo[i].note == (int8_t)note && vo[i].grp == grp && vo[i].gate) {
            vo[i].gate = 0;
            vo[i].stage = 3;
        }
}

static volatile uint32_t clock_smp;      /* the audio clock, in samples */

/* MIDI out, over USB and Bluetooth */
static uint32_t midi_to(void) { return TO_USB | (set.ble ? TO_BLE : 0u); }
static uint8_t ch_last;
static void midi_note(uint32_t on, uint32_t g, uint32_t m, uint32_t v)   /* group g: out only if it's on MIDI */
{
    uint32_t sel = set.snd[g % NGRP], ch;
    if (sel < NSOUNDS)
        return;
    ch = sel - NSOUNDS;
    ch_last = (uint8_t)ch;
    plat_midi_send((on ? 0x09u | (0x90u | ch) << 8 | m << 16 | v << 24 : 0x08u | (0x80u | ch) << 8 | m << 16),
                   midi_to());
}
static void loop_rec(uint32_t on, uint32_t ch, uint32_t p, uint32_t v);
static void out_on(uint32_t m, uint32_t v, uint32_t ch)
{
    voice_on(m, v, ch % NGRP);
    midi_note(1, ch, m, v);
    loop_rec(1, ch, m, v);
}
static void out_off(uint32_t m, uint32_t ch)
{
    voice_off(m, ch % NGRP);
    midi_note(0, ch, m, 0);
    loop_rec(0, ch, m, 0);
}
static uint32_t pad_ch(uint32_t i)       /* pads 1-8 on 1, 9-16 on 2; LIB's preview (i = NPADS) on the keyboard's */
{
    return i >= NPADS ? CH_KEYS : i < 8u ? CH_PADS1 : CH_PADS2;
}

/* ================================================================== LOOP === */
/* the loop recorder: REC opens it and arms it (REC blinks), PLAY gives a bar's intro, then it loops and records what
 * goes out as MIDI (every key, every rhythm, MIDI in), a layer a pass. All of it runs on the audio side; the UI
 * sends it commands (QE_LOOP) and reads it to draw */
#define LMAX 440u
#define LOOP_MAGIC 0x31504F4Cu           /* "LOP1" */
typedef struct {
    uint16_t s, d;                       /* start, length: ticks (PPQ) in the loop; d 0: still held */
    uint8_t p, v, ch, layer;
} lnote_t;
typedef struct {
    uint32_t magic;
    uint8_t bars, metro, quant, swing;   /* bars 1 2 4 8; metro LM_*; quant 0/1 (16ths); swing 50 .. 75 */
    uint16_t n, rsv;
    lnote_t e[LMAX];
} loop_t;
enum { LM_OFF, LM_INTRO, LM_ALWAYS, LM_N };
enum { LR_STOP, LR_INTRO, LR_PLAY };
enum { LC_INTRO, LC_PLAY, LC_STOP, LC_RECON, LC_RECOFF, LC_CLEAR, LC_UNDO };
static PLAT_POOL loop_t L;
static PLAT_POOL float lp_left[LMAX];    /* a note sounding: ticks left */
static PLAT_POOL uint8_t lp_open[LMAX];  /* a note recording: held still */
static volatile uint8_t lp_run, lp_rec, lp_layer, lp_used;
static volatile float lp_pos;            /* ticks into the loop (the intro: into its bar) */
static uint32_t loop_len(void) { return (uint32_t)(L.bars ? L.bars : 4u) * 4u * PPQ; }
static void loop_rec(uint32_t on, uint32_t ch, uint32_t p, uint32_t v)   /* audio side: a MIDI note went out */
{
    uint32_t k, len = loop_len();
    float pos;
    if (!lp_rec || (lp_run != LR_PLAY && !(lp_run == LR_INTRO && lp_pos > (float)(4u * PPQ - 24u))))
        return;
    pos = lp_run == LR_INTRO ? 0.0f : lp_pos;   /* the intro's last 16th: on the 1 */
    if (on) {
        if (L.n >= LMAX)
            return;
        k = L.n;
        L.e[k].s = (uint16_t)pos;
        L.e[k].d = 0;
        L.e[k].p = (uint8_t)p;
        L.e[k].v = (uint8_t)(v ? v : 1u);
        L.e[k].ch = (uint8_t)ch;
        L.e[k].layer = lp_layer;
        lp_open[k] = 1;
        lp_left[k] = 0.0f;
        L.n = (uint16_t)(k + 1u);
        lp_used = 1;
    } else
        for (k = 0; k < L.n; k++)
            if (lp_open[k] && L.e[k].p == p && L.e[k].ch == ch) {
                uint32_t d = ((uint32_t)pos + len - L.e[k].s) % len;
                L.e[k].d = (uint16_t)(d < 6u ? 6u : d);
                lp_open[k] = 0;
            }
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
            voice_on(d1, d2, (st & 15u) < NGRP ? st & 15u : CH_KEYS);   /* MIDI in: its channel's sound */
            loop_rec(1, (st & 15u) < NGRP ? st & 15u : CH_KEYS, d1, d2);
        } else if (hi == 0x80u || hi == 0x90u) {
            voice_off(d1, (st & 15u) < NGRP ? st & 15u : CH_KEYS);
            loop_rec(0, (st & 15u) < NGRP ? st & 15u : CH_KEYS, d1, 0);
        }
    }
}

/* LENGTH, as music: a note this long (ticks, PPQ 96), AS IS (0), LEGATO (each note to the next). Index len + 5 */
#define LEN_MAX 4
static const uint16_t LEN_TK[10] = {12, 24, 48, 96, 192, 0, 384, 768, 1536, 0};
static const char *const LEN_NAME[10] = {"1/32", "1/16", "1/8", "1/4", "1/2", "AS IS", "1 BAR", "2 BARS", "4 BARS",
                                         "LEGATO"};
#define LEN_LEGATO(l) ((l) == LEN_MAX)
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
static int32_t seq_len(uint32_t i)       /* a key's sequence: a progression's bars, a chord's bar */
{
    return pads[i].type == PAD_PROG ? (int32_t)pads[i].len : (int32_t)(4u * PPQ);
}
static int32_t win_end(uint32_t i) { return pfx[i].sl ? pfx[i].sl : seq_len(i); }
static uint32_t pwin_len(uint32_t i)     /* a key's window: START to END */
{
    int32_t l = win_end(i) - pfx[i].st;
    return l > 12 ? (uint32_t)l : 12u;
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
        we = (float)pwin_len(i);         /* the window: from START, this long */
        a = P->pos;
        b = a + dt;
        for (k = 0; k < pd->pn && k < MAXPH; k++) {
            const pnote_t *x = &ph[i][k];
            float s = swing_t((float)x->s, f->swing) - (float)f->st;
            float e = fm_minf(P->snd[k] ? (float)P->endt[k] : s + (float)x->d, we);
            int starts = s >= a && s < b && s >= 0.0f && s < we;
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
                if (f->len && li < 10u) {
                    if (!LEN_LEGATO(f->len))
                        d = LEN_TK[li];
                    else {
                        uint32_t j, nx = (uint32_t)we + f->st;
                        for (j = 0; j < pd->pn && j < MAXPH; j++)
                            if (ph[i][j].s > x->s && ph[i][j].s < nx)
                                nx = ph[i][j].s;
                        d = nx - x->s;
                    }
                    if (d < 2u)
                        d = 2u;
                }
                P->endt[k] = (uint16_t)((uint32_t)s + d > 65535u ? 65535u : (uint32_t)s + d);
                e = fm_minf((float)P->endt[k], we);
            }
            if (P->snd[k] && e < b && !(starts && e >= b)) {
                out_off(P->snd[k] - 1u, P->ch);
                P->snd[k] = 0;
            }
        }
        P->pos = b;
        if (P->pos >= we) {              /* START to END once: no loop (the LOOP recorder loops) */
            pl_release(i);
            P->on = 0;
        }
    }
}

/* chord players: CHORD (held), STRUM (low to high), ARP (in turn), STABS (1, the and of 2, 4, each bar) */
#define MAXC 8u
typedef struct {
    uint8_t on, n, cur, mode, ch, human, arp, swing;
    uint8_t note[MAXC], vel[MAXC], sounded[MAXC];
    uint32_t at[MAXC];
    uint32_t t, next, idx, off, gate;
    float spd;
    int8_t len;
    float u, left, st, wl;               /* rhythms and arps: ticks into the window, the hit's ticks left, START, LENGTH */
} cplay_t;
static cplay_t cp[NPADS];
static uint8_t cbuf[NPADS][MAXC], cbuf_n[NPADS];
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
    ss = (uint32_t)((float)strum_ms(f) * 0.001f * SR / c->spd);   /* STRUM (or CHORD with a strum): ms a note */
    c->n = cbuf_n[i] < MAXC ? cbuf_n[i] : (uint8_t)MAXC;
    c->mode = f->plays;
    c->ch = (uint8_t)pad_ch(i);
    c->human = f->human;
    c->arp = (uint8_t)(RH_ARP(f->plays) ? (uint32_t)f->plays - RH_ARPUP : 0u);
    c->swing = f->swing < 50u ? 50u : f->swing;
    c->len = f->len;
    if (f->vel)
        vel = f->vel;
    for (k = 0; k < c->n; k++) {
        c->note[k] = cbuf[i][k];
        c->vel[k] = (uint8_t)(f->human ? human_vel(vel, f->human) : vel);
        c->sounded[k] = 0;
        c->at[k] = k * ss + (f->human ? cs_rnd() % (f->human * 13u + 1u) : 0u);
        if (f->st < 0)                   /* START before the chord: a gap before it sounds */
            c->at[k] += (uint32_t)((float)(-f->st) * SR * 60.0f / ((float)tempo * (float)PPQ) / c->spd);
    }
    c->t = c->next = c->idx = c->off = 0;
    c->u = c->left = 0.0f;
    c->st = (float)f->st;
    c->wl = (float)pwin_len(i);
    c->cur = 0;
    c->on = c->n != 0;
}
static void cp_hush(cplay_t *c)          /* a rhythm's or an arp's sounding notes off */
{
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
    c->left = 0.0f;
}
static float grid_t(uint32_t j, float step, uint32_t sw)   /* the j-th step's time, swung in pairs */
{
    return (float)(j / 2u) * 2.0f * step + (j & 1u ? 2.0f * step * (float)(sw > 50u ? sw : 50u) / 100.0f : 0.0f);
}
static void cp_fire(cplay_t *c, float t0, float t1)   /* the hits in [t0, t1), pattern time in ticks */
{
    int arp = RH_ARP(c->mode);
    float step = arp ? 48.0f : 24.0f, g;
    uint32_t j = t0 > step ? (uint32_t)(t0 / step) - 1u : 0u, k, li = (uint32_t)(c->len + 5);
    for (;; j++) {
        float tj = grid_t(j, step, c->swing);
        if (tj >= t1 || tj >= (float)(4u * PPQ))   /* past the bar: the window's silent extra */
            break;
        if (tj < t0)
            continue;
        if (arp) {
            uint32_t q = arp_pick(c->arp, j, c->n);
            if (c->cur)
                out_off(c->cur - 1u, c->ch);
            out_on(c->note[q], c->human ? human_vel(c->vel[0], c->human) : c->vel[0], c->ch);
            c->cur = (uint8_t)(c->note[q] + 1u);
            g = c->len && !LEN_LEGATO(c->len) && li < 10u ? (float)LEN_TK[li] : 0.0f;
            c->left = g > 0.0f && g < step ? g : 0.0f;   /* else: to the next */
        } else if (RH_PAT[c->mode] >> (j % 16u) & 1u) {
            for (k = 0; k < c->n; k++) {
                if (c->sounded[k])
                    out_off(c->note[k], c->ch);
                out_on(c->note[k], c->human ? human_vel(c->vel[0], c->human) : c->vel[0], c->ch);
                c->sounded[k] = 1;
            }
            g = step * 1.5f;
            if (c->len && li < 10u)
                g = LEN_LEGATO(c->len) ? 1e9f : (float)LEN_TK[li];
            c->left = g;
        }
    }
}
static void chord_tick(uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < NPADS; i++) {
        cplay_t *c = &cp[i];
        if (!c->on)
            continue;
        if (((c->mode >= RH_STABS && c->mode <= RH_SYNC) || RH_ARP(c->mode)) && c->n) {
            /* rhythms (16ths) and arps (8ths), in ticks through the window: START in, LENGTH long, round again */
            float adv = (float)n * (float)tempo * (float)PPQ / (60.0f * SR) * c->spd, a = c->u, b = a + adv;
            if (c->left > 0.0f) {        /* the last hit's notes end */
                c->left -= adv;
                if (c->left <= 0.0f)
                    cp_hush(c);
            }
            while (a < b) {
                float e = fm_minf(b, c->wl);
                cp_fire(c, c->st + a, c->st + e);
                if (b >= c->wl) {
                    b -= c->wl;
                    a = 0.0f;
                } else
                    a = b;
            }
            c->u = b;
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

/* the loop, audio side */
static void lp_click(int hi)
{
    voice_on(hi ? 96u : 84u, 90u, NGRP);
    voice_off(hi ? 96u : 84u, NGRP);
}
static void lp_sound(uint32_t k, int on)   /* loop playback: the FM-1 and MIDI out, never recorded again */
{
    const lnote_t *e = &L.e[k];
    uint32_t g = e->ch < NGRP ? e->ch : CH_KEYS;
    if (on) {
        voice_on(e->p, e->v, g);
        midi_note(1, e->ch, e->p, e->v);
    } else {
        voice_off(e->p, g);
        midi_note(0, e->ch, e->p, 0);
    }
}
static void lp_silence(void)
{
    uint32_t k;
    for (k = 0; k < L.n; k++)
        if (lp_left[k] > 0.0f) {
            lp_sound(k, 0);
            lp_left[k] = 0.0f;
        }
}
static void lp_close(void)               /* recording stops: the notes still held end here */
{
    uint32_t k, len = loop_len();
    for (k = 0; k < L.n; k++)
        if (lp_open[k]) {
            uint32_t d = ((uint32_t)lp_pos + len - L.e[k].s) % len;
            L.e[k].d = (uint16_t)(d < 6u ? 6u : d);
            lp_open[k] = 0;
        }
}
static uint32_t lp_top(void)             /* the newest layer */
{
    uint32_t k, t = 0;
    for (k = 0; k < L.n; k++)
        if (L.e[k].layer > t)
            t = L.e[k].layer;
    return t;
}
static void loop_cmd(uint32_t c)
{
    uint32_t k, j;
    switch (c) {
    case LC_INTRO:
        lp_silence();
        lp_run = LR_INTRO;
        lp_pos = 0.0f;
        lp_layer = (uint8_t)(lp_top() + 1u);
        lp_used = 0;
        lp_rec = 1;
        if (L.metro != LM_OFF)
            lp_click(1);
        break;
    case LC_PLAY:
        lp_silence();
        lp_run = LR_PLAY;
        lp_pos = 0.0f;
        lp_rec = 0;
        break;
    case LC_STOP:
        lp_close();
        lp_silence();
        lp_run = LR_STOP;
        lp_rec = 0;
        break;
    case LC_RECON:
        lp_layer = (uint8_t)(lp_top() + 1u);
        lp_used = 0;
        lp_rec = 1;
        break;
    case LC_RECOFF:
        lp_close();
        lp_rec = 0;
        break;
    case LC_CLEAR:
        lp_silence();
        lp_close();
        L.n = 0;
        lp_layer = 1;
        break;
    case LC_UNDO:                        /* the newest layer off */
        lp_close();
        lp_silence();
        {
            uint32_t t = lp_top();
            for (k = j = 0; k < L.n; k++)
                if (L.e[k].layer != t)
                    L.e[j++] = L.e[k];
            L.n = (uint16_t)j;
        }
        if (lp_rec) {
            lp_layer = (uint8_t)(lp_top() + 1u);
            lp_used = 0;
        }
        break;
    }
}
static float lp_start(const lnote_t *e, uint32_t len)   /* where a note plays: QUANTIZE (16ths) and SWING */
{
    float s = (float)e->s;
    if (L.quant) {
        s = (float)(((uint32_t)e->s + 12u) / 24u * 24u % len);
        s = swing_t(s, L.swing);
    }
    return s;
}
static void lp_span(float a, float b, uint32_t len)   /* the notes starting in [a, b) */
{
    uint32_t k;
    for (k = 0; k < L.n; k++) {
        float st;
        if (lp_open[k] || !L.e[k].d)
            continue;
        st = lp_start(&L.e[k], len);
        if (st >= a && st < b) {
            if (lp_left[k] > 0.0f)
                lp_sound(k, 0);
            lp_sound(k, 1);
            lp_left[k] = (float)L.e[k].d;
        }
    }
}
static void lp_clicks(float a, float b, int intro)   /* the beats in [a, b) */
{
    uint32_t q = (uint32_t)(a / (float)PPQ) + 1u;
    if (a <= 0.0f && b > 0.0f && !intro)
        q = 0;
    for (; (float)(q * PPQ) < b; q++)
        if ((float)(q * PPQ) >= a)
            lp_click(q % 4u == 0u);
}
static void loop_tick(uint32_t n)
{
    float adv = (float)n * (float)tempo * (float)PPQ / (60.0f * SR);
    uint32_t k, len = loop_len();
    if (lp_run == LR_STOP)
        return;
    for (k = 0; k < L.n; k++)            /* the notes sounding count down */
        if (lp_left[k] > 0.0f) {
            lp_left[k] -= adv;
            if (lp_left[k] <= 0.0f) {
                lp_left[k] = 0.0f;
                lp_sound(k, 0);
            }
        }
    if (lp_run == LR_INTRO) {
        float a = lp_pos, b = a + adv;
        if (L.metro != LM_OFF && b < (float)(4u * PPQ))
            lp_clicks(a, b, 1);
        lp_pos = b;
        if (b >= (float)(4u * PPQ)) {
            lp_run = LR_PLAY;
            lp_pos = 0.0f;
            if (L.metro == LM_ALWAYS)
                lp_click(1);
            lp_span(-1.0f, b - (float)(4u * PPQ), len);
            lp_pos = b - (float)(4u * PPQ);
        }
        return;
    }
    {
        float a = lp_pos, b = a + adv;
        if (b < (float)len) {
            lp_span(a, b, len);
            if (L.metro == LM_ALWAYS)
                lp_clicks(a, b, 0);
            lp_pos = b;
        } else {                         /* round again: a new layer if this pass recorded */
            lp_span(a, (float)len, len);
            if (L.metro == LM_ALWAYS)
                lp_clicks(a, (float)len, 0);
            b -= (float)len;
            if (lp_rec && lp_used) {
                lp_layer++;
                lp_used = 0;
            }
            lp_span(-1.0f, b, len);
            if (L.metro == LM_ALWAYS)
                lp_click(1);
            lp_pos = b;
        }
    }
}

static uint32_t clk_acc;                 /* MIDI clock out: samples toward the next tick, in 24ths of a beat x 16 */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, v;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    static float mix[APP_BLOCK];
    for (i = 0; i < NGRP; i++)
        grp_snd[i] = set.snd[i];
    while (q_r != q_w) {
        BARRIER();
        uint32_t e = q_ev[q_r % QN], type = e >> 16 & 255u, a = e >> 8 & 127u, b = e & 127u, g = e >> 24;
        if (type == QE_ON) {
            voice_on(a, b, g);
            if (g < NGRP)
                loop_rec(1, g, a, b);
        } else if (type == QE_OFF) {
            voice_off(a, g);
            if (g < NGRP)
                loop_rec(0, g, a, 0);
        } else if (type == QE_LOOP)
            loop_cmd(a);
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
    loop_tick(n);
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
        const sound_t *S = &SOUNDS[x->snd];
        float fc, c, inc2, g = x->vel * S->gain;
        if (!x->stage)
            continue;
        fc = S->fc + S->fc_vel * x->vel * x->vel + S->fc_env * x->env;
        c = fm_minf(0.95f, 6.2831853f * fc / SR);
        inc2 = x->inc * S->det;
        for (i = 0; i < n; i++) {
            float s;
            switch (x->stage) {
            case 1:
                x->env += S->att;
                if (x->env >= 1.0f) {
                    x->env = 1.0f;
                    x->stage = 2;
                }
                break;
            case 2:
                x->env += (S->sus - x->env) * S->dec;
                if (S->sus == 0.0f && x->env < 0.0004f) {
                    x->env = 0.0f;
                    x->stage = 0;
                    x->note = -1;
                }
                break;
            default:
                x->env *= S->rel;
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
            switch (S->wave) {
            case W_SAW2:
                s = (x->p1 + x->p2) - 1.0f;
                break;
            case W_SQR2:
                s = ((x->p1 < 0.5f) ? 0.5f : -0.5f) + ((x->p2 < 0.5f) ? 0.5f : -0.5f);
                break;
            case W_FM:
                s = 0.8f * sinp(frac1(x->p1 + 2.0f + S->fm * x->env * x->vel * 0.16f * sinp(x->p2)));
                break;
            case W_ORGAN:
                s = 0.55f * sinp(x->p1) + 0.3f * sinp(frac1(2.0f * x->p1)) + 0.2f * sinp(frac1(3.0f * x->p1));
                break;
            default:                     /* BASS: a square and a sine an octave down */
                s = ((x->p1 < 0.5f) ? 0.5f : -0.5f) + 0.7f * sinp(x->p2);
            }
            x->lp1 += c * (s - x->lp1);
            x->lp2 += c * (x->lp1 - x->lp2);
            mix[i] += x->lp2 * x->env * g;
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
        int32_t m = clamp((int32_t)notes[i] + 12 * (ch == CH_BPADS ? set.boct : set.octave), 0, 127);
        h->note[h->n++] = (uint8_t)m;
        ev_push_g(QE_ON, (uint32_t)m, vel, ch % NGRP);
        midi_note(1, ch, (uint32_t)m, vel);
    }
}
static void notes_off(uint32_t k)
{
    uint32_t i;
    held_t *h = &held[k];
    for (i = 0; i < h->n; i++) {
        ev_push_g(QE_OFF, h->note[i], 0, h->ch % NGRP);
        midi_note(0, h->ch, h->note[i], 0);
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
        }
        cbuf_n[i] = (uint8_t)n;
        ev_push(QE_CSTART, i, vel);
    } else if (p->type == PAD_PROG) {
        uint32_t j;
        for (j = 0; j < NPADS; j++)      /* one progression at a time: a new one stops the one playing */
            if (j != i && pads[j].type == PAD_PROG && (pl[j].on || pad_latched[j])) {
                pad_latched[j] = 0;
                pad_held[j] = 0;
                ev_push(QE_PSTOP, j, 0);
            }
        ev_push(QE_PSTART, i, vel);
    }
}

/* ======================================================================= UI === */
enum { SCR_HOME, SCR_PICK, SCR_LIB, SCR_PAD, SCR_SETUP, SCR_SOUND, SCR_LOOP };
enum { CS_NONE, CS_CHOOSE, CS_NOTES, CS_SCALE, CS_PADS, CS_BASS, CS_SHAPE, CS_CPADS, CS_DONE };
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
    uint8_t l_arm;                       /* LOOP: armed (REC blinks): PLAY gives the intro, then records */
    uint8_t l_reload;
    uint8_t peek, peek_pg;               /* the screen (+1) and page to go back to when the held key is let go */                    /* LOAD: the loop to load once its player stopped */
    uint32_t tap[4];                     /* TAP: the last taps */
    uint8_t tap_n;
    uint8_t lib_t, lib_sel;              /* LIB: the type shown (NCTYPES: the progressions), the pick */
    uint8_t grp;                         /* the group played last: G_* */
    uint8_t page;                        /* the key pages: PG_SEQ, PG_ENV, PG_LFO */
    uint32_t fx_at;                      /* FX went down then (0 up, all ones: held, done) */
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
#define SESS_MAGIC 0x35534553u           /* "SES5" */
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
        plat_store_save(OBJ_NOTES, note_buf, nn ? nn * sizeof(pnote_t) : sizeof(pnote_t)) ||
        plat_store_save(OBJ_LOOP, &L, sizeof L))
        return -1;
    set_dirty = 0;
    return 0;
}
static void loop_load_defaults(void)
{
    __builtin_memset(&L, 0, sizeof L);
    L.magic = LOOP_MAGIC;
    L.bars = 4;
    L.metro = LM_INTRO;
    L.swing = 50;
}
static void loop_load(void)              /* the saved loop (the player stopped first: LOAD) */
{
    lp_run = LR_STOP;
    lp_rec = 0;
    __builtin_memset(lp_left, 0, sizeof lp_left);
    __builtin_memset(lp_open, 0, sizeof lp_open);
    if (plat_store_load(OBJ_LOOP, &L, sizeof L) != (int)sizeof L || L.magic != LOOP_MAGIC || L.n > LMAX ||
        (L.bars != 1u && L.bars != 2u && L.bars != 4u && L.bars != 8u) || L.metro >= LM_N || L.swing < 50u ||
        L.swing > 75u)
        loop_load_defaults();
    lp_layer = 1;
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
        if (SS.d[i].src > SRC_EMPTY || SS.d[i].t >= NCTYPES || SS.f[i].plays >= PL_N || SS.f[i].speed > 6u ||
            SS.f[i].bm >= BM_N || SS.f[i].strum > 40u || SS.f[i].swing > 75u)
            return -1;
    for (i = 0; i < NPADS; i++) {
        desc[i] = SS.d[i];
        pfx[i] = SS.f[i];
        if (pfx[i].len > LEN_MAX)
            pfx[i].len = LEN_MAX;
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
    static const char *const T[] = {"", "WELCOME TO SEED", "PICK YOUR NOTES", "PICK A SCALE", "PLAY THE KEYS",
                                    "PLAY THE BASS", "SHAPE IT", "BASS OR NOT", "YOU'RE SET"};
    return T[s % 9u];
}
static const char *coach_text(uint32_t s)
{
    static const char *const T[] = {"",
                                    "SHOW THESE STEPS? TURN K1 TO CHOOSE, PRESS PLAY.",
                                    "PLAY A FEW NOTES YOU LIKE, ON ANY KEYS. PLAY ONE AGAIN TO TAKE IT OUT.",
                                    "TURN K1 TO PICK A SCALE THAT FITS. PRESS PLAY TO USE IT.",
                                    "THE WHITE KEYS: 8 CHORDS, THEN 8 PROGRESSIONS. PLAY A FEW.",
                                    "THE BRIGHT BLACK KEY IS THE BASS OF YOUR CHORD. PLAY ALONG.",
                                    "ARP CHANGES HOW THEY PLAY. TAP IT A FEW TIMES.",
                                    "THE LAST 4 BLACK KEYS: BASS, NO BASS, NO ROOT, TAP. TRY ONE.",
                                    "THAT'S SEED. HOME + FX ANY TIME FOR THESE STEPS."};
    return T[s % 9u];
}

/* ----------------------------------------------------------------- LOOP --- */
static uint32_t bpb_smp(void) { return (uint32_t)(SR * 60.0f / (float)tempo); }   /* a beat, in samples */
static void loop_send(uint32_t c) { ev_push(QE_LOOP, c, 0); }
static void loop_play(void)              /* PLAY: stop it; armed, the intro and recording; else play it */
{
    if (lp_run != LR_STOP) {
        loop_send(LC_STOP);
        ui.l_arm = 0;
        app_say("LOOP", "STOPPED");
    } else if (ui.l_arm) {
        loop_send(LC_INTRO);
        ui.l_arm = 0;
        app_say("LOOP", "A BAR IN, THEN RECORDING");
    } else if (L.n) {
        loop_send(LC_PLAY);
        app_say("LOOP", "PLAYING");
    } else
        app_say("LOOP", "EMPTY: REC, THEN PLAY");
}

static void sec_text(uint32_t tk, int32_t i, char *t)   /* ticks as seconds at the BPM and the key's speed: "2.40 S" */
{
    float sp = (float)SPEED_PCT[pfx[i].speed % 7u] / 100.0f;
    uint32_t cs = (uint32_t)((float)tk * 6000.0f / ((float)tempo * (float)PPQ) / sp + 0.5f), l;   /* hundredths */
    num_text((int32_t)(cs / 100u), t, 0);
    l = cs_len(t);
    t[l] = '.';
    t[l + 1u] = (char)('0' + cs / 10u % 10u);
    t[l + 2u] = (char)('0' + cs % 10u);
    t[l + 3u] = 0;
    text_cat(t, " S");
}
/* SELECT's list for a group: a sound, or MIDI 1-4 (silent on the FM-1, out on that channel) */
static void sel_name(uint32_t v, char *t)
{
    if (v < NSOUNDS)
        str_set(t, SOUNDS[v].name, 12);
    else {
        str_set(t, "MIDI ", 12);
        num_text((int32_t)(v - NSOUNDS + 1u), t + 5, 0);
    }
}
static void sel_step(uint32_t g, int32_t d)   /* a group's sound or channel, round and round */
{
    uint32_t old = set.snd[g];
    char t[12];
    static const char *const GN[NGRP] = {"CHORD KEYS", "PROGRESSION KEYS", "NOTE KEYS", "KEYBOARD"};
    set.snd[g] = (uint8_t)(((int32_t)old + d % (int32_t)NSEL + (int32_t)NSEL) % (int32_t)NSEL);
    if (old >= NSOUNDS)                  /* off the old channel: its notes ended there */
        plat_midi_send(0x0Bu | (0xB0u | (old - NSOUNDS)) << 8 | 123u << 16, midi_to());
    sel_name(set.snd[g], t);
    app_say(GN[g], t);
    set_dirty = 1;
}

/* ================================================================== input === */
/* the groups: every chord key, every progression key, the note keys. A change goes to the group you played last;
 * hold a key while you make it and it goes to that key alone. LOCKed keys are left out of a group change */
enum { G_CHORD, G_PROG, G_NOTE };
enum { PG_SEQ, PG_ENV, PG_LFO };         /* SEQ: the window. ENV: the notes' shape. LFO: the groove */
static const char *const GRP_NAME[3] = {"CHORD KEYS", "PROGRESSION KEYS", "NOTE KEYS"};
static uint32_t pad_grp(uint32_t i) { return pads[i].type == PAD_PROG ? G_PROG : G_CHORD; }
static uint32_t held_pads(void)          /* the chord and progression keys held now */
{
    uint32_t j, m = 0;
    for (j = 0; j < NPADS; j++)
        if ((ui.keys >> WK[j] & 1u) && pads[j].type != PAD_EMPTY)
            m |= 1u << j;
    return m;
}
static uint32_t held_notes(void)
{
    uint32_t j, m = 0;
    for (j = 0; j < 7u; j++)
        if (ui.keys >> BK[j] & 1u)
            m |= 1u << j;
    return m;
}
static uint32_t grp_pads(uint32_t g)     /* the group's keys, the locked ones left out */
{
    uint32_t j, m = 0;
    for (j = 0; j < NPADS; j++)
        if (pads[j].type != PAD_EMPTY && pad_grp(j) == g && !desc[j].lock)
            m |= 1u << j;
    return m;
}
static uint32_t edit_pads(void)          /* where a change goes: the keys held, else the group (0: the note keys) */
{
    uint32_t h = held_pads();
    if (h)
        return h;
    if (held_notes() || ui.grp == G_NOTE)
        return 0;
    return grp_pads(ui.grp);
}
static int32_t edit_ref(void)            /* the key EDIT shows: one held, the one played last, the group's first */
{
    uint32_t m = held_pads(), j;
    if (!m) {
        if (held_notes() || ui.grp == G_NOTE)
            return -1;
        if (ui.pad >= 0 && pads[ui.pad].type != PAD_EMPTY && pad_grp((uint32_t)ui.pad) == ui.grp)
            return ui.pad;
        m = grp_pads(ui.grp);
        for (j = 0; j < NPADS; j++)
            if (pads[j].type != PAD_EMPTY && pad_grp(j) == ui.grp)
                m |= 1u << j;
    }
    for (j = 0; j < NPADS; j++)
        if (m >> j & 1u)
            return (int32_t)j;
    return -1;
}
static uint32_t cur_bm(void)             /* the bass mode the C pads show: the key EDIT would show's */
{
    int32_t r = edit_ref();
    return r >= 0 ? pfx[r].bm : set.bassm;
}
static void edit_say(const char *what, const char *val)   /* "CHORD KEYS: RHYTHM" / "CHORD 3: RHYTHM" */
{
    uint32_t h = held_pads();
    char t[32];
    if (h && !(h & (h - 1u))) {
        uint32_t j;
        for (j = 0; !(h >> j & 1u); j++)
            ;
        str_set(t, j < 8u ? "CHORD " : "PROG ", sizeof t);
        num_text((int32_t)(j < 8u ? j + 1u : j - 7u), t + cs_len(t), 0);
    } else
    {
        static const char *const SH[3] = {"CHORDS", "PROGS", "NOTES"};
        str_set(t, h ? "HELD KEYS" : SH[ui.grp % 3u], sizeof t);
    }
    text_cat(t, ": ");
    text_cat_n(t, what, sizeof t);
    app_say(t, val);
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), i, now = plat_ms();
    btn_now = btn;
    touch_poll();
    keys = touch_keys();

    if (ui.coach == CS_DONE && ((keys & ~ui.keys) || now - ui.co_at > 4000u))
        coach_end();
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
                key_label(i, t);
                app_say(t, ui.lib_t < NCTYPES ? "A CHORD FROM LIB" : "A PROGRESSION FROM LIB");
            }
            pad_held[i] = 1;
            pad_start(i, vel ? vel : 100u);
            ui.pad = (int32_t)i;
            if (pads[i].type != PAD_EMPTY)
                ui.grp = (uint8_t)pad_grp(i);
            coach_event(CE_APAD);
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit)) {
            pad_held[i] = 0;
            if (!pad_latched[i]) {
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
            vel = vel ? vel : 100u;
            notes_on(k, &m, 1, set.nhum ? human_vel(vel, set.nhum) : vel, CH_BPADS);
            ui.grp = G_NOTE;
            coach_event(CE_BPAD);
            ui.redraw = 1;
        } else if (!(keys & bit) && (ui.keys & bit))
            notes_off(k);
    }
    /* the C pads: BASS, NO BASS, NO ROOT, TAP */
    for (i = 0; i < 4u; i++) {
        uint32_t bit = 1u << BK[7u + i];
        if ((keys & bit) && !(ui.keys & bit)) {
            if (i < 3u) {                /* the bass mode: of the keys held, else of the group played last */
                uint32_t m = held_pads(), j;
                if (!m) {
                    m = ui.grp == G_NOTE ? grp_pads(G_CHORD) | grp_pads(G_PROG) : grp_pads(ui.grp);
                    set.bassm = (uint8_t)i;
                }
                undo_snap();
                for (j = 0; j < NPADS; j++)
                    if (m >> j & 1u) {
                        pfx[j].bm = (uint8_t)i;
                        pad_make(j);
                    }
                set_dirty = 1;
                edit_say("BASS", BM_NAME[i]);
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
        if (i == B_OCTDN || i == B_OCTUP) {   /* the octave: of the keys held, else the group played last */
            if (down) {
                int32_t d = i == B_OCTUP ? 1 : -1, o = 0;
                uint32_t j, ka = 0, kb = 0;
                char t[8];
                if (ui.screen == SCR_PICK) {     /* PICK NOTES: the keyboard's */
                    o = clamp(set.octave + d, -2, 2);
                    set.octave = (int8_t)o;
                    num_text(o, t, 1);
                    app_say("KEYBOARD OCTAVE", t);
                    set_dirty = 1;
                    continue;
                }
                ka = held_pads();
                kb = held_notes();
                if (!ka && !kb) {
                    if (ui.grp == G_NOTE)
                        kb = 1;
                    else {
                        int32_t r = edit_ref();
                        ka = grp_pads(ui.grp);
                        if (r >= 0)      /* the group: all to one octave, the shown key's moved */
                            o = clamp(pfx[r].oct + d, -2, 2);
                        for (j = 0; j < NPADS; j++)
                            if (ka >> j & 1u) {
                                pfx[j].oct = (int8_t)o;
                                if (pads[j].type == PAD_PROG)
                                    pl[j].oct = pfx[j].oct;
                            }
                        ka = 0;
                        set_dirty = 1;
                    }
                }
                for (j = 0; j < NPADS; j++)
                    if (ka >> j & 1u) {  /* a held key: its octave, heard at once */
                        pfx[j].oct = (int8_t)clamp(pfx[j].oct + d, -2, 2);
                        o = pfx[j].oct;
                        if (pads[j].type == PAD_PROG)
                            pl[j].oct = pfx[j].oct;
                        else
                            pad_start(j, 100u);
                        set_dirty = 1;
                    }
                if (kb) {                /* the note keys' octave; the held ones sound again there */
                    set.boct = (int8_t)clamp(set.boct + d, -2, 2);
                    o = set.boct;
                    set_dirty = 1;
                    for (j = 0; j < 7u; j++)
                        if (held_notes() >> j & 1u) {
                            uint8_t m = (uint8_t)clamp(deg_pitch((int32_t)j), 0, 127);
                            notes_off(BK[j]);
                            notes_on(BK[j], &m, 1, 100u, CH_BPADS);
                        }
                    if (!ka) {
                        num_text(o, t, 1);
                        app_say("NOTE KEYS: OCTAVE", t);
                        ui.redraw = 1;
                        continue;
                    }
                }
                num_text(o, t, 1);
                edit_say("OCTAVE", t);
                ui.redraw = 1;
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
                        app_say("STEPS", "HOME + FX FOR THEM AGAIN");
                    }
                    if (ui.l_arm) {
                        ui.l_arm = 0;
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
                    app_say("STEPS OFF", "HOME + FX FOR THEM");
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
                app_say("KEYS CLEARED", "ARP UNDOES IT");
            } else if (ui.screen == SCR_PICK) {
                pick_use();
                coach_event(CE_KEYSET);
            } else if (ui.screen == SCR_LIB) {   /* hear the pick */
                pad_stop(PREVIEW);
                if (ui.lib_t < NCTYPES) {
                    uint32_t k;
                    pad_t *p = &pads[PREVIEW];
                    chord_into(p, ui.lib_sel, ui.lib_t, set.bassm);
                    p->type = PAD_PROG;
                    p->pn = 0;
                    p->len = (uint16_t)(PPQ * 2u);
                    for (k = 0; k < p->n; k++)
                        ph_note(PREVIEW, p->note[k], 0, PPQ * 2u - 10u, 100);
                } else
                {
                    pfx_default(&pfx[PREVIEW]);
                    prog_into(PREVIEW, ui.lib_sel, set.type, set.plays);
                }
                ev_push(QE_PSTART, PREVIEW, 100);
            } else
                loop_play();
        } else if (i == B_SEQ) {         /* SEQ: what the keys hold: your notes, the scale, the root, the type */
            if (ui.screen == SCR_PICK)
                ui.screen = SCR_HOME;
            else {
                pick_enter();
                app_say("SEQ", "PLAY NOTES YOU LIKE");
            }
        } else if (i == B_SEL) {         /* LIB */
            ui.screen = ui.screen == SCR_LIB ? SCR_HOME : SCR_LIB;
        } else if (i == B_EDIT || i == B_ENV || i == B_LFO) {   /* EDIT: pad edit (the window); SHIFT + EDIT: LOCK */
            uint8_t pg = i == B_EDIT ? PG_SEQ : i == B_ENV ? PG_ENV : PG_LFO;
            if (i == B_EDIT && shift()) {
                if (ui.pad < 0 || pads[ui.pad].type == PAD_EMPTY)
                    app_say("LOCK", "PLAY A KEY FIRST");
                else {
                    char t[24];
                    desc[ui.pad].lock = (uint8_t)!desc[ui.pad].lock;
                    if (!desc[ui.pad].lock)
                        pad_make((uint32_t)ui.pad);
                    key_label((uint32_t)ui.pad, t);
                    app_say(t, desc[ui.pad].lock ? "LOCKED: KEPT AS IT IS" : "UNLOCKED");
                    set_dirty = 1;
                }
            } else if (ui.screen == SCR_PAD && ui.page == pg) {
                ui.screen = SCR_HOME;
                ui.peek = 0;
            } else {
                ui.screen = SCR_PAD;
                ui.page = pg;
                ui.peek = 0;
            }
        } else if (i == B_ARP) {         /* ARP: the next RHYTHM for the group; SHIFT + ARP: UNDO */
            if (shift()) {
                if (ui.screen == SCR_LOOP) {
                    loop_send(LC_UNDO);
                    app_say("LOOP", L.n ? "LAST LAYER OFF" : "NOTHING TO UNDO");
                } else
                    undo_swap();
            } else {
                /* the keys held, else the chord or progression group played last (note keys have no rhythm) */
                uint32_t hp = held_pads(), m, j, g;
                int32_t r = -1;
                if (hp)
                    m = hp;
                else {
                    g = ui.grp != G_NOTE ? ui.grp : ui.pad >= 0 && pads[ui.pad].type != PAD_EMPTY ? pad_grp((uint32_t)ui.pad)
                                                                                                    : G_CHORD;
                    m = grp_pads(g);
                    ui.grp = (uint8_t)g;
                    if (ui.pad >= 0 && (m >> ui.pad & 1u))
                        r = ui.pad;
                }
                for (j = 0; j < NPADS && r < 0; j++)
                    if (m >> j & 1u)
                        r = (int32_t)j;
                if (r < 0)
                    app_say("RHYTHM", "PLAY A CHORD OR PROGRESSION KEY");
                else {
                    uint8_t nv = (uint8_t)((pfx[r].plays + 1u) % PL_N);
                    for (j = 0; j < NPADS; j++)
                        if (m >> j & 1u) {
                            pfx[j].plays = nv;
                            if (pads[j].type == PAD_PROG)
                                pad_make(j);
                        }
                    set_dirty = 1;
                    edit_say("RHYTHM", PLAYS_NAME[nv]);
                    coach_event(CE_SHAPE);
                }
            }
        } else if (i == B_REC) {
            if (shift()) {               /* SHIFT + REC: the loop cleared */
                loop_send(LC_CLEAR);
                ui.l_arm = 0;
                app_say("LOOP", "CLEARED");
            } else if (ui.screen != SCR_LOOP) {   /* the loop window, armed */
                ui.screen = SCR_LOOP;
                if (lp_run == LR_STOP) {
                    ui.l_arm = 1;
                    app_say("LOOP", "ARMED: PLAY STARTS IT");
                }
            } else if (lp_run == LR_PLAY) {       /* playing: recording on and off */
                loop_send(lp_rec ? LC_RECOFF : LC_RECON);
                app_say("LOOP", lp_rec ? "PLAYING" : "RECORDING");
            } else if (lp_run == LR_STOP) {
                ui.l_arm = (uint8_t)!ui.l_arm;
                app_say("LOOP", ui.l_arm ? "ARMED: PLAY STARTS IT" : "NOT ARMED");
            }
        }
        ui.redraw = 1;
    }
    {                                    /* SAVE held 0.8 s: LOAD */
        if (ui.save_at && ui.save_at != 0xFFFFFFFFu && now - ui.save_at >= 800u) {
            ui.save_at = 0xFFFFFFFFu;
            players_stop();
            loop_send(LC_STOP);          /* the loop: loaded once the player has stopped (app_frame) */
            ui.l_arm = 0;
            ui.l_reload = 1;
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
            if (k1)
                ui.co_pick = (uint8_t)clamp(ui.co_pick + k1, CO_OFF, CO_ALWAYS);
            es = k1 = k2 = k3 = k4 = ek = ea = 0;
        }
        /* SELECT, PRESET, ALGO: only with a key held. SELECT: the group's sound or MIDI channel. PRESET: the START
         * line, ALGO: the END line of the window the group plays, smooth (HOME held: the finest) */
        if ((es || ek || ea) && ui.screen != SCR_PICK) {
            uint32_t hp = held_pads(), hn = held_notes(), j, r = 0;
            int seqp = ui.screen == SCR_PAD && ui.page == PG_SEQ && !hp && !hn && edit_ref() >= 0;   /* SEQ: no hold */
            if (hp)
                while (!(hp >> r & 1u))
                    r++;
            else if (seqp)
                r = (uint32_t)edit_ref();
            if (es && (hp || hn))
                sel_step(hp ? (r < 8u ? 0u : 1u) : 2u, es);
            if ((ek || ea) && (hp || seqp)) {   /* the window's lines, smooth: tiny steps, bigger the faster you turn */
                static uint32_t at_k, at_a;
                uint32_t now2 = plat_ms(), dtk = now2 - (ek ? at_k : at_a);
                int32_t full_r = seq_len(r), stp = full_r / 300 > 1 ? full_r / 300 : 1, d = ek ? ek : ea;
                int in_edit = ui.screen == SCR_PAD && ui.page == PG_SEQ;
                uint32_t g = pad_grp(r), m = in_edit ? (hp ? hp : 1u << r) : grp_pads(g) | hp;   /* EDIT: the selected key */
                int hold_chord = pads[r].type == PAD_CHORD && (pfx[r].plays == RH_HOLD);
                char t[16];
                if (shift())
                    stp = 1;
                else if (dtk < 40u)
                    stp *= 6;
                else if (dtk < 110u)
                    stp *= 3;
                if (ek)
                    at_k = now2;
                else
                    at_a = now2;
                if (ea && hold_chord)
                    app_say("END", "n/a: A HELD CHORD PLAYS TILL YOU LET GO");
                else {
                    int32_t ref_st = pfx[r].st + d * stp, ref_en = win_end(r) + d * stp;
                    for (j = 0; j < NPADS; j++)
                        if ((m >> j & 1u) && pads[j].type != PAD_EMPTY && pad_grp(j) == g) {
                            int32_t full = seq_len(j), q = full / 4;
                            int hc = pads[j].type == PAD_CHORD && pfx[j].plays == RH_HOLD;
                            if (ek)              /* START: a quarter before it .. the END */
                                pfx[j].st = (int16_t)clamp(ref_st, -q, hc ? 0 : win_end(j) - 12);
                            if (ea && !hc) {     /* END: the START .. a quarter after it */
                                int32_t e = clamp(ref_en, pfx[j].st + 12, full + q);
                                pfx[j].sl = (int16_t)(e == full ? 0 : e);
                            }
                            cp[j].st = (float)pfx[j].st;
                            cp[j].wl = (float)pwin_len(j);
                        }
                    sec_text(pwin_len(r), r, t);
                    if (in_edit) {               /* EDIT: the key's own name */
                        char kl[32];
                        key_label(r, kl);
                        text_cat(kl, ek ? ": START" : ": END");
                        app_say(kl, t);
                    } else
                        edit_say(ek ? "START" : "END", t);
                }
                set_dirty = 1;
            } else if ((ek || ea) && !hp)
                app_say(ek ? "START" : "END", hn ? "NOTE KEYS: n/a" : "HOLD A CHORD OR PROGRESSION KEY");
            else if (es && !hp && !hn)
                app_say("SOUND", "HOLD A KEY AND TURN SELECT");
            es = ek = ea = 0;
            ui.redraw = 1;
        }
        if (ui.screen == SCR_PICK) {     /* EDIT: K1 the scale (of those that fit), K2 start again, K3 the root,
                                          * K4 the chord TYPE */
            char t[24];
            if (k1 && fd_cnt)
                fd_sel = (uint8_t)clamp((int32_t)fd_sel + k1, 0, (int32_t)fd_cnt - 1);
            else if (k1) {               /* no notes picked: every scale */
                set.scale = (uint8_t)(((int32_t)set.scale + k1 % (int32_t)NSCALES + (int32_t)NSCALES) % (int32_t)NSCALES);
                rekey();
                key_name(set.root, set.scale, t);
                app_say("KEY", t);
            }
            if (k2) {
                fd_n = 0;
                fd_pcs = 0;
                fd_rank();
            }
            if (k3) {
                set.root = (uint8_t)(((int32_t)set.root + k3 % 12 + 12) % 12);
                rekey();
                key_name(set.root, set.scale, t);
                app_say("KEY", t);
            }
            if (k4) {
                set.type = (uint8_t)clamp(set.type + k4, 0, (int32_t)NCTYPES - 1);
                rekey();
                app_say("TYPE", CTYPE_NAME[set.type]);
            }
            es = k1 = k2 = k3 = k4 = ek = ea = 0;
        } else if (ui.screen == SCR_LIB) {
            if (k1) {                    /* the type shown: TRIADS .. ADD9, PROGRESSIONS */
                ui.lib_t = (uint8_t)clamp(ui.lib_t + k1, 0, (int32_t)NCTYPES);
                ui.lib_sel = 0;
            }
            if (k2)                      /* the pick */
                ui.lib_sel = (uint8_t)clamp((int32_t)ui.lib_sel + k2, 0, ui.lib_t < NCTYPES ? 6 : (int32_t)NPROGS - 1);
            es = k1 = k2 = 0;
        } else if (ui.screen == SCR_PAD) {   /* the key pages. ENV: K1 LENGTH, K2 HUMANIZE, K3 VELOCITY, K4 STRUM.
                                              * LFO: K1 SWING, K2 SPEED, K3 INVERSION. The group played last, all
                                              * set to the one value; hold a key: its group the same */
            int32_t r = edit_ref();
            uint32_t m = edit_pads(), j;
            char t[12];
            if (ui.page == PG_SEQ) {
                if (k1 || k2 || k3 || k4)
                    app_say("EDIT", "PRESET: START. ALGO: END");
            } else if (r < 0) {          /* the note keys: HUMANIZE only */
                if (k2 && ui.page == PG_ENV) {
                    set.nhum = (uint8_t)clamp(set.nhum + 5 * k2, 0, 50);
                    num_text(set.nhum * 2, t, 0);
                    text_cat(t, "%");
                    app_say("NOTE KEYS: HUMANIZE", set.nhum ? t : "OFF");
                    set_dirty = 1;
                } else if (k1 || k2 || k3 || k4)
                    app_say("NOTE KEYS", "ENV K2 HUMANIZE ONLY");
            } else if (k1 || k2 || k3 || k4) {
                pfx_t v = pfx[r];
                uint32_t remake = 0, f_len = 0, f_hum = 0, f_vel = 0, f_str = 0, f_sw = 0, f_spd = 0, f_inv = 0;
                const char *what = "";
                t[0] = 0;
                if (ui.page == PG_ENV) {
                    if (k1) {
                        v.len = (int8_t)clamp(v.len + k1, -5, LEN_MAX);
                        what = "LENGTH";
                        str_set(t, LEN_NAME[v.len + 5], sizeof t);
                        f_len = 1;
                    } else if (k2) {
                        v.human = (uint8_t)clamp(v.human + 5 * k2, 0, 50);
                        what = "HUMANIZE";
                        num_text(v.human * 2, t, 0);
                        text_cat(t, "%");
                        f_hum = 1;
                    } else if (k3) {
                        v.vel = (uint8_t)(v.vel ? clamp((int32_t)v.vel + 8 * k3, 0, 127) : (k3 > 0 ? 40 : 0));
                        what = "VELOCITY";
                        if (v.vel)
                            num_text(v.vel, t, 0);
                        else
                            str_set(t, "AS PLAYED", sizeof t);
                        f_vel = 1;
                    } else if (k4) {
                        v.strum = (uint8_t)clamp(v.strum + k4, 0, 40);
                        what = "STRUM";
                        num_text((int32_t)strum_ms(&v), t, 0);
                        text_cat(t, " MS");
                        f_str = remake = 1;
                    }
                } else {
                    if (k1) {
                        v.swing = (uint8_t)clamp(v.swing + k1, 50, 75);
                        what = "SWING";
                        num_text(v.swing, t, 0);
                        text_cat(t, "%");
                        f_sw = 1;
                    } else if (k2) {
                        v.speed = (uint8_t)clamp(v.speed + k2, 0, 6);
                        what = "SPEED";
                        num_text(SPEED_PCT[v.speed], t, 0);
                        text_cat(t, "%");
                        f_spd = 1;
                    } else if (k3) {
                        v.inv = (int8_t)clamp(v.inv + k3, -3, 3);
                        what = "INVERSION";
                        num_text(v.inv, t, 1);
                        f_inv = 1;
                    }
                }
                for (j = 0; j < NPADS; j++)
                    if (m >> j & 1u) {   /* overwrite: every key in it gets the value */
                        pfx_t *f = &pfx[j];
                        if (f_len)
                            f->len = v.len;
                        if (f_hum)
                            f->human = v.human;
                        if (f_vel)
                            f->vel = v.vel;
                        if (f_str)
                            f->strum = v.strum;
                        if (f_sw)
                            f->swing = v.swing;
                        if (f_spd)
                            f->speed = v.speed;
                        if (f_inv)
                            f->inv = v.inv;
                        if (remake && pads[j].type == PAD_PROG)
                            pad_make(j);
                    }
                if (*what) {
                    set_dirty = 1;
                    edit_say(what, t);
                }
            }
            es = k1 = k2 = k3 = k4 = ek = ea = 0;
        } else if (ui.screen == SCR_LOOP) {   /* K1 METRONOME, K2 BARS, K3 QUANTIZE, K4 SWING */
            static const uint8_t BARS[4] = {1, 2, 4, 8};
            static const char *const MN[LM_N] = {"OFF", "INTRO ONLY", "ALWAYS"};
            char t[12];
            if (k1) {
                L.metro = (uint8_t)clamp(L.metro + k1, 0, LM_N - 1);
                app_say("METRONOME", MN[L.metro]);
            }
            if (k2) {
                uint32_t q;
                for (q = 0; q < 3u && BARS[q] != L.bars; q++)
                    ;
                q = (uint32_t)clamp((int32_t)q + k2, 0, 3);
                L.bars = BARS[q];
                num_text(L.bars, t, 0);
                text_cat(t, L.bars == 1u ? " BAR" : " BARS");
                app_say("LOOP LENGTH", t);
            }
            if (k3) {
                L.quant = k3 > 0;
                app_say("QUANTIZE", L.quant ? "ON: 16THS" : "OFF");
            }
            if (k4) {
                L.swing = (uint8_t)clamp(L.swing + k4, 50, 75);
                num_text(L.swing, t, 0);
                text_cat(t, "%");
                app_say("SWING", L.swing > 50u ? t : "STRAIGHT");
            }
            if (k1 || k2 || k3 || k4)
                set_dirty = 1;
            es = k1 = k2 = k3 = k4 = ek = ea = 0;
        } else if (ui.screen == SCR_SOUND) {   /* each group's sound or MIDI channel; the keyboard's too */
            int32_t kk[NGRP] = {k1, k2, k3, k4};
            uint32_t g;
            for (g = 0; g < NGRP; g++)
                if (kk[g])
                    sel_step(g, kk[g]);
            es = k1 = k2 = k3 = k4 = 0;
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
        if (es || k1 || k2 || k3 || k4 || ek || ea)
            ui.redraw = 1;
    }
    if ((btn & 1u << B_GLO) && !(ui.btn & 1u << B_GLO) && ui.coach != CS_CHOOSE) {   /* GLO: STOP ALL */
        all_notes_off();
        loop_send(LC_STOP);
        ui.l_arm = 0;
        app_say("STOP ALL", "EVERYTHING OFF");
        ui.redraw = 1;
    }
    {                                    /* FX: a tap, SOUND; held 0.8 s, SETUP; SHIFT + FX, COACH */
        if (btn & 1u << B_FX) {
            if (!ui.fx_at) {
                ui.fx_at = now ? now : 1u;
                if (shift()) {
                    ui.fx_at = 0xFFFFFFFFu;
                    ui.shift_used = 1;
                    coach_start();
                }
            } else if (ui.fx_at != 0xFFFFFFFFu && now - ui.fx_at >= 800u) {
                ui.fx_at = 0xFFFFFFFFu;
                if (ui.coach != CS_CHOOSE)
                    ui.screen = ui.screen == SCR_SETUP ? SCR_HOME : SCR_SETUP;
                ui.redraw = 1;
            }
        } else {
            if (ui.fx_at && ui.fx_at != 0xFFFFFFFFu && ui.coach != CS_CHOOSE) {
                ui.screen = ui.screen == SCR_SOUND ? SCR_HOME : SCR_SOUND;
                ui.redraw = 1;
            }
            ui.fx_at = 0;
        }
    }
    if (ui.peek && !held_pads()) {       /* the held key let go: back where it was */
        ui.screen = (uint8_t)(ui.peek - 1u);
        ui.page = ui.peek_pg;
        ui.peek = 0;
        ui.redraw = 1;
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
        plat_lights(blink ? 1u << B_PLAY : 0u, 0, 1u << B_GLO, 0, 1);
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
        btn = 1u << B_SEQ;
        if (fd_cnt && (ui.coach != CS_SCALE || blink))
            btn |= 1u << B_PLAY;
        plat_lights(btn | ui.btn, bright, 1u << B_GLO, dim, 1);
        plat_lights_mid(0, 0);
        return;
    }
    for (i = 0; i < 16u; i++) {
        uint32_t bit = 1u << WK[i];
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
        int on = i < 3u ? cur_bm() == i : (int)((now * (uint32_t)tempo / 60u) % 1000u < 120u);   /* TAP: the beat */
        if (on || (ui.keys & bit) || (ui.coach == CS_CPADS && blink))
            bright |= bit;
        else
            dim |= bit;
    }
    if (set_dirty)
        btn |= 1u << B_SAVE;
    if (ui.coach == CS_SHAPE && blink)
        btn |= 1u << B_ARP;
    if ((lp_rec && (lp_run == LR_PLAY || fast)) || (ui.l_arm && blink))   /* REC: recording, the intro, armed */
        btn |= 1u << B_REC;
    if (lp_run != LR_STOP)
        btn |= 1u << B_PLAY;
    btn |= 1u << (ui.screen == SCR_LIB ? B_SEL : ui.screen == SCR_PAD ? (ui.page == PG_SEQ ? B_EDIT : ui.page == PG_ENV ? B_ENV : B_LFO)
                  : ui.screen == SCR_SOUND ? B_FX
                  : ui.screen == SCR_SETUP ? B_FX : ui.screen == SCR_LOOP ? B_REC : ui.screen == SCR_PICK ? B_SEQ
                  : B_HOME);
    plat_lights(btn | ui.btn, bright, 1u << B_GLO, dim, 1);
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
        int on = i < 3u && cur_bm() == i;
        cv_rect(x, 50, 56, 16, on ? EARTH_C : ES_SLATE);
        cv_text(x + (56 - text_w(&FONT_XS, CN[i])) / 2, 52, &FONT_XS, CN[i], C_WHITE);
    }
    {
        static const char *const LBL[4] = {"KEY", "TYPE", "RHYTHM", "TAP BPM"};
        char v[4][12];
        int32_t r = edit_ref();
        spell(set.root, letter_of(set.root, set.scale), v[0]);
        text_cat(v[0], " ");
        text_cat_n(v[0], SCALE_SHORT[set.scale], sizeof v[0]);
        str_set(v[1], CTYPE_NAME[set.type], sizeof v[1]);
        str_set(v[2], r >= 0 ? PLAYS_NAME[pfx[r].plays % PL_N] : "n/a", sizeof v[2]);
        num_text((int32_t)tempo, v[3], 0);
        bottom_bar(72, LBL, v);
    }
    cv_blit(0, 120);
}

static void pad_heading(char *t)         /* "PAD 3  Em7" */
{
    const pad_t *p = &pads[ui.pad];
    key_label((uint32_t)ui.pad, t);
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
    top_bar(lp_rec && lp_run ? "SEED  LOOP REC" : lp_run ? "SEED  LOOP" : "SEED", lp_rec && lp_run ? FIRE_C : EARTH_C);
    if (p && p->type == PAD_CHORD) {
        char t[24], r[2], rn[4];
        const char *acc;
        int32_t w;
        spell(p->root_pc, tonic_letter() + p->deg, rn);
        r[0] = rn[0];
        r[1] = 0;
        acc = rn + 1;
        key_label((uint32_t)ui.pad, t);
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
        char t[24];
        key_label((uint32_t)ui.pad, t);
        if (desc[ui.pad].src == SRC_REC)
            text_cat(t, ", RECORDED");
        cv_text(8, 32, &FONT_B, t, LAB_LABEL);
        text_c(56, &FONT_B, p->title, C_WHITE);
    } else {
        text_c(40, &FONT_M, "PLAY A KEY", LAB_LABEL);
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
    top_bar("SEQ: YOUR NOTES", WATER_C);
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
        cv_text(6, 84, &FONT_XS, fd_cnt ? "K2: START AGAIN" : "NO SCALE HOLDS ALL OF THEM", LAB_LABEL);
    }
    cv_blit(0, 0);
}
static void draw_pick_bottom(void)
{
    uint32_t i, first;
    lab_begin();
    cv_text(6, 4, &FONT_XS, "SCALES THAT FIT (K1, THEN PLAY)", LAB_LABEL);
    first = fd_sel > 2u ? fd_sel - 2u : 0u;
    for (i = 0; i < 3u && first + i < fd_cnt; i++) {
        char t[24];
        uint32_t k = first + i;
        int32_t y = 20 + (int32_t)i * 24;
        key_name(fd_list[k] >> 4, fd_list[k] & 15u, t);
        if (k == fd_sel)
            cv_rect(2, y - 2, 236, 22, AIR_C);
        cv_text(10, y, &FONT_B, t, k == fd_sel ? ES_INK : C_WHITE);
    }
    if (!fd_cnt)
        text_c(44, &FONT_B, fd_n ? "TAKE A NOTE OUT" : "K1: EVERY SCALE", LAB_LABEL);
    {
        char t[40], k[24];
        key_name(set.root, set.scale, k);
        str_set(t, "K3 ROOT: ", sizeof t);
        text_cat_n(t, k, sizeof t);
        text_cat_n(t, ". K4 TYPE: ", sizeof t);
        text_cat_n(t, CTYPE_NAME[set.type], sizeof t);
        cv_rect(0, 94, 240, 1, ES_SLATE);
        text_c(100, fit_font(t, 236, &FONT_XS), t, AIR_C);
    }
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
    text_c(84, &FONT_XS, "K2: THE PICK", LAB_LABEL);
    text_c(98, &FONT_XS, "PLAY: HEAR IT. A WHITE KEY: PUT IT THERE", LAB_LABEL);
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

/* a pad's notes as the web's pad edit draws them, a playhead across them while it plays. A progression: its bars.
 * A chord: one bar of how it plays (CHORD, STRUM, ARP, STABS), at its speed */
#define RN 64u
static void draw_roll(uint32_t i, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    const pad_t *p = &pads[i];
    const pfx_t *f = &pfx[i];
    float st[RN], du[RN], W, head = -1.0f;
    int16_t pi[RN];
    uint32_t n = 0, k, lo = 127, hi = 0, bars = 1;
    int32_t rh;
    if (p->type == PAD_PROG) {
        W = (float)p->len;
        bars = p->bars ? p->bars : 1u;
        for (k = 0; k < p->pn && k < MAXPH && n < RN; k++, n++) {
            st[n] = swing_t((float)ph[i][k].s, f->swing);
            du[n] = f->len && !LEN_LEGATO(f->len) ? (float)LEN_TK[f->len + 5] : (float)ph[i][k].d;
            pi[n] = (int16_t)(ph[i][k].p + 12 * f->oct);
        }
        if (pl[i].on)
            head = pl[i].pos + (float)f->st;
    } else if (p->type == PAD_CHORD) {
        float spd = (float)SPEED_PCT[f->speed % 7u] / 100.0f, gap = (float)strum_ms(f) * (float)tempo * 96.0f / 60000.0f;
        uint32_t cn = p->n < MAXC ? p->n : MAXC, j;
        int32_t v[MAXC];
        int32_t inv = f->inv;
        W = 384.0f;
        for (k = 0; k < cn; k++)
            v[k] = p->note[k];
        while (inv > 0 && cn > 1u) {
            int32_t l = v[0] + 12;
            for (k = 0; k + 1u < cn; k++)
                v[k] = v[k + 1u];
            v[cn - 1u] = l;
            inv--;
        }
        while (inv < 0 && cn > 1u) {
            int32_t t = v[cn - 1u] - 12;
            for (k = cn - 1u; k > 0; k--)
                v[k] = v[k - 1u];
            v[0] = t;
            inv++;
        }
        for (k = 0; k < cn; k++)
            v[k] += 12 * f->oct;
        if (RH_ARP(f->plays) && cn) {
            uint32_t dir = f->plays - RH_ARPUP;
            float step = 48.0f, t = 0.0f;
            for (j = 0; t < W && n < RN; j++, n++) {
                float sl = grid_t(j + 1u, step, f->swing) - grid_t(j, step, f->swing);
                t = grid_t(j, step, f->swing);
                st[n] = t;
                du[n] = f->len && !LEN_LEGATO(f->len) ? fm_minf((float)LEN_TK[f->len + 5], sl) : sl * 0.9f;
                pi[n] = (int16_t)v[dir == AR_RANDOM ? j % cn : arp_pick(dir, j, cn)];
            }
        } else if (f->plays >= RH_STABS && f->plays <= RH_SYNC && cn) {
            float step = 24.0f, t = 0.0f;
            for (j = 0; t < W; j++) {
                float sl = 0.0f;
                t = grid_t(j, step, f->swing);
                if (t >= W)
                    break;
                if (RH_PAT[f->plays] >> (j % 16u) & 1u)
                    for (k = 0; k < cn && n < RN; k++, n++) {
                        st[n] = t;
                        du[n] = f->len ? (LEN_LEGATO(f->len) ? W - t : (float)LEN_TK[f->len + 5]) : step * 1.5f;
                        pi[n] = (int16_t)v[k];
                    }
                (void)sl;
            }
        } else
            for (k = 0; k < cn && n < RN; k++, n++) {
                st[n] = (float)k * gap;
                du[n] = W - st[n];
                pi[n] = (int16_t)v[k];
            }
        if (cp[i].on && f->plays != RH_HOLD)
            head = cp[i].u + cp[i].st;
        else if (cp[i].on)               /* held: from the START line, then it stays at the bar's end */
            head = fm_minf((float)f->st + (float)cp[i].t * (float)tempo * (float)PPQ * spd / (60.0f * SR), W - 1.0f);
    } else
        return;
    {
        /* the view: the sequence, and the lines wherever they are (a quarter past either edge at most) */
        int hc = p->type == PAD_CHORD && f->plays == RH_HOLD;
        float D0 = fm_minf(0.0f, (float)f->st), D1 = hc ? W : fm_maxf(W, (float)win_end(i)), DW = D1 - D0;
        float a = (float)f->st, e = hc ? W : (float)win_end(i);
#define RX(t) (x0 + (int32_t)(((t) - D0) / DW * (float)w))
        int32_t xa = RX(a), xe = RX(e);
        cv_rect(x0, y0, w, h, ES_SLATE);
        cv_rect(RX(0.0f), y0 + h - 2, RX(W) - RX(0.0f), 2, ES_PURPLE);   /* the sequence itself */
        if (xa > x0)                     /* outside the window: dark, silent */
            cv_rect(x0, y0, xa - x0, h, ES_INK);
        if (xe < x0 + w)
            cv_rect(xe, y0, x0 + w - xe, h, ES_INK);
        for (k = 1; k < bars; k++)
            cv_rect(RX(W * (float)k / (float)bars), y0, 1, h, ES_PURPLE);
        cv_rect(xa, y0, 2, h, AIR_C);
        if (!hc)
            cv_rect(xe > x0 + 1 ? xe - 2 : xe, y0, 2, h, AIR_C);
        if (!n || W <= 0.0f)
            return;
        for (k = 0; k < n; k++) {
            if ((uint32_t)pi[k] < lo)
                lo = (uint32_t)pi[k];
            if ((uint32_t)pi[k] > hi)
                hi = (uint32_t)pi[k];
        }
        rh = (h - 4) / (int32_t)(hi - lo + 1u);
        if (rh > 8)
            rh = 8;
        if (rh < 1)
            rh = 1;
        for (k = 0; k < n; k++) {
            int32_t x = RX(st[k]), xw = (int32_t)(du[k] / DW * (float)w);
            int32_t y = y0 + h - 4 - ((int32_t)((uint32_t)pi[k] - lo) + 1) * rh;
            int on = head >= st[k] && head < st[k] + du[k], out = st[k] < a || st[k] >= e;
            if (x >= x0 + w)
                continue;
            if (xw < 2)
                xw = 2;
            if (x + xw > x0 + w)
                xw = x0 + w - x;
            cv_rect(x, y, xw, rh > 2 ? rh - 1 : rh, out ? ES_PURPLE : (uint32_t)pi[k] == lo ? EARTH_C : on ? C_WHITE : WATER_C);
        }
        if (head >= D0 && head < D1)
            cv_rect(RX(head), y0, 2, h, FIRE_C);
#undef RX
    }
}
static void draw_pad(void)               /* EDIT: the group played last (or the keys held), as the web's pad edit */
{
    int32_t r = edit_ref();
    uint32_t h = held_pads();
    char t[32], v[4][12], w[3][12];
    static const char *const LBE[4] = {"K1 LENGTH", "K2 HUMAN", "K3 VELOCITY", "K4 STRUM"};
    static const char *const LBL_[4] = {"K1 SWING", "K2 SPEED", "K3 INVERT", "K4"};
    static const char *const LB2[3] = {"SELECT SOUND", "PRESET START", "ALGO END"};
    static const char *const PGN[3] = {"EDIT ", "ENV ", "LFO "};
    const char *const *LBL = ui.page == PG_LFO ? LBL_ : LBE;
    uint32_t k;
    lab_begin();
    str_set(t, PGN[ui.page % 3u], sizeof t);
    if (h)
        text_cat(t, h & (h - 1u) ? "HELD KEYS" : r < 8 ? "CHORD KEY" : "PROG KEY");
    else
        text_cat(t, ui.grp == G_CHORD ? "CHORD KEYS" : ui.grp == G_PROG ? "PROG KEYS" : "NOTE KEYS");
    top_bar(t, ui.grp == G_NOTE && !h ? WATER_C : ui.grp == G_PROG || (h && r >= 8) ? AIR_C : EARTH_C);
    if (r < 0) {                         /* the note keys */
        text_c(34, &FONT_XS, "THE NOTES OF YOUR KEY, ONE A KEY", LAB_LABEL);
        draw_bpads(52);
        cv_blit(0, 0);
        lab_begin();
        for (k = 0; k < 4u; k++)
            str_set(v[k], "n/a", sizeof v[k]);
        if (ui.page == PG_ENV) {
            if (set.nhum) {
                num_text(set.nhum * 2, v[1], 0);
                text_cat(v[1], "%");
            } else
                str_set(v[1], "OFF", sizeof v[1]);
        }
        if (ui.page != PG_SEQ)
            bottom_bar(8, LBL, v);
        text_c(56, &FONT_XS, "THEY SOUND WHILE HELD", LAB_LABEL);
        text_c(72, &FONT_XS, "OCT- / OCT+: THEIR OCTAVE. FX: THEIR SOUND", LAB_LABEL);
        cv_blit(0, 120);
        return;
    }
    {
        const pfx_t *f = &pfx[r];
        uint32_t ms = strum_ms(f);
        str_set(t, r < 8 ? "CHORD " : "PROG ", sizeof t);
        num_text(r < 8 ? r + 1 : r - 7, t + cs_len(t), 0);
        text_cat(t, "  ");
        if (pads[r].type == PAD_CHORD) {
            char rn[4];
            spell(pads[r].root_pc, tonic_letter() + pads[r].deg, rn);
            text_cat(t, rn);
            text_cat(t, pads[r].qual);
        } else
            text_cat_n(t, pads[r].title, sizeof t);
        if (desc[r].lock)
            text_cat(t, "  LOCKED");
        text_c(28, fit_font(t, 230, &FONT_M), t, C_WHITE);
        draw_roll((uint32_t)r, 6, 48, 228, 68);
        cv_blit(0, 0);
        lab_begin();
        if (ui.page == PG_ENV) {
            str_set(v[0], LEN_NAME[f->len + 5], sizeof v[0]);
            if (f->human) {
                num_text(f->human * 2, v[1], 0);
                text_cat(v[1], "%");
            } else
                str_set(v[1], "OFF", sizeof v[1]);
            if (f->vel)
                num_text(f->vel, v[2], 0);
            else
                str_set(v[2], "AS PLAYED", sizeof v[2]);
            if (ms) {
                num_text((int32_t)ms, v[3], 0);
                text_cat(v[3], " MS");
            } else
                str_set(v[3], "OFF", sizeof v[3]);
        } else {
            if (f->swing > 50u) {
                num_text(f->swing, v[0], 0);
                text_cat(v[0], "%");
            } else
                str_set(v[0], "STRAIGHT", sizeof v[0]);
            num_text(SPEED_PCT[f->speed % 7u], v[1], 0);
            text_cat(v[1], "%");
            if (pads[r].type == PAD_CHORD)
                num_text(f->inv, v[2], f->inv != 0);
            else
                str_set(v[2], "n/a", sizeof v[2]);
            str_set(v[3], "", sizeof v[3]);
        }
        if (ui.page != PG_SEQ)
            bottom_bar(8, LBL, v);
        else {                           /* SEQ: the rhythm (ARP) on top */
            text_c(10, &FONT_XS, "ARP: RHYTHM", LAB_LABEL);
            text_c(24, &FONT_B, PLAYS_NAME[f->plays % PL_N], FIRE_C);
        }
        sel_name(set.snd[r < 8 ? 0 : 1], w[0]);
        if (f->st < 0) {                 /* START: the gap before (-), or how far in */
            w[1][0] = '-';
            sec_text((uint32_t)(-f->st), r, w[1] + 1);
        } else
            sec_text((uint32_t)f->st, r, w[1]);
        if (pads[r].type == PAD_CHORD && f->plays == RH_HOLD)
            str_set(w[2], "TILL LET GO", sizeof w[2]);
        else
            sec_text((uint32_t)win_end((uint32_t)r), r, w[2]);
        cv_rect(0, 52, 240, 1, ES_SLATE);
        for (k = 0; k < 3u; k++) {
            int32_t cx = (int32_t)k * 80;
            const felucca_font_t *ft = fit_font(w[k], 78, &FONT_B);
            cv_text(cx + (80 - text_w(&FONT_XS, LB2[k])) / 2, 56, &FONT_XS, LB2[k], LAB_LABEL);
            cv_text(cx + 1 + (78 - text_w(ft, w[k])) / 2, 72, ft, w[k], C_WHITE);
        }
        str_set(t, "WINDOW ", sizeof t);
        if (pads[r].type == PAD_CHORD && f->plays == RH_HOLD)
            text_cat(t, "TILL YOU LET GO");
        else
            sec_text(pwin_len((uint32_t)r), r, t + cs_len(t));
        text_c(98, &FONT_XS, t, AIR_C);
    }
    cv_blit(0, 120);
}

static uint16_t ch_col(uint32_t ch)      /* a loop note's colour: what played it */
{
    return ch == CH_PADS1 ? WATER_C : ch == CH_PADS2 ? AIR_C : ch == CH_BPADS ? FIRE_C : C_WHITE;
}
static void draw_loop(void)              /* LOOP: the notes, the bars, the playhead */
{
    static const char *const MN[LM_N] = {"OFF", "INTRO", "ALWAYS"};
    static const char *const LBL[4] = {"K1 METRO", "K2 BARS", "K3 QUANT", "K4 SWING"};
    uint32_t len = loop_len(), k, n = L.n, lo = 127, hi = 0, bars = L.bars ? L.bars : 4u;
    int32_t x0 = 6, y0 = 48, w = 228, h = 68, rh;
    char t[24], v[4][12];
    const char *st;
    uint16_t sc;
    lab_begin();
    if (lp_run == LR_INTRO) {
        st = "INTRO";
        sc = AIR_C;
    } else if (lp_run == LR_PLAY) {
        st = lp_rec ? "RECORDING" : "PLAYING";
        sc = lp_rec ? FIRE_C : WATER_C;
    } else {
        st = ui.l_arm ? "ARMED" : "STOPPED";
        sc = ui.l_arm ? FIRE_C : LAB_LABEL;
    }
    top_bar("LOOP", FIRE_C);
    text_c(28, &FONT_M, st, sc);
    cv_rect(x0, y0, w, h, ES_SLATE);
    for (k = 1; k < bars; k++)
        cv_rect(x0 + w * (int32_t)k / (int32_t)bars, y0, 1, h, ES_PURPLE);
    for (k = 0; k < n; k++) {
        if (L.e[k].p < lo)
            lo = L.e[k].p;
        if (L.e[k].p > hi)
            hi = L.e[k].p;
    }
    if (n) {
        rh = (h - 4) / (int32_t)(hi - lo + 1u);
        rh = rh > 6 ? 6 : rh < 1 ? 1 : rh;
        for (k = 0; k < n; k++) {
            const lnote_t *e = &L.e[k];
            float s0 = lp_start(e, len);
            int32_t x = x0 + (int32_t)(s0 / (float)len * (float)w);
            int32_t xw = (int32_t)((float)(e->d ? e->d : 24u) / (float)len * (float)w);
            int32_t y = y0 + h - 2 - ((int32_t)(e->p - lo) + 1) * rh;
            if (s0 >= (float)len)
                continue;
            if (xw < 2)
                xw = 2;
            if (x + xw > x0 + w)
                xw = x0 + w - x;
            cv_rect(x, y, xw, rh > 2 ? rh - 1 : rh, lp_left[k] > 0.0f ? C_WHITE : ch_col(e->ch));
        }
    } else
        text_c(76, &FONT_XS, "EMPTY. PLAY, THEN PLAY YOUR KEYS", LAB_LABEL);
    if (lp_run == LR_PLAY)
        cv_rect(x0 + (int32_t)(lp_pos / (float)len * (float)w), y0, 2, h, FIRE_C);
    else if (lp_run == LR_INTRO)
        cv_rect(x0, y0 + h - 3, (int32_t)(lp_pos / (float)(4u * PPQ) * (float)w), 3, AIR_C);
    cv_blit(0, 0);
    lab_begin();
    str_set(v[0], MN[L.metro % LM_N], sizeof v[0]);
    num_text(bars, v[1], 0);
    str_set(v[2], L.quant ? "16THS" : "OFF", sizeof v[2]);
    if (L.swing > 50u) {
        num_text(L.swing, v[3], 0);
        text_cat(v[3], "%");
    } else
        str_set(v[3], "STRAIGHT", sizeof v[3]);
    bottom_bar(8, LBL, v);
    text_c(54, &FONT_XS, lp_run ? "PLAY: STOP. REC: RECORDING ON / OFF" : "PLAY: START. REC: ARM", LAB_LABEL);
    text_c(68, &FONT_XS, "ARP: LAST LAYER OFF. HOME + REC: CLEAR", LAB_LABEL);
    num_text((int32_t)n, t, 0);
    text_cat(t, n == 1u ? " NOTE" : " NOTES");
    text_c(88, &FONT_B, t, C_WHITE);
    cv_blit(0, 120);
}

static void draw_sound(void)
{
    static const char *const LBL[4] = {"K1 CHORDS", "K2 PROGS", "K3 NOTES", "K4 KEYBD"};
    char v[4][12];
    uint32_t g;
    lab_begin();
    top_bar("SOUND", FIRE_C);
    text_c(34, &FONT_XS, "EACH GROUP: A SOUND, OR MIDI 1-4", LAB_LABEL);
    text_c(52, &FONT_XS, "A SOUND PLAYS ON THE FM-1, NO MIDI.", LAB_LABEL);
    text_c(66, &FONT_XS, "MIDI IS SILENT HERE, OUT ON ITS CHANNEL.", LAB_LABEL);
    text_c(90, &FONT_XS, "HOLD A KEY + SELECT: THE SAME, ANYWHERE", WATER_C);
    cv_blit(0, 0);
    lab_begin();
    for (g = 0; g < NGRP; g++)
        sel_name(set.snd[g], v[g]);
    bottom_bar(8, LBL, v);
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
    text_cat(t, clock_ext() ? " BPM, FROM CLOCK IN" : " BPM (TAP)");
    text_c(40, &FONT_B, t, C_WHITE);
    text_c(66, &FONT_XS, "HOME + FX: THE STEPS. HOLD SAVE: LOAD", LAB_LABEL);
    text_c(86, &FONT_XS, "MIDI: CHORD KEYS CH 1, PROGRESSIONS CH 2", WATER_C);
    text_c(100, &FONT_XS, "NOTE KEYS CH 3, KEYBOARD CH 4", WATER_C);
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
    text_c(102, &FONT_XS, "CHANGE IT LATER: HOLD FX, K4", LAB_LABEL);
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
            set.octave < -2 || set.octave > 2 || set.boct < -2 || set.boct > 2 || set.type >= NCTYPES || set.plays >= PL_N || set.bassm >= BM_N ||
            set.human > 50u || set.len < -5 || set.len > LEN_MAX || set.bpm < 40u || set.bpm > 240u || set.coach >= CO_N ||
            set.snd[0] >= NSEL || set.snd[1] >= NSEL || set.snd[2] >= NSEL || set.snd[3] >= NSEL)
            settings_defaults();
    }
    tempo = set.bpm;
    kit_fresh();
    session_load();                      /* the saved pads, if any */
    loop_load();
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
    if (ui.l_reload && q_r == q_w && lp_run == LR_STOP) {
        ui.l_reload = 0;
        loop_load();
        ui.redraw = 1;
    }
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
    if ((ui.pad >= 0 && pl[ui.pad].on) || (ui.screen == SCR_LOOP && lp_run != LR_STOP) ||
        (ui.screen == SCR_PAD && edit_ref() >= 0 && (pl[edit_ref()].on || cp[edit_ref()].on))) {
        static uint32_t at;
        if (now - at >= (ui.screen == SCR_PAD ? 60u : 100u)) {
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
        else if (ui.screen == SCR_PAD)
            draw_pad();
        else if (ui.screen == SCR_SETUP)
            draw_setup();
        else if (ui.screen == SCR_SOUND)
            draw_sound();
        else if (ui.screen == SCR_LOOP)
            draw_loop();
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
    else if (!strcmp(what, "nhum"))
        *got = set.nhum;
    else if (!strcmp(what, "grp"))
        *got = ui.grp;
    else if (!strncmp(what, "bm", 2))
        *got = pfx[atoi(what + 2) % NPADS].bm;
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
        *got = lp_rec;
    else if (!strcmp(what, "looprun"))
        *got = lp_run;
    else if (!strcmp(what, "loopn"))
        *got = L.n;
    else if (!strcmp(what, "looparm"))
        *got = ui.l_arm;
    else if (!strcmp(what, "loopbars"))
        *got = L.bars;
    else if (!strncmp(what, "strum", 5))
        *got = pfx[atoi(what + 5) % NPADS].strum;
    else if (!strncmp(what, "swing", 5))
        *got = pfx[atoi(what + 5) % NPADS].swing;
    else if (!strncmp(what, "rhythm", 6))
        *got = pfx[atoi(what + 6) % NPADS].plays;
    else if (!strncmp(what, "speed", 5))
        *got = pfx[atoi(what + 5) % NPADS].speed;
    else if (!strncmp(what, "st", 2) && what[2] >= '0' && what[2] <= '9')
        *got = pfx[atoi(what + 2) % NPADS].st;
    else if (!strncmp(what, "sl", 2) && what[2] >= '0' && what[2] <= '9')
        *got = pfx[atoi(what + 2) % NPADS].sl;
    else if (!strncmp(what, "len", 3))
        *got = pfx[atoi(what + 3) % NPADS].len;
    else if (!strncmp(what, "human", 5))
        *got = pfx[atoi(what + 5) % NPADS].human;
    else if (!strcmp(what, "boct"))
        *got = set.boct;
    else if (!strncmp(what, "padoct", 6))
        *got = pfx[atoi(what + 6) % NPADS].oct;
    else if (!strncmp(what, "snd", 3))
        *got = set.snd[atoi(what + 3) % NGRP];
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
