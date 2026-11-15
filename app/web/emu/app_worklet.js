// MPS-16 ver FM1's own part of the emulator's worklet. Copyright (C) 2026 BeatSmith Media (BeatSmith).
// A .mid from the page (app.js) copied into the app's buffer and written to its drive (app.c cs_web_import).

function name83(name) {                  // "my song.mid" -> "MYSONG  MID"
  let base = String(name || "").replace(/\.[^.]*$/, "").toUpperCase().replace(/[^A-Z0-9_-]/g, "").slice(0, 8);
  if (!base) base = "IMPORT";
  return (base.padEnd(8, " ") + "MID");
}

globalThis.fm1App = {
  message(fm1, m) {
    if (!m || !(m.importMidi instanceof ArrayBuffer)) return;
    const ex = fm1.ex, bytes = new Uint8Array(m.importMidi);
    let r = -9;
    if (ex.cs_web_import && bytes.length <= ex.cs_web_buf_len()) {
      new Uint8Array(ex.memory.buffer, ex.cs_web_buf(), bytes.length).set(bytes);
      const nm = name83(m.name), dst = new Uint8Array(ex.memory.buffer, ex.cs_web_name(), 12);
      for (let i = 0; i < 11; i++) dst[i] = nm.charCodeAt(i);
      dst[11] = 0;
      r = ex.cs_web_import(bytes.length);
    }
    fm1.port.postMessage({ type: "app", imported: r });
  },
};
