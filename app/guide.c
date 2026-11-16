/* MPS-16 ver FM1: GUIDE, the getting-started pages. Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence: to be
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
    {"WELCOME", "TURN SELECT FOR THE NEXT PAGE. HOME LEAVES.", 0, 0},
    {"SURFACE", "LOAD A MIDI FILE: IT'S ON THE 16 PADS AT ONCE.", 0, G_WHITE},
    {"THE CUT", "PRESET: HOW THE SONG IS CUT. K1: WHERE IN THE SONG.", 0, G_WHITE},
    {"BLACK KEYS", "K2: WHAT KEYS 1-7 PLAY. K3: WHAT THE LAST 4 DO.", 0, G_DEG | G_FKEYS},
    {"HOW PADS PLAY", "K4: CHORD, STRUM, ARP AND MORE. ALGORITHM: NOTE LENGTH.", 0, G_WHITE},
    {"SECOND JOBS", "HOLD OCT- OR OCT+ AND TURN A KNOB FOR MORE.", GB(B_OCTDN) | GB(B_OCTUP), 0},
    {"MUTATE", "TURN SELECT: REPEATED CHORDS GET MORE INTERESTING.", 0, G_WHITE},
    {"CORE", "HOLD HOME FOR CORE. TAP HOME TO COME BACK.", GB(B_HOME), 0},
    {"PLAY CHORDS", "THE WHITE KEYS ARE 16 PADS. PLAY THEM NOW.", 0, G_WHITE},
    {"BASS AND LEAD", "KEYS 1-7 PLAY BASS OR LEAD. THE LIT BUTTON SWITCHES.", GB(B_SEQ), G_DEG},
    {"PICK A KEY", "THE LIT BUTTON: ROOT, SCALE AND SCALE LOCK.", GB(B_EDIT), 0},
    {"FIND A KEY", "HOLD REC, PRESS PLAY. TAP NOTES. SELECT PICKS A KEY.", GB(B_REC) | GB(B_PLAY), 0},
    {"CHORD LIBRARY", "THE LIT BUTTON: YOUR KEY'S CHORDS. A WHITE KEY DROPS ONE.", GB(B_LFO), 0},
    {"CHORD FX", "THE LIT BUTTON: STRUM, ARP AND HUMANIZE ON THE K KNOBS.", GB(B_FX), 0},
    {"LOAD A FILE", "THE LIT BUTTON: USB DRIVE. COPY .MID FILES ON, EJECT.", GB(B_GLO), 0},
    {"SLICE IT", "THE LIT BUTTON SHOWS THE FILE. A WHITE KEY DROPS A SLICE.", GB(B_SEL), G_WHITE},
    {"TRIM A PAD", "PLAY A PAD, THEN THE LIT BUTTON: START, END, LOOP.", GB(B_ENV), 0},
    {"A BRIDGE", "F2-F4 MOVE THE SONG TO A NEW KEY. F1: BACK HOME.", 0, G_FKEYS},
    {"WHAT DOES IT DO?", "HOLD OCT- AND OCT+, THEN TOUCH A KEY OR PAD.", GB(B_OCTDN) | GB(B_OCTUP), G_WHITE | G_DEG | G_FKEYS},
    {"SAVE", "THE LIT BUTTON KEEPS YOUR PADS. HOLD SYS FOR THIS GUIDE.", GB(B_SAVE), 0},
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
static uint32_t guide_wrap_f(const felucca_font_t *f, const char *t, int32_t w, char ln[][48], uint32_t max)
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
            if (text_w(f, tryl) <= w && cs_len(tryl) < 47u) {
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
static uint32_t guide_wrap(const char *t, int32_t w, char ln[][48], uint32_t max)
{
    return guide_wrap_f(&FONT_XS, t, w, ln, max);
}


/* big text on a 240 x 240 screen drawn as two 120-row halves: each line drawn in both, the canvas clips it */
static void big_rows(int half, int32_t y0, int32_t step, const felucca_font_t *f, char ln[][48], uint32_t n, uint16_t c)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        cv_text(10, y0 + (int32_t)i * step - (half ? 120 : 0), f, ln[i], c);
}
static void draw_guide(void)
{
    const gpage_t *g = &GUIDE[ui.guide_pg % NGUIDE];
    char ln[6][48], pg[8];
    uint32_t n = guide_wrap_f(&FONT_L, g->text, 226, ln, 6), h;
    num_text((int32_t)ui.guide_pg + 1, pg, 0);
    text_cat(pg, "/");
    num_text((int32_t)NGUIDE, pg + cs_len(pg), 0);
    for (h = 0; h < 2u; h++) {
        lab_begin();
        if (!h)
            top_bar("GUIDE", pg, C_GRAY);
        cv_text(10, 26 - (int32_t)h * 120, &FONT_M, g->title, AMBER);
        big_rows((int)h, 54, 31, &FONT_L, ln, n, C_WHITE);
        cv_blit(0, h * 120u);
    }
}
