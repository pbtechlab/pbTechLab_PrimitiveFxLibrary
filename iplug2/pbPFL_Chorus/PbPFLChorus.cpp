#include "PbPFLChorus.h"
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
// Matches juce::dsp::Chorus constants: oscVolumeMultiplier = 0.5,
// maximumDelayModulation = 20.0 (so delay modulation = 10ms * depth * sin around
// the centre delay). Keeps the migrated chorus sample-faithful to the original.
constexpr float kOscVolumeMultiplier = 0.5f;
constexpr float kMaxDelayModulation = 20.0f;
// Anchor symbol used to resolve THIS module's path at runtime (works for both the
// VST3 DLL and the standalone EXE, independent of iPlug2's gHINSTANCE plumbing which
// is not wired for VST3 on Windows).
const char kPbPFLModuleAnchor = 0;
}

PbPFLChorus::PbPFLChorus(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Parameter set is a 1:1 match of the original JUCE Chorus (ranges, defaults,
  // steps and units are identical, so nothing is lost in the migration).
  GetParam(kRate)->InitDouble("Rate", 0.8, 0.05, 8.0, 0.01, "Hz");
  GetParam(kDepth)->InitDouble("Depth", 45.0, 0.0, 100.0, 1.0, "%");
  GetParam(kDelay)->InitDouble("Delay", 14.0, 1.0, 35.0, 0.1, "ms");
  GetParam(kFeedback)->InitDouble("Feedback", 8.0, -90.0, 90.0, 1.0, "%");
  GetParam(kSpread)->InitDouble("Spread", 110.0, 0.0, 180.0, 1.0, "deg");
  GetParam(kDryWet)->InitDouble("Dry/Wet", 45.0, 0.0, 100.0, 1.0, "%");
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

  MakePreset("Chorus Init", 0.8, 45.0, 14.0, 8.0, 110.0, 45.0, 0.0, 0.0);
}

#if IPLUG_DSP
void PbPFLChorus::OnReset()
{
  for (auto& d : mDelay)
    d.Reset(kMaxDelaySamples);
  mLfoPhase = 0.0f;
  mLastOutput[0] = mLastOutput[1] = 0.0f;
}

void PbPFLChorus::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
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

  // --- parameter snapshot (same derivation as PrimitiveFxProcessor) -----------
  const float rate        = static_cast<float>(GetParam(kRate)->Value());
  const float depth       = static_cast<float>(GetParam(kDepth)->Value()) * 0.01f;   // 0..1
  const float centreDelay = static_cast<float>(GetParam(kDelay)->Value());           // ms
  const float fb          = static_cast<float>(GetParam(kFeedback)->Value()) * 0.01f;
  const float wetGain     = std::clamp(static_cast<float>(GetParam(kDryWet)->Value()) * 0.01f, 0.0f, 1.0f);
  const float dryGain     = 1.0f - wetGain;
  const float outGain     = static_cast<float>(DBToAmp(GetParam(kOutputGain)->Value()));

  // juce::dsp::Chorus modulation: oscVolume = depth * 0.5, delay (ms) =
  // max(1, 20 * (depth*0.5) * sin(phase) + centreDelay), then converted to samples.
  const float oscVol = depth * kOscVolumeMultiplier;

  float outPeak = 0.0f;

  for (int i = 0; i < nFrames; ++i)
  {
    mLfoPhase += rate / static_cast<float>(sr);
    if (mLfoPhase >= 1.0f)
      mLfoPhase -= 1.0f;

    // Single shared LFO across channels, matching juce::dsp::Chorus.
    const float lfo = std::sin(mLfoPhase * kTwoPi);
    float delayMs = kMaxDelayModulation * (oscVol * lfo) + centreDelay;
    delayMs = std::max(1.0f, delayMs);
    float dSamp = delayMs * static_cast<float>(sr) / 1000.0f;
    dSamp = std::clamp(dSamp, 1.0f, static_cast<float>(kMaxDelaySamples - 1));

    for (int ch = 0; ch < nChans; ++ch)
    {
      const float dry = static_cast<float>(inputs[ch][i]);
      const float in = dry - mLastOutput[ch];
      const float delayed = mDelay[ch].Read(dSamp);
      mDelay[ch].Write(in);
      mLastOutput[ch] = delayed * fb;

      const float mixed = (dry * dryGain + delayed * wetGain) * outGain;
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

void PbPFLChorus::OnIdle()
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

bool PbPFLChorus::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  // Parameter and bypass changes arrive through the standard SPVFUI path; preset/
  // help/url handling lives in the WebView UI itself.
  return false;
}
