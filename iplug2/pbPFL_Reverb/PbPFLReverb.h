#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <cstring>
#include <vector>

using namespace iplug;

const int kNumPresets = 1;

enum EParams
{
  kSize = 0,
  kDamping,
  kDiffusion,
  kMod,
  kWidth,
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

// ----------------------------------------------------------------------------
// Freeverb reimplementation. This is a 1:1 port of juce::dsp::Reverb (which wraps
// juce::Reverb), using the identical comb/allpass tunings, gain scaling and the
// same one-pole damping/feedback updates, so the ported algorithm is sound-faithful
// to the original JUCE Reverb. No JUCE dependency.
// ----------------------------------------------------------------------------
class FreeverbComb
{
public:
  void SetSize(int size)
  {
    if (size < 1) size = 1;
    mBuffer.assign(static_cast<size_t>(size), 0.0f);
    mSize = size;
    mIndex = 0;
    mLast = 0.0f;
  }
  void Clear()
  {
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mIndex = 0;
    mLast = 0.0f;
  }
  inline float Process(float input, float damp, float feedback)
  {
    const float output = mBuffer[static_cast<size_t>(mIndex)];
    mLast = (output * (1.0f - damp)) + (mLast * damp);
    const float temp = input + (mLast * feedback);
    mBuffer[static_cast<size_t>(mIndex)] = temp;
    if (++mIndex >= mSize) mIndex = 0;
    return output;
  }
private:
  std::vector<float> mBuffer;
  int mSize = 0, mIndex = 0;
  float mLast = 0.0f;
};

class FreeverbAllPass
{
public:
  void SetSize(int size)
  {
    if (size < 1) size = 1;
    mBuffer.assign(static_cast<size_t>(size), 0.0f);
    mSize = size;
    mIndex = 0;
  }
  void Clear()
  {
    std::fill(mBuffer.begin(), mBuffer.end(), 0.0f);
    mIndex = 0;
  }
  inline float Process(float input)
  {
    const float buffered = mBuffer[static_cast<size_t>(mIndex)];
    const float temp = input + (buffered * 0.5f);
    mBuffer[static_cast<size_t>(mIndex)] = temp;
    if (++mIndex >= mSize) mIndex = 0;
    return buffered - input;
  }
private:
  std::vector<float> mBuffer;
  int mSize = 0, mIndex = 0;
};

// Simple one-pole smoother matching juce::SmoothedValue (linear ramp over a fixed
// time). For per-sample stepping we approximate with an exponential one-pole, which
// settles to the target quickly (10ms) just like the JUCE original.
class ParamSmoother
{
public:
  void Reset(double sampleRate, double smoothTimeSec)
  {
    const double n = std::max(1.0, smoothTimeSec * sampleRate);
    mCoeff = 1.0f - static_cast<float>(std::exp(-1.0 / (n / 4.0)));
  }
  void SetTarget(float t) { mTarget = t; }
  void SetCurrent(float v) { mCurrent = v; }
  inline float Next()
  {
    mCurrent += (mTarget - mCurrent) * mCoeff;
    return mCurrent;
  }
private:
  float mTarget = 0.0f, mCurrent = 0.0f, mCoeff = 1.0f;
};

class Freeverb
{
public:
  void SetSampleRate(double sampleRate)
  {
    static const short combTunings[] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const short allPassTunings[] = { 556, 441, 341, 225 };
    const int stereoSpread = 23;
    const int isr = static_cast<int>(sampleRate);

    for (int i = 0; i < kNumCombs; ++i)
    {
      mComb[0][i].SetSize((isr * combTunings[i]) / 44100);
      mComb[1][i].SetSize((isr * (combTunings[i] + stereoSpread)) / 44100);
    }
    for (int i = 0; i < kNumAllPasses; ++i)
    {
      mAllPass[0][i].SetSize((isr * allPassTunings[i]) / 44100);
      mAllPass[1][i].SetSize((isr * (allPassTunings[i] + stereoSpread)) / 44100);
    }
    const double smoothTime = 0.01;
    mDamping.Reset(sampleRate, smoothTime);
    mFeedback.Reset(sampleRate, smoothTime);
    mWetGain1.Reset(sampleRate, smoothTime);
    mWetGain2.Reset(sampleRate, smoothTime);
  }

