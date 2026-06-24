#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

using namespace iplug;

const int kNumPresets = 1;

enum EParams
{
  kRate = 0,
  kDepth,
  kCenter,
  kFeedback,
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

// First-order topology-preserving transform (TPT) all-pass filter. This is a
// 1:1 reimplementation of juce::dsp::FirstOrderTPTFilter in allpass mode, used by
// the original JUCE juce::dsp::Phaser, so the cascade is sample-faithful.
class TPTAllpass
{
public:
  void Prepare(double sampleRate)
  {
    mSampleRate = sampleRate;
    Reset();
  }

  void Reset() { mState = 0.0f; }

  inline void SetCutoff(float freqHz)
  {
    const float g = static_cast<float>(std::tan(3.14159265358979323846 * freqHz / mSampleRate));
    mG = g / (1.0f + g);
  }

  inline float Process(float x)
  {
    const float v = mG * (x - mState);
    const float y = v + mState;
    mState = y + v;
    return 2.0f * y - x; // allpass
  }

private:
  double mSampleRate = 44100.0;
  float mG = 0.0f;
  float mState = 0.0f;
};

class PbPFLPhaser final : public Plugin
{
public:
  PbPFLPhaser(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  static constexpr int kNumStages = 6;

  TPTAllpass mFilters[2][kNumStages];
  float mLastOutput[2] = { 0.0f, 0.0f };
  float mLfoPhase = 0.0f;

  std::atomic<float> mInputPeak { 0.0f };
  std::atomic<float> mOutputPeak { 0.0f };
};
