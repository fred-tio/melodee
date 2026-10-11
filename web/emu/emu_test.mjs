// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The browser build (build/emu/melodee.wasm, web/emu/melodee_web.c) in Node, as the worklet runs it. After Felucca's
// web/emu/emu_test.mjs, itself after X0X's (charlesvestal/fm1-x0x, GPL-3.0).
//   node web/emu/emu_test.mjs [build/emu/melodee.wasm] [NATIVE.f32]
// Checks: it boots (the splash, then HOME and the HOME LED), the second core is online (the Standard build's voice
// caps), a held key sounds and lights its LED, a knob changes the screen, Web MIDI notes sound, PLAY starts the
// sequencer; a sound saved on the panel (SAVE, OCT+ x3) reaches the flash and the sectors, kept as the page keeps
// them, bring it back in a fresh instance; the heavy song twice gives the same samples and, with NATIVE.f32
// (web/emu/native_check.c), the native build's, bit for bit; then the cost: 1 s of the heavy song against real time
// (BENCH_MAX, default 0.5).
import fs from "fs";
import { storeSectors, restoreSectors } from "./worklet.js";

const wasmPath = process.argv[2] || "build/emu/melodee.wasm", nativePath = process.argv[3];
const bytes = fs.readFileSync(wasmPath);
const B = { FX: 0, SCL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11, OCTDN: 12, OCTUP: 13 };
const EN = { SELECT: 0, ALGO: 1, PRESETS: 2, K1: 3, K2: 4, K3: 5, K4: 6 };
let fails = 0;
const check = (what, ok) => { console.log(`emu: ${what.padEnd(84)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };

async function device(sectors) {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const ex = instance.exports;
  ex._initialize?.();
  ex.web_nor_erase();
  if (sectors) restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), sectors);
  ex.web_boot();
  let tape = null;
  const rms = (n) => {
    const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), n), r = new Float32Array(ex.memory.buffer, ex.web_out_r(), n);
    let s = 0;
    for (let i = 0; i < n; i++) s += l[i] * l[i] + r[i] * r[i];
    if (tape) for (let i = 0; i < n; i++) tape.push(l[i], r[i]);
    return Math.sqrt(s / (2 * n));
  };
  const run = (ms) => {                          // 128-frame calls, as an AudioWorklet asks; the loudest one
    let peak = 0;
    for (let k = 0, n = ms * 441 / 10; k < n; k += 128) {
      const m = Math.min(128, n - k);
      ex.web_render(m);
      peak = Math.max(peak, rms(m));
    }
    return peak;
  };
  const screen = () => new Uint16Array(ex.memory.buffer, ex.web_screen(), 240 * 240).slice();
  const press = (b) => { ex.web_buttons(1 << b); run(60); ex.web_buttons(0); run(300); };
  const sectorsNow = () => storeSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()));
  const record = (on) => { const t = tape; tape = on ? [] : null; return t; };
  return { ex, run, screen, press, sectorsNow, record };
}
const differ = (a, b) => a.length !== b.length || a.some((v, i) => v !== b[i]);

// ---- M1, M2: boot, panel, sound
const d = await device();
d.run(200);
const splash = d.screen();
check("splash: drawn, HOME LED still dark", new Set(splash).size > 2 && !(d.ex.web_lit_buttons() >> B.HOME & 1));
d.run(1000);
const home = d.screen();
check("HOME: the screen changed after the splash, the HOME LED lit",
  differ(home, splash) && (d.ex.web_lit_buttons() >> B.HOME & 1) === 1);
check("second core online (Standard voice caps)", d.ex.web_core1_online() === 1);
check("silence before a key", d.run(300) < 1e-6);
d.ex.web_keys(1 << 12);
check("a held key sounds", d.run(400) > 0.01);
check("its LED is lit", (d.ex.web_lit_keys() >> 12 & 1) === 1);
d.ex.web_keys(0);
d.run(1500);
check("released: the sound ends", d.run(200) < 1e-4);
const before = d.screen();
d.ex.web_enc(EN.K1, 3);
d.run(300);
check("KNOB 1 turned: the screen changes", differ(d.screen(), before));
d.ex.web_enc(EN.K1, -3);
d.run(1500);
check("Web MIDI: a note on sounds", d.ex.web_midi(0x90, 60, 100) === 1 && d.run(400) > 0.01);
d.ex.web_midi(0x80, 60, 0);
d.run(1500);
check("Web MIDI: its note off ends it", d.run(200) < 1e-4);
d.press(B.PLAY);
check("PLAY starts the sequencer and lights PLAY", d.ex.web_playing() === 1 && (d.ex.web_lit_buttons() >> B.PLAY & 1) === 1);

// ---- M3: a sound saved on the panel survives a reload; a fresh device does not have it
const a = await device();
a.run(1500);
const writes0 = a.ex.web_flash_writes_count();
a.press(B.SAVE);
a.press(B.OCTUP);                                 // the slot's sheet, "Save here"
a.press(B.OCTUP);                                 // the name
a.press(B.OCTUP);                                 // saved
check("SAVE, OCT+ x3: the sound is written to the flash", a.ex.web_flash_writes_count() > writes0);
a.press(B.HOME);
a.press(B.SAVE);
const savedPage = a.screen();
const b = await device(a.sectorsNow());
b.run(1500);
b.press(B.SAVE);
check("a fresh instance with the kept sectors shows the saved slot", !differ(b.screen(), savedPage));
const c = await device();
c.run(1500);
c.press(B.SAVE);
check("a fresh instance without them (reset) does not", differ(c.screen(), savedPage));

// ---- M4: the heavy song, deterministic, the native build's samples, the cost
async function heavy() {
  const h = await device();
  h.run(1500);
  h.ex.web_test_heavy();
  h.ex.web_buttons(1 << B.PLAY);
  h.run(60);
  h.ex.web_buttons(0);
  h.record(true);
  h.run(2000);
  return { h, samples: Float32Array.from(h.record(false)) };
}
const h1 = await heavy(), h2 = await heavy();
check("heavy song: playing and loud", h1.h.ex.web_playing() === 1 && h1.samples.reduce((m, v) => Math.max(m, Math.abs(v)), 0) > 0.05);
check("heavy song twice: the same samples (deterministic)", !differ(h1.samples, h2.samples));
if (nativePath) {
  const native = new Float32Array(fs.readFileSync(nativePath).buffer.slice(0));
  check(`the native build's samples, bit for bit (${native.length / 2} frames)`, !differ(h1.samples, native));
} else {
  console.log("emu: native comparison skipped (no NATIVE.f32: web/emu/native_check.c)");
}
const max = Number(process.env.BENCH_MAX || 0.5), t0 = performance.now();
h1.h.run(1000);
const cost = (performance.now() - t0) / 1000;
check(`1 s of the heavy song in ${(cost * 1000).toFixed(0)} ms: at most ${max * 100} % of real time`, cost <= max);

console.log(fails ? `emu: ${fails} FAILED` : "emu: all passed");
process.exit(fails ? 1 : 0);
