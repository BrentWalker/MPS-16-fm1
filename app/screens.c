/* MPS-16 ver FM1: the KEY, FX and LIB screens. Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence: to be
 * chosen. Part of app.c (after the other screens). */

static void draw_key(void)
{
    char t[24];
    uint32_t i;
    int32_t x;
    const uint8_t *sc = SCALES[set.scale];
    lab_begin();
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
    cv_text(x + 12, 72, &FONT_B, set.lock ? t : "SCALE LOCK OFF", C_GRAY);
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
    lab_begin();
    for (i = 0; i < 7u; i++) {
        char rn[6];
        uint8_t dim;
        int32_t cx = 6 + (int32_t)i * 33, w;
        roman_of(i, rn, &dim);
        cv_rect(cx, 0, 30, 22, C_LINE);
        w = text_w(&FONT_B, rn) + (dim ? 5 : 0);
        x = cv_text(cx + (30 - w) / 2, 2, &FONT_B, rn, C_GRAY);
        if (dim)
            deg_sign(x + 1, 4, C_GRAY);
    }
    cv_text(10, 30, &FONT_B, "TAP PADS: FIND A KEY", AMBER);
    if (cur_file >= 0)
        cv_text(10, 50, &FONT_B, "PLAY: THE FILE'S KEY", C_GRAY);
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
    cv_text(x + 8, y + 2, &FONT_XS, t, C_GRAY);
    cv_text(x + 8, y + 14, fit_font(val, 100, &FONT_M), val, C_WHITE);
    cv_rect(x + 8, y + 40, 96, 4, C_LINE);
    if (fill > 0)
        cv_rect(x + 8, y + 40, fill * 96 / 100, 4, AMBER);
}

