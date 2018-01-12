#include "audio/dsp/hifi/dynamic_range_control.h"

#include "audio/dsp/decibels.h"
#include "audio/dsp/hifi/dynamic_range_control_functions.h"
#include "audio/dsp/testing_util.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"
#include "third_party/eigen3/Eigen/Core"

#include "audio/dsp/porting.h"  // auto-added.


namespace audio_dsp {
namespace {

float CheckReferenceCompressor(float input_level_db,
                               float threshold_db,
                               float ratio,
                               float knee_width_db) {
  const float half_knee = knee_width_db / 2;
  if (input_level_db - threshold_db <= -half_knee) {
    return input_level_db;
  } else if (input_level_db - threshold_db >= half_knee) {
    return threshold_db + (input_level_db - threshold_db) / ratio;
  } else {
    const float knee_end = input_level_db - threshold_db + half_knee;
    return input_level_db +
        ((1 / ratio) - 1) * knee_end * knee_end / (knee_width_db * 2);
  }
}

float CheckReferenceExpander(float input_level_db,
                             float threshold_db,
                             float ratio,
                             float knee_width_db) {
  const float half_knee = knee_width_db / 2;
  if (input_level_db - threshold_db >= half_knee) {
    return input_level_db;
  } else if (input_level_db - threshold_db <= -half_knee) {
    return threshold_db + (input_level_db - threshold_db) * ratio;
  } else {
    const float knee_end = input_level_db - threshold_db - half_knee;
    return input_level_db +
        (1 - ratio) * knee_end * knee_end / (knee_width_db * 2);
  }
}

// Helper functions for testing a single scalar at a time. Verifies that
// a reference implementation is matched.
float OutputLevelCompressor(float input, float threshold,
                            float ratio, float knee) {
  Eigen::ArrayXf input_arr(1);
  Eigen::ArrayXf output_arr(1);
  input_arr[0] = input;
  ::audio_dsp::OutputLevelCompressor(input_arr, threshold, ratio, knee,
                                     &output_arr);
  EXPECT_NEAR(CheckReferenceCompressor(input, threshold, ratio, knee),
                                       output_arr.value(), 1e-4f);
  return output_arr.value();
}

float OutputLevelLimiter(float input, float threshold, float knee) {
  Eigen::ArrayXf input_arr(1);
  Eigen::ArrayXf output_arr(1);
  input_arr[0] = input;
  ::audio_dsp::OutputLevelLimiter(input_arr, threshold, knee, &output_arr);
  // A limiter is a compressor with a ratio of infinity.
  EXPECT_NEAR(CheckReferenceCompressor(
      input, threshold, std::numeric_limits<float>::infinity(), knee),
              output_arr.value(), 1e-4f);
  return output_arr.value();
}

float OutputLevelExpander(float input, float threshold,
                          float ratio, float knee) {
  Eigen::ArrayXf input_arr(1);
  Eigen::ArrayXf output_arr(1);
  input_arr[0] = input;
  ::audio_dsp::OutputLevelExpander(input_arr, threshold, ratio, knee,
                                   &output_arr);
  EXPECT_NEAR(CheckReferenceExpander(input, threshold, ratio, knee),
                                     output_arr.value(), 1e-4f);
  return output_arr.value();
}

TEST(ComputeGainTest, CompressorTest) {
  // No knee, above the threshold.
  EXPECT_FLOAT_EQ(OutputLevelCompressor(5.0f, 0.0f, 3.0f, 0.0f), 5.0f / 3.0f);
  EXPECT_FLOAT_EQ(OutputLevelCompressor(5.0f, 0.0f, 6.0f, 0.0f), 5.0f / 6.0f);
  EXPECT_FLOAT_EQ(OutputLevelCompressor(8.0f, 0.0f, 3.0f, 0.0f), 8.0f / 3.0f);
  EXPECT_FLOAT_EQ(OutputLevelCompressor(8.0f, 0.0f, 6.0f, 0.0f), 8.0f / 6.0f);
  EXPECT_FLOAT_EQ(OutputLevelCompressor(5.0f, -1.0f, 3.0f, 0.0f), 1.0f);
  EXPECT_FLOAT_EQ(OutputLevelCompressor(8.0f, -1.0f, 3.0f, 0.0f),
                  -1.0f + 9.0f / 3.0f);
  // No knee, below the threshold, input = output.
  for (float input = -40.0f; input < 20.0; input += 2.0) {
    ASSERT_FLOAT_EQ(OutputLevelCompressor(input, input + 0.1, 3.0f, 0.0f),
                    input);
  }
  // Add a knee and check for...
  for (float input = -50.0f; input < 50.0f; input += 0.1) {
    // Continuity.
    ASSERT_NEAR(OutputLevelCompressor(input, -10.0f, 3.0f, 10.0f),
                OutputLevelCompressor(input + 0.1, -10.0f, 3.0f, 10.0f),
                0.1 + 1e-5);
    // Monotonic decrease as knee increases.
    for (float knee_db = 0.5f; knee_db < 30.0f; knee_db += 1.5) {
      float new_knee = 10.0 + knee_db;
      ASSERT_GE(OutputLevelCompressor(input, -10.0f, 3.0f, 10.0f),
                OutputLevelCompressor(input, -10.0f, 3.0f, new_knee) - 1e-5);
    }
  }

  // Check that knee kicks in at the right place.
  for (float knee : {4.0, 8.0, 12.0}) {
    float half_knee = knee / 2;
    EXPECT_FLOAT_EQ(OutputLevelCompressor(-half_knee, 0.0f, 3.0f, 0.0f),
                    OutputLevelCompressor(-half_knee, 0.0f, 3.0f, knee));
    EXPECT_GT(
        std::abs(OutputLevelCompressor(-(half_knee - 0.2f), 0.0f, 3.0f, 0.0f) -
                 OutputLevelCompressor(-(half_knee - 0.2f), 0.0f, 3.0f, knee)),
              1e-3);
    EXPECT_GT(std::abs(OutputLevelCompressor(0.0f, 0.0f, 3.0f, 0.0f) -
                      OutputLevelCompressor(0.0f, 0.0f, 3.0f, knee)), 3e-1);
    EXPECT_GT(
        std::abs(OutputLevelCompressor(half_knee - 0.2f, 0.0f, 3.0f, 0.0f) -
                 OutputLevelCompressor(half_knee - 0.2f, 0.0f, 3.0f, knee)),
              1e-3);
    EXPECT_FLOAT_EQ(OutputLevelCompressor(half_knee, 0.0f, 3.0f, 0.0f),
                    OutputLevelCompressor(half_knee, 0.0f, 3.0f, knee));
  }
}

TEST(ComputeGainTest, LimiterTest) {
  // No knee, above the threshold.
  EXPECT_FLOAT_EQ(OutputLevelLimiter(5.0f, 0.0f, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(OutputLevelLimiter(5.0f, 0.0f, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(OutputLevelLimiter(8.0f, 0.0f, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(OutputLevelLimiter(8.0f, 0.0f, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(OutputLevelLimiter(5.0f, -1.0f, 0.0f), -1.0f);
  EXPECT_FLOAT_EQ(OutputLevelLimiter(8.0f, -1.0f, 0.0f), -1.0f);
  // No knee, below the threshold, input = output.
  for (float input = -40.0f; input < 20.0; input += 2.0) {
    ASSERT_FLOAT_EQ(OutputLevelLimiter(input, input + 0.1, 0.0f), input);
  }
  // No knee, above the threshold, input = threshold.
  for (float input = -40.0f; input < 20.0; input += 2.0) {
    float threshold_db = input - 0.1;
    ASSERT_FLOAT_EQ(OutputLevelLimiter(input, threshold_db, 0.0f),
                    threshold_db);
  }
  // Add a knee and check for...
  for (float input = -50.0f; input < 50.0f; input += 0.1) {
    // Continuity.
    ASSERT_NEAR(OutputLevelLimiter(input, -10.0f, 10.0f),
                OutputLevelLimiter(input + 0.1, -10.0f, 10.0f), 0.1 + 1e-5);
    // Monotonic decrease as knee increases.
    for (float knee_db = 0.5f; knee_db < 30.0f; knee_db += 1.5) {
      float new_knee = 10.0 + knee_db;
      ASSERT_GE(OutputLevelLimiter(input, -10.0f, 10.0f),
                OutputLevelLimiter(input, -10.0f, new_knee) - 1e-5);
    }
  }
  // Check that knee kicks in at the right place.
  for (float knee : {4.0, 8.0, 12.0}) {
    float half_knee = knee / 2;
    EXPECT_FLOAT_EQ(OutputLevelLimiter(-half_knee, 0.0f, 0.0f),
                    OutputLevelLimiter(-half_knee, 0.0f, knee));
    EXPECT_GT(
        std::abs(OutputLevelLimiter(-(half_knee - 0.2f), 0.0f, 0.0f) -
                 OutputLevelLimiter(-(half_knee - 0.2f), 0.0f, knee)), 1e-3);
    EXPECT_GT(std::abs(OutputLevelLimiter(0.0f, 0.0f, 0.0f) -
                      OutputLevelLimiter(0.0f, 0.0f, knee)), 3e-1);
    EXPECT_GT(
        std::abs(OutputLevelLimiter(half_knee - 0.2f, 0.0f, 0.0f) -
                 OutputLevelLimiter(half_knee - 0.2f, 0.0f, knee)), 1e-3);
    EXPECT_FLOAT_EQ(OutputLevelLimiter(half_knee, 0.0f, 0.0f),
                    OutputLevelLimiter(half_knee, 0.0f, knee));
  }
}

TEST(ComputeGainTest, ExpanderTest) {
  // No knee, above the threshold.
  EXPECT_FLOAT_EQ(OutputLevelExpander(-5.0f, 0.0f, 3.0f, 0.0f), -15.0f);
  EXPECT_FLOAT_EQ(OutputLevelExpander(-5.0f, 0.0f, 6.0f, 0.0f), -30.0f);
  EXPECT_FLOAT_EQ(OutputLevelExpander(-8.0f, 0.0f, 3.0f, 0.0f), -24.0f);
  EXPECT_FLOAT_EQ(OutputLevelExpander(-8.0f, 0.0f, 6.0f, 0.0f), -48.0f);
  EXPECT_FLOAT_EQ(OutputLevelExpander(-5.0f, -1.0f, 3.0f, 0.0f), -13.0f);
  EXPECT_FLOAT_EQ(OutputLevelExpander(-8.0f, -1.0f, 3.0f, 0.0f), -22.0f);
  // No knee, above the threshold, input = output.
  for (float input = -40.0f; input < 20.0; input += 2.0) {
    ASSERT_FLOAT_EQ(OutputLevelExpander(input, input - 0.1, 3.0f, 0.0f),
                    input);
  }
  // Add a knee and check for...
  for (float input = -50.0f; input < 50.0f; input += 0.1) {
    // Continuity.
    ASSERT_NEAR(OutputLevelExpander(input, -10.0f, 3.0f, 10.0f),
                OutputLevelExpander(input + 0.1, -10.0f, 3.0f, 10.0f),
                0.3 /* ratio * 0.1 */ + 1e-5);
    // Monotonic decrease as knee increases.
    for (float knee_db = 0.5f; knee_db < 30.0f; knee_db += 1.5) {
      float new_knee = 10.0 + knee_db;
      ASSERT_GE(OutputLevelExpander(input, -10.0f, 3.0f, 10.0f),
                OutputLevelExpander(input, -10.0f, 3.0f, new_knee) - 2e-5);
    }
  }

  // Check that knee kicks in at the right place.
  for (float knee : {4.0, 8.0, 12.0}) {
    float half_knee = knee / 2;
    EXPECT_FLOAT_EQ(OutputLevelExpander(-half_knee, 0.0f, 3.0f, 0.0f),
                    OutputLevelExpander(-half_knee, 0.0f, 3.0f, knee));
    EXPECT_GT(
        std::abs(OutputLevelExpander(-(half_knee - 0.2f), 0.0f, 3.0f, 0.0f) -
                 OutputLevelExpander(-(half_knee - 0.2f), 0.0f, 3.0f, knee)),
              1e-3);
    EXPECT_GT(std::abs(OutputLevelExpander(0.0f, 0.0f, 3.0f, 0.0f) -
                      OutputLevelExpander(0.0f, 0.0f, 3.0f, knee)), 3e-1);
    EXPECT_GT(
        std::abs(OutputLevelExpander(half_knee - 0.2f, 0.0f, 3.0f, 0.0f) -
                 OutputLevelExpander(half_knee - 0.2f, 0.0f, 3.0f, knee)),
              1e-3);
    EXPECT_FLOAT_EQ(OutputLevelExpander(half_knee, 0.0f, 3.0f, 0.0f),
                    OutputLevelExpander(half_knee, 0.0f, 3.0f, knee));
  }
}

TEST(DynamicRangeControl, InputOutputGainTest) {
  constexpr int kNumChannels = 2;
  constexpr int kNumSamples = 4;
  Eigen::ArrayXXf input(kNumChannels, kNumSamples);
  input << 0.1, 0.1, 0.1, 0.2,
           0.3, 0.3, 0.3, 0.0;

  Eigen::ArrayXXf output(kNumChannels, kNumSamples);
  {  // Input gain scales output linearly (below threshold).
    DynamicRangeControlParams params;
    params.threshold_db = 200;
    params.input_gain_db = AmplitudeRatioToDecibels(2);
    DynamicRangeControl drc(params);
    drc.Init(kNumChannels, kNumSamples, 48000.0f);
    drc.ProcessBlock(input, &output);
    EXPECT_THAT(output, EigenArrayNear(2 * input, 1e-4));
  }
  {  // Output gain scales output linearly (below threshold).
    DynamicRangeControlParams params;
    params.threshold_db = 200;
    params.output_gain_db = AmplitudeRatioToDecibels(2);
    DynamicRangeControl drc(params);
    drc.Init(kNumChannels, kNumSamples, 48000.0f);
    drc.ProcessBlock(input, &output);
    EXPECT_THAT(output, EigenArrayNear(2 * input, 1e-4));
  }
}

TEST(DynamicRangeControl, NoiseGateTest) {
  constexpr int kNumChannels = 2;
  constexpr int kNumSamples = 4;
  Eigen::ArrayXXf input(kNumChannels, kNumSamples);
  input << 0.1, 0.1, 0.1, 0.2,
           0.3, 0.3, 0.3, 0.0;

  Eigen::ArrayXXf output(kNumChannels, kNumSamples);
  {  // Signal is completely attenuated because it is below the threshold.
    DynamicRangeControlParams params;
    params.dynamics_type = kNoiseGate;
    params.threshold_db = 200;
    DynamicRangeControl drc(params);
    drc.Init(kNumChannels, kNumSamples, 48000.0f);
    drc.ProcessBlock(input, &output);
    EXPECT_THAT(output, EigenArrayNear(0 * input, 1e-4));
  }
}

// Tests that the gain gets applied.
TEST(DynamicRangeControl, CompressesSignalTest) {
  constexpr int kNumChannels = 2;
  constexpr int kNumSamples = 4;
  constexpr float kInputGainDb = 10;
  Eigen::ArrayXXf input = Eigen::ArrayXXf::Constant(
      kNumChannels, kNumSamples, DecibelsToAmplitudeRatio(kInputGainDb));
  Eigen::ArrayXXf output(kNumChannels, kNumSamples);
  {  // Input is smaller than output because it is above threshold.
    DynamicRangeControlParams params;
    params.envelope_type = kPeak;
    params.threshold_db = 0;
    params.ratio = 5;
    params.attack_s = 1e-8;  // Effectively removes the smoother.
    DynamicRangeControl drc(params);
    drc.Init(kNumChannels, kNumSamples, 48000.0f);
    drc.ProcessBlock(input, &output);

    float expected_db = kInputGainDb / params.ratio;
    EXPECT_FLOAT_EQ(OutputLevelCompressor(
        kInputGainDb, params.threshold_db, params.ratio, params.knee_width_db),
                    expected_db);
    Eigen::ArrayXXf expected = Eigen::ArrayXXf::Constant(
        kNumChannels, kNumSamples, DecibelsToAmplitudeRatio(expected_db));
    EXPECT_THAT(output, EigenArrayNear(expected, 1e-4));
  }
  {  // Input is smaller than output because it is well above threshold.
    DynamicRangeControlParams params;
    params.envelope_type = kPeak;
    params.threshold_db = 0;
    params.ratio = 4;
    params.attack_s = 1e-8;  // Effectively removes the smoother.
    DynamicRangeControl drc(params);
    drc.Init(kNumChannels, kNumSamples, 48000.0f);
    drc.ProcessBlock(input, &output);

    float expected_db = kInputGainDb / params.ratio;
    EXPECT_FLOAT_EQ(OutputLevelCompressor(
        kInputGainDb, params.threshold_db, params.ratio, params.knee_width_db),
                    expected_db);
    Eigen::ArrayXXf expected = Eigen::ArrayXXf::Constant(
        kNumChannels, kNumSamples, DecibelsToAmplitudeRatio(expected_db));
    EXPECT_THAT(output, EigenArrayNear(expected, 1e-4));
  }
}


TEST(DynamicRangeControl, ResetTest) {
  constexpr int kNumChannels = 1;
  constexpr int kNumSamples = 400;

  Eigen::ArrayXXf input = Eigen::ArrayXXf::Random(kNumChannels, kNumSamples);
  Eigen::ArrayXXf output1(kNumChannels, kNumSamples);
  Eigen::ArrayXXf output2(kNumChannels, kNumSamples);

  DynamicRangeControlParams params;
  DynamicRangeControl drc(params);
  drc.Init(kNumChannels, kNumSamples, 48000.0f);
  drc.ProcessBlock(input, &output1);
  drc.Reset();
  drc.ProcessBlock(input, &output2);

  EXPECT_THAT(output1, EigenArrayNear(output2, 1e-6));
}

TEST(DynamicRangeControl, InPlaceTest) {
  constexpr int kNumChannels = 1;
  constexpr int kNumSamples = 400;

  Eigen::ArrayXXf input = Eigen::ArrayXXf::Random(kNumChannels, kNumSamples);
  Eigen::ArrayXXf output(kNumChannels, kNumSamples);

  DynamicRangeControlParams params;
  DynamicRangeControl drc(params);
  drc.Init(kNumChannels, kNumSamples, 48000.0f);
  drc.ProcessBlock(input, &output);
  drc.Reset();
  drc.ProcessBlock(input, &input);

  EXPECT_THAT(output, EigenArrayNear(input, 1e-6));
}

TEST(DynamicRangeControl, BlockSizeTest) {
  constexpr int kNumChannels = 1;
  constexpr int kNumSamples = 400;

  Eigen::ArrayXXf input = Eigen::ArrayXXf::Random(kNumChannels, kNumSamples);
  Eigen::ArrayXXf output_1(kNumChannels, kNumSamples);
  Eigen::ArrayXXf output_2(kNumChannels, kNumSamples);

  DynamicRangeControlParams params;
  DynamicRangeControl drc(params);
  drc.Init(kNumChannels, kNumSamples, 48000.0f);
  drc.ProcessBlock(input, &output_1);
  drc.Init(kNumChannels, kNumSamples * 2, 48000.0f);
  drc.ProcessBlock(input, &output_2);

  EXPECT_THAT(output_2, EigenArrayNear(output_1, 1e-6));
}

void BM_Compressor(benchmark::State& state) {
  constexpr int kNumSamples = 1000;
  Eigen::ArrayXf input = Eigen::ArrayXf::Random(kNumSamples);
  Eigen::ArrayXf output(kNumSamples);
  DynamicRangeControl drc(
      DynamicRangeControlParams::ReasonableCompressorParams());
  drc.Init(2, kNumSamples, 48000.0f);

  while (state.KeepRunning()) {
    drc.ProcessBlock(input, &output);
    benchmark::DoNotOptimize(output);
  }
  state.SetItemsProcessed(kNumSamples * state.iterations());
}
BENCHMARK(BM_Compressor);

}  // namespace
}  // namespace audio_dsp
