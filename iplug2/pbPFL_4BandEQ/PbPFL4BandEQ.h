#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

using namespace iplug;

namespace pbpfl_eq
{
constexpr double kPi = 3.14159265358979323846;
}

const int kNumPresets = 1;

enum EParams
{
  kLowGain = 0,
  kLowFreq,
  kLowQ,
  kMid1Gain,
  kMid1Freq,
  kMid1Q,
  kMid2Gain,
  kMid2Freq,
  kMid2Q,
  kHighGain,
  kHighFreq,
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

// RBJ biquad (Direct Form I), behaviourally equivalent to the
// juce::dsp::IIR::Filter + Coefficients used by the original JUCE 4-band EQ.
// Supports the three response types the EQ needs: low shelf, peak, high shelf.
class RBJBiquad
{
public:
  void Reset()
  {
    mX1 = mX2 = mY1 = mY2 = 0.0f;
  }

  void SetLowShelf(double sr, double freq, double q, double gainLin)
  {
    const double A = std::sqrt(gainLin);
    const double w0 = 2.0 * pbpfl_eq::kPi * freq / sr;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;

    const double b0 = A * ((A + 1.0) - (A - 1.0) * cw + twoSqrtAAlpha);
    const double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
    const double b2 = A * ((A + 1.0) - (A - 1.0) * cw - twoSqrtAAlpha);
    const double a0 = (A + 1.0) + (A - 1.0) * cw + twoSqrtAAlpha;
    const double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
    const double a2 = (A + 1.0) + (A - 1.0) * cw - twoSqrtAAlpha;
    Normalize(b0, b1, b2, a0, a1, a2);
  }

  void SetHighShelf(double sr, double freq, double q, double gainLin)
  {
    const double A = std::sqrt(gainLin);
    const double w0 = 2.0 * pbpfl_eq::kPi * freq / sr;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;

    const double b0 = A * ((A + 1.0) + (A - 1.0) * cw + twoSqrtAAlpha);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
    const double b2 = A * ((A + 1.0) + (A - 1.0) * cw - twoSqrtAAlpha);
    const double a0 = (A + 1.0) - (A - 1.0) * cw + twoSqrtAAlpha;
    const double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
    const double a2 = (A + 1.0) - (A - 1.0) * cw - twoSqrtAAlpha;
    Normalize(b0, b1, b2, a0, a1, a2);
  }

  void SetPeak(double sr, double freq, double q, double gainLin)
  {
    const double A = std::sqrt(gainLin);
    const double w0 = 2.0 * pbpfl_eq::kPi * freq / sr;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cw;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cw;
    const double a2 = 1.0 - alpha / A;
    Normalize(b0, b1, b2, a0, a1, a2);
  }

  inline float Process(float x)
  {
    const float y = mB0 * x + mB1 * mX1 + mB2 * mX2 - mA1 * mY1 - mA2 * mY2;
    mX2 = mX1;
    mX1 = x;
    mY2 = mY1;
    mY1 = y;
    return y;
  }

private:
  void Normalize(double b0, double b1, double b2, double a0, double a1, double a2)
  {
    mB0 = static_cast<float>(b0 / a0);
    mB1 = static_cast<float>(b1 / a0);
    mB2 = static_cast<float>(b2 / a0);
    mA1 = static_cast<float>(a1 / a0);
    mA2 = static_cast<float>(a2 / a0);
  }

  float mB0 = 1.0f, mB1 = 0.0f, mB2 = 0.0f, mA1 = 0.0f, mA2 = 0.0f;
  float mX1 = 0.0f, mX2 = 0.0f, mY1 = 0.0f, mY2 = 0.0f;
};

class PbPFL4BandEQ final : public Plugin
{
public:
  PbPFL4BandEQ(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  // One filter instance per band per channel. The original JUCE EQ applies the
  // low shelf and the two peak (bell) filters; the high shelf coefficients are
  // computed but not applied in the source, so we mirror that signal chain.
  RBJBiquad mLow[2];
  RBJBiquad mMid1[2];
  RBJBiquad mMid2[2];

  std::atomic<float> mInputPeak { 0.0f };
  std::atomic<float> mOutputPeak { 0.0f };
};
