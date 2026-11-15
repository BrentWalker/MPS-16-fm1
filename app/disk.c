/* MPS-16 ver FM1: the drive. Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence: to be chosen (see README).
 *
 * A small FAT12 volume in the flash's data area, 0x97000 .. 0xDBFFF (552 blocks of 512 bytes). In USB drive mode the
 * computer reads and writes it as a disk (plat_disk_t); the rest of the time the app lists the .mid files on it,
 * reads them in place (plat_data), and writes its own (REC's captures).
 *
 * Layout: block 0 the boot sector, 1-2 the first FAT, 3-4 the second, 5-8 the root directory (64 entries), then the
 * data, one block a cluster (clusters 2 .. 544).
 *
 * The flash erases 4 KiB at a time, so writes go through a one-sector cache: a block written lands in the cache,
 * and the cache is written back when another sector is touched or on flush. A write-back that only clears bits
 * (1 -> 0) programs in place without an erase. Part of app.c. */

#define DISK_LO 0x97000u
#define DISK_HI 0xDC000u                 /* the platform's panel calibration starts here (APP_STORE_MAP) */
#define DISK_BLOCKS ((DISK_HI - DISK_LO) / 512u)   /* 552 */
#define FAT_LBA 1u
#define FAT_BLOCKS 2u
#define ROOT_LBA 5u
#define ROOT_ENTRIES 64u
#define DATA_LBA 9u
#define NCLUST (DISK_BLOCKS - DATA_LBA)  /* 543 data clusters: 2 .. 544 */

static uint8_t dc_buf[4096];             /* the cached sector */
static int32_t dc_sec = -1;
static uint8_t dc_dirty;

static void dc_flush(void)
{
    uint32_t off, i;
    const uint8_t *f;
    int need_erase = 0;
    if (dc_sec < 0 || !dc_dirty)
        return;
    off = DISK_LO + (uint32_t)dc_sec * 4096u;
    f = plat_data(off);
    for (i = 0; i < 4096u; i++)
        if ((f[i] & dc_buf[i]) != dc_buf[i]) {
            need_erase = 1;
            break;
        }
    if (need_erase)
        plat_data_erase(off);
    plat_data_write(off, dc_buf, 4096u);
    dc_dirty = 0;
}

static const uint8_t *blk_read(uint32_t lba)
{
    if (lba >= DISK_BLOCKS)
        return 0;
    if (dc_sec == (int32_t)(lba / 8u))
        return dc_buf + (lba % 8u) * 512u;
    return plat_data(DISK_LO + lba * 512u);
}

static int blk_write(uint32_t lba, const uint8_t *b)
{
    int32_t sec = (int32_t)(lba / 8u);
    if (lba >= DISK_BLOCKS)
        return -1;
    if (dc_sec != sec) {
        dc_flush();
        __builtin_memcpy(dc_buf, plat_data(DISK_LO + (uint32_t)sec * 4096u), 4096u);
        dc_sec = sec;
    }
    __builtin_memcpy(dc_buf + (lba % 8u) * 512u, b, 512u);
    dc_dirty = 1;
    return 0;
}

static const plat_disk_t DISK = {DISK_BLOCKS, blk_read, blk_write, dc_flush, "BEATSMTH", "MPS-16 FM1"};

/* --------------------------------------------------------------- format --- */
static int disk_valid(void)
{
    const uint8_t *b = blk_read(0);
    return b[510] == 0x55 && b[511] == 0xAA && b[11] == 0x00 && b[12] == 0x02 && b[13] == 1 &&
           b[16] == 2 && (b[19] | b[20] << 8) == (int)DISK_BLOCKS;
}

