#include "PbPFLPhaser.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
constexpr float kTwoPi = 6.28318530717958647692f;
// Anchor symbol used to resolve THIS module's path at runtime (works for both the
// VST3 DLL and the standalone EXE, independent of iPlug2's gHINSTANCE plumbing which
// is not wired for VST3 on Windows).
const char kPbPFLModuleAnchor = 0;

// Log mapping helpers — 1:1 with juce::mapToLog10 / juce::mapFromLog10.
inline float MapToLog10(float value0to1, float min, float max)
{
  return min * std::pow(max / min, value0to1);
}
inline float MapFromLog10(float value, float min, float max)
{
  return std::log(value / min) / std::log(max / min);
}
}

PbPFLPhaser::PbPFLPhaser(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Parameter set is a 1:1 match of the original JUCE Phaser (ranges, defaults,
  // steps and units are identical, so nothing is lost in the migration).
  GetParam(kRate)->InitDouble("Rate", 0.55, 0.03, 8.0, 0.01, "Hz");
  GetParam(kDepth)->InitDouble("Depth", 62.0, 0.0, 100.0, 1.0, "%");
  GetParam(kCenter)->InitDouble("Center", 950.0, 200.0, 5000.0, 1.0, "Hz");
  GetParam(kFeedback)->InitDouble("Feedback", 35.0, -95.0, 95.0, 1.0, "%");
  GetParam(kDryWet)->InitDouble("Dry/Wet", 55.0, 0.0, 100.0, 1.0, "%");
  GetParam(kOutputGain)->InitDouble("Output", 0.0, -24.0, 12.0, 0.1, "dB");
  GetParam(kBypass)->InitBool("Bypass", false);

#ifdef WEBVIEW_EDITOR_DELEGATE
#ifdef _DEBUG
  SetEnableDevTools(true);
#endif

  mEditorInitFunc = [&]() {
#ifdef OS_WIN
    // Windows: gHINSTANCE is not reliably set for VST3/AAX, so resolve THIS module's
    // own path from a symbol address and load index.html from the "web" folder placed
    // next to the binary by the CMake POST_BUILD step.
    HMODULE thisModule = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&kPbPFLModuleAnchor), &thisModule);
    wchar_t modPathW[MAX_PATH] = {};
    ::GetModuleFileNameW(thisModule, modPathW, MAX_PATH);
    const std::filesystem::path indexPath =
        std::filesystem::path(modPathW).parent_path() / "web" / "index.html";
    LoadFile(indexPath.string().c_str(), nullptr);
#else
    // macOS: iPlug2 packages the WEB_RESOURCES into the plugin bundle.
    LoadIndexHtml(__FILE__, GetBundleID());
#endif
    EnableScroll(false);
    // iPlug2's OpenWebView ignores the requested size and only sets the WebView2
    // bounds on a host resize event. Hosts that open the editor without firing a
    // resize (several VST3 hosts) otherwise leave the WebView2 at uninitialised
    // bounds and render black. Force the correct bounds here.
    SetWebViewBounds(0.0f, 0.0f, static_cast<float>(GetEditorWidth()),
                     static_cast<float>(GetEditorHeight()), 1.0f);
  };
#endif

  MakePreset("Phaser Init", 0.55, 62.0, 950.0, 35.0, 55.0, 0.0, 0.0);
}

#if IPLUG_DSP
void PbPFLPhaser::OnReset()
{
  const double sr = GetSampleRate();
  for (int ch = 0; ch < 2; ++ch)
  {
    for (int n = 0; n < kNumStages; ++n)
      mFilters[ch][n].Prepare(sr);
    mLastOutput[ch] = 0.0f;
  }
  mLfoPhase = 0.0f;
}

