#include "PbPFLPitchShifter.h"
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

PbPFLPitchShifter::PbPFLPitchShifter(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Parameter set is a 1:1 match of the original JUCE PitchShifter (ranges,
  // defaults, steps and units are identical, so nothing is lost in the migration).
  GetParam(kSemitones)->InitDouble("Semitone", 0.0, -24.0, 24.0, 1.0, "st");
  GetParam(kCents)->InitDouble("Cents", 0.0, -100.0, 100.0, 1.0, "ct");
  GetParam(kGrain)->InitDouble("Grain", 80.0, 20.0, 180.0, 1.0, "ms");
  GetParam(kDryWet)->InitDouble("Dry/Wet", 50.0, 0.0, 100.0, 1.0, "%");
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

  MakePreset("PitchShifter Init", 0.0, 0.0, 80.0, 50.0, 0.0);
}

#if IPLUG_DSP
void PbPFLPitchShifter::OnReset()
{
  for (auto& s : mSrc)
    s.clear();
}

void PbPFLPitchShifter::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
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
  const float ratio = std::pow(2.0f,
      (static_cast<float>(GetParam(kSemitones)->Value()) +
       static_cast<float>(GetParam(kCents)->Value()) / 100.0f) / 12.0f);
  const int grain = std::clamp(static_cast<int>(GetParam(kGrain)->Value() * sr * 0.001), 32, 8192);
  const float wetGain = std::clamp(static_cast<float>(GetParam(kDryWet)->Value()) * 0.01f, 0.0f, 1.0f);
  const float dryGain = 1.0f - wetGain;
  const float outGain = static_cast<float>(DBToAmp(GetParam(kOutputGain)->Value()));

  const int n = nFrames;
  const int nm1 = std::max(1, n - 1);

  float outPeak = 0.0f;

  for (int ch = 0; ch < nChans; ++ch)
  {
    // Copy the input block (= JUCE's src.makeCopyOf(wet)). The pitch-shift reads
    // block-relative positions, so the source must be the unmodified input.
    std::vector<float>& src = mSrc[ch];
    if (static_cast<int>(src.size()) < n)
      src.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
      src[static_cast<size_t>(i)] = static_cast<float>(inputs[ch][i]);

    const float* in = src.data();

    for (int i = 0; i < n; ++i)
    {
      const float read = std::fmod(static_cast<float>(i) * ratio, static_cast<float>(nm1));
      const int i0 = static_cast<int>(read);
      const int i1 = std::min(i0 + 1, n - 1);
      const float frac = read - static_cast<float>(i0);
      const float shifted = in[i0] + (in[i1] - in[i0]) * frac;
      const float window = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i % grain) / static_cast<float>(grain));
      const float wet = shifted * (0.65f + window * 0.35f);

      const float dry = in[i];
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

void PbPFLPitchShifter::OnIdle()
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

bool PbPFLPitchShifter::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  // Parameter and bypass changes arrive through the standard SPVFUI path; preset/
  // help/url handling lives in the WebView UI itself.
  return false;
}
