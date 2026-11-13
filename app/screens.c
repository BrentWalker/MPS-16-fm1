/* ChordSmith FM: the KEY, FX and LIB screens. Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence: to be
 * chosen. Part of app.c (after the other screens). */

static void draw_key(void)
{
    char t[24];
    uint32_t i;
    int32_t x;
    const uint8_t *sc = SCALES[set.scale];
    cv_begin(240, 120, C_BLACK);
    top_bar("KEY", set.lock ? "LOCK ON" : "LOCK OFF", set.lock ? AMBER : C_GRAY);
    tonic_name(t);
    {
        char r[2] = {t[0], 0};
        x = cv_text(14, 40, &FONT_L, r, AMBER);
        if (t[1])
            x = cv_text(x, 40, &FONT_B, t + 1, AMBER);
    }
    cv_text(x + 12, 44, &FONT_M, SCALE_NAME[set.scale], C_WHITE);
    str_set(t, "STEP SHIFT ", sizeof t);
    num_text(set.steps, t + cs_len(t), 1);
    cv_text(x + 12, 72, &FONT_XS, set.lock ? t : "STEP SHIFT: LOCK OFF", C_GRAY);
    for (i = 0; i < 7u; i++) {           /* the notes in the key, their numerals under them */
        char nm[4], rn[6];
        uint8_t dim;
        int32_t cx = 6 + (int32_t)i * 33;
        spell((set.root + sc[i]) % 12u, tonic_letter() + i, nm);
        roman_of(i, rn, &dim);
        cv_rect(cx, 92, 30, 26, C_LINE);
        cv_text(cx + (30 - text_w(&FONT_B, nm)) / 2, 93, &FONT_B, nm, C_WHITE);
        (void)rn;
    }
    cv_blit(0, 0);
    cv_begin(240, 120, C_BLACK);
    for (i = 0; i < 7u; i++) {
        char rn[6];
        uint8_t dim;
        int32_t cx = 6 + (int32_t)i * 33, w;
        roman_of(i, rn, &dim);
        cv_rect(cx, 0, 30, 18, C_LINE);
        w = text_w(&FONT_XS, rn) + (dim ? 5 : 0);
        x = cv_text(cx + (30 - w) / 2, 1, &FONT_XS, rn, C_GRAY);
        if (dim)
            deg_sign(x + 1, 3, C_GRAY);
    }
    cv_text(10, 30, &FONT_T, cur_file >= 0 ? "PLAY: DETECT THE KEY OF THE FILE" : "OCT- + OCT+ TOGETHER: LOCK ON/OFF",
            C_DIM);
    cv_text(10, 46, &FONT_T, "LOCK: EVERY NOTE SNAPS TO THE KEY", C_DIM);
    {
        static const char *const LBL[4] = {"ROOT", "SCALE", "STEP", "LOCK"};
        char v[4][12];
        tonic_name(v[0]);
        str_set(v[1], SCALE_SHORT[set.scale], 12);
        num_text(set.steps, v[2], 1);
        str_set(v[3], set.lock ? "ON" : "OFF", 12);
        bottom_bar(68, LBL, v);
    }
    cv_blit(0, 120);
}

static void fx_tile(int32_t x, int32_t y, const char *k, const char *name, const char *val, int32_t fill)
{
    char t[20];
    str_set(t, k, sizeof t);
    text_cat(t, " ");
    text_cat(t, name);
    cv_rect(x, y, 112, 52, RGB(20, 20, 24));
    cv_text(x + 8, y + 3, &FONT_T, t, C_GRAY);
    cv_text(x + 8, y + 15, &FONT_B, val, C_WHITE);
    cv_rect(x + 8, y + 40, 96, 4, C_LINE);
    if (fill > 0)
        cv_rect(x + 8, y + 40, fill * 96 / 100, 4, AMBER);
}