  void Clear()
  {
    for (int j = 0; j < kNumChannels; ++j)
    {
      for (int i = 0; i < kNumCombs; ++i) mComb[j][i].Clear();
      for (int i = 0; i < kNumAllPasses; ++i) mAllPass[j][i].Clear();
    }
  }

  // roomSize, damping, width in 0..1. wetLevel=1, dryLevel=0 (matches processReverb).
  void SetParameters(float roomSize, float damping, float width)
  {
    const float wetScaleFactor = 3.0f;
    const float wet = 1.0f * wetScaleFactor; // wetLevel = 1
    mWetGain1.SetTarget(0.5f * wet * (1.0f + width));
    mWetGain2.SetTarget(0.5f * wet * (1.0f - width));
    mGain = 0.015f; // freezeMode = 0

    const float roomScaleFactor = 0.28f;
    const float roomOffset = 0.7f;
    const float dampScaleFactor = 0.4f;
    mDamping.SetTarget(damping * dampScaleFactor);
    mFeedback.SetTarget(roomSize * roomScaleFactor + roomOffset);
  }

  // dryLevel = 0, so the dry term in JUCE's processStereo vanishes; this writes the
  // pure wet reverb into left/right (the host's dry/wet mix happens later).
  void ProcessStereo(float* left, float* right, int numSamples)
  {
    for (int i = 0; i < numSamples; ++i)
    {
      const float input = (left[i] + right[i]) * mGain;
      float outL = 0.0f, outR = 0.0f;
      const float damp = mDamping.Next();
      const float fb = mFeedback.Next();

      for (int j = 0; j < kNumCombs; ++j)
      {
        outL += mComb[0][j].Process(input, damp, fb);
        outR += mComb[1][j].Process(input, damp, fb);
      }
      for (int j = 0; j < kNumAllPasses; ++j)
      {
        outL = mAllPass[0][j].Process(outL);
        outR = mAllPass[1][j].Process(outR);
      }
      const float wet1 = mWetGain1.Next();
      const float wet2 = mWetGain2.Next();
      left[i]  = outL * wet1 + outR * wet2;
      right[i] = outR * wet1 + outL * wet2;
    }
  }

  void ProcessMono(float* samples, int numSamples)
  {
    for (int i = 0; i < numSamples; ++i)
    {
      const float input = samples[i] * mGain;
      float output = 0.0f;
      const float damp = mDamping.Next();
      const float fb = mFeedback.Next();
      for (int j = 0; j < kNumCombs; ++j)
        output += mComb[0][j].Process(input, damp, fb);
      for (int j = 0; j < kNumAllPasses; ++j)
        output = mAllPass[0][j].Process(output);
      const float wet1 = mWetGain1.Next();
      samples[i] = output * wet1;
    }
  }

private:
  enum { kNumCombs = 8, kNumAllPasses = 4, kNumChannels = 2 };
  FreeverbComb mComb[kNumChannels][kNumCombs];
  FreeverbAllPass mAllPass[kNumChannels][kNumAllPasses];
  ParamSmoother mDamping, mFeedback, mWetGain1, mWetGain2;
  float mGain = 0.015f;
};

class PbPFLReverb final : public Plugin
{
public:
  PbPFLReverb(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  Freeverb mReverb;
  float mLfoPhase = 0.0f;
  float mDiffusionState[2] = { 0.0f, 0.0f };

  std::vector<float> mWetL, mWetR;

  std::atomic<float> mInputPeak { 0.0f };
  std::atomic<float> mOutputPeak { 0.0f };
};
