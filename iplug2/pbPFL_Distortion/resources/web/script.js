"use strict";

/* iPlug2 UI->DSP senders. These wrap the host-injected window.IPlugSendMsg and were
   part of the iPlug2 template's script.js — they MUST exist for SPVFUI/SAMFUI/etc. */
function __ipsend(m) { try { if (window.IPlugSendMsg) window.IPlugSendMsg(m); } catch (e) {} }
window.SPVFUI = (paramIdx, value) => __ipsend({ msg: "SPVFUI", paramIdx: parseInt(paramIdx), value });
window.BPCFUI = (paramIdx) => __ipsend({ msg: "BPCFUI", paramIdx: parseInt(paramIdx) });
window.EPCFUI = (paramIdx) => __ipsend({ msg: "EPCFUI", paramIdx: parseInt(paramIdx) });
window.SAMFUI = (msgTag, ctrlTag = -1, data = 0) => __ipsend({ msg: "SAMFUI", msgTag, ctrlTag, data });

/* =====================================================================================
   Host abstraction bridge — one UI for BOTH iPlug2 WebView and JUCE WebBrowserComponent
   (+ standalone browser preview). See knob-render.js for the procedural knob.
   ===================================================================================== */
const Host = (() => {
  const idToIndex = {};
  PLUGIN_PARAMS.forEach((p, i) => { idToIndex[p.id] = i; });
  idToIndex["bypass"] = BYPASS_PARAM_INDEX;

  const ranges = {};
  PLUGIN_PARAMS.forEach(p => { ranges[p.id] = { min: p.min, max: p.max }; });
  ranges["bypass"] = { min: 0, max: 1 };

  const isIPlug = () => typeof window.IPlugSendMsg === "function";
  const isJUCE  = () => !!(window.__JUCE__ && window.__JUCE__.backend);

  const norm = (id, real) => { const r = ranges[id] || { min: 0, max: 1 }; return (r.max === r.min) ? 0 : (real - r.min) / (r.max - r.min); };
  const denorm = (id, n) => { const r = ranges[id] || { min: 0, max: 1 }; return r.min + n * (r.max - r.min); };
  const indexToId = idx => (idx === BYPASS_PARAM_INDEX ? "bypass" : (PLUGIN_PARAMS[idx] ? PLUGIN_PARAMS[idx].id : null));

  let onParam = () => {};
  let onMeters = () => {};

  function sendParam(id, real) {
    const idx = idToIndex[id];
    if (idx === undefined) return;
    if (isIPlug()) window.SPVFUI(idx, norm(id, real));
    else if (isJUCE()) window.__JUCE__.backend.emitEvent("uiEvent", { event: "paramChange", id, value: real });
  }
  function beginGesture(id) { const idx = idToIndex[id]; if (idx !== undefined && isIPlug()) window.BPCFUI(idx); }
  function endGesture(id) { const idx = idToIndex[id]; if (idx !== undefined && isIPlug()) window.EPCFUI(idx); }
  function sendEvent(name, payload) {
    if (isJUCE()) window.__JUCE__.backend.emitEvent("uiEvent", Object.assign({ event: name }, payload || {}));
    else if (isIPlug()) {
      const tag = { uiReady: 0, requestState: 1, savePreset: 2, help: 3, openUrl: 4 }[name];
      if (tag === undefined) return;
      let data = 0;
      try { data = window.btoa(JSON.stringify(payload || {})); } catch (e) { data = 0; }
      window.SAMFUI(tag, -1, data);
    }
  }

  window.SPVFD = (paramIdx, normVal) => { const id = indexToId(paramIdx); if (id) onParam(id, denorm(id, normVal)); };
  window.SCVFD = () => {};
  window.SCMFD = () => {};
  window.SAMFD = (msgTag, dataSize, b64) => {
    if (msgTag !== -1) return;
    let json; try { json = JSON.parse(window.atob(b64)); } catch (e) { return; }
    if (json.id === "params" && Array.isArray(json.params)) {
      json.params.forEach(pp => { const id = indexToId(pp.id); if (id && ranges[id]) { ranges[id].min = pp.min; ranges[id].max = pp.max; } });
    } else if (json.id === "meters") { onMeters(json); }
  };
  window.SMMFD = () => {};
  window.SSMFD = () => {};
  window.OnParamChange = (idx, val) => window.SPVFD(idx, val);
  window.OnControlChange = () => {};
  window.OnMessage = () => {};

  window.handleHostMessage = (msg) => {
    if (!msg) return;
    if (msg.type === "state" && msg.payload) {
      if (Array.isArray(msg.payload.params)) msg.payload.params.forEach(hp => onParam(hp.id, hp.value));
      if (typeof msg.payload.bypass !== "undefined") onParam("bypass", msg.payload.bypass ? 1 : 0);
    } else if (msg.type === "meters" && msg.payload) {
      onMeters({ bpm: Number(msg.payload.bpm), inPeak: msg.payload.inPeak, outPeak: msg.payload.outPeak });
    }
  };

  return { sendParam, beginGesture, endGesture, sendEvent, setHandlers(p, m) { if (p) onParam = p; if (m) onMeters = m; }, isHosted() { return isIPlug() || isJUCE(); } };
})();