static void draw_fx(void)
{
    char a[12], b[12], c[12], d[12];
    cv_begin(240, 120, C_BLACK);
    top_bar("FX", ui.fx_page ? "PAGE 2/2  FX: BACK" : "PAGE 1/2  FX: MORE", C_GRAY);
    if (!ui.fx_page) {
        num_text(set.strum * 2, a, 0);
        text_cat(a, " MS");
        num_text(set.human, c, 0);
        text_cat(c, "%");
        num_text(set.inv, d, 1);
        fx_tile(6, 34, "K1", "STRUM", a, set.strum);
        fx_tile(122, 34, "K2", "ARP", ARP_NAME[set.arp], set.arp * 25);
    } else {
        fx_tile(6, 34, "K1", "ARP RATE", ARP_RATE_NAME[set.arprate], set.arprate * 20);
        fx_tile(122, 34, "K2", "BASS NOTE", set.bass ? "ON" : "OFF", set.bass ? 100 : 0);
    }
    cv_blit(0, 0);
    cv_begin(240, 120, C_BLACK);
    if (!ui.fx_page) {
        fx_tile(6, 0, "K3", "HUMANIZE", c, set.human * 2);
        fx_tile(122, 0, "K4", "INVERT", d, (set.inv + 3) * 100 / 6);
    }
    cv_text(10, 54, &FONT_T, "ON CHORD PADS. PHRASES PLAY AS RECORDED.", C_DIM);
    {
        static const char *const L1[4] = {"STRUM", "ARP", "HUMAN", "INVERT"};
        static const char *const L2[4] = {"RATE", "BASS", "", ""};
        char v[4][12];
        if (!ui.fx_page) {
            num_text(set.strum * 2, v[0], 0);
            str_set(v[1], ARP_NAME[set.arp], 12);
            num_text(set.human, v[2], 0);
            text_cat(v[2], "%");
            num_text(set.inv, v[3], 1);
        } else {
            str_set(v[0], ARP_RATE_NAME[set.arprate], 12);
            str_set(v[1], set.bass ? "ON" : "OFF", 12);
            v[2][0] = 0;
            v[3][0] = 0;
        }
        bottom_bar(68, ui.fx_page ? L2 : L1, v);
    }
    cv_blit(0, 120);
    (void)b;
}

static void draw_lib(void)
{
    char t[24];
    uint32_t i, cnt = lib_count();
    int32_t first = lib_sel > 3u ? (int32_t)lib_sel - 3 : 0;
    cv_begin(240, 120, C_BLACK);
    key_text(t);
    text_cat(t, " ");
    text_cat(t, lib_grp ? RHY_NAME[lib_rhy] : CTYPE_NAME[lib_type]);
    top_bar("LIB", t, C_GRAY);
    for (i = 0; i < 5u && (uint32_t)first + i < cnt; i++) {
        uint32_t r = (uint32_t)first + i;
        int32_t y = 32 + (int32_t)i * 17, x;
        int cur = r == lib_sel;
        pad_t tmp;
        uint16_t fg = cur ? C_BLACK : C_GRAY;
        if (cur)
            cv_rect(4, y, 232, 16, AMBER);
        if (!lib_grp) {
            char rn[6];
            uint8_t dim;
            int32_t v[6];
            uint32_t n = lib_chord(r, lib_type, v), k;
            roman_of(r, rn, &dim);
            x = cv_text(10, y, &FONT_XS, rn, fg);
            if (dim)
                deg_sign(x + 1, y + 3, fg);
            tmp.n = (uint8_t)n;
            for (k = 0; k < n; k++)
                tmp.note[k] = (uint8_t)v[k];
            tmp.root_pc = (uint8_t)(v[0] % 12);
            name_chord(&tmp);
            spell((uint32_t)v[0] % 12u, tonic_letter() + r, t);
            text_cat(t, tmp.qual);
            cv_text(64, y, &FONT_XS, t, cur ? C_BLACK : C_WHITE);
        } else {
            char rn[6];
            uint8_t dim;
            uint32_t b;
            t[0] = 0;
            for (b = 0; b < 4u; b++) {
                roman_of(PROGS[r][b], rn, &dim);
                if (b)
                    text_cat(t, "  ");
                text_cat(t, rn);
            }
            cv_text(10, y, &FONT_XS, t, cur ? C_BLACK : C_WHITE);
        }
    }
    cv_blit(0, 0);
    cv_begin(240, 120, C_BLACK);
    cv_text(10, 4, &FONT_T, "SELECT: PICK AND HEAR    WHITE KEY: DROP ON PAD", C_DIM);
    cv_text(10, 20, &FONT_T, "K4: BUILD A WHOLE KIT (8 CHORDS, 8 PROGRESSIONS)", C_DIM);
    {
        static const char *const LBL[4] = {"TYPE", "RHYTHM", "GROUP", "KIT"};
        char v[4][12];
        str_set(v[0], CTYPE_NAME[lib_type], 12);
        str_set(v[1], lib_rhy == 2u ? "ARP" : RHY_NAME[lib_rhy], 12);
        str_set(v[2], lib_grp ? "PROGS" : "CHORDS", 12);
        str_set(v[3], "BUILD", 12);
        bottom_bar(68, LBL, v);
    }
    cv_blit(0, 120);
}