static void disk_format(void)
{
    static const uint8_t BOOT[62] = {
        0xEB, 0x3C, 0x90, 'B', 'E', 'A', 'T', 'S', 'M', 'T', 'H', 0x00,   /* jump, OEM name */
        0x02, 0x01, 0x01, 0x00, 0x02,                                     /* 512 B/sector, 1/cluster, 1 reserved, 2 FATs */
        ROOT_ENTRIES & 0xFF, ROOT_ENTRIES >> 8,
        DISK_BLOCKS & 0xFF, DISK_BLOCKS >> 8, 0xF8, FAT_BLOCKS, 0x00,     /* sectors, media, sectors per FAT */
        0x20, 0x00, 0x02, 0x00, 0, 0, 0, 0, 0, 0, 0, 0,                   /* geometry, hidden, total32 */
        0x80, 0x00, 0x29, 0x16, 0x07, 0x19, 0x87,                         /* drive, sig, volume id */
        'C', 'H', 'O', 'R', 'D', 'S', 'M', 'I', 'T', 'H', ' ', 'F', 'A', 'T', '1', '2', ' ', ' ', ' '};
    static uint8_t b[512];
    uint32_t i;
    for (i = 0; i < 512u; i++)
        b[i] = 0;
    __builtin_memcpy(b, BOOT, sizeof BOOT);
    b[510] = 0x55;
    b[511] = 0xAA;
    blk_write(0, b);
    for (i = 0; i < 512u; i++)
        b[i] = 0;
    for (i = 0; i < FAT_BLOCKS; i++) {   /* both FATs: entries 0 and 1 reserved */
        b[0] = i ? 0 : 0xF8;
        b[1] = i ? 0 : 0xFF;
        b[2] = i ? 0 : 0xFF;
        blk_write(FAT_LBA + i, b);
        blk_write(FAT_LBA + FAT_BLOCKS + i, b);
    }
    for (i = 0; i < 512u; i++)
        b[i] = 0;
    for (i = 0; i < 4u; i++)
        blk_write(ROOT_LBA + i, b);
    __builtin_memcpy(b, "MPS-16     ", 11);   /* the volume label */
    b[11] = 0x08;
    blk_write(ROOT_LBA, b);
    dc_flush();
}

/* ------------------------------------------------------------------ FAT --- */
static uint32_t fat_byte(uint32_t i) { return blk_read(FAT_LBA + i / 512u)[i % 512u]; }

static uint32_t fat_get(uint32_t c)
{
    uint32_t i = c + c / 2u, v = fat_byte(i) | fat_byte(i + 1u) << 8;
    return c & 1u ? v >> 4 : v & 0xFFFu;
}

static void fat_set(uint32_t c, uint32_t v)
{
    static uint8_t b[512];
    uint32_t i = c + c / 2u, k, f;
    for (f = 0; f < 2u; f++) {           /* both copies */
        uint32_t base = FAT_LBA + f * FAT_BLOCKS;
        for (k = 0; k < 2u; k++) {
            uint32_t at = i + k, lba = base + at / 512u;
            uint8_t nb;
            __builtin_memcpy(b, blk_read(lba), 512u);
            if (c & 1u)
                nb = k ? (uint8_t)(v >> 4) : (uint8_t)((b[at % 512u] & 0x0Fu) | (v & 0x0Fu) << 4);
            else
                nb = k ? (uint8_t)((b[at % 512u] & 0xF0u) | (v >> 8 & 0x0Fu)) : (uint8_t)v;
            b[at % 512u] = nb;
            blk_write(lba, b);
        }
    }
}

/* ---------------------------------------------------------------- files --- */
#define MAXFILES 24u
#define MAXFBLK 192u                     /* a file's blocks: 96 KiB at most */
typedef struct {
    char name[22];                       /* the long name if it has one, else the 8.3 name */
    uint16_t clus;
    uint32_t size;
} file_t;
static file_t files[MAXFILES];
static uint32_t nfiles;

