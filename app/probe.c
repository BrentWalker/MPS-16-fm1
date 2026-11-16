/* MPS-16 ver FM1: PROBE. Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence: to be chosen. Part of app.c.
 * Hold ARP: every control is frozen and says what it does instead, here (SURFACE, or CORE's screen it came from):
 * its name, what it does, and what it does with OCT- or OCT+ held. Tap ARP or HOME to leave. */

enum { PR_ENC = 20, PR_KEY = 32 };       /* the item: a button (B_*), PR_ENC + 0 .. 6 (SELECT, ALGORITHM, PRESET, K1-K4),
                                          * PR_KEY + a key */
typedef struct {
    const char *title, *what, *om, *op;  /* om, op: with OCT- held, with OCT+ held (0: nothing) */
} probe_t;

static const probe_t PR_BTN[NB] = {
    {"FX", "CHORD FX: STRUM, ARP, HUMANIZE, INVERSION, BASS.", 0, 0},
    {"SLICE", "CUT THE FILE BY HAND, DROP SLICES ON PADS. AGAIN: FILES.", 0, 0},
    {"TRIM", "THE LAST PAD: START, END, TUNE, LEVEL, LOOP.", 0, 0},
    {"LIB", "YOUR KEY'S CHORDS. A WHITE KEY DROPS ONE ON A PAD.", 0, 0},
    {"KEY", "ROOT, SCALE, STEPS AND SCALE LOCK.", 0, 0},
    {"SYS", "TAP: USB DRIVE FOR MIDI FILES. HOLD: GUIDE.", 0, 0},
    {"HOME", "TAP: SURFACE. HOLD: CORE.", 0, 0},
    {"SAVE", "KEEPS YOUR PADS AND SETTINGS.", 0, 0},
    {"ARP", "HOLD: PROBE, THIS. TAP TO LEAVE.", 0, 0},
    {"MODE", "WHAT KEYS 1-7 PLAY: ROOT, LEAD, 3RD, 5TH.", 0, 0},
    {"PLAY", "ALL NOTES OFF. WITH REC: FIND A KEY.", 0, 0},
    {"REC", "RECORD MIDI IN. AGAIN: SAVE IT.", 0, 0},
    {"OCT-", "OCTAVE DOWN WHEN YOU LET GO. HOLD + KNOB: MORE.", 0, 0},
    {"OCT+", "OCTAVE UP WHEN YOU LET GO. HOLD + KNOB: MORE.", 0, 0},
};
static const probe_t PR_SURF[7] = {
    {"MUTATE", "REPEATED CHORDS CHANGE: EXTEND, COLOR, FLIP, SUB, MIX.",
     "WHICH CHORDS CHANGE", "A NEW MIX ROLL"},
    {"NOTE LENGTH", "SHORT TO LONG, THEN LEGATO.", "SLICE LENGTH", "SOFTER OR HARDER"},
    {"SLICE", "HOW THE SONG IS CUT: EQUAL, BARS, HITS, CHORDS.", 0, 0},
    {"START", "WHERE IN THE SONG THE 16 PADS BEGIN.", 0, 0},
    {"BLACK 7", "WHAT KEYS 1-7 PLAY: ROOT, LEAD, 3RD, 5TH.", 0, 0},
    {"BLACK 4", "THE LAST 4 BLACK KEYS: BRIDGE, SUGGEST, OUTSIDE, BORROW.", 0, 0},
    {"PLAY", "HOW PADS PLAY: AS IS, CHORD, STRUM, ARP, RHYTHMS.",
     "HOW MUCH", "VOICING"},
};
static const probe_t PR_ALGO_CORE = {"ROOT", "THE KEY'S ROOT.", "THE SCALE", "THE SCALE"};
static const probe_t PR_PRESET_CORE = {"KIT", "THE NEXT KIT, LOADED AT ONCE (SAVE FIRST TO KEEP CHANGES).", 0, 0};

