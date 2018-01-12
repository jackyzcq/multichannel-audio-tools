#include "audio/dsp/envelope_detector.h"

#include <cmath>

#include "audio/dsp/testing_util.h"
#include "audio/linear_filters/biquad_filter_design.h"
#include "gtest/gtest.h"
#include "third_party/eigen3/Eigen/Core"

#include "audio/dsp/porting.h"  // auto-added.


namespace audio_dsp {
namespace {

TEST(EnvelopeDetectorTypedTest, EstimatesRmsOfSineWave) {
  constexpr float kSampleRate = 16000.0f;
  constexpr int kNumSamples = 6400;

  // We will measure the RMS of these sinusoids. The first should have an
  // RMS value of 1 / sqrt(2) and the second will be removed by the prefilter
  // and have a measured RMS of 0.0;
  constexpr float kChannelOneFrequency = 200.0f;
  constexpr float kChannelTwoFrequency = 8000.0f;

  EnvelopeDetector detector;

  linear_filters::BiquadFilterCascadeCoefficients coeffs =
      linear_filters::ButterworthFilterDesign(2).
          BandpassCoefficients(kSampleRate, 20, 1000);
  detector.Init(2, kSampleRate, 5.0, 100.0, coeffs);

  Eigen::ArrayXXf sinusoids(2, kNumSamples);
  constexpr float rads_per_sample_one =
      2 * M_PI * kChannelOneFrequency / kSampleRate;
  constexpr float rads_per_sample_two =
      2 * M_PI * kChannelTwoFrequency / kSampleRate;
  for (int i = 0; i < kNumSamples; ++i) {
    sinusoids(0, i) = std::sin(rads_per_sample_one * i);
    sinusoids(1, i) = std::sin(rads_per_sample_two * i);
  }

  Eigen::ArrayXXf output;
  CHECK(detector.ProcessBlock(sinusoids, &output));

  const int last_sample_index = output.row(0).size() - 1;
  // The RMS power of a sine wave is 1 / sqrt(2).
  EXPECT_NEAR(output(0, last_sample_index), 1 / M_SQRT2, 1e-2);
  // The high frequency signal was highly attenuated and not captured by the
  // envelope detector.
  EXPECT_NEAR(output(1, last_sample_index), 0, 1e-2);

  EXPECT_THAT(output.col(last_sample_index),
              EigenArrayEq(detector.MostRecentRmsEnvelopeValue()));
}

TEST(EnvelopeDetectorTypedTest, CanGetMostRecentSamplesWhenProcessReturnsNone) {
  constexpr float kSampleRate = 16000.0f;
  constexpr int kNumSamples = 640;

  // We will get very few output samples per block of input samples.
  EnvelopeDetector detector;
  detector.Init(1, kSampleRate, 1.0, 30.0f);

  Eigen::ArrayXXf noise(1, kNumSamples);
  std::mt19937 rng;
  for (int i = 0; i < kNumSamples; ++i) {
    noise(0, i) = std::normal_distribution<float>(-1.0, 1.0)(rng);
  }

  Eigen::ArrayXXf output;
  CHECK(detector.ProcessBlock(noise, &output));
  float current_output = detector.MostRecentRmsEnvelopeValue()[0];

  // Pass a very small sample block, we expect no samples in return.
  Eigen::Array<float, Eigen::Dynamic, Eigen::Dynamic> small_buffer(1, 10);
  for (int i = 0; i < 10; ++i) {
    small_buffer(0, i) = std::normal_distribution<float>(-1.0, 1.0)(rng);
  }

  CHECK(detector.ProcessBlock(small_buffer, &output));
  // We didn't get any output samples, but we can still get a recent measurement
  // of the RMS power.
  EXPECT_EQ(output.cols(), 0);
  EXPECT_EQ(detector.MostRecentRmsEnvelopeValue()[0], current_output);
}

}  // namespace
}  // namespace audio_dsp