/* ===================================== UI state ===================================== */
const state = {
  bypass: false, activeAB: "A", presets: [], currentPresetIndex: 0, undoStack: [], redoStack: [], params: PLUGIN_PARAMS, ab: null,
  lfoOn: false, lfoType: 0, lfoRate: 1.0, lfoDepth: 0.44, lfoRateTempoSync: false, lfoRateSyncIndex: 7, lfoPhase: 0
};
let SYNC_BPM = 120;

const LFO_TYPE_NAMES = ["SINE", "TRI", "SAW", "RAMP", "SQR", "S&H"];
const SYNC_NOTES = [
  { label: "1/1D", beats: 6 }, { label: "1/1", beats: 4 }, { label: "1/1T", beats: 8 / 3 },
  { label: "1/2D", beats: 3 }, { label: "1/2", beats: 2 }, { label: "1/2T", beats: 4 / 3 },
  { label: "1/4D", beats: 1.5 }, { label: "1/4", beats: 1 }, { label: "1/4T", beats: 2 / 3 },
  { label: "1/8D", beats: 3 / 4 }, { label: "1/8", beats: 1 / 2 }, { label: "1/8T", beats: 1 / 3 },
  { label: "1/16D", beats: 3 / 8 }, { label: "1/16", beats: 1 / 4 }, { label: "1/16T", beats: 1 / 6 },
  { label: "1/32D", beats: 3 / 16 }, { label: "1/32", beats: 1 / 8 }, { label: "1/32T", beats: 1 / 12 },
  { label: "1/64D", beats: 3 / 32 }, { label: "1/64", beats: 1 / 16 }, { label: "1/64T", beats: 1 / 24 }
];

