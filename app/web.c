/* The browser emulator's exports for the page (web/emu/app.js, app_worklet.js): included by base/web/emu/emu_web.c
 * after the simulator, where __attribute__ means itself again. A .mid the page puts in cs_web_buf, its 8.3 name in
 * cs_web_name, goes onto the drive and opens in SLICE (app.c web_import). */
__attribute__((used, visibility("default"))) uint8_t *cs_web_buf(void) { return web_buf; }
__attribute__((used, visibility("default"))) uint32_t cs_web_buf_len(void) { return sizeof web_buf; }
__attribute__((used, visibility("default"))) char *cs_web_name(void) { return web_name; }
__attribute__((used, visibility("default"))) int cs_web_import(uint32_t n) { return web_import(n); }
