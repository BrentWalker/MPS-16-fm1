/* ChordSmith FM: the chord library. Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence: to be chosen.
 *
 * Built from the key, as ChordSmith Web's: the seven diatonic chords of a type (triads, 7ths, 9ths, sus2, sus4,
 * add9), and ten common four-bar progressions played in a rhythm (sustain, stabs, arp 8ths) with a bass line, the
 * chords voiced to move as little as they can. Part of app.c (after the slicing). */

#define NCTYPES 6u
static const char *const CTYPE_NAME[NCTYPES] = {"TRIADS", "7THS", "9THS", "SUS2", "SUS4", "ADD9"};
static const uint8_t CTYPE[NCTYPES][5] = {{0, 2, 4, 0xFF, 0xFF}, {0, 2, 4, 6, 0xFF}, {0, 2, 4, 6, 8},
                                          {0, 1, 4, 0xFF, 0xFF}, {0, 3, 4, 0xFF, 0xFF}, {0, 2, 4, 8, 0xFF}};
#define NRHY 3u
static const char *const RHY_NAME[NRHY] = {"SUSTAIN", "STABS", "ARP 8THS"};
#define NPROGS 10u
static const uint8_t PROGS[NPROGS][4] = {{0, 4, 5, 3}, {1, 4, 0, 0}, {0, 5, 3, 4}, {5, 3, 0, 4}, {0, 3, 4, 3},
                                         {0, 5, 2, 6}, {0, 3, 4, 0}, {0, 6, 5, 6}, {3, 4, 2, 5}, {0, 2, 3, 4}};
static uint8_t lib_type = 1, lib_rhy = 1, lib_grp, lib_sel;

static uint32_t lib_count(void) { return lib_grp ? NPROGS : 7u; }

/* degree d's chord of type t, as MIDI notes into v: how many */
static uint32_t lib_chord(uint32_t d, uint32_t t, int32_t *v)
{
    uint32_t k, n = 0;
    for (k = 0; k < 5u && CTYPE[t][k] != 0xFF; k++)
        v[n++] = deg_pitch((int32_t)(d + CTYPE[t][k]));
    return n;
}

static void roman_of(uint32_t d, char *o, uint8_t *dim)
{
    pad_t tmp;
    tmp.deg = (uint8_t)d;
    roman_for(&tmp);
    str_set(o, tmp.roman, 6);
    *dim = tmp.dim;
}

/* the inversion of v closest to the previous chord's middle: chords that move as little as they can */
static void lib_nearest(int32_t *v, uint32_t n, float prev)
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

static void lib_note(uint32_t dst, int32_t p, uint32_t s, uint32_t d, uint32_t v)
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

/* library row r (of the group shown) into pad slot dst */
static void lib_make(uint32_t r, uint32_t dst)
{
    pad_t *p = &pads[dst];
    int32_t v[6];
    uint32_t k, n;
    __builtin_memset(p, 0, sizeof *p);
    if (!lib_grp) {                      /* a chord */
        n = lib_chord(r, lib_type, v);
        p->type = PAD_CHORD;
        p->n = (uint8_t)n;
        for (k = 0; k < n; k++) {
            p->note[k] = (uint8_t)v[k];
            spell((uint32_t)v[k] % 12u, tonic_letter() + r + CTYPE[lib_type][k], p->nm[k]);
        }
        p->deg = (uint8_t)(r % 7u);
        p->root_pc = (uint8_t)(p->note[0] % 12u);
        name_chord(p);
        roman_for(p);
        return;
    }
    {                                    /* a progression: four bars, a chord a bar, the rhythm, a bass line */
        const uint8_t *seq = PROGS[r % NPROGS];
        uint32_t t = lib_type == 2u ? 1u : lib_type, bar, bl = PPQ * 4u;
        float prev = (float)base_note() + 5.0f;
        p->type = PAD_PHRASE;
        p->len = (uint16_t)(4u * bl);
        for (bar = 0; bar < 4u; bar++) {
            uint32_t o = bar * bl, d = seq[bar];
            int32_t bass = deg_pitch((int32_t)d) - 12;
            char rn[6];
            uint8_t dim;
            n = lib_chord(d, t, v);
            lib_nearest(v, n, prev);
            prev = 0.0f;
            for (k = 0; k < n; k++)
                prev += (float)v[k];
            prev /= (float)n;
            if (lib_rhy == 0u) {         /* sustain */
                for (k = 0; k < n; k++)
                    lib_note(dst, v[k], o, bl - 10u, 96);
                lib_note(dst, bass, o, bl - 10u, 104);
            } else if (lib_rhy == 1u) {  /* stabs: 1, the and of 2, 4; the bass on 1 and 3 */
                static const uint16_t S[3] = {0, 144, 288}, D[3] = {72, 48, 72};
                static const uint8_t V[3] = {108, 86, 96};
                uint32_t j;
                for (j = 0; j < 3u; j++)
                    for (k = 0; k < n; k++)
                        lib_note(dst, v[k], o + S[j], D[j], V[j]);
                lib_note(dst, bass, o, 58, 112);
                lib_note(dst, bass, o + 192u, 72, 100);
            } else {                     /* arp 8ths, the bass held */
                uint32_t j;
                for (j = 0; j < 8u; j++)
                    lib_note(dst, v[j % n] + (j >= n ? 12 : 0), o + j * 48u, 43, j % 2u ? 82 : 100);
                lib_note(dst, bass, o, bl - 10u, 100);
            }
            roman_of(d, rn, &dim);       /* the title: "i VI III VII" */
            if (bar)
                text_cat_n(p->title, " ", sizeof p->title);
            text_cat_n(p->title, rn, sizeof p->title);
        }
    }
}

/* the whole kit from the library: pads 1-7 the chords, 8 the first an octave up, 9-16 the first eight progressions */
static void lib_build_kit(void)
{
    uint32_t i, g = lib_grp;
    lib_grp = 0;
    for (i = 0; i < 8u; i++)
        lib_make(i, i);
    lib_grp = 1;
    for (i = 0; i < 8u; i++)
        lib_make(i, 8u + i);
    lib_grp = (uint8_t)g;
}

/* the key that fits the loaded file best (Krumhansl's profiles against its notes, held time and velocity) */
static void key_detect(void)
{
    static const float MA[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
    static const float MI[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.6f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};
    float h[12] = {0}, best = -2.0f;
    uint32_t i, r, k, br = set.root, bs = set.scale;
    for (i = 0; i < song_n; i++)
        if (note_used(&song[i]))
            h[song[i].p % 12u] += (float)song[i].d * (float)song[i].v;
    for (r = 0; r < 12u; r++) {
        for (k = 0; k < 2u; k++) {
            const float *pr = k ? MI : MA;
            float mx = 0.0f, my = 0.0f, a = 0.0f, b = 0.0f, c = 0.0f, cor;
            uint32_t j;
            for (j = 0; j < 12u; j++) {
                mx += h[(j + r) % 12u];
                my += pr[j];
            }
            mx /= 12.0f;
            my /= 12.0f;
            for (j = 0; j < 12u; j++) {
                float x = h[(j + r) % 12u] - mx, y = pr[j] - my;
                a += x * y;
                b += x * x;
                c += y * y;
            }
            cor = b > 0.0f && c > 0.0f ? a / fm_sqrtf(b * c) : -1.0f;
            if (cor > best) {
                best = cor;
                br = r;
                bs = k ? 0u : 1u;        /* SCALES: 0 minor, 1 major */
            }
        }
    }
    set.root = (uint8_t)br;
    set.scale = (uint8_t)bs;
}
