#pragma once

#include <optional>
#include "AudioFile.h"

#include "log.h"

template <typename T = float>
void write_wav(const std::string& filename,
               const typename std::vector<float>& samples,
               int sample_rate) {
  if (samples.size() == 0) {
    LOG_ERROR("Error writing to wav file {}, sample count is 0.", filename);
    return;
  }
  AudioFile<T> audio_file;
  audio_file.setAudioBufferSize(1, samples.size());
  audio_file.setSampleRate(sample_rate);
  audio_file.setBitDepth(32);
  audio_file.samples[0] = samples;
  audio_file.save(filename, AudioFileFormat::Wave);
  LOG_INFO("Wrote {} samples to {}", samples.size(), filename);
}

template <typename T = float>
typename std::optional<typename AudioFile<T>::AudioBuffer> read_wav(
    const std::string& filename) {
  AudioFile<float> audioFile;
  bool loaded = audioFile.load(filename);
  if (!loaded) {
    LOG_ERROR("Failed to load audio file.");
    return std::nullopt;
  }
  LOG_INFO("Read audio file: {}", filename);
  LOG_INFO("Bit Depth: {}", audioFile.getBitDepth());
  LOG_INFO("Sample Rate: {}", audioFile.getSampleRate());
  LOG_INFO("Channels: {}", audioFile.getNumChannels());
  LOG_INFO("Samples: {}", audioFile.getNumSamplesPerChannel());
  LOG_INFO("Length in Seconds: {}", audioFile.getLengthInSeconds());
  return audioFile.samples;
}

namespace SuperSonic {

// vector
using Bits = std::vector<uint8_t>;
using Bytes = std::vector<uint8_t>;
using Samples = std::vector<float>;
// span
using BitView = std::span<const uint8_t>;
using ByteView = std::span<const uint8_t>;
using SampleView = std::span<const float>;
// mut span
using MutBitView = std::span<uint8_t>;
using MutByteView = std::span<uint8_t>;
using MutSampleView = std::span<float>;

namespace Signal {

// np.concatenate(a, ...)
template <typename V, typename... Args>
constexpr auto concatenate(V a, Args... args) {
  using T = typename V::value_type;
  std::vector<T> result;
  result.reserve(a.size() + (... + args.size()));
  result.insert(result.end(), a.begin(), a.end());
  (result.insert(result.end(), args.begin(), args.end()), ...);
  return result;
}

// np.flip(a)
template <typename V>
constexpr auto flip(V a) {
  using T = typename V::value_type;
  std::vector<T> result;
  result.reserve(a.size());
  result.insert(result.end(), a.rbegin(), a.rend());
  return result;
}

// np.zeros(n)
template <typename T=float>
constexpr std::vector<T> zeros(int n) {
  return std::vector<T>(n, 0);
}

// np.linspace(start, stop, num)
// does not include stop
inline constexpr auto linspace(float start, float stop, int num) {
  std::vector<float> result;
  result.reserve(num);
  float step = (stop - start) / (num);
  for (int i = 0; i < num; i++) {
    result.push_back(start + i * step);
  }
  return result;
}

template <typename V1, typename V2>
auto dot(V1 a, V2 b) {
  using T = typename V1::value_type;
  if (a.size() != b.size()) {
    LOG_ERROR("Size mismatch in dot product: {} vs {}", a.size(), b.size());
    return static_cast<T>(0);
  }
  T result = 0;
  for (size_t i = 0; i < a.size(); i++) {
    result += a[i] * b[i];
  }
  return result;
}

// np.abs(a)
template <typename V>
auto absv(V a) {
  using T = typename V::value_type;
  std::vector<T> result;
  result.reserve(a.size());
  for (size_t i = 0; i < a.size(); i++) {
    result.push_back(std::abs(a[i]));
  }
  return result;
}

// np.max(a)
template <typename V>
auto maxv(V a) {
  using T = typename V::value_type;
  T result = a[0];
  for (size_t i = 1; i < a.size(); i++) {
    result = std::max(result, a[i]);
  }
  return result;
}

// np.argmax(a)
template <typename V>
auto argmax(V a) {
  if (a.size() == 0) {
    LOG_ERROR("argmax on empty vector");
    return static_cast<size_t>(0);
  }
  size_t result = 0;
  for (size_t i = 1; i < a.size(); i++) {
    if (a[i] > a[result]) {
      result = i;
    }
  }
  return result;
}

template <typename V>
auto scale(V a, float s) {
  using T = typename V::value_type;
  std::vector<T> result;
  result.reserve(a.size());
  for (size_t i = 0; i < a.size(); i++) {
    result.push_back(a[i] * s);
  }
  return result;
}

template <typename V>
auto sine_wave(float freq, V t, float phase = 0) {
  using T = typename V::value_type;
  std::vector<T> result;
  result.reserve(t.size());
  for (size_t i = 0; i < t.size(); i++) {
    result.push_back(std::sin(2 * M_PI * freq * t[i] + phase));
  }
  return result;
}

}  // namespace Signal

}  // namespace SuperSonic