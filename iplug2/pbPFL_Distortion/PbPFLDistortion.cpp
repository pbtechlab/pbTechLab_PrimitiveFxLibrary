#include "PbPFLDistortion.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
// Anchor symbol used to resolve THIS module's path at runtime (works for both the
// VST3 DLL and the standalone EXE, independent of iPlug2's gHINSTANCE plumbing which
// is not wired for VST3 on Windows).
const char kPbPFLModuleAnchor = 0;

inline float smooth(float current, float target, float coeff)
{
  return current + (target - current) * coeff;
}
}

PbPFLDistortion::PbPFLDistortion(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Parameter set is a 1:1 match of the original JUCE Distortion (ranges, defaults,
  // steps and units are identical, so nothing is lost in the migration).
  GetParam(kDrive)->InitDouble("Drive", 12.0, 0.0, 36.0, 0.1, "dB");
  GetParam(kBias)->InitDouble("Bias", 0.0, -1.0, 1.0, 0.01, "");
  GetParam(kTone)->InitDouble("Tone", 55.0, 0.0, 100.0, 1.0, "%");
  GetParam(kLowCut)->InitDouble("Low Cut", 80.0, 20.0, 1000.0, 1.0, "Hz");
  GetParam(kShape)->InitDouble("Shape", 45.0, 0.0, 100.0, 1.0, "%");
  GetParam(kDryWet)->InitDouble("Dry/Wet", 70.0, 0.0, 100.0, 1.0, "%");
  GetParam(kOutputGain)->InitDouble("Output", -6.0, -36.0, 12.0, 0.1, "dB");
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

  MakePreset("Distortion Init", 12.0, 0.0, 55.0, 80.0, 45.0, 70.0, -6.0, 0.0);
}

#if IPLUG_DSP
void PbPFLDistortion::OnReset()
{
  for (int ch = 0; ch < 2; ++ch)
  {
    mHp[ch] = 0.0f;
    mPrev[ch] = 0.0f;
  }
}

void PbPFLDistortion::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
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
  const float drive    = static_cast<float>(DBToAmp(GetParam(kDrive)->Value()));
  const float bias     = static_cast<float>(GetParam(kBias)->Value());
  const float shape    = 1.0f + static_cast<float>(GetParam(kShape)->Value()) * 0.06f;
  // The original references param("presence"), which is not part of the Distortion
  // parameter set; it resolves to 0 dB => presence gain == 1.0. Reproduced faithfully.
  const float presence = 1.0f;
  const float tone     = static_cast<float>(GetParam(kTone)->Value()) * 0.01f;
  const float lowCut   = std::exp(-2.0f * kPi * static_cast<float>(GetParam(kLowCut)->Value()) / static_cast<float>(sr));

  const float wetGain  = std::clamp(static_cast<float>(GetParam(kDryWet)->Value()) * 0.01f, 0.0f, 1.0f);
  const float dryGain  = 1.0f - wetGain;
  const float outGain  = static_cast<float>(DBToAmp(GetParam(kOutputGain)->Value()));

  float outPeak = 0.0f;

  for (int ch = 0; ch < nChans; ++ch)
  {
    // hp/prev are reset per channel/block, exactly as in the JUCE processDistortion().
    float hp = 0.0f, prev = 0.0f;
    for (int i = 0; i < nFrames; ++i)
    {
      const float dry = static_cast<float>(inputs[ch][i]);

      const float driven = dry * drive + bias;
      float y = std::tanh(driven * shape) / std::tanh(shape);
      hp = lowCut * (hp + y - prev);
      prev = y;
      y = hp * (0.7f + presence * 0.3f);
      const float wet = smooth(y, std::tanh(y * 1.8f), tone);

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

void PbPFLDistortion::OnIdle()
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

bool PbPFLDistortion::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  // Parameter and bypass changes arrive through the standard SPVFUI path; preset/
  // help/url handling lives in the WebView UI itself.
  return false;
}
