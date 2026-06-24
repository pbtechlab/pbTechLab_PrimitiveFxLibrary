# pbTechLab Primitive Fx Library

[日本語 README](README.ja.md)

![pbTechLab Primitive Fx Library GUI collection](public_gui_screenshots/pbTechLab_PrimitiveFxLibrary_GUI_Collection_Public.png)

pbTechLab Primitive Fx Library is a collection of 17 lightweight audio effect plug-ins for Windows and macOS. Every plug-in ships as a **VST3** and an **AAX** (Pro Tools) plug-in, with a shared WebView-based editor and a self-contained native DSP core.

The plug-ins were migrated from the original JUCE 8 implementation to **[iPlug2](https://github.com/iPlug2/iPlug2)** with a **WebView UI** (WebView2 on Windows, WKWebView on macOS). The migrated, build-ready projects live under [`iplug2/`](iplug2). Each plug-in is an independent iPlug2 project with its own product name, 4-character plug-in code, parameter set, DSP, and embedded HTML/CSS/JavaScript editor.

## Download

**Latest release: [v2.0.0](https://github.com/pbtechlab/pbTechLab_PrimitiveFxLibrary/releases/latest)** — the complete migration to iPlug2 + WebView, with AAX (Pro Tools) added alongside VST3.

| Platform | Installer | Installs |
|---|---|---|
| Windows (x64) | [`pbTechLab_PrimitiveFxLibrary_..._Windows_Setup.exe`](https://github.com/pbtechlab/pbTechLab_PrimitiveFxLibrary/releases/latest) | VST3 + AAX to the common plug-in folders |
| macOS (universal — Apple Silicon + Intel) | [`pbTechLab_PrimitiveFxLibrary-...-macOS.pkg`](https://github.com/pbtechlab/pbTechLab_PrimitiveFxLibrary/releases/latest) | VST3 + AAX, signed & notarized |

The macOS installer is code-signed (Developer ID), notarized, and stapled; AAX is PACE-signed for Pro Tools on both platforms. Installers ship **only** through [GitHub Releases](https://github.com/pbtechlab/pbTechLab_PrimitiveFxLibrary/releases) — they are never committed to the repository.

> The plug-in/installer internal version string remains `1.0.0`; the `v2.0.0` tag denotes the iPlug2 + WebView rebuild and the AAX addition.

## Formats & Platforms

| Format | Windows | macOS | Notes |
|---|:---:|:---:|---|
| VST3 | ✅ | ✅ | Scanned from the common VST3 folder by every DAW |
| AAX Native | ✅ | ✅ | Pro Tools. Requires PACE signing for release distribution |
| Standalone | ✅ | ✅ | For development / quick checks |

The macOS release plug-ins are code-signed, PACE-signed (AAX), notarized, and stapled. Distribution installers ship only through GitHub Releases — they are not committed to the repository.

## Included Plug-Ins

| Plug-in | Code | Main controls | Purpose |
|---|---|---|---|
| pbPFL Reverb | `PrRv` | Size, Damping, Diffusion, Mod, Width, Dry/Wet, Output | Algorithmic stereo reverb (Freeverb-style comb/allpass network). |
| pbPFL Delay | `PrDl` | Time, Feedback, Tone, Spread, Mod Rate, Mod Depth, Dry/Wet, Output | Stereo delay with feedback coloration, spread, and modulation. |
| pbPFL Distortion | `PrDs` | Drive, Bias, Tone, Shape, Dry/Wet, Output | Saturation / distortion for harmonic edge and tone shaping. |
| pbPFL Compressor | `PrCp` | Threshold, Ratio, Attack, Release, Makeup, Dry/Wet, Output | Feed-forward dynamics with parallel-compression mix. |
| pbPFL Limiter | `PrLm` | Input, Ceiling, Release, Dry/Wet, Output | Peak limiting / loudness control with envelope smoothing. |
| pbPFL 3BandEQ | `Pr3E` | Low/Mid/High gain + freq + Q, Dry/Wet, Output | Three-band RBJ-biquad equalizer. |
| pbPFL 4BandEQ | `Pr4E` | Low shelf, two bells, high band, Dry/Wet, Output | Four-band RBJ-biquad equalizer. |
| pbPFL PitchShifter | `PrPs` | Semitone, Cents, Grain, Crossfade, Tone, Dry/Wet, Output | Time-domain pitch shifting. |
| pbPFL Chorus | `PrCh` | Rate, Depth, Delay, Feedback, Spread, Dry/Wet, Output | Stereo chorus (modulated fractional delay). |
| pbPFL Phaser | `PrPh` | Rate, Depth, Center, Feedback, Dry/Wet, Output | Moving all-pass phaser with feedback. |
| pbPFL Flanger | `PrFl` | Rate, Depth, Delay, Feedback, Spread, Dry/Wet, Output | Stereo flanger with short modulated delay + bipolar feedback. |
| pbPFL StereoEnhancer | `PrSE` | Width, Enhance, Focus, Bass Mono, Dry/Wet, Output | Stereo enhancement and bass-mono management. |
| pbPFL StereoWidth | `PrSW` | Width, Mono, Balance, Rotation, Dry/Wet, Output | Stereo width / mono blend / balance / rotation utility. |
| pbPFL MidSideProcessor | `PrMS` | Mid Gain, Side Gain, Width, Balance, Dry/Wet, Output | Mid-side gain and width processing. |
| pbPFL AutoPan | `PrAP` | Rate, Depth, Phase, Shape, Offset, Dry/Wet, Output | Tempo-sync-capable auto panner. |
| pbPFL Tremolo | `PrTR` | Rate, Depth, Shape, Phase, Dry/Wet, Output | Amplitude modulation for pulse / rhythmic movement. |
| pbPFL Vibrato | `PrVB` | Rate, Depth, Delay, Shape, Spread, Dry/Wet, Output | Pitch-modulation vibrato. |

Manufacturer code: `PbTL`.

## Editor (WebView UI)

Every plug-in shares one HTML/CSS/JS editor rendered in a platform WebView (WebView2 / WKWebView). The visual language matches the original ImGui design and is reproduced pixel-faithfully — the knob is a 1:1 port of the ImGui procedural knob shader, drawn to an HTML canvas.

- **Procedural LED knobs** (`knob-render.js`): cyan LED ring (270°), dark cap, white pointer + blue tip, bipolar ring for ±parameters, and an animated LFO ring.
- **LFO chip** popup on tempo/rate parameters (Type / Rate / Depth + waveform icon).
- **Tempo sync** (`S`) with note↔Hz conversion, driven by host BPM.
- **A/B** slots, **Undo/Redo** (64-deep), **10 factory presets**, **Bypass**, **Help**.
- Knob interaction: drag, fine-drag (Shift), wheel, double-click reset, direct numeric entry.
- One UI asset set drives both the iPlug2 build and the legacy JUCE WebView build via a host-abstraction bridge in `script.js`.

## DSP

Each plug-in re-implements its signal processing with self-contained, permissively-licensed algorithms (no JUCE, no proprietary DSP libraries): RBJ biquads (EQ), a Freeverb-style network (Reverb), a feed-forward compressor, an all-pass cascade (Phaser), and linear-interpolated fractional delay lines (Flanger / Chorus / Delay / Vibrato). The signal flow is faithful to the original: per-sample processing → `dry·(1−wet) + processed·wet` → output gain → bypass passthrough, plus input/output peak metering.

## Build From Source (iPlug2)

Requirements: CMake 3.14+, a C++17 compiler, an [iPlug2](https://github.com/iPlug2/iPlug2) checkout, Visual Studio 2022 (Windows) or Xcode (macOS), the Avid AAX SDK (for AAX), and the WebView2 SDK (Windows, fetched by iPlug2).

Each plug-in is its own project under `iplug2/<plugin>/`. Windows example:

```powershell
cd iplug2/pbPFL_Flanger
cmake -B build -G "Visual Studio 17 2022" -A x64 -DIPLUG2_DIR="path/to/iPlug2"
cmake --build build --config Release --target pbPFL_Flanger-vst3   # VST3
cmake --build build --config Release --target pbPFL_Flanger-aax    # AAX (needs AAX SDK)
```

macOS example (universal):

```bash
cd iplug2/pbPFL_Flanger
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DIPLUG2_DIR="$HOME/iPlug2"
cmake --build build --config Release
```

Override the AAX SDK with `-DAAX_SDK_DIR=...`. The VST3 is deployed to the system VST3 folder and the AAX to the Avid Plug-Ins folder automatically.

The original JUCE 8 implementation (shared `Common/` core + per-plug-in `mock/` UIs) remains in the repository for reference.

## Repository Privacy

This repository intentionally excludes build trees, installer artifacts, signing/PACE credentials, private keys, machine names, server scripts, API tokens, and local handover/log files. Release binaries are distributed only through GitHub Releases.
