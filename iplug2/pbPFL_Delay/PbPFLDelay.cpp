#include "PbPFLDelay.h"
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
}

PbPFLDelay::PbPFLDelay(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Parameter set is a 1:1 match of the original JUCE Delay (ranges, defaults,
  // steps and units are identical, so nothing is lost in the migration).
  GetParam(kTime)->InitDouble("Time", 360.0, 1.0, 2000.0, 1.0, "ms");
  GetParam(kFeedback)->InitDouble("Feedback", 38.0, 0.0, 95.0, 1.0, "%");
  GetParam(kTone)->InitDouble("Tone", 54.0, 0.0, 100.0, 1.0, "%");
  GetParam(kSpread)->InitDouble("Spread", 28.0, 0.0, 100.0, 1.0, "%");
  GetParam(kDryWet)->InitDouble("Dry/Wet", 32.0, 0.0, 100.0, 1.0, "%");
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

  MakePreset("Delay Init", 360.0, 38.0, 54.0, 28.0, 32.0, 0.0, 0.0);
}

#if IPLUG_DSP
void PbPFLDelay::OnReset()
{
  for (auto& d : mDelay)
    d.Reset(kMaxDelaySamples);
  mFeedbackFilter[0] = mFeedbackFilter[1] = 0.0f;
  mLfoPhase = 0.0f;
}

void PbPFLDelay::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
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
  const float baseDelay = static_cast<float>(GetParam(kTime)->Value()) * static_cast<float>(sr) * 0.001f;
  const float fb        = std::clamp(static_cast<float>(GetParam(kFeedback)->Value()) * 0.01f, 0.0f, 0.95f);
  const float spread    = static_cast<float>(GetParam(kSpread)->Value()) * 0.005f;
  const float tone      = 0.05f + static_cast<float>(GetParam(kTone)->Value()) * 0.009f;
  const float wetGain   = std::clamp(static_cast<float>(GetParam(kDryWet)->Value()) * 0.01f, 0.0f, 1.0f);
  const float dryGain   = 1.0f - wetGain;
  const float outGain   = static_cast<float>(DBToAmp(GetParam(kOutputGain)->Value()));

  // The original JUCE Delay reads modRate/modDepth which are not part of the Delay
  // parameter set, so APVTS returns 0 for them -> the LFO contributes no modulation.
  // We mirror that exactly (mod = 0).
  float outPeak = 0.0f;

  for (int i = 0; i < nFrames; ++i)
  {
    for (int ch = 0; ch < nChans; ++ch)
    {
      const float chSpread = (ch == 0 ? -spread : spread) * baseDelay;
      const float d = std::clamp(baseDelay + chSpread, 1.0f, 191999.0f);

      const float in = static_cast<float>(inputs[ch][i]);
      const float delayed = mDelay[ch].Read(d);

      // One-pole tone smoothing in the feedback path.
      const float filtered = mFeedbackFilter[ch] + (delayed - mFeedbackFilter[ch]) * tone;
      mFeedbackFilter[ch] = filtered;

      mDelay[ch].Write(in + filtered * fb);

      // Wet signal is the (unfiltered) delayed tap; common mixDryWet applies here.
      const float mixed = (in * dryGain + delayed * wetGain) * outGain;
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

void PbPFLDelay::OnIdle()
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

bool PbPFLDelay::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  // Parameter and bypass changes arrive through the standard SPVFUI path; preset/
  // help/url handling lives in the WebView UI itself.
  return false;
}
