#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <cstring>

using namespace iplug;

const int kNumPresets = 1;

enum EParams
{
  kInputGain = 0, // "input" collides with iplug::ERoute::kInput, so suffixed.
  kCeiling,
  kRelease,
  kSoftClip,
  kDryWet,
  kOutputGain, // "output" collides with iplug::ERoute::kOutput, so suffixed.
  kBypass,
  kNumParams
};

// Arbitrary message tags (UI -> DSP). Parameter and bypass changes use the
// standard SPVFUI path, so these are only for non-parameter editor events.
enum EMsgTags
{
  kMsgTagUIReady = 0,
  kMsgTagRequestState,
  kMsgTagSavePreset,
  kMsgTagHelp,
  kMsgTagOpenUrl
};

class PbPFLLimiter final : public Plugin
{
public:
  PbPFLLimiter(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  // Shared limiter envelope state (matches JUCE PrimitiveFxProcessor::limiterEnv).
  float mLimiterEnv = 1.0f;

  std::atomic<float> mInputPeak { 0.0f };
  std::atomic<float> mOutputPeak { 0.0f };
};
