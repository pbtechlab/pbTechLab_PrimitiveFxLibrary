#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <cmath>
#include <cstring>

using namespace iplug;

const int kNumPresets = 1;

enum EParams
{
  kLowGain = 0,
  kLowFreq,
  kLowQ,
  kMidGain,
  kMidFreq,
  kMidQ,
  kHighGain,
  kHighQ,
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

// RBJ biquad (Direct Form I). Behaviourally equivalent to
// juce::dsp::IIR::Filter with Coefficients::makeLowShelf / makePeakFilter /
// makeHighShelf as used by the original JUCE ThreeBandEQ, so the ported
// algorithm is sample-faithful.
class RBJBiquad
{
public:
  enum Type { kLowShelf, kPeak, kHighShelf };

  void Clear()
  {
    mX1 = mX2 = mY1 = mY2 = 0.0;
  }

  // gainLinear is the linear amplitude gain (NOT dB).
  void SetCoeffs(Type type, double sampleRate, double freq, double q, double gainLinear)
  {
    const double A = std::sqrt(gainLinear);
    const double w0 = 6.283185307179586 * freq / sampleRate;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * q);

    double b0, b1, b2, a0, a1, a2;

    switch (type)
    {
      case kPeak:
      {
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cosw0;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cosw0;
        a2 = 1.0 - alpha / A;
        break;
      }
      case kLowShelf:
      {
        const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAalpha);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
        b2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAalpha);
        a0 = (A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAalpha;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
        a2 = (A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAalpha;
        break;
      }
      case kHighShelf:
      default:
      {
        const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAalpha);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
        b2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAalpha);
        a0 = (A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAalpha;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
        a2 = (A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAalpha;
        break;
      }
    }

    const double inv = 1.0 / a0;
    mB0 = b0 * inv;
    mB1 = b1 * inv;
    mB2 = b2 * inv;
    mA1 = a1 * inv;
    mA2 = a2 * inv;
  }

  inline float Process(float in)
  {
    const double x = static_cast<double>(in);
    const double y = mB0 * x + mB1 * mX1 + mB2 * mX2 - mA1 * mY1 - mA2 * mY2;
    mX2 = mX1;
    mX1 = x;
    mY2 = mY1;
    mY1 = y;
    return static_cast<float>(y);
  }

private:
  double mB0 = 1.0, mB1 = 0.0, mB2 = 0.0, mA1 = 0.0, mA2 = 0.0;
  double mX1 = 0.0, mX2 = 0.0, mY1 = 0.0, mY2 = 0.0;
};

class PbPFL3BandEQ final : public Plugin
{
public:
  PbPFL3BandEQ(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  RBJBiquad mLow[2];
  RBJBiquad mMid[2];
  RBJBiquad mHigh[2];

  std::atomic<float> mInputPeak { 0.0f };
  std::atomic<float> mOutputPeak { 0.0f };
};