function clamp(v, min, max) { return Math.max(min, Math.min(max, v)); }
function msForNote(note) { return (60000 / SYNC_BPM) * note.beats; }
function hzForNote(note) { return 1000 / msForNote(note); }
function isTimeParam(p) { return p.unit === "ms"; }
function isRateParam(p) { return p.id === "rate"; }
function syncValueForNote(p, note) { return isRateParam(p) ? hzForNote(note) : msForNote(note); }
function canTempoSync(p) { return p.id === "rate"; }
function availableNotes(p) { return canTempoSync(p) ? SYNC_NOTES : []; }
function nearestNoteIndex(p, value) { const notes = availableNotes(p); if (!notes.length) return -1; if (Number.isInteger(p.syncIndex)) return clamp(p.syncIndex, 0, notes.length - 1); let best = 0, bestDelta = Infinity; notes.forEach((n, i) => { const d = Math.abs(syncValueForNote(p, n) - value); if (d < bestDelta) { best = i; bestDelta = d; } }); return best; }
function currentNote(p) { const notes = availableNotes(p); const i = nearestNoteIndex(p, p.value); return i >= 0 ? notes[i] : null; }
function rawNorm(p) { return (p.value - p.min) / (p.max - p.min); }
function displayNorm(p) { if (p.sync && canTempoSync(p)) { const notes = availableNotes(p); const i = nearestNoteIndex(p, p.value); return notes.length > 1 && i >= 0 ? i / (notes.length - 1) : 0; } return rawNorm(p); }
function snapshot() { return Object.fromEntries(state.params.map(p => [p.id, p.value])); }
function restore(data, notify = true) { state.params.forEach(p => setParam(p.id, data[p.id] ?? p.defaultValue, notify, false)); }
function pushUndo() { state.undoStack.push(snapshot()); if (state.undoStack.length > 64) state.undoStack.shift(); state.redoStack.length = 0; updateHistoryButtons(); }
function updateHistoryButtons() { document.getElementById("undoBtn")?.toggleAttribute("disabled", state.undoStack.length === 0); document.getElementById("redoBtn")?.toggleAttribute("disabled", state.redoStack.length === 0); }
function formatValue(p) { if (p.sync && canTempoSync(p)) return currentNote(p)?.label || `${p.value.toFixed(2)} ${p.unit}`; const decimals = p.step < 0.1 ? 2 : (p.step < 1 ? 1 : 0); return `${Number(p.value).toFixed(decimals)}${p.unit ? " " + p.unit : ""}`; }

/* ---- knob rendering ---- */
function renderKnobCanvas(p) {
  if (!p._canvas) return;
  const isRate = p.id === "rate";
  PBKnob.renderKnob(p._canvas, displayNorm(p), {
    ledRing: true,
    bipolarRing: p.min < 0 && p.max > 0,
    lfoRing: isRate && state.lfoOn,
    lfoPhase: state.lfoPhase,
    lfoDepth: state.lfoDepth,
    lfoType: state.lfoType
  });
}
function updateParamElement(p) {
  const el = p._cell;
  if (!el) return;
  renderKnobCanvas(p);
  const vEl = el.querySelector(".knob-value");
  if (vEl && !vEl.classList.contains("editing")) vEl.textContent = formatValue(p);
  el.querySelector('.knob-tab[data-tab="sync"]')?.classList.toggle("active", !!p.sync);
}

function setParam(id, value, notify = true, record = false) {
  const p = state.params.find(x => x.id === id);
  if (!p) return;
  if (record) pushUndo();
  if (p.sync && canTempoSync(p)) {
    const notes = availableNotes(p);
    p.syncIndex = nearestNoteIndex({ ...p, syncIndex: undefined }, Number(value));
    const note = notes[p.syncIndex];
    p.value = note ? syncValueForNote(p, note) : Number(value);
  } else {
    p.syncIndex = undefined;
    p.value = clamp(Number(value), p.min, p.max);
  }
  updateParamElement(p);
  if (notify) Host.sendParam(id, p.value);
}

function stepSyncedParam(p, direction, notify = true, record = true) { const notes = availableNotes(p); if (!notes.length) return; const current = nearestNoteIndex(p, p.value); const next = clamp(current + direction, 0, notes.length - 1); p.syncIndex = next; setParam(p.id, syncValueForNote(p, notes[next]), notify, record); }
function parseInputValue(p, raw) { const text = String(raw).trim(); if (p.sync && canTempoSync(p)) { const exact = availableNotes(p).find(n => n.label.toLowerCase() === text.toLowerCase()); if (exact) return syncValueForNote(p, exact); } return Number(text.replace(/[^0-9.+-]/g, "")); }

