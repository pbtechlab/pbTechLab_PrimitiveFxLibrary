#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <cstring>
#include <vector>

using namespace iplug;

const int kNumPresets = 1;

enum EParams
{
  kSemitones = 0,
  kCents,
  kGrain,
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

class PbPFLPitchShifter final : public Plugin
{
public:
  PbPFLPitchShifter(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  // Scratch buffer holding a copy of the current input block per channel. The
  // JUCE original operates per-block on a copy of the wet buffer (block-relative
  // read positions and window phase), so we mirror that exactly here.
  std::vector<float> mSrc[2];

  std::atomic<float> mInputPeak { 0.0f };
  std::atomic<float> mOutputPeak { 0.0f };
};
