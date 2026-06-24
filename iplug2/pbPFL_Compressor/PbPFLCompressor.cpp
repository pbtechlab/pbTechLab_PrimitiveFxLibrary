#include "PbPFLCompressor.h"
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

PbPFLCompressor::PbPFLCompressor(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Parameter set is a 1:1 match of the original JUCE Compressor (ranges, defaults,
  // steps and units are identical, so nothing is lost in the migration).
  GetParam(kThreshold)->InitDouble("Threshold", -18.0, -60.0, 0.0, 0.1, "dB");
  GetParam(kRatio)->InitDouble("Ratio", 4.0, 1.0, 20.0, 0.1, ":1");
  GetParam(kAttack)->InitDouble("Attack", 12.0, 0.1, 100.0, 0.1, "ms");
  GetParam(kRelease)->InitDouble("Release", 160.0, 10.0, 1000.0, 1.0, "ms");
  GetParam(kMakeup)->InitDouble("Makeup", 4.0, 0.0, 24.0, 0.1, "dB");
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

  MakePreset("Compressor Init", -18.0, 4.0, 12.0, 160.0, 4.0, 100.0, 0.0, 0.0);
}

#if IPLUG_DSP
void PbPFLCompressor::OnReset()
{
  mEnv[0] = mEnv[1] = 0.0f;
}

void PbPFLCompressor::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
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
  // juce::dsp::Compressor: feed-forward, peak ballistics envelope follower with
  // attack/release coefficients, hard-knee static curve, then makeup gain.
  const float thresholdDb = static_cast<float>(GetParam(kThreshold)->Value());
  const float threshold   = static_cast<float>(DBToAmp(thresholdDb));
  const float ratio       = static_cast<float>(GetParam(kRatio)->Value());
  const float ratioInv    = 1.0f / ratio;
  const float attackMs    = static_cast<float>(GetParam(kAttack)->Value());
  const float releaseMs   = static_cast<float>(GetParam(kRelease)->Value());
  const float makeup      = static_cast<float>(DBToAmp(GetParam(kMakeup)->Value()));

  // BallisticsFilter coefficients (peak mode): exp(-1 / (time_seconds * sr)).
  const float attCoeff = std::exp(-1.0f / (std::max(1.0e-4f, attackMs * 0.001f) * static_cast<float>(sr)));
  const float relCoeff = std::exp(-1.0f / (std::max(1.0e-4f, releaseMs * 0.001f) * static_cast<float>(sr)));

  const float wetGain = std::clamp(static_cast<float>(GetParam(kDryWet)->Value()) * 0.01f, 0.0f, 1.0f);
  const float dryGain = 1.0f - wetGain;
  const float outGain = static_cast<float>(DBToAmp(GetParam(kOutputGain)->Value()));

  float outPeak = 0.0f;

  for (int i = 0; i < nFrames; ++i)
  {
    for (int ch = 0; ch < nChans; ++ch)
    {
      const float dry = static_cast<float>(inputs[ch][i]);

      // Peak envelope follower (ballistics filter).
      const float in = std::fabs(dry);
      float& env = mEnv[ch];
      const float coeff = (in > env) ? attCoeff : relCoeff;
      env = in + coeff * (env - in);

      // Static hard-knee gain curve.
      float gain = 1.0f;
      if (env > threshold && env > 0.0f)
        gain = std::pow(env / threshold, ratioInv - 1.0f);

      const float wet = dry * gain * makeup;

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

void PbPFLCompressor::OnIdle()
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

bool PbPFLCompressor::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  // Parameter and bypass changes arrive through the standard SPVFUI path; preset/
  // help/url handling lives in the WebView UI itself.
  return false;
}
