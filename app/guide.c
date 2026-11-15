/* ChordSmith FM: GUIDE, the getting-started pages. Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence: to be
 * chosen. Part of app.c. Hold SYS to open it (and it opens by itself the first time). SELECT turns the pages; the
 * controls a page talks about blink, and pressing one of the buttons leaves the guide to do it. */

#define GB(b) (1u << (b))
#define G_WHITE 0x5AD5AD5u               /* the white keys (PLAT_WHITE_KEYS as bits) */
#define G_DEG ((1u << 1) | (1u << 3) | (1u << 5) | (1u << 8) | (1u << 10) | (1u << 13) | (1u << 15))
#define G_FKEYS ((1u << 17) | (1u << 20) | (1u << 22) | (1u << 25))
typedef struct {
    const char *title, *text;
    uint32_t btn, keys;                  /* what blinks */
} gpage_t;
static const gpage_t GUIDE[] = {
    {"WELCOME", "CHORDSMITH MAKES THE FM-1 A CHORD AND PHRASE SAMPLER. TURN SELECT FOR THE NEXT PAGE. HOME LEAVES.", 0,
     0},
    {"ORCHID", "LOAD A MIDI FILE: IT'S ON THE 16 PADS AT ONCE. PRESET: HOW IT'S CUT. K1: WHERE IN THE SONG.",
     0, G_WHITE},
    {"ORCHID KNOBS", "K2: THE 7 BLACK KEYS. K3: THE 4 EXTRA BLACK KEYS. K4: HOW THE PADS PLAY. HOLD OCT- OR OCT+ AND TURN K4 TO SHAPE IT.",
     GB(B_OCTDN) | GB(B_OCTUP), G_DEG | G_FKEYS},
    {"FM MODE", "HOLD HOME FOR FM MODE: EVERYTHING ELSE ON THE NEXT PAGES. TAP HOME TO COME BACK TO ORCHID.", GB(B_HOME),
     0},
    {"PLAY CHORDS", "THE WHITE KEYS ARE 16 PADS. EACH PLAYS A CHORD IN YOUR KEY. PLAY THEM NOW.", 0, G_WHITE},
    {"BASS AND LEAD", "BLACK KEYS 1-7 PLAY THE KEY'S NOTES. THE LIT BUTTON (MODE) SWITCHES THEM: BASS (ROOT) OR LEAD.",
     GB(B_SEQ), G_DEG},
    {"PICK A KEY", "TURN THE ALGORITHM KNOB FOR THE ROOT. THE LIT BUTTON (KEY) PICKS THE SCALE.", GB(B_EDIT), 0},
    {"FIND A KEY", "HOLD REC AND PRESS PLAY. TAP A FEW NOTES, TURN SELECT TO PICK A KEY, TURN K4 TO USE IT.",
     GB(B_REC) | GB(B_PLAY), 0},
    {"CHORD LIBRARY", "THE LIT BUTTON (LIB) LISTS THE KEY'S CHORDS. PRESS A WHITE KEY TO PUT ONE ON THAT PAD.",
     GB(B_LFO), 0},
    {"CHORD FX", "THE LIT BUTTON (FX): STRUM, ARPEGGIO, HUMANIZE AND MORE, ON THE FOUR K KNOBS.", GB(B_FX), 0},
    {"LOAD A MIDI FILE", "THE LIT BUTTON (SYS) MAKES THE FM-1 A USB DRIVE. COPY .MID FILES ON, THEN EJECT IT.",
     GB(B_GLO), 0},
    {"SLICE IT", "THE LIT BUTTON (SLICE) SHOWS THE FILE. K1 CUTS IT, SELECT PICKS A SLICE, A WHITE KEY DROPS IT.",
     GB(B_SEL), G_WHITE},
    {"TRIM A PAD", "PLAY A PAD, THEN THE LIT BUTTON (TRIM): ITS START, END, TUNE, LEVEL AND LOOP.", GB(B_ENV), 0},
    {"A BRIDGE", "THE LAST 4 BLACK KEYS CHANGE KEY. TAP ONE TO GO, TAP IT AGAIN TO COME HOME.", 0, G_FKEYS},
    {"SAVE", "THE LIT BUTTON (SAVE) KEEPS YOUR PADS AND SETTINGS. HOLD SYS TO SEE THIS GUIDE AGAIN.", GB(B_SAVE),
     0},
};
#define NGUIDE (sizeof GUIDE / sizeof GUIDE[0])
static uint32_t guide_count(void) { return NGUIDE; }

static void guide_open(void)
{
    ui.guide_pg = 0;
    ui.screen = SCR_GUIDE;
    ui.msg_until = 0;
    ui.redraw = 1;
}

static void guide_lights(void)          /* the page's controls blink; all else dark */
{
    const gpage_t *g = &GUIDE[ui.guide_pg % NGUIDE];
    int on = (plat_ms() / 400u & 1u) == 0;
    plat_lights((on ? g->btn : 0) | ui.btn, (on ? g->keys : 0) | ui.keys, 0, 0, 1);
    plat_lights_mid(0, 0);
}

/* the words of t, wrapped to w pixels: up to max lines into ln (each up to 48 characters) */
static uint32_t guide_wrap(const char *t, int32_t w, char ln[][48], uint32_t max)
{
    uint32_t n = 0, len = 0;
    ln[0][0] = 0;
    while (*t && n < max) {
        char word[48];
        uint32_t k = 0;
        while (*t == ' ')
            t++;
        while (*t && *t != ' ' && k < 47u)
            word[k++] = *t++;
        word[k] = 0;
        if (!k)
            break;
        if (len) {                       /* the word on this line, or the next */
            char tryl[48];
            str_set(tryl, ln[n], sizeof tryl);
            text_cat(tryl, " ");
            text_cat(tryl, word);
            if (text_w(&FONT_XS, tryl) <= w && cs_len(tryl) < 47u) {
                str_set(ln[n], tryl, 48);
                len = cs_len(tryl);
                continue;
            }
            if (++n >= max)
                break;
        }
        str_set(ln[n], word, 48);
        len = k;
    }
    return n < max ? n + 1u : max;
}

static void draw_guide(void)
{
    const gpage_t *g = &GUIDE[ui.guide_pg % NGUIDE];
    char ln[9][48], pg[8];
    uint32_t n = guide_wrap(g->text, 220, ln, 9), i;
    num_text((int32_t)ui.guide_pg + 1, pg, 0);
    text_cat(pg, "/");
    num_text((int32_t)NGUIDE, pg + cs_len(pg), 0);
    cv_begin(240, 120, C_BLACK);
    top_bar("GUIDE", pg, C_GRAY);
    cv_text(10, 32, &FONT_B, g->title, AMBER);
    for (i = 0; i < n && i < 3u; i++)
        cv_text(10, 66 + (int32_t)i * 16, &FONT_XS, ln[i], C_WHITE);
    cv_blit(0, 0);
    cv_begin(240, 120, C_BLACK);
    for (i = 3; i < n; i++)
        cv_text(10, 2 + (int32_t)(i - 3u) * 16, &FONT_XS, ln[i], C_WHITE);
    for (i = 0; i < NGUIDE; i++)         /* where you are */
        cv_rect(120 - (int32_t)NGUIDE * 5 + (int32_t)i * 10, 84, 6, 6, i == ui.guide_pg ? AMBER : C_LINE);
    cv_text((240 - text_w(&FONT_XS, "SELECT: PAGES   HOME: DONE")) / 2, 98, &FONT_XS, "SELECT: PAGES   HOME: DONE",
            C_DIM);
    cv_blit(0, 120);
}
