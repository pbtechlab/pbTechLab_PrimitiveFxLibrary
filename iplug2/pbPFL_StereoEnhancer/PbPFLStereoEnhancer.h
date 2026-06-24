#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <cstring>

using namespace iplug;

const int kNumPresets = 1;

enum EParams
{
  kWidth = 0,
  kEnhance,
  kFocus,
  kBassMono,
  kDryWet,
  kOutputGain,
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

class PbPFLStereoEnhancer final : public Plugin
{
public:
  PbPFLStereoEnhancer(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  // One-pole "focus" smoother state for the side signal (mirrors smoothingStateL in
  // the original JUCE PrimitiveFxProcessor::processStereoEnhancer).
  float mSmoothingState = 0.0f;

  std::atomic<float> mInputPeak { 0.0f };
  std::atomic<float> mOutputPeak { 0.0f };
};