/* ---- main knobs (ImGui TexturedKnobFloat look) ---- */
function attachKnobDrag(p, knob, opts) {
  // opts: { get, set, range, syncAware }
  let startY = 0, startValue = 0, dragging = false, moved = false;
  const begin = y => { dragging = true; moved = false; startY = y; startValue = opts.get(); document.body.style.cursor = "ns-resize"; };
  const move = (y, fine) => {
    if (!dragging) return;
    if (opts.syncAware && p && p.sync && canTempoSync(p)) {
      const threshold = fine ? 22 : 12, delta = startY - y;
      if (Math.abs(delta) >= threshold) {
        if (!moved) { pushUndo(); Host.beginGesture(p.id); moved = true; }
        const notes = availableNotes(p);
        const current = nearestNoteIndex({ ...p, value: startValue }, startValue);
        const next = clamp(current + Math.trunc(delta / threshold), 0, notes.length - 1);
        setParam(p.id, syncValueForNote(p, notes[next]), true, false);
      }
      return;
    }
    const scale = (fine ? 0.0020 : 0.0050) * opts.range;
    if (!moved && Math.abs(startY - y) > 1) { moved = true; opts.onStart && opts.onStart(); }
    opts.set(startValue + (startY - y) * scale);
  };
  const end = () => { if (moved && opts.onEnd) opts.onEnd(); dragging = false; document.body.style.cursor = ""; };
  knob.addEventListener("pointerdown", e => { knob.setPointerCapture(e.pointerId); begin(e.clientY); e.preventDefault(); });
  knob.addEventListener("pointermove", e => move(e.clientY, e.shiftKey));
  knob.addEventListener("pointerup", end);
}

function makeKnob(p) {
  const isRate = p.id === "rate";
  p.sync = false;
  const cell = document.createElement("div");
  cell.className = "knob-cell";
  cell.dataset.param = p.id;
  cell.innerHTML =
    `<div class="knob-label">${p.name}</div>` +
    `<div class="knob-canvas-wrap"><canvas width="132" height="132"></canvas>` +
    (isRate ? `<div class="knob-tab left" data-tab="sync" title="Tempo Sync">S</div><div class="knob-tab right" data-tab="lfo" title="LFO Modulation">L</div>` : ``) +
    `</div>` +
    `<div class="knob-value"></div>`;
  p._cell = cell;
  p._canvas = cell.querySelector("canvas");
  const knob = p._canvas;

  attachKnobDrag(p, knob, {
    get: () => p.value, range: p.max - p.min, syncAware: true,
    set: v => setParam(p.id, v, true, false),
    onStart: () => { pushUndo(); Host.beginGesture(p.id); },
    onEnd: () => Host.endGesture(p.id)
  });
  knob.addEventListener("dblclick", () => setParam(p.id, p.defaultValue, true, true));
  knob.addEventListener("wheel", e => {
    if (p.sync && canTempoSync(p)) stepSyncedParam(p, e.deltaY < 0 ? 1 : -1, true, true);
    else setParam(p.id, p.value + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.005 : 0.02) * (p.max - p.min), true, true);
    e.preventDefault();
  }, { passive: false });

  if (isRate) {
    cell.querySelector('[data-tab="sync"]').addEventListener("click", e => {
      e.stopPropagation(); pushUndo(); p.sync = !p.sync;
      if (p.sync) { p.syncIndex = nearestNoteIndex({ ...p, syncIndex: undefined }, p.value); const note = currentNote(p); if (note) p.value = syncValueForNote(p, note); }
      else { p.syncIndex = undefined; p.value = clamp(p.value, p.min, p.max); }
      updateParamElement(p); Host.sendParam(p.id, p.value);
    });
    cell.querySelector('[data-tab="lfo"]').addEventListener("click", e => { e.stopPropagation(); toggleLfoChip(); });
  }

  const valueEl = cell.querySelector(".knob-value");
  valueEl.addEventListener("dblclick", e => {
    e.stopPropagation(); valueEl.classList.add("editing");
    const input = document.createElement("input");
    input.className = "knob-value-input"; input.type = "text"; input.inputMode = "decimal";
    input.value = p.sync && canTempoSync(p) ? (currentNote(p)?.label || String(p.value)) : String(p.value);
    valueEl.textContent = ""; valueEl.appendChild(input); input.focus(); input.select();
    let committed = false;
    const finish = commit => { if (committed) return; committed = true; const next = parseInputValue(p, input.value); valueEl.classList.remove("editing"); if (commit && Number.isFinite(next)) setParam(p.id, next, true, true); else updateParamElement(p); };
    input.addEventListener("keydown", ev => { if (ev.key === "Enter") finish(true); if (ev.key === "Escape") finish(false); ev.stopPropagation(); });
    input.addEventListener("blur", () => finish(true));
  });
  return cell;
}