/* CORE's knobs, by the screen PROBE came from: SELECT, K1-K4 */
static void probe_core_knob(uint32_t e, uint32_t scr, probe_t *o)
{
    static const char *const HOME_[5][2] = {{"SELECT", "NOTHING ON HOME."},
                                            {"STRUM", "THE CHORD FX STRUM: THE NOTES ONE AFTER ANOTHER."},
                                            {"ARP", "THE ARPEGGIO: OFF, UP, DOWN, UP-DOWN, RANDOM."},
                                            {"HUMANIZE", "TIMING AND VELOCITY OFF BY A LITTLE."},
                                            {"BPM", "THE TEMPO."}};
    static const char *const SLICE_[5][2] = {{"SELECT", "PICKS A SLICE, AND PLAYS IT."},
                                             {"MODE", "HITS OR GRID."},
                                             {"SENS / GRID", "HOW SURE A HIT MUST BE; OR THE GRID'S SIZE."},
                                             {"START", "TRIMS THE SLICE'S START."},
                                             {"END", "TRIMS THE SLICE'S END."}};
    static const char *const TRIM_[5][2] = {{"PLAY MODE", "ONE-SHOT, GATE OR LOOP."},
                                            {"START", "WHERE THE PAD STARTS, BY 16THS."},
                                            {"END", "WHERE THE PAD ENDS, BY 16THS."},
                                            {"TUNE", "UP OR DOWN BY SEMITONES."},
                                            {"LEVEL", "HOW LOUD, 0 TO 100."}};
    static const char *const KEY_[5][2] = {{"SELECT", "NOTHING ON KEY."},
                                           {"ROOT", "THE KEY'S ROOT."},
                                           {"SCALE", "MAJOR, MINOR AND THE MODES."},
                                           {"STEP", "MOVES THE CHORDS ALONG THE SCALE, WITH THE LOCK ON."},
                                           {"LOCK", "SCALE LOCK: EVERY NOTE SNAPS TO THE KEY."}};
    static const char *const FX_[5][2] = {{"SELECT", "THE NEXT PAGE OF FX."},
                                          {"STRUM / RATE", "THE STRUM; ON PAGE 2 THE ARP'S RATE."},
                                          {"ARP / BASS", "THE ARPEGGIO; ON PAGE 2 A BASS NOTE UNDER EVERY CHORD."},
                                          {"HUMANIZE", "TIMING AND VELOCITY OFF BY A LITTLE."},
                                          {"INVERT", "THE CHORDS' INVERSION."}};
    static const char *const LIB_[5][2] = {{"SELECT", "PICKS A CHORD OR A PROGRESSION, AND PLAYS IT."},
                                           {"TYPE", "TRIADS, 7THS, 9THS, SUS AND MORE."},
                                           {"RHYTHM", "HOW A PROGRESSION PLAYS: HELD, STABS, ARP."},
                                           {"GROUP", "CHORDS OR PROGRESSIONS."},
                                           {"KIT", "TURN RIGHT: THE WHOLE LIST ON THE PADS AT ONCE."}};
    static const char *const FIND_[5][2] = {{"SELECT", "PICKS A KEY THAT HAS THE NOTES."},
                                            {"UNDO", "TAKES BACK THE LAST NOTE."},
                                            {"CLEAR", "CLEARS THE NOTES."},
                                            {"-", "NOTHING HERE."},
                                            {"USE", "TURN RIGHT: USES THE KEY AND OPENS LIB."}};
    const char *const (*t)[2] = scr == SCR_SLICE || scr == SCR_FILES ? SLICE_ : scr == SCR_TRIM ? TRIM_
                                : scr == SCR_KEY ? KEY_ : scr == SCR_FX ? FX_ : scr == SCR_LIB ? LIB_
                                : scr == SCR_FIND ? FIND_ : HOME_;
    uint32_t r = e == 0u ? 0u : e - 2u;  /* SELECT, or K1-K4 (e 3 .. 6) */
    o->title = t[r][0];
    o->what = t[r][1];
    o->om = o->op = 0;
}

