#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <cstring>

using namespace iplug;

const int kNumPresets = 1;

enum EParams
{
  kThreshold = 0,
  kRatio,
  kAttack,
  kRelease,
  kMakeup,
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

class PbPFLCompressor final : public Plugin
{
public:
  PbPFLCompressor(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  // Per-channel peak envelope follower state (matches juce::dsp::Compressor's
  // ballistic peak detector, one BallisticsFilter per channel).
  float mEnv[2] = { 0.0f, 0.0f };

  std::atomic<float> mInputPeak { 0.0f };
  std::atomic<float> mOutputPeak { 0.0f };
};
