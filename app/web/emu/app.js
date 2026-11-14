// ChordSmith FM's own part of the emulator page. Copyright (C) 2026 BeatSmith Media (BeatSmith).
// "Load MIDI file" puts a .mid on the emulated FM-1's drive, as copying it over USB does on the device, and opens it
// in SLICE. The worklet's side is app_worklet.js.

export function attach(ctx) {
  const bar = document.querySelector(".bar");
  if (!bar) return;
  const input = Object.assign(document.createElement("input"),
    { type: "file", accept: ".mid,.midi,audio/midi,audio/x-midi", hidden: true });
  const btn = Object.assign(document.createElement("button"), { type: "button", textContent: "Load MIDI file" });
  btn.addEventListener("click", () => input.click());
  input.addEventListener("change", async () => {
    const f = input.files && input.files[0];
    if (!f) return;
    const buf = await f.arrayBuffer();
    input.value = "";
    ctx.node.port.postMessage({ type: "app", importMidi: buf, name: f.name }, [buf]);
  });
  bar.append(btn, input);
  attach.status = ctx.status;
}

export function message(m) {
  if (m && typeof m.imported === "number" && m.imported !== 0 && attach.status)
    attach.status(m.imported === -9 ? "That file is too big for the drive (96 KB at most)."
      : m.imported === -2 ? "The drive is full. Delete files on it first." : "The file could not be copied.");
}