static void probe_info(int32_t it, probe_t *o)
{
    o->title = o->what = o->om = o->op = 0;
    if (it < 0)
        return;
    if (it < (int32_t)NB) {
        *o = PR_BTN[it];
        return;
    }
    if (it < PR_KEY) {
        uint32_t e = (uint32_t)(it - PR_ENC);
        if (orchid && ui.probe_from == SCR_HOME)
            *o = PR_SURF[e % 7u];
        else if (e == 1u)
            *o = PR_ALGO_CORE;
        else if (e == 2u)
            *o = PR_PRESET_CORE;
        else
            probe_core_knob(e, ui.probe_from, o);
        return;
    }
    {
        static char line[64];
        uint32_t k = (uint32_t)(it - PR_KEY), j;
        for (j = 0; j < 16u; j++)
            if (WK[j] == k) {
                o->title = "A PAD";
                o->what = orchid ? "A SLICE OF THE SONG, PLAYED AS K4 SAYS."
                                 : "ITS CHORD OR PHRASE.";
                return;
            }
        for (j = 0; j < 7u; j++)
            if (BK[j] == k) {
                str_set(line, "BLACK KEY ", sizeof line);
                num_text((int32_t)j + 1, line + cs_len(line), 0);
                text_cat(line, ": THE KEY'S NOTE, AS ");
                text_cat(line, MODE_NAME[set.mode % MODE_N]);
                text_cat(line, ". K2 CHANGES IT.");
                o->title = MODE_NAME[set.mode % MODE_N];
                o->what = line;
                return;
            }
        for (j = 0; j < 4u; j++)
            if (BK[7u + j] == k)
                break;
        if (set.om_f4 == FK_BRIDGE && j < 4u) {   /* say where it goes */
            char kn[24];
            fd_name(fk_target(j + 1u), kn);
            str_set(line, j ? "TAP: SONG AND KEYS TO " : "TAP: BACK HOME TO ", sizeof line);
            text_cat(line, kn);
            text_cat(line, j ? ". F1: HOME." : ".");
            o->title = FK_NAME[j];
            o->what = line;
            return;
        }
        str_set(line, "AN EXTRA BLACK KEY, AS ", sizeof line);
        text_cat(line, FK_MODE[set.om_f4 % FK_N]);
        text_cat(line, ". K3 CHANGES IT.");
        o->title = FK_MODE[set.om_f4 % FK_N];
        o->what = line;
    }
}

static void probe_open(void)
{
    ui.probe = 1;
    ui.probe_from = ui.screen;
    ui.probe_item = -1;
    ui.probe_skip = 1;                   /* the hold's own release */
    players_stop();
    ui.redraw = 1;
}

/* PROBE's input: nothing acts; what was touched last is shown. ARP tapped (or HOME): out */
static void probe_input(uint32_t keys, uint32_t btn)
{
    uint32_t i;
    int32_t e;
    static const uint8_t ENC[7] = {EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4};
    for (i = 0; i < 7u; i++)
        if ((e = plat_enc(ENC[i])) != 0) {
            ui.probe_item = PR_ENC + (int32_t)i;
            ui.redraw = 1;
        }
    for (i = 0; i < NB; i++) {
        uint32_t bit = 1u << i;
        if (i == B_ARP && !(btn & bit) && (ui.btn & bit)) {
            if (ui.probe_skip)
                ui.probe_skip = 0;
            else {
                ui.probe = 0;
                ui.redraw = 1;
            }
        } else if (i == B_HOME && (btn & bit) && !(ui.btn & bit)) {
            ui.probe = 0;
            ui.redraw = 1;
        } else if ((btn & bit) && !(ui.btn & bit) && i != B_ARP) {
            ui.probe_item = (int32_t)i;
            ui.redraw = 1;
        }
    }
    for (i = 0; i < NKEYS; i++)
        if ((keys >> i & 1u) && !(ui.keys >> i & 1u)) {
            ui.probe_item = PR_KEY + (int32_t)i;
            ui.redraw = 1;
        }
    if (!ui.probe)                       /* leaving: no hold or tap left half done */
        ui.home_at = 0xFFFFFFFFu;
}

