"use strict";
/* =====================================================================================
   knob-render.js — 1:1 port of the ImGui procedural knob shader
   (Plugin_GUI/ImGui/src/main.cpp : GenerateKnobPixels + helpers).
   Renders a pixel-faithful "textured" knob (cyan LED ring + dark cap + white pointer +
   blue tip, optional LFO ring) into a <canvas>. Used to strictly match the ImGui design.
   ===================================================================================== */
(function (global) {
  const PI = Math.PI;

  const sat = v => (v < 0 ? 0 : v > 1 ? 1 : v);
  function smoothstep(e0, e1, x) { const t = sat((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }
  function overlay(s, d) { const ia = 1 - s.a; return { r: s.r * s.a + d.r * ia, g: s.g * s.a + d.g * ia, b: s.b * s.a + d.b * ia, a: s.a + d.a * ia }; }
  const col = (r, g, b, a) => ({ r, g, b, a });

  function boxDistance(px, py, ax, ay, bx, by, thickness) {
    const vx = bx - ax, vy = by - ay;
    const len = Math.sqrt(vx * vx + vy * vy);
    if (len <= 1e-5) return 1;
    const dx = vx / len, dy = vy / len;
    const qx = px - (ax + bx) * 0.5, qy = py - (ay + by) * 0.5;
    const rx = dx * qx + dy * qy, ry = -dy * qx + dx * qy;
    const sx = Math.abs(rx) - len * 0.5, sy = Math.abs(ry) - thickness * 0.5;
    const ox = Math.max(sx, 0), oy = Math.max(sy, 0);
    return Math.sqrt(ox * ox + oy * oy) + Math.min(Math.max(sx, sy), 0);
  }

  function lfoShapeValue(phase, type) {
    const p = phase - Math.floor(phase);
    switch (Math.max(0, Math.min(5, type | 0))) {
      case 1: return p < 0.5 ? p * 2 : 2 - p * 2;
      case 2: return p;
      case 3: return 1 - p;
      case 4: return p < 0.5 ? 0 : 1;
      case 5: { const stepped = Math.floor(p * 8); const n = Math.sin((stepped + 1) * 12.9898) * 43758.5453; return n - Math.floor(n); }
      default: return 0.5 + 0.5 * Math.sin((p * 2 - 0.25) * PI);
    }
  }

  // Faithful port of GenerateKnobPixels -> writes RGBA into `data` (Uint8ClampedArray).
  function generate(data, size, normalizedValue, opts) {
    const ledRing = !!opts.ledRing;
    const secondFin = !!opts.secondFin;
    const secondValue = opts.secondValue ?? 0.78;
    const lfoRing = !!opts.lfoRing;
    const lfoPhase = opts.lfoPhase ?? 0.0;
    const bipolarRing = !!opts.bipolarRing;
    const lfoDepth = opts.lfoDepth ?? 0.44;
    const lfoType = opts.lfoType ?? 0;
    const ledCount = opts.ledCount ?? 40;

    const range = 0.75;
    const av = (1 - range) * PI + sat(normalizedValue) * 2 * PI * range;
    const aa = 1 / size;

    function rotate2d(x0, y0, angle) {
      const c = Math.cos(angle), s = Math.sin(angle);
      return [-c * x0 + s * y0, -s * x0 - c * y0];
    }

    for (let y = 0; y < size; ++y) {
      for (let x = 0; x < size; ++x) {
        const fragX = x + 0.5;
        const fragY = (size - 1 - y) + 0.5;
        const uvx = ((fragX - size * 0.5) / size) * 1.25;
        const uvy = ((fragY - size * 0.5) / size) * 1.25;
        const l = Math.sqrt(uvx * uvx + uvy * uvy);
        const theta = Math.atan2(uvx, uvy);
        let a2 = (((theta + PI * 0.75) / (2 * PI)) % 1);
        let a3 = (((-theta + PI * 0.75) / (2 * PI)) % 1);
        const fixedA2 = a2 < 0 ? a2 + 1 : a2;
        const fixedA3 = a3 < 0 ? a3 + 1 : a3;

        const arc = smoothstep(fixedA2 - aa, fixedA2 + aa, 0.75) * smoothstep(l - aa, l + aa, 0.45);
        const arcb = (Math.max(smoothstep(fixedA3 - 0.005, fixedA3 + 0.005, 0.75), 0)
          + Math.max(smoothstep(fixedA2 - 0.005, fixedA2 + 0.005, 0.75), 0))
          * smoothstep(l - 0.025, l + 0.025, 0.44) * 0.5;

        const [lineX, lineY] = rotate2d(uvx, uvy, av);
        const [p1x, p1y] = rotate2d(uvx, uvy, PI);
        const [p2x, p2y] = rotate2d(uvx, uvy, PI + 0.73 * PI);
        const [p3x, p3y] = rotate2d(uvx, uvy, PI - 0.73 * PI);

        const ln = boxDistance(lineX, lineY, 0, 0.15, 0, 0.2725, 0.05);
        const ln2 = boxDistance(lineX, lineY, 0, 0.30, 0, 0.34, 0.05);
        let secondLine = 1, secondLineTip = 1;
        if (secondFin) {
          const av2 = (1 - range) * PI + sat(secondValue) * 2 * PI * range;
          const [secondX, secondY] = rotate2d(uvx, uvy, av2);
          secondLine = boxDistance(secondX, secondY, 0, 0.145, 0, 0.2725, 0.040);
          secondLineTip = boxDistance(secondX, secondY, 0, 0.296, 0, 0.34, 0.042);
        }
        const point1 = boxDistance(p1x, p1y, 0, 0.46, 0, 0.51, 0.05);
        const point2 = boxDistance(p2x, p2y, 0, 0.46, 0, 0.51, 0.05);
        const point3 = boxDistance(p3x, p3y, 0, 0.46, 0, 0.51, 0.05);

        const p1s = smoothstep(point1 - 0.015, point1 + 0.015, 0) * 0.5;
        const p2s = smoothstep(point2 - 0.015, point2 + 0.015, 0) * 0.5;
        const p3s = smoothstep(point3 - 0.015, point3 + 0.015, 0) * 0.5;

        const btm = smoothstep(l - aa, l + aa, 0.375);
        const cs = smoothstep(l - 0.01, l + 0.01, 0.335) * 0.4;
        const sd = Math.sqrt(uvx * uvx + (uvy + 0.1) * (uvy + 0.1));
        const s = smoothstep(sd - 0.2, sd + 0.2, 0.3) * 0.9;
        const cbtm = smoothstep(l - aa, l + aa, 0.3);
        const cout = smoothstep(l - aa, l + aa, 0.275);
        const capt = smoothstep(l - aa, l + aa, 0.265);
        const lines = smoothstep(ln - 0.02, ln + 0.02, 0) * 0.5;
        const line = smoothstep(ln - aa, ln + aa, 0);
        const lineb = smoothstep(ln2 - aa, ln2 + aa, 0);
        const secondLines = smoothstep(secondLine - 0.02, secondLine + 0.02, 0) * 0.42;
        const secondLineAlpha = smoothstep(secondLine - aa, secondLine + aa, 0);
        const secondTipAlpha = smoothstep(secondLineTip - aa, secondLineTip + aa, 0);

        let lfoLineRange = 0, lfoMovingTip = 0;
        if (lfoRing) {
          const lfoRange = sat(lfoDepth) * 0.5;
          const lfoMin = sat(normalizedValue - lfoRange);
          const lfoMax = sat(normalizedValue + lfoRange);
          const lfoCenter = lfoMin + (lfoMax - lfoMin) * lfoShapeValue(lfoPhase, lfoType);
          const avMin = (1 - range) * PI + lfoMin * 2 * PI * range;
          const avMax = (1 - range) * PI + lfoMax * 2 * PI * range;
          const avLfo = (1 - range) * PI + lfoCenter * 2 * PI * range;
          const [dotX, dotY] = rotate2d(uvx, uvy, avLfo);
          let thetaLfo = Math.atan2(uvx, uvy);
          const thetaMin = avMin - PI, thetaMax = avMax - PI;
          while (thetaLfo < thetaMin) thetaLfo += 2 * PI;
          while (thetaLfo > thetaMax) thetaLfo -= 2 * PI;
          const inAngularRange = thetaLfo >= thetaMin && thetaLfo <= thetaMax;
          const tipRadius = 0.320;
          const radialRange = 1 - smoothstep(0.014, 0.024, Math.abs(l - tipRadius));
          lfoLineRange = inAngularRange ? radialRange : 0;
          const movingTip = boxDistance(dotX, dotY, 0, 0.292, 0, 0.345, 0.048);
          lfoMovingTip = smoothstep(movingTip - aa, movingTip + aa, 0);
        }

        let colour = col(0.255, 0.278, 0.302, 0.0);
        const capbgMix = Math.abs(0.5 - uvy);
        const capbg = col(
          0.224 * (1 - capbgMix) + 0.129 * capbgMix,
          0.243 * (1 - capbgMix) + 0.145 * capbgMix,
          0.263 * (1 - capbgMix) + 0.161 * capbgMix, cbtm);
        const captgMix = Math.abs(0.5 - uvy);
        const captg = col(
          0.271 * (1 - captgMix) + 0.188 * captgMix,
          0.294 * (1 - captgMix) + 0.208 * captgMix,
          0.314 * (1 - captgMix) + 0.227 * captgMix, capt);

        if (!ledRing) {
          colour = overlay(col(0, 0, 0, arcb), colour);
          colour = overlay(col(0.49, 0.541, 0.576, arc), colour);
        } else {
          const ledStart = -0.75 * PI, ledEnd = 0.75 * PI, ledRadius = 0.430, ledThickness = 0.064;
          const ledActive = sat(normalizedValue) * ledCount;
          let ledTheta = Math.atan2(uvx, uvy);
          if (ledTheta < ledStart) ledTheta += 2 * PI;
          const ledT = (ledTheta - ledStart) / (ledEnd - ledStart);
          if (ledT >= 0 && ledT <= 1) {
            const scaled = ledT * ledCount;
            const led = Math.max(0, Math.min(ledCount - 1, scaled | 0));
            const local = scaled - led;
            const gap = 0.095;
            const angularAlpha = smoothstep(gap, gap + 0.007, local) * (1 - smoothstep(1 - gap - 0.007, 1 - gap, local));
            const radialAlpha = 1 - smoothstep(ledThickness * 0.5 - aa, ledThickness * 0.5 + aa, Math.abs(l - ledRadius));
            const ledAlpha = angularAlpha * radialAlpha;
            let activeLed = (led + 1) <= ledActive + 0.001;
            if (bipolarRing) {
              const ledValue = (led + 0.5) / ledCount;
              const lo = Math.min(0.5, sat(normalizedValue));
              const hi = Math.max(0.5, sat(normalizedValue));
              activeLed = ledValue >= lo && ledValue <= hi;
            }
            const ledColor = activeLed ? col(0.0, 0.847, 1.0, ledAlpha) : col(0.051, 0.165, 0.208, ledAlpha * 0.95);
            if (activeLed) {
              const glowRadial = 1 - smoothstep(ledThickness * 1.3, ledThickness * 1.3 + aa * 10, Math.abs(l - ledRadius));
              const glowAlpha = angularAlpha * glowRadial * 0.18;
              colour = overlay(col(0.0, 0.847, 1.0, glowAlpha), colour);
            }
            colour = overlay(ledColor, colour);
          }
        }
        colour = overlay(col(0.137, 0.153, 0.165, btm), colour);
        colour = overlay(col(0, 0, 0, cs), colour);
        colour = overlay(col(0.075, 0.092, 0.102, s), colour);
        colour = overlay(capbg, colour);
        colour = overlay(col(0.230, 0.286, 0.314, cout), colour);
        colour = overlay(captg, colour);
        colour = overlay(col(0, 0, 0, lines), colour);
        colour = overlay(col(0.940, 0.950, 0.960, line), colour);
        if (lfoRing) colour = overlay(col(0.0, 0.52, 0.78, lfoLineRange * 0.72), colour);
        colour = overlay(col(0.024, 0.514, 0.765, lineb), colour);
        if (lfoRing) colour = overlay(col(0.0, 0.92, 1.0, lfoMovingTip), colour);
        if (secondFin) {
          colour = overlay(col(0, 0, 0, secondLines), colour);
          colour = overlay(col(0.98, 0.74, 0.25, secondLineAlpha), colour);
          colour = overlay(col(1.0, 0.38, 0.12, secondTipAlpha), colour);
        }

        const i = (y * size + x) * 4;
        data[i + 0] = sat(colour.r) * 255;
        data[i + 1] = sat(colour.g) * 255;
        data[i + 2] = sat(colour.b) * 255;
        data[i + 3] = sat(colour.a) * 255;
      }
    }
  }

  // Render into a canvas element. opts: {value01, ledRing, bipolarRing, lfoRing, lfoPhase,
  // lfoDepth, lfoType, secondFin, secondValue, ledCount}
  function renderKnob(canvas, value01, opts) {
    opts = opts || {};
    const size = canvas.width; // square
    const ctx = canvas.getContext("2d");
    let img = canvas._knobImg;
    if (!img || img.width !== size) { img = ctx.createImageData(size, size); canvas._knobImg = img; }
    generate(img.data, size, value01, opts);
    ctx.putImageData(img, 0, 0);
  }

  global.PBKnob = { renderKnob, lfoShapeValue };
})(window);
