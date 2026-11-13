/* ChordSmith FM: reading standard MIDI files. Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence: to be chosen.
 *
 * The open file (disk.c fs_open) parsed into one list of notes, every track's, sorted by start: times in 96ths of a
 * beat whatever the file's resolution. Formats 0 and 1, running status, tempo, time signature, track names. Part of
 * app.c. */

#define PPQ 96u
#define MAXNOTES 1024u
#define MAXTRK 12u
typedef struct {
    uint32_t s;                          /* start, 96ths of a beat; the top 8 bits: the track */
    uint16_t d;                          /* length, 96ths */
    uint8_t p, v;                        /* pitch, velocity */
} note_t;
#define N_START(n) ((n)->s & 0xFFFFFFu)
#define N_TRK(n) ((n)->s >> 24)
static note_t song[MAXNOTES];
static uint32_t song_n;
typedef struct {
    char name[14];
    uint16_t count;
    uint8_t drums, on;
} track_t;
static track_t trk[MAXTRK];
static uint32_t ntrk, song_bpm, song_bpb, song_len;   /* song_len: the end, 96ths, rounded up to a bar */

static uint32_t rd_pos;
static uint32_t rd8(void) { return fs_byte(rd_pos++); }
static uint32_t rd16(void) { uint32_t v = rd8() << 8; return v | rd8(); }
static uint32_t rd32(void) { uint32_t v = rd16() << 16; return v | rd16(); }
static uint32_t rdvlq(uint32_t end)
{
    uint32_t v = 0, c, k = 0;
    do {
        c = rd8();
        v = v << 7 | (c & 0x7Fu);
    } while ((c & 0x80u) && rd_pos < end && ++k < 4u);
    return v;
}

static void song_sort(void)              /* insertion sort by start (files come mostly in order) */
{
    uint32_t i, j;
    for (i = 1; i < song_n; i++) {
        note_t t = song[i];
        for (j = i; j > 0 && N_START(&song[j - 1]) > N_START(&t); j--)
            song[j] = song[j - 1];
        song[j] = t;
    }
}

/* 0 = read; < 0 not a MIDI file we can read */
static int smf_load(void)
{
    uint32_t hl, ntracks, div, t, i, end_max = 0;
    rd_pos = 0;
    song_n = ntrk = 0;
    song_bpm = 90;
    song_bpb = 4;
    if (rd32() != 0x4D546864u)           /* "MThd" */
        return -1;
    hl = rd32();
    (void)rd16();
    ntracks = rd16();
    div = rd16();
    if (div & 0x8000u || !div)
        div = 480;                       /* SMPTE time: read as 480 per beat */
    rd_pos = 8u + hl;
    for (t = 0; t < ntracks && rd_pos + 8u <= fsize; t++) {
        uint32_t len, end, tick = 0, rs = 0, tr = ntrk < MAXTRK ? ntrk : MAXTRK - 1u;
        static uint32_t open_at[16][8];  /* a few notes held per channel: their starts and velocities, by pitch slot */
        static uint8_t open_p[16][8], open_n[16];
        uint32_t first = song_n;
        track_t *T = &trk[tr];
        if (rd32() != 0x4D54726Bu) {      /* "MTrk" */
            rd_pos += rd32();
            continue;
        }
        len = rd32();
        end = rd_pos + len;
        for (i = 0; i < 16u; i++)
            open_n[i] = 0;
        T->name[0] = 0;
        T->drums = 0;
        while (rd_pos < end && rd_pos < fsize) {
            uint32_t st, hi, ch;
            tick += rdvlq(end);
            st = fs_byte(rd_pos);
            if (st & 0x80u) {
                rd_pos++;
                if (st < 0xF0u)
                    rs = st;
            } else {
                st = rs;
            }
            if (st == 0xFFu) {
                uint32_t type = rd8(), l = rdvlq(end), k;
                if (type == 0x03u && !T->name[0]) {
                    for (k = 0; k < l && k < 13u; k++) {
                        uint32_t ch2 = fs_byte(rd_pos + k);
                        T->name[k] = ch2 >= 32u && ch2 < 127u ? (char)ch2 : ' ';
                    }
                    T->name[k] = 0;
                } else if (type == 0x51u && l == 3u && song_bpm == 90u) {
                    uint32_t us = fs_byte(rd_pos) << 16 | fs_byte(rd_pos + 1u) << 8 | fs_byte(rd_pos + 2u);
                    if (us)
                        song_bpm = (60000000u + us / 2u) / us;
                } else if (type == 0x58u && l >= 1u) {
                    song_bpb = fs_byte(rd_pos) ? fs_byte(rd_pos) : 4u;
                }
                rd_pos += l;
                continue;
            }
            if (st == 0xF0u || st == 0xF7u) {
                rd_pos += rdvlq(end);
                continue;
            }
            hi = st & 0xF0u;
            ch = st & 0x0Fu;
            if (hi == 0x90u || hi == 0x80u) {
                uint32_t p = rd8() & 0x7Fu, v = rd8() & 0x7Fu, k, t96 = tick / div * PPQ + tick % div * PPQ / div;
                if (ch == 9u)
                    T->drums = 1;
                if (hi == 0x90u && v) {
                    if (open_n[ch] < 8u) {
                        open_p[ch][open_n[ch]] = (uint8_t)p;
                        open_at[ch][open_n[ch]] = t96 | v << 24;
                        open_n[ch]++;
                    }
                } else {
                    for (k = 0; k < open_n[ch]; k++)
                        if (open_p[ch][k] == p) {
                            uint32_t s = open_at[ch][k] & 0xFFFFFFu, d = t96 > s ? t96 - s : 1u;
                            if (song_n < MAXNOTES) {
                                song[song_n].s = s | tr << 24;
                                song[song_n].d = (uint16_t)(d > 0xFFFFu ? 0xFFFFu : d);
                                song[song_n].p = (uint8_t)p;
                                song[song_n].v = (uint8_t)(open_at[ch][k] >> 24);
                                song_n++;
                                if (s + d > end_max)
                                    end_max = s + d;
                            }
                            open_p[ch][k] = open_p[ch][open_n[ch] - 1u];
                            open_at[ch][k] = open_at[ch][open_n[ch] - 1u];
                            open_n[ch]--;
                            break;
                        }
                }
            } else if (hi == 0xC0u || hi == 0xD0u) {
                rd_pos += 1;
            } else {
                rd_pos += 2;
            }
        }
        rd_pos = end;
        if (song_n > first) {            /* a track with notes counts */
            T->count = (uint16_t)(song_n - first);
            if (!T->name[0]) {
                __builtin_memcpy(T->name, "TRACK ", 6);
                T->name[6] = (char)('1' + tr % 9u);
                T->name[7] = 0;
            }
            T->on = !T->drums;           /* drums start off: they muddy chord detection */
            if (ntrk < MAXTRK)
                ntrk++;
        }
    }
    if (!song_n)
        return -2;
    if (ntrk && !trk[0].on && !trk[ntrk > 1u].on)
        trk[0].on = 1;
    song_sort();
    {
        uint32_t bar = PPQ * song_bpb;
        song_len = (end_max + bar - 1u) / bar * bar;
        if (song_len < bar)
            song_len = bar;
    }
    return 0;
}