/* ---- LFO chip (matches DrawLfoChip: Type / Rate / Depth) ---- */
let lfoChipEl = null;

function renderLfoChip() {
  if (!lfoChipEl) return;
  const typeC = lfoChipEl.querySelector('[data-mini="type"] canvas');
  const rateC = lfoChipEl.querySelector('[data-mini="rate"] canvas');
  const depthC = lfoChipEl.querySelector('[data-mini="depth"] canvas');
  PBKnob.renderKnob(typeC, state.lfoType / (LFO_TYPE_NAMES.length - 1), { ledRing: true });
  const rateNorm = state.lfoRateTempoSync ? (state.lfoRateSyncIndex / (SYNC_NOTES.length - 1)) : ((state.lfoRate - 0.10) / (8.0 - 0.10));
  PBKnob.renderKnob(rateC, rateNorm, { ledRing: true });
  PBKnob.renderKnob(depthC, state.lfoDepth, { ledRing: true });
  lfoChipEl.querySelector('[data-mini="type"] .mini-value').textContent = LFO_TYPE_NAMES[state.lfoType];
  lfoChipEl.querySelector('[data-mini="rate"] .mini-value').textContent = state.lfoRateTempoSync ? SYNC_NOTES[state.lfoRateSyncIndex].label : state.lfoRate.toFixed(2) + "Hz";
  lfoChipEl.querySelector('[data-mini="depth"] .mini-value').textContent = state.lfoDepth.toFixed(2);
  lfoChipEl.querySelector('[data-mini="rate"] .mini-tab')?.classList.toggle("active", state.lfoRateTempoSync);
  drawLfoShape(lfoChipEl.querySelector('.mini-shape'), state.lfoType);
}

