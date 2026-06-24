#include "PbPFLStereoEnhancer.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
// One-pole smoother, identical to PrimitiveFxProcessor::smooth().
inline float Smooth(float current, float target, float coeff)
{
  return current + (target - current) * coeff;
}
// Anchor symbol used to resolve THIS module's path at runtime (works for both the
// VST3 DLL and the standalone EXE, independent of iPlug2's gHINSTANCE plumbing which
// is not wired for VST3 on Windows).
const char kPbPFLModuleAnchor = 0;
}

PbPFLStereoEnhancer::PbPFLStereoEnhancer(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Parameter set is a 1:1 match of the original JUCE StereoEnhancer (ranges,
  // defaults, steps and units are identical, so nothing is lost in the migration).
  GetParam(kWidth)->InitDouble("Width", 130.0, 0.0, 200.0, 1.0, "%");
  GetParam(kEnhance)->InitDouble("Enhance", 35.0, 0.0, 100.0, 1.0, "%");
  GetParam(kFocus)->InitDouble("Focus", 55.0, 0.0, 100.0, 1.0, "%");
  GetParam(kBassMono)->InitDouble("Bass Mono", 30.0, 0.0, 100.0, 1.0, "%");
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

  MakePreset("StereoEnhancer Init", 130.0, 35.0, 55.0, 30.0, 100.0, 0.0);
}

#if IPLUG_DSP
void PbPFLStereoEnhancer::OnReset()
{
  mSmoothingState = 0.0f;
}

void PbPFLStereoEnhancer::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nChans = std::min(2, NOutChansConnected());

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
  const float width      = static_cast<float>(GetParam(kWidth)->Value()) * 0.01f;
  const float enhance    = static_cast<float>(GetParam(kEnhance)->Value()) * 0.01f;
  const float focusCoeff = 0.02f + static_cast<float>(GetParam(kFocus)->Value()) * 0.008f;
  const float bassMono   = static_cast<float>(GetParam(kBassMono)->Value()) * 0.01f;
  const float wetGain    = std::clamp(static_cast<float>(GetParam(kDryWet)->Value()) * 0.01f, 0.0f, 1.0f);
  const float dryGain    = 1.0f - wetGain;
  const float outGain    = static_cast<float>(DBToAmp(GetParam(kOutputGain)->Value()));

  float outPeak = 0.0f;

  if (nChans < 2)
  {
    // Mono: the StereoEnhancer is a no-op on the wet path (JUCE early-returns when
    // there are fewer than 2 channels), so only dry/wet + output gain apply.
    for (int s = 0; s < nFrames; ++s)
    {
      const float dry = static_cast<float>(inputs[0][s]);
      const float mixed = (dry * dryGain + dry * wetGain) * outGain;
      outputs[0][s] = static_cast<sample>(mixed);
      outPeak = std::max(outPeak, std::fabs(mixed));
    }
    mOutputPeak.store(outPeak, std::memory_order_relaxed);
    return;
  }

  for (int i = 0; i < nFrames; ++i)
  {
    const float dryL = static_cast<float>(inputs[0][i]);
    const float dryR = static_cast<float>(inputs[1][i]);

    const float mid = (dryL + dryR) * 0.5f;
    float side = (dryL - dryR) * 0.5f;
    mSmoothingState = Smooth(mSmoothingState, side, focusCoeff);
    side = side * width + (side - mSmoothingState) * enhance;
    side *= (1.0f - bassMono * 0.45f);

    const float wetL = mid + side;
    const float wetR = mid - side;

    const float outL = (dryL * dryGain + wetL * wetGain) * outGain;
    const float outR = (dryR * dryGain + wetR * wetGain) * outGain;

    outputs[0][i] = static_cast<sample>(outL);
    outputs[1][i] = static_cast<sample>(outR);
    outPeak = std::max(outPeak, std::max(std::fabs(outL), std::fabs(outR)));
  }

  // If the host gives us more output channels than we processed, silence them.
  for (int ch = nChans; ch < NOutChansConnected(); ++ch)
    for (int s = 0; s < nFrames; ++s)
      outputs[ch][s] = 0.0;

  mOutputPeak.store(outPeak, std::memory_order_relaxed);
}

void PbPFLStereoEnhancer::OnIdle()
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

bool PbPFLStereoEnhancer::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  // Parameter and bypass changes arrive through the standard SPVFUI path; preset/
  // help/url handling lives in the WebView UI itself.
  return false;
}