static void lfn_put(char *ln, const uint8_t *e)   /* one long-name entry's 13 characters, ASCII kept */
{
    static const uint8_t AT[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    uint32_t seq = (e[0] & 0x1Fu) - 1u, i;
    for (i = 0; i < 13u; i++) {
        uint32_t pos = seq * 13u + i, ch = e[AT[i]] | e[AT[i] + 1u] << 8;
        if (pos < 63u)
            ln[pos] = ch == 0 || ch == 0xFFFFu ? 0 : ch < 128u ? (char)ch : '?';
    }
}

static int is_midi(uint32_t clus)
{
    const uint8_t *b;
    if (clus < 2u || clus >= NCLUST + 2u)
        return 0;
    b = blk_read(DATA_LBA + clus - 2u);
    return b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd';
}

static void fs_scan(void)
{
    char ln[64];
    uint32_t i, k;
    int have_ln = 0;
    nfiles = 0;
    for (i = 0; i < ROOT_ENTRIES && nfiles < MAXFILES; i++) {
        const uint8_t *e = blk_read(ROOT_LBA + i / 16u) + (i % 16u) * 32u;
        if (e[0] == 0)
            break;
        if (e[0] == 0xE5) {
            have_ln = 0;
            continue;
        }
        if (e[11] == 0x0F) {             /* a long-name piece: they come last piece first */
            if (e[0] & 0x40u) {
                for (k = 0; k < 64u; k++)
                    ln[k] = 0;
                have_ln = 1;
            }
            lfn_put(ln, e);
            continue;
        }
        if (!(e[11] & 0x18u) && e[0] != '.' && is_midi(e[26] | e[27] << 8) && !(have_ln && ln[0] == '.')) {
            file_t *f = &files[nfiles++];
            f->clus = (uint16_t)(e[26] | e[27] << 8);
            f->size = (uint32_t)(e[28] | e[29] << 8 | e[30] << 16 | (uint32_t)e[31] << 24);
            if (have_ln && ln[0]) {
                for (k = 0; k < 21u && ln[k]; k++)
                    f->name[k] = ln[k];
                f->name[k] = 0;
            } else {                     /* NAME.EXT from the 8.3 entry */
                uint32_t n = 0;
                for (k = 0; k < 8u && e[k] != ' '; k++)
                    f->name[n++] = (char)e[k];
                f->name[n++] = '.';
                for (k = 8; k < 11u && e[k] != ' '; k++)
                    f->name[n++] = (char)e[k];
                f->name[n] = 0;
            }
        }
        have_ln = 0;
    }
}

/* a file's blocks, in order, so it reads as one array of bytes */
static uint16_t fblk[MAXFBLK];
static uint32_t fblk_n, fsize;
static int fs_open(const file_t *f)
{
    uint32_t c = f->clus, guard = NCLUST;
    fblk_n = 0;
    fsize = f->size;
    while (c >= 2u && c < NCLUST + 2u && fblk_n < MAXFBLK && guard--) {
        fblk[fblk_n++] = (uint16_t)(DATA_LBA + c - 2u);
        c = fat_get(c);
    }
    if (fsize > fblk_n * 512u)
        fsize = fblk_n * 512u;
    return fblk_n ? 0 : -1;
}
static uint32_t fs_byte(uint32_t i)      /* byte i of the open file (0 past its end) */
{
    if (i >= fsize)
        return 0;
    return blk_read(fblk[i / 512u])[i % 512u];
}

/* a new file in the root, written whole: REC's captures (and the simulator's imports). 0 = written */
static int fs_write(const char *name83, const uint8_t *data, uint32_t n)
{
    static uint8_t b[512];
    uint32_t need = (n + 511u) / 512u, i, c, prev = 0, first = 0, got = 0, slot = 0xFFFFu;
    for (i = 0; i < ROOT_ENTRIES; i++) { /* a free directory entry */
        const uint8_t *e = blk_read(ROOT_LBA + i / 16u) + (i % 16u) * 32u;
        if (e[0] == 0 || e[0] == 0xE5) {
            slot = i;
            break;
        }
    }
    if (slot == 0xFFFFu)
        return -1;
    for (c = 2; c < NCLUST + 2u && got < need; c++)   /* enough free clusters? */
        got += fat_get(c) == 0;
    if (got < need)
        return -2;
    for (c = 2, got = 0; c < NCLUST + 2u && got < need; c++) {
        uint32_t k, len;
        if (fat_get(c))
            continue;
        len = n - got * 512u < 512u ? n - got * 512u : 512u;
        for (k = 0; k < 512u; k++)
            b[k] = k < len ? data[got * 512u + k] : 0;
        blk_write(DATA_LBA + c - 2u, b);
        fat_set(c, 0xFFFu);
        if (prev)
            fat_set(prev, c);
        else
            first = c;
        prev = c;
        got++;
    }
    __builtin_memcpy(b, blk_read(ROOT_LBA + slot / 16u), 512u);
    {
        uint8_t *e = b + (slot % 16u) * 32u;
        for (i = 0; i < 32u; i++)
            e[i] = 0;
        __builtin_memcpy(e, name83, 11);
        e[11] = 0x20;                    /* archive */
        e[26] = (uint8_t)first;
        e[27] = (uint8_t)(first >> 8);
        e[28] = (uint8_t)n;
        e[29] = (uint8_t)(n >> 8);
        e[30] = (uint8_t)(n >> 16);
        e[31] = (uint8_t)(n >> 24);
    }
    blk_write(ROOT_LBA + slot / 16u, b);
    dc_flush();
    return 0;
}

/* a root entry by its 8.3 name ("KIT01   CSK"): its index, -1 none */
static int32_t fs_find(const char *name83)
{
    uint32_t i;
    for (i = 0; i < ROOT_ENTRIES; i++) {
        const uint8_t *e = blk_read(ROOT_LBA + i / 16u) + (i % 16u) * 32u;
        if (e[0] == 0)
            break;
        if (e[0] != 0xE5 && e[11] != 0x0F && !(e[11] & 0x18u) && !__builtin_memcmp(e, name83, 11))
            return (int32_t)i;
    }
    return -1;
}

/* delete a file by its 8.3 name: its clusters freed, its entry marked deleted. 0 = gone (or never there) */
static int fs_delete(const char *name83)
{
    static uint8_t b[512];
    int32_t i = fs_find(name83);
    uint32_t c, guard = NCLUST;
    if (i < 0)
        return 0;
    {
        const uint8_t *e = blk_read(ROOT_LBA + (uint32_t)i / 16u) + ((uint32_t)i % 16u) * 32u;
        c = e[26] | e[27] << 8;
    }
    while (c >= 2u && c < NCLUST + 2u && guard--) {
        uint32_t nx = fat_get(c);
        fat_set(c, 0);
        c = nx;
    }
    __builtin_memcpy(b, blk_read(ROOT_LBA + (uint32_t)i / 16u), 512u);
    b[((uint32_t)i % 16u) * 32u] = 0xE5;
    blk_write(ROOT_LBA + (uint32_t)i / 16u, b);
    dc_flush();
    return 0;
}

/* read a file by its 8.3 name into dst (at most max bytes): the bytes read, -1 none */
static int32_t fs_read(const char *name83, void *dst, uint32_t max)
{
    int32_t i = fs_find(name83);
    file_t f;
    uint32_t k, n;
    if (i < 0)
        return -1;
    {
        const uint8_t *e = blk_read(ROOT_LBA + (uint32_t)i / 16u) + ((uint32_t)i % 16u) * 32u;
        f.clus = (uint16_t)(e[26] | e[27] << 8);
        f.size = (uint32_t)(e[28] | e[29] << 8 | e[30] << 16 | (uint32_t)e[31] << 24);
    }
    if (fs_open(&f))
        return -1;
    n = fsize < max ? fsize : max;
    for (k = 0; k < n; k++)
        ((uint8_t *)dst)[k] = (uint8_t)fs_byte(k);
    return (int32_t)n;
}

static uint32_t fs_free_kb(void)
{
    uint32_t c, n = 0;
    for (c = 2; c < NCLUST + 2u; c++)
        n += fat_get(c) == 0;
    return n / 2u;
}

static void disk_init(void)
{
    if (!disk_valid())
        disk_format();
    fs_scan();
}