void PbPFLPhaser::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nChans = std::min(2, NOutChansConnected());
  const double sr = GetSampleRate();

  // --- input peak -------------------------------------------------------------
  float inPeak = 0.0f;
  for (int ch = 0; ch < nChans; ++ch)
    for (int s = 0; s < nFrames; ++s)
      inPeak = std::max(inPeak, std::fabs(static_cast<float>(inputs[ch][s])));
  mInputPeak.store(inPeak, std::memory_order_relaxed);

  // --- bypass: identity passthrough (matches the JUCE early-return) -----------
  if (GetParam(kBypass)->Bool())
  {
    for (int ch = 0; ch < nChans; ++ch)
      for (int s = 0; s < nFrames; ++s)
        outputs[ch][s] = inputs[ch][s];
    mOutputPeak.store(inPeak, std::memory_order_relaxed);
    return;
  }

  // --- parameter snapshot (same derivation as juce::dsp::Phaser) --------------
  const float rate     = static_cast<float>(GetParam(kRate)->Value());
  const float depth    = static_cast<float>(GetParam(kDepth)->Value()) * 0.01f;
  const float centre   = static_cast<float>(GetParam(kCenter)->Value());
  const float fb       = static_cast<float>(GetParam(kFeedback)->Value()) * 0.01f;
  const float wetGain  = std::clamp(static_cast<float>(GetParam(kDryWet)->Value()) * 0.01f, 0.0f, 1.0f);
  const float dryGain  = 1.0f - wetGain;
  const float outGain  = static_cast<float>(DBToAmp(GetParam(kOutputGain)->Value()));

  // Frequency mapping range matches juce::dsp::Phaser: 20 Hz .. min(20k, 0.49*sr).
  const float fMin = 20.0f;
  const float fMax = static_cast<float>(std::min(20000.0, 0.49 * sr));
  const float normCentre = MapFromLog10(std::clamp(centre, fMin, fMax), fMin, fMax);
  const float oscVolume = depth * 0.5f;

  float outPeak = 0.0f;

  for (int i = 0; i < nFrames; ++i)
  {
    mLfoPhase += rate / static_cast<float>(sr);
    if (mLfoPhase >= 1.0f)
      mLfoPhase -= 1.0f;

    const float oscVal = std::sin(mLfoPhase * kTwoPi) * oscVolume;
    const float lfo = std::clamp(oscVal + normCentre, 0.0f, 1.0f);
    const float cutoff = MapToLog10(lfo, fMin, fMax);

    for (int ch = 0; ch < nChans; ++ch)
    {
      for (int n = 0; n < kNumStages; ++n)
        mFilters[ch][n].SetCutoff(cutoff);

      const float dry = static_cast<float>(inputs[ch][i]);
      float wet = dry - mLastOutput[ch];
      for (int n = 0; n < kNumStages; ++n)
        wet = mFilters[ch][n].Process(wet);

      mLastOutput[ch] = wet * fb;

      const float mixed = (dry * dryGain + wet * wetGain) * outGain;
      outputs[ch][i] = static_cast<sample>(mixed);
      outPeak = std::max(outPeak, std::fabs(mixed));
    }
  }

  // If the host gives us more output channels than we processed, silence them.
  for (int ch = nChans; ch < NOutChansConnected(); ++ch)
    for (int s = 0; s < nFrames; ++s)
      outputs[ch][s] = 0.0;

  mOutputPeak.store(outPeak, std::memory_order_relaxed);
}

void PbPFLPhaser::OnIdle()
{
  // Push host tempo + meter levels to the WebView so tempo-sync and meters work.
  char json[160];
  std::snprintf(json, sizeof(json),
                "{\"id\":\"meters\",\"bpm\":%.3f,\"inPeak\":%.4f,\"outPeak\":%.4f}",
                GetTempo(),
                static_cast<double>(mInputPeak.load(std::memory_order_relaxed)),
                static_cast<double>(mOutputPeak.load(std::memory_order_relaxed)));
  SendArbitraryMsgFromDelegate(-1, static_cast<int>(std::strlen(json)), json);
}
#endif

bool PbPFLPhaser::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  // Parameter and bypass changes arrive through the standard SPVFUI path; preset/
  // help/url handling lives in the WebView UI itself.
  return false;
}
