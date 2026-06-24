#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <cstring>

using namespace iplug;

const int kNumPresets = 1;

enum EParams
{
  kDrive = 0,
  kBias,
  kTone,
  kLowCut,
  kShape,
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

class PbPFLDistortion final : public Plugin
{
public:
  PbPFLDistortion(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  // Per-channel one-pole high-pass state (matches the JUCE Distortion DC/low-cut path).
  float mHp[2] = { 0.0f, 0.0f };
  float mPrev[2] = { 0.0f, 0.0f };

  std::atomic<float> mInputPeak { 0.0f };
  std::atomic<float> mOutputPeak { 0.0f };
};