static void probe_lights(void)          /* ARP, and what was touched */
{
    uint32_t b = 1u << B_ARP, k = 0;
    if (ui.probe_item >= 0 && ui.probe_item < (int32_t)NB)
        b |= 1u << ui.probe_item;
    else if (ui.probe_item >= PR_KEY)
        k = 1u << (ui.probe_item - PR_KEY);
    plat_lights(b | ui.btn, k | ui.keys, 0, 0, 1);
    plat_lights_mid(0, 0);
}

static void draw_probe(void)
{
    probe_t o;
    uint32_t n = 0;
    probe_info(ui.probe_item, &o);
    cv_begin(240, 120, LAB_BG);
    lab_paper();
    cv_rect(0, 0, 240, 27, LAB_BG);
    cv_rect(4, 5, text_w(&FONT_XS, "PROBE") + 10, 17, FIRE_C);
    cv_text(9, 6, &FONT_XS, "PROBE", C_BLACK);
    cv_text(text_w(&FONT_XS, "PROBE") + 22, 6, &FONT_XS, orchid && ui.probe_from == SCR_HOME ? "SURFACE" : "CORE",
            orchid && ui.probe_from == SCR_HOME ? AIR_C : EARTH_C);
    if (!o.title) {
        {
            uint32_t h;
            for (h = 0; h < 2u; h++) {
                int32_t dy = (int32_t)h * 120;
                if (h) {
                    cv_begin(240, 120, LAB_BG);
                    lab_paper();
                }
                cv_rect(0, 28 - dy, 240, 212, LAB_BG);
                cv_text(10, 32 - dy, &FONT_M, "TOUCH ANYTHING", FIRE_C);
                cv_text(10, 66 - dy, &FONT_L, "A KNOB, BUTTON", C_WHITE);
                cv_text(10, 99 - dy, &FONT_L, "OR KEY: IT SAYS", C_WHITE);
                cv_text(10, 132 - dy, &FONT_L, "WHAT IT DOES.", C_WHITE);
                cv_text(10, 182 - dy, &FONT_M, "TAP ARP TO LEAVE", LAB_LABEL);
                cv_blit(0, h * 120u);
            }
        }
        return;
    }
    {
        /* big: the title, then what it does, then its second jobs, each row in the colour of whose it is */
        char rows[8][48], tmp[4][48], t[96];
        uint16_t col[8];
        uint32_t nr = 0, h, k;
        n = guide_wrap_f(&FONT_M, o.what, 224, tmp, 4);
        for (k = 0; k < n && nr < 8u; k++, nr++) {
            str_set(rows[nr], tmp[k], 48);
            col[nr] = C_WHITE;
        }
        if (o.om) {
            str_set(t, "OCT-: ", sizeof t);
            text_cat(t, o.om);
            n = guide_wrap_f(&FONT_M, t, 224, tmp, 3);
            for (k = 0; k < n && nr < 7u; k++, nr++) {
                str_set(rows[nr], tmp[k], 48);
                col[nr] = EARTH_C;
            }
        }
        if (o.op) {
            str_set(t, "OCT+: ", sizeof t);
            text_cat(t, o.op);
            n = guide_wrap_f(&FONT_M, t, 224, tmp, 3);
            for (k = 0; k < n && nr < 7u; k++, nr++) {
                str_set(rows[nr], tmp[k], 48);
                col[nr] = AIR_C;
            }
        }
        for (h = 0; h < 2u; h++) {
            int32_t dy = (int32_t)h * 120;
            if (h) {
                cv_begin(240, 120, LAB_BG);
                lab_paper();
            }
            cv_rect(0, 28 - dy, 240, 212, LAB_BG);
            cv_text(10, 30 - dy, &FONT_M, o.title, FIRE_C);
            for (k = 0; k < nr; k++)
                cv_text(10, 58 + (int32_t)k * 25 - dy, &FONT_M, rows[k], col[k]);
            cv_blit(0, h * 120u);
        }
    }
}
