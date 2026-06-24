#include "PbPFL3BandEQ.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
// Anchor symbol used to resolve THIS module's path at runtime (works for both the
// VST3 DLL and the standalone EXE, independent of iPlug2's gHINSTANCE plumbing which
// is not wired for VST3 on Windows).
const char kPbPFLModuleAnchor = 0;
}

PbPFL3BandEQ::PbPFL3BandEQ(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Parameter set is a 1:1 match of the original JUCE ThreeBandEQ (ranges,
  // defaults, steps and units are identical, so nothing is lost in migration).
  GetParam(kLowGain)->InitDouble("Low", 0.0, -18.0, 18.0, 0.1, "dB");
  GetParam(kLowFreq)->InitDouble("Low Freq", 120.0, 40.0, 400.0, 1.0, "Hz");
  GetParam(kLowQ)->InitDouble("Low Q", 0.7, 0.2, 8.0, 0.01, "Q");
  GetParam(kMidGain)->InitDouble("Mid", 0.0, -18.0, 18.0, 0.1, "dB");
  GetParam(kMidFreq)->InitDouble("Mid Freq", 1200.0, 200.0, 5000.0, 1.0, "Hz");
  GetParam(kMidQ)->InitDouble("Mid Q", 1.1, 0.2, 8.0, 0.01, "Q");
  GetParam(kHighGain)->InitDouble("High", 0.0, -18.0, 18.0, 0.1, "dB");
  GetParam(kHighQ)->InitDouble("High Q", 0.7, 0.2, 8.0, 0.01, "Q");
  GetParam(kDryWet)->InitDouble("Dry/Wet", 100.0, 0.0, 100.0, 1.0, "%");
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

  MakePreset("3BandEQ Init", 0.0, 120.0, 0.7, 0.0, 1200.0, 1.1, 0.0, 0.7, 100.0, 0.0);
}

#if IPLUG_DSP
void PbPFL3BandEQ::OnReset()
{
  for (int ch = 0; ch < 2; ++ch)
  {
    mLow[ch].Clear();
    mMid[ch].Clear();
    mHigh[ch].Clear();
  }
}

void PbPFL3BandEQ::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
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
  const double lowFreq  = GetParam(kLowFreq)->Value();
  const double lowQ     = GetParam(kLowQ)->Value();
  const double lowGain  = DBToAmp(GetParam(kLowGain)->Value());
  const double midFreq  = GetParam(kMidFreq)->Value();
  const double midQ     = GetParam(kMidQ)->Value();
  const double midGain  = DBToAmp(GetParam(kMidGain)->Value());
  const double highQ    = GetParam(kHighQ)->Value();
  const double highGain = DBToAmp(GetParam(kHighGain)->Value());
  // The original JUCE high shelf uses a fixed 6000 Hz centre frequency.
  const double kHighFreq = 6000.0;

  const float wetGain = std::clamp(static_cast<float>(GetParam(kDryWet)->Value()) * 0.01f, 0.0f, 1.0f);
  const float dryGain = 1.0f - wetGain;
  const float outGain = static_cast<float>(DBToAmp(GetParam(kOutputGain)->Value()));

  for (int ch = 0; ch < nChans; ++ch)
  {
    mLow[ch].SetCoeffs(RBJBiquad::kLowShelf, sr, lowFreq, lowQ, lowGain);
    mMid[ch].SetCoeffs(RBJBiquad::kPeak, sr, midFreq, midQ, midGain);
    mHigh[ch].SetCoeffs(RBJBiquad::kHighShelf, sr, kHighFreq, highQ, highGain);
  }

  float outPeak = 0.0f;

  for (int ch = 0; ch < nChans; ++ch)
  {
    for (int i = 0; i < nFrames; ++i)
    {
      const float dry = static_cast<float>(inputs[ch][i]);
      float wet = dry;
      wet = mLow[ch].Process(wet);
      wet = mMid[ch].Process(wet);
      wet = mHigh[ch].Process(wet);

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

void PbPFL3BandEQ::OnIdle()
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

bool PbPFL3BandEQ::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  // Parameter and bypass changes arrive through the standard SPVFUI path; preset/
  // help/url handling lives in the WebView UI itself.
  return false;
}