static void draw_fx(void)
{
    char a[12], b[12], c[12], d[12];
    lab_begin();
    top_bar("FX", ui.fx_page ? "2/2  FX: BACK" : "1/2  FX: MORE", C_GRAY);
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
    lab_begin();
    if (!ui.fx_page) {
        fx_tile(6, 0, "K3", "HUMANIZE", c, set.human * 2);
        fx_tile(122, 0, "K4", "INVERT", d, (set.inv + 3) * 100 / 6);
    }
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
    int32_t first = lib_sel > 2u ? (int32_t)lib_sel - 2 : 0;
    lab_begin();
    key_text(t);
    text_cat(t, " ");
    text_cat(t, lib_grp ? RHY_NAME[lib_rhy] : CTYPE_NAME[lib_type]);
    top_bar("LIB", t, C_GRAY);
    for (i = 0; i < 4u && (uint32_t)first + i < cnt; i++) {
        uint32_t r = (uint32_t)first + i;
        int32_t y = 31 + (int32_t)i * 22, x;
        int cur = r == lib_sel;
        pad_t tmp;
        uint16_t fg = cur ? C_BLACK : C_GRAY;
        if (cur)
            cv_rect(4, y, 232, 21, AMBER);
        y += 2;
        if (!lib_grp) {
            char rn[6];
            uint8_t dim;
            int32_t v[6];
            uint32_t n = lib_chord(r, lib_type, v), k;
            roman_of(r, rn, &dim);
            x = cv_text(10, y, &FONT_B, rn, fg);
            if (dim)
                deg_sign(x + 1, y + 3, fg);
            tmp.n = (uint8_t)n;
            for (k = 0; k < n; k++)
                tmp.note[k] = (uint8_t)v[k];
            tmp.root_pc = (uint8_t)(v[0] % 12);
            name_chord(&tmp);
            spell((uint32_t)v[0] % 12u, tonic_letter() + r, t);
            text_cat(t, tmp.qual);
            cv_text(64, y, &FONT_B, t, cur ? C_BLACK : C_WHITE);
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
            cv_text(10, y, &FONT_B, t, cur ? C_BLACK : C_WHITE);
        }
    }
    cv_blit(0, 0);
    lab_begin();
    cv_text(10, 8, &FONT_B, "A PAD: DROP IT THERE", C_GRAY);
    cv_text(10, 32, &FONT_B, "K4: A WHOLE KIT AT ONCE", C_GRAY);
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

/* TRIM: the pad played last. A phrase as a piano roll, its window shaded between START (green) and END (red), the
 * playhead white; a chord, its notes. K1 START, K2 END, K3 TUNE, K4 LEVEL; SELECT: ONE-SHOT, GATE or LOOP */
static void draw_trim(void)
{
    static const char *const LBL[4] = {"START", "END", "TUNE", "LEVEL"};
    const pad_t *p = &pads[ui.pad < 0 ? 0 : ui.pad];
    char t[32], v[4][12];
    uint32_t k, len = p->len ? p->len : 1u, ws = p->ts, we = pad_end(p);
    int phrase = p->type == PAD_PHRASE;
    {
        char n[8] = "PAD ";
        num_text(ui.pad + 1, n + 4, 0);
        lab_begin();
        top_bar("TRIM", n, C_GRAY);
    }
    if (phrase) {
        int32_t lo = 127, hi = 0, x0 = 8, w = 224, y0 = 34, h = 80, xs, xe;
        for (k = 0; k < p->pn && k < MAXPH; k++) {
            if (ph[ui.pad][k].p < lo)
                lo = ph[ui.pad][k].p;
            if (ph[ui.pad][k].p > hi)
                hi = ph[ui.pad][k].p;
        }
        if (hi - lo < 11) {              /* at least an octave tall */
            lo -= (11 - (hi - lo)) / 2;
            hi = lo + 11;
        }
        xs = x0 + (int32_t)(ws * (uint32_t)w / len);
        xe = x0 + (int32_t)(we * (uint32_t)w / len);
        cv_rect(x0, y0, w, h, RGB(14, 14, 18));
        cv_rect(xs, y0, xe - xs, h, RGB(16, 36, 34));
        for (k = 0; k < p->pn && k < MAXPH; k++) {
            const pnote_t *x = &ph[ui.pad][k];
            int32_t nx = x0 + (int32_t)(x->s * (uint32_t)w / len), nw = (int32_t)(x->d * (uint32_t)w / len);
            int32_t ny = y0 + h - 4 - (x->p - lo) * (h - 6) / (hi - lo + 1);
            int in = x->s >= ws && x->s < we;
            if (nw < 2)
                nw = 2;
            if (nx + nw > x0 + w)
                nw = x0 + w - nx;
            cv_rect(nx, ny, nw, 3, in ? TEAL : TEAL_DIM);
        }
        cv_rect(xs, y0, 2, h, RGB(92, 207, 122));
        cv_rect(xe - 2 > xs ? xe - 2 : xs, y0, 2, h, RGB(224, 98, 92));
        if (pl[ui.pad].on) {
            uint32_t pos = (uint32_t)pl[ui.pad].pos;
            cv_rect(x0 + (int32_t)((pos < len ? pos : len) * (uint32_t)w / len), y0, 1, h, C_WHITE);
        }
    } else {
        int32_t tw = 0, x;
        for (k = 0; k < p->n; k++)       /* its notes, as on HOME */
            tw += text_w(&FONT_B, p->nm[k]) + 16;
        x = (240 - tw + 16) / 2;
        for (k = 0; k < p->n; k++) {
            int32_t nw = text_w(&FONT_B, p->nm[k]);
            cv_rect(x - 5, 52, nw + 10, 26, C_LINE);
            cv_text(x, 53, &FONT_B, p->nm[k], C_WHITE);
            x += nw + 16;
        }
        cv_text((240 - text_w(&FONT_B, "A CHORD: TUNE AND LEVEL")) / 2, 92, &FONT_B, "A CHORD: TUNE AND LEVEL",
                C_GRAY);
    }
    cv_blit(0, 0);

    lab_begin();
    pad_title(p, t);
    cv_text(10, 2, fit_font(t, 224, &FONT_M), t, phrase ? TEAL : AMBER);
    if (phrase) {
        char l[12];
        fmt_len(we - ws, l);
        str_set(t, l, sizeof t);
        text_cat(t, "  ");
        text_cat(t, PM_NAME[p->pmode % 3u]);
        cv_text(10, 36, &FONT_B, t, C_WHITE);
        fmt_pos(ws, v[0]);
        fmt_pos(we, v[1]);
    } else {
        str_set(t, "CHORD  ", sizeof t);
        text_cat(t, p->roman);
        cv_text(10, 36, &FONT_B, t, C_WHITE);
        str_set(v[0], "-", sizeof v[0]);
        str_set(v[1], "-", sizeof v[1]);
    }
    num_text(p->tune, v[2], 1);
    num_text(100 - p->cut, v[3], 0);
    bottom_bar(68, LBL, v);
    cv_blit(0, 120);
}

/* FIND: the notes tapped, the keys that have them all (best first), the picked key's notes with the tapped lit */
static void draw_find(void)
{
    static const char *const LBL[4] = {"UNDO", "CLEAR", "", "USE"};
    char t[24], v[4][12] = {"", "", "", "LIB"};
    uint32_t i;
    int32_t first = fd_sel > 1u ? (int32_t)fd_sel - 1 : 0, x = 6;
    lab_begin();
    str_set(t, "", sizeof t);
    num_text(fd_cnt, t, 0);
    text_cat(t, fd_cnt == 1u ? " KEY" : " KEYS");
    top_bar("FIND", fd_n ? t : "TAP NOTES", C_GRAY);
    if (!fd_n) {
        cv_text((240 - text_w(&FONT_M, "TAP A FEW NOTES")) / 2, 46, &FONT_M, "TAP A FEW NOTES", AMBER);
        cv_text((240 - text_w(&FONT_B, "ANY A, B OR C PADS")) / 2, 82, &FONT_B, "ANY A, B OR C PADS", C_GRAY);
        cv_blit(0, 0);
        lab_begin();
        bottom_bar(68, LBL, v);
        cv_blit(0, 120);
        return;
    }
    for (i = 0; i < fd_n; i++) {         /* the notes tapped */
        char n[4];
        pc_name(fd_seq[i], n);
        cv_rect(x, 31, text_w(&FONT_B, n) + 10, 21, i == 0 ? AMBER : C_LINE);
        cv_text(x + 5, 33, &FONT_B, n, i == 0 ? C_BLACK : C_WHITE);
        x += text_w(&FONT_B, n) + 14;
    }
    for (i = 0; i < 3u && (uint32_t)first + i < fd_cnt; i++) {   /* the keys */
        uint32_t r = (uint32_t)first + i;
        int cur = r == fd_sel;
        int32_t y = 56 + (int32_t)i * 21;
        if (cur)
            cv_rect(4, y, 232, 20, AMBER);
        fd_name(fd_list[r], t);
        cv_text(10, y + 2, &FONT_B, t, cur ? C_BLACK : C_WHITE);
    }
    cv_blit(0, 0);

    lab_begin();
    if (fd_cnt) {                        /* the picked key's notes, the ones tapped lit */
        uint32_t e = fd_list[fd_sel], root = e >> 4, sc = e & 15u, l = letter_of(root, sc);
        for (i = 0; i < 7u; i++) {
            char n[4];
            uint32_t pc = (root + SCALES[sc][i]) % 12u;
            int on = (fd_pcs >> pc) & 1u;
            int32_t cx = 6 + (int32_t)i * 33;
            spell(pc, l + i, n);
            cv_rect(cx, 6, 30, 24, on ? AMBER : C_LINE);
            cv_text(cx + (30 - text_w(&FONT_B, n)) / 2, 7, &FONT_B, n, on ? C_BLACK : C_GRAY);
        }
        cv_text(10, 40, &FONT_B, "SELECT: PICK  K4: USE IT", C_GRAY);
    } else {
        cv_text(10, 10, &FONT_B, "NO KEY HAS ALL OF THEM.", AMBER);
        cv_text(10, 32, &FONT_B, "K1: UNDO THE LAST NOTE", C_GRAY);
    }
    bottom_bar(68, LBL, v);
    cv_blit(0, 120);
}

/* SURFACE (ORCHID in the code): the Earth Science look. Graph paper; the file as Water (the slices, the ruler of the
 * song, the pads); the knobs in their elements: K1 Water, K2 Earth (bass), K3 Air (above), K4 Fire (playing) */
static const uint16_t ORCHID_C = RGB(0x6B, 0xB6, 0xD6), ORCHID_DIM = RGB(26, 56, 70);   /* Water */
static void draw_orchid(void)
{
    static const char *const LBL[4] = {"K1 START", "K2 B PADS", "K3 C PADS", "K4 PLAY"};
    const uint16_t EL[4] = {WATER_C, EARTH_C, AIR_C, FIRE_C};
    char t[40], v[4][12];
    uint32_t i, first = set.om_slice == OM_EQUAL ? 0u : om_first(), len = song_len ? song_len : 1u;
    int32_t x;
    cv_begin(240, 120, LAB_BG);
    lab_paper();
    cv_rect(0, 0, 240, 27, LAB_BG);
    if (plat_ms() < ui.msg_until)
        top_bar(0, 0, 0);
    else {
        int32_t w = text_w(&FONT_B, "SURFACE");
        cv_rect(4, 3, w + 12, 21, AIR_C);   /* the mode: Air */
        cv_text(10, 5, &FONT_B, "SURFACE", C_BLACK);
        key_text(t);                     /* the key: Earth, outlined; a C pad's key change in Air beside it */
        w = text_w(&FONT_B, t);
        cv_rect(222 - w, 3, w + 14, 21, EARTH_C);
        cv_rect(224 - w, 5, w + 10, 17, LAB_BG);
        cv_text(229 - w, 5, &FONT_B, t, EARTH_C);
        if (set.fmove) {
            char f[3] = {'C', (char)('0' + set.fmove), 0};
            int32_t fw = text_w(&FONT_B, f);
            cv_rect(214 - w - fw, 3, fw + 10, 21, AIR_C);
            cv_text(219 - w - fw, 5, &FONT_B, f, C_BLACK);
        }
        cv_rect(0, 27, 240, 1, C_LINE);
    }
    if (!song_n) {
        cv_text(10, 34, &FONT_M, "NO MIDI FILE", WATER_C);
        cv_text(10, 66, &FONT_B, "A PADS PLAY YOUR KEY'S", C_WHITE);
        cv_text(10, 84, &FONT_B, "CHORDS. FILES: TAP SYS.", C_WHITE);
        cv_blit(0, 0);
    } else {
        cv_text(8, 30, &FONT_M, OM_NAME[set.om_slice], WATER_C);   /* how it's cut, big */
        if (nsl) {                       /* "1-16/33" */
            uint32_t last = first + 16u < nsl ? first + 16u : nsl;
            str_set(t, "", sizeof t);
            num_text((int32_t)first + 1, t, 0);
            text_cat(t, "-");
            num_text((int32_t)last, t + cs_len(t), 0);
            text_cat(t, "/");
            num_text((int32_t)nsl, t + cs_len(t), 0);
            cv_text(234 - text_w(&FONT_B, t), 36, &FONT_B, t, C_WHITE);
        }
        /* the song as a ruler: a tick a bar (longer every 4), the pads' stretch in Water */
        {
            uint32_t bar = PPQ * (song_bpb ? song_bpb : 4u), nb = (len + bar - 1u) / bar, b, every = 1;
            while (nb / every > 56u)
                every *= 2u;
            cv_rect(4, 63, 232, 1, RGB(70, 78, 84));
            for (b = 0; b <= nb; b += every) {
                int32_t tx = 4 + (int32_t)(b * bar * 232u / len), big = b % (4u * every) == 0u;
                if (tx > 236)
                    tx = 236;
                cv_rect(tx, big ? 58 : 60, 1, big ? 10 : 6, RGB(90, 98, 104));
            }
            if (nsl) {
                uint32_t last = (first + 16u < nsl ? first + 16u : nsl) - 1u;
                int32_t a = 4 + (int32_t)(sl_s[first] * 232u / len), z = 4 + (int32_t)(sl_e[last] * 232u / len);
                cv_rect(a, 60, z - a > 2 ? z - a : 2, 6, WATER_C);
            }
        }
        /* the 16 A pads, big, in Water */
        for (i = 0; i < 16u; i++) {
            const pad_t *p = &pads[i];
            int32_t bx = 4 + (int32_t)(i % 8u) * 29, by = 71 + (int32_t)(i / 8u) * 24;
            int lit = (ui.keys >> WK[i] & 1u) || pl[i].on || cp[i].on;
            uint16_t bg = p->type == PAD_EMPTY ? RGB(26, 29, 32) : lit ? WATER_C : ORCHID_DIM;
            char tb[8];
            const felucca_font_t *f;
            cv_rect(bx, by, 28, 22, bg);
            if (p->title[0])           /* a chord's name: CHORDS, or MUTATE's (moved by a bridge) */
                title_bridged(p->title, tb, sizeof tb);
            else
                num_text((int32_t)i + 1, tb, 0);
            f = text_w(&FONT_B, tb) <= 28 ? &FONT_B : &FONT_XS;
            cv_text(bx + (28 - text_w(f, tb)) / 2, by + (f == &FONT_B ? 3 : 4), f, tb,
                    lit ? C_BLACK : p->title[0] ? C_WHITE : LAB_LABEL);
        }
        cv_blit(0, 0);
    }

    cv_begin(240, 120, LAB_BG);
    lab_paper();
    cv_rect(0, 0, 240, 120, LAB_BG);
    if (ui.pad >= 0 && pads[ui.pad].type == PAD_PHRASE && song_bpb) {   /* the A pad played last */
        const pad_t *p = &pads[ui.pad];
        str_set(t, "PAD ", sizeof t);
        num_text(ui.pad + 1, t + 4, 0);
        if (p->title[0]) {
            char tb[8];
            title_bridged(p->title, tb, sizeof tb);
            text_cat(t, ": ");
            text_cat(t, tb);
        }
        x = cv_text(8, 0, &FONT_B, t, WATER_C);
        fmt_len(pad_end(p) - p->ts, v[0]);
        cv_text(234 - text_w(&FONT_B, v[0]), 0, &FONT_B, v[0], C_WHITE);
        (void)x;
    } else if (set.om_mut) {
        x = cv_text(8, 0, &FONT_B, "MUTATE", LAB_LABEL);
        cv_text(x + 8, 0, &FONT_B, MU_NAME[set.om_mut % MU_N], AMBER);
    } else
        cv_text(8, 0, &FONT_B, "HOLD HOME: CORE", LAB_LABEL);
    if (set.om_slice == OM_EQUAL || !nsl)
        str_set(v[0], "-", sizeof v[0]);
    else {
        str_set(v[0], "", sizeof v[0]);
        num_text((int32_t)first + 1, v[0], 0);
    }
    str_set(v[1], MODE_NAME[set.mode % MODE_N], sizeof v[1]);
    str_set(v[2], FK_MODE[set.om_f4 % FK_N], sizeof v[2]);
    str_set(v[3], OP_NAME[set.om_play % OP_N], sizeof v[3]);
    cv_rect(0, 21, 240, 1, RGB(42, 48, 53));
    for (i = 0; i < 4u; i++) {           /* the four knobs, two by two, big, each in its element */
        int32_t cx = (int32_t)(i % 2u) * 120, cy = 24 + (int32_t)(i / 2u) * 48;
        cv_text(cx + 8, cy + 1, &FONT_XS, LBL[i], LAB_LABEL);
        fit_text_c(cx + 4, 112, cy + 14, v[i], &FONT_M, EL[i]);
        cv_rect(cx + 8, cy + 42, 104, 3, EL[i]);
    }
    cv_blit(0, 120);
}
