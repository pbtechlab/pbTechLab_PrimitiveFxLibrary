#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <cstring>
#include <vector>

using namespace iplug;

const int kNumPresets = 1;

enum EParams
{
  kTime = 0,
  kFeedback,
  kTone,
  kSpread,
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

// Fractional delay line with linear interpolation. Behaviourally equivalent to
// juce::dsp::DelayLine<float, DelayLineInterpolationTypes::Linear> as used by the
// original JUCE Delay, so the ported algorithm is sample-faithful.
class InterpDelayLine
{
public:
  void Reset(int maxSamples)
  {
    mSize = maxSamples + 4;
    mBuffer.assign(static_cast<size_t>(mSize), 0.0f);
    mWrite = 0;
  }

  void Clear()
  {
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mWrite = 0;
  }

  inline float Read(float delaySamples) const
  {
    float rp = static_cast<float>(mWrite) - delaySamples;
    while (rp < 0.0f)
      rp += static_cast<float>(mSize);
    int i0 = static_cast<int>(rp);
    const float frac = rp - static_cast<float>(i0);
    if (i0 >= mSize)
      i0 -= mSize;
    int i1 = i0 + 1;
    if (i1 >= mSize)
      i1 -= mSize;
    return mBuffer[static_cast<size_t>(i0)] +
           (mBuffer[static_cast<size_t>(i1)] - mBuffer[static_cast<size_t>(i0)]) * frac;
  }

  inline void Write(float x)
  {
    mBuffer[static_cast<size_t>(mWrite)] = x;
    if (++mWrite >= mSize)
      mWrite = 0;
  }

private:
  std::vector<float> mBuffer;
  int mSize = 0;
  int mWrite = 0;
};

class PbPFLDelay final : public Plugin
{
public:
  PbPFLDelay(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  static constexpr int kMaxDelaySamples = 192000;

  InterpDelayLine mDelay[2];
  float mFeedbackFilter[2] = { 0.0f, 0.0f };
  float mLfoPhase = 0.0f;

  std::atomic<float> mInputPeak { 0.0f };
  std::atomic<float> mOutputPeak { 0.0f };
};
