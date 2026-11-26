/* SEED: MPS-16's chord builder for the M-VAVE FM-1. Built on fm1-base.
 * Copyright (C) 2026 BeatSmith Media (BeatSmith). Licence of this file: to be chosen (see README).
 * This header is the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "SEED"
#define APP_TAGLINE "AN MPS-16 FUNCTION FOR FM-1"
#define APP_SPLASH_RGB RGB(124, 93, 72)    /* Earth */
#define APP_BLOCK 256u                     /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                          /* velocity from the keys (lib/touch.h) */
#define APP_BLE_AT_BOOT (seed_ble_on())     /* Bluetooth MIDI at power-on: as SETUP left it */
int seed_ble_on(void);

/* the store: the app's objects and their flash offsets (the panel calibration is the platform's) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_SESS, OBJ_NOTES, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xD8000u, 0xDA000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks (host/plat_host.c "expect") */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