function drawLfoShape(canvas, type) {
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const w = canvas.width, h = canvas.height, pad = 2;
  ctx.clearRect(0, 0, w, h);
  ctx.lineJoin = "round"; ctx.lineCap = "round";
  for (let pass = 0; pass < 2; ++pass) {
    ctx.beginPath();
    for (let i = 0; i < 48; ++i) {
      const u = i / 47;
      const v = PBKnob.lfoShapeValue(u, type);
      const x = pad + u * (w - pad * 2);
      const y = (h - pad) - v * (h - pad * 2);
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.strokeStyle = pass === 0 ? "rgba(0,180,235,0.37)" : "rgba(230,236,240,1)";
    ctx.lineWidth = pass === 0 ? 3.2 : 1.4;
    ctx.stroke();
  }
}

function buildLfoChip() {
  const chip = document.createElement("div");
  chip.className = "lfo-chip hidden";
  chip.innerHTML =
    `<div class="mini" data-mini="type"><div class="mini-label">Type</div><div class="mini-wrap"><canvas width="100" height="100"></canvas></div><div class="mini-value"></div><canvas class="mini-shape" width="60" height="22"></canvas></div>` +
    `<div class="mini" data-mini="rate"><div class="mini-label">Rate</div><div class="mini-wrap"><canvas width="100" height="100"></canvas><div class="mini-tab" title="Tempo Sync">S</div></div><div class="mini-value"></div></div>` +
    `<div class="mini" data-mini="depth"><div class="mini-label">Depth</div><div class="mini-wrap"><canvas width="100" height="100"></canvas></div><div class="mini-value"></div></div>`;
  document.querySelector(".plugin-container").appendChild(chip);
  lfoChipEl = chip;

  // Type knob: snaps to 0..5
  attachKnobDrag(null, chip.querySelector('[data-mini="type"] canvas'), {
    get: () => state.lfoType / (LFO_TYPE_NAMES.length - 1), range: 1,
    set: n => { state.lfoType = clamp(Math.round(clamp(n, 0, 1) * (LFO_TYPE_NAMES.length - 1)), 0, LFO_TYPE_NAMES.length - 1); renderLfoChip(); }
  });
  // Rate knob
  attachKnobDrag(null, chip.querySelector('[data-mini="rate"] canvas'), {
    get: () => state.lfoRateTempoSync ? state.lfoRateSyncIndex / (SYNC_NOTES.length - 1) : (state.lfoRate - 0.10) / 7.90, range: 1,
    set: n => {
      n = clamp(n, 0, 1);
      if (state.lfoRateTempoSync) { state.lfoRateSyncIndex = clamp(Math.round(n * (SYNC_NOTES.length - 1)), 0, SYNC_NOTES.length - 1); state.lfoRate = 0.10 + (state.lfoRateSyncIndex / (SYNC_NOTES.length - 1)) * 7.90; }
      else state.lfoRate = 0.10 + n * 7.90;
      renderLfoChip();
    }
  });
  // Depth knob
  attachKnobDrag(null, chip.querySelector('[data-mini="depth"] canvas'), {
    get: () => state.lfoDepth, range: 1,
    set: v => { state.lfoDepth = clamp(v, 0, 1); renderLfoChip(); }
  });
  chip.querySelector('[data-mini="rate"] .mini-tab').addEventListener("click", e => { e.stopPropagation(); state.lfoRateTempoSync = !state.lfoRateTempoSync; renderLfoChip(); });
}

function positionLfoChip() {
  if (!lfoChipEl) return;
  const rate = state.params.find(p => p.id === "rate");
  const wrap = rate && rate._cell && rate._cell.querySelector(".knob-canvas-wrap");
  const container = document.querySelector(".plugin-container");
  if (!wrap || !container) return;
  const wr = wrap.getBoundingClientRect();
  const cr = container.getBoundingClientRect();
  const chipW = 174, chipH = 96, margin = 8;
  let left = (wr.right - cr.left) + 10;
  if (left + chipW > cr.width - margin) left = (wr.left - cr.left) - chipW - 6;
  left = clamp(left, margin, cr.width - chipW - margin);
  let top = (wr.top - cr.top) - 4;
  top = clamp(top, 60, cr.height - chipH - margin);
  const pointRight = left < (wr.left - cr.left); // chip placed to the LEFT of the knob
  lfoChipEl.classList.toggle("point-right", pointRight);
  const knobCenterY = (wr.top - cr.top) + wr.height * 0.5;
  lfoChipEl.style.setProperty("--ptr-y", clamp(knobCenterY - top - 9, 6, chipH - 24) + "px");
  lfoChipEl.style.left = left + "px";
  lfoChipEl.style.top = top + "px";
}

function toggleLfoChip() {
  state.lfoOn = !state.lfoOn;
  const rate = state.params.find(p => p.id === "rate");
  rate?._cell.querySelector('[data-tab="lfo"]')?.classList.toggle("active", state.lfoOn);
  if (state.lfoOn) { positionLfoChip(); renderLfoChip(); lfoChipEl.classList.remove("hidden"); }
  else lfoChipEl.classList.add("hidden");
  if (rate) renderKnobCanvas(rate);
}

function lfoAnimLoop() {
  if (state.lfoOn) {
    state.lfoPhase = (performance.now() / 1000) * Math.max(0.01, state.lfoRate);
    const rate = state.params.find(p => p.id === "rate");
    if (rate) renderKnobCanvas(rate);
  }
  requestAnimationFrame(lfoAnimLoop);
}

/* ---- Factory presets: exact replication of PrimitiveFxImGuiEditor::factoryPresetValues ---- */
const GENERAL_SHAPE = [0.50, 0.30, 0.70, 0.86, 0.18, 0.62, 0.40, 0.78, 0.24, 0.92];
const MIX_SHAPE     = [0.50, 0.24, 0.40, 0.58, 0.78, 0.32, 0.66, 0.90, 0.46, 0.70];
const MOTION_SHAPE  = [0.28, 0.18, 0.38, 0.56, 0.78, 0.08, 0.48, 0.68, 0.30, 0.88];
const OUTPUT_SHAPE  = [0.67, 0.62, 0.64, 0.60, 0.56, 0.68, 0.58, 0.54, 0.66, 0.50];

function factoryPresetValues(preset) {
  const valueAt = (p, n) => p.min + (p.max - p.min) * clamp(n, 0, 1);
  const out = {};
  state.params.forEach(p => {
    if (preset === 0) { out[p.id] = p.defaultValue; return; }
    const id = p.id; let target = p.defaultValue;
    if (id === "output") target = valueAt(p, OUTPUT_SHAPE[preset]);
    else if (id === "dryWet") target = valueAt(p, MIX_SHAPE[preset]);
    else if (id === "rate" || id === "time" || id === "attack" || id === "release") target = valueAt(p, MOTION_SHAPE[preset]);
    else if (id === "width" || id === "spread" || id === "stereo" || id === "phase" || id === "rotation") target = valueAt(p, clamp(GENERAL_SHAPE[preset] + 0.10, 0, 1));
    else if (id === "depth" || id === "feedback" || id === "drive" || id === "ratio" || id === "input") target = valueAt(p, clamp(GENERAL_SHAPE[preset] + 0.05, 0, 1));
    else if (/gain/i.test(id) || id === "bias" || id === "balance" || id === "offset" || id === "semitones" || id === "cents") target = valueAt(p, 1.0 - GENERAL_SHAPE[preset]);
    else if (/freq/i.test(id) || id === "center" || id === "tone" || id === "focus") target = valueAt(p, clamp(GENERAL_SHAPE[preset] * 0.82 + 0.08, 0, 1));
    else if (id === "ceiling") target = valueAt(p, 0.82 - GENERAL_SHAPE[preset] * 0.22);
    else if (id === "lowCut" || id === "bassMono" || id === "mono") target = valueAt(p, 1.0 - MIX_SHAPE[preset]);
    else target = valueAt(p, GENERAL_SHAPE[preset]);
    out[id] = clamp(target, p.min, p.max);
  });
  return out;
}

const FACTORY_PRESET_NAMES = ["Distortion Init", "Warm Drive", "Crunch", "Fuzz Box", "Tube Sat", "Hard Clip", "Edge Bite", "Vintage Grit", "Bass Growl", "Wall of Fuzz"];

function rebuildPresetSelect() { const sel = document.querySelector(".preset-select"); if (!sel) return; sel.innerHTML = ""; state.presets.forEach((p, i) => { const o = document.createElement("option"); o.value = String(i); o.textContent = p.name; sel.appendChild(o); }); sel.value = String(state.currentPresetIndex); }
function loadPresetIndex(index) { const next = (index + state.presets.length) % state.presets.length; pushUndo(); state.currentPresetIndex = next; rebuildPresetSelect(); restore(state.presets[next].values, true); }
function saveCurrentPreset() { const name = prompt("Preset name", `User ${Math.max(1, state.presets.length - FACTORY_PRESET_NAMES.length + 1)}`); if (!name) return; const existing = state.presets.findIndex(p => p.name === name); const item = { name, values: snapshot() }; if (existing >= 0) { state.presets[existing] = item; state.currentPresetIndex = existing; } else { state.presets.push(item); state.currentPresetIndex = state.presets.length - 1; } rebuildPresetSelect(); Host.sendEvent("savePreset", { presetName: name, values: item.values }); }

const HELP_URL = "https://pbtechlab.com/";
function closeHelp() { document.querySelector(".help-overlay")?.remove(); document.removeEventListener("keydown", handleHelpKey); }
function handleHelpKey(e) { if (e.key === "Escape") closeHelp(); }
function showHelp() { closeHelp(); const o = document.createElement("div"); o.className = "help-overlay"; o.innerHTML = `<div class="help-dialog" role="dialog" aria-modal="true" aria-label="Help"><button class="help-close" type="button" aria-label="Close">&times;</button><div class="help-title">pbTechLab</div><div class="help-version">Primitive Fx Library Ver 1.0.0</div><a class="help-link" href="${HELP_URL}">${HELP_URL}</a></div>`; o.addEventListener("click", e => { if (e.target === o) closeHelp(); }); o.querySelector(".help-close").addEventListener("click", closeHelp); o.querySelector(".help-link").addEventListener("click", e => { e.preventDefault(); Host.sendEvent("openUrl", { url: HELP_URL }); }); document.body.appendChild(o); document.addEventListener("keydown", handleHelpKey); Host.sendEvent("help"); }

function saveAB() { state.ab = state.ab || {}; state.ab[state.activeAB] = snapshot(); }
function loadAB(slot) { saveAB(); state.activeAB = slot; document.querySelectorAll(".ab-btn").forEach(b => b.classList.toggle("active", b.dataset.slot === slot)); state.ab = state.ab || {}; restore(state.ab[slot] || Object.fromEntries(state.params.map(p => [p.id, p.defaultValue])), true); }

/* ---- inbound from host ---- */
function applyHostParam(id, realValue) {
  if (id === "bypass") { state.bypass = Number(realValue) >= 0.5; document.querySelector(".btn-bypass")?.classList.toggle("active", state.bypass); return; }
  setParam(id, realValue, false, false);
}
function applyHostMeters(m) {
  if (m && Number.isFinite(Number(m.bpm)) && Number(m.bpm) > 0) {
    SYNC_BPM = clamp(Number(m.bpm), 30, 300);
    state.params.filter(p => p.sync).forEach(p => setParam(p.id, p.value, false, false));
  }
}

document.addEventListener("DOMContentLoaded", () => {
  Host.setHandlers(applyHostParam, applyHostMeters);

  const row = document.querySelector(".knob-row");
  row.style.setProperty("--knob-count", String(state.params.length));
  state.params.forEach(p => row.appendChild(makeKnob(p)));
  state.params.forEach(p => setParam(p.id, p.value, false, false));

  buildLfoChip();
  requestAnimationFrame(lfoAnimLoop);

  state.presets = FACTORY_PRESET_NAMES.map((name, i) => ({ name, values: factoryPresetValues(i) }));
  state.currentPresetIndex = 0;
  rebuildPresetSelect();
  state.ab = { A: snapshot(), B: Object.fromEntries(state.params.map(p => [p.id, p.defaultValue])) };

  document.querySelector(".btn-bypass")?.addEventListener("click", e => { state.bypass = !state.bypass; e.currentTarget.classList.toggle("active", state.bypass); Host.sendParam("bypass", state.bypass ? 1 : 0); });
  document.querySelectorAll(".ab-btn").forEach(b => b.addEventListener("click", () => loadAB(b.dataset.slot)));
  document.querySelector(".preset-select")?.addEventListener("change", e => loadPresetIndex(Number(e.target.value)));
  document.getElementById("prevPreset")?.addEventListener("click", () => loadPresetIndex(state.currentPresetIndex - 1));
  document.getElementById("nextPreset")?.addEventListener("click", () => loadPresetIndex(state.currentPresetIndex + 1));
  document.getElementById("undoBtn")?.addEventListener("click", () => { if (!state.undoStack.length) return; state.redoStack.push(snapshot()); restore(state.undoStack.pop(), true); updateHistoryButtons(); });
  document.getElementById("redoBtn")?.addEventListener("click", () => { if (!state.redoStack.length) return; state.undoStack.push(snapshot()); restore(state.redoStack.pop(), true); updateHistoryButtons(); });
  document.getElementById("savePresetBtn")?.addEventListener("click", saveCurrentPreset);
  document.getElementById("helpBtn")?.addEventListener("click", showHelp);
  updateHistoryButtons();

  Host.sendEvent("uiReady");
});
