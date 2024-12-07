#pragma once

#include <AudioFile.h>
#include <numbers>
#include <optional>
#include <span>

#include "log.h"

template <typename T = float>
void write_wav(const std::string& filename,
               const typename std::vector<T>& samples,
               int sample_rate) {
  if (samples.size() == 0) {
    LOG_ERROR("Error writing to wav file {}, sample count is 0.", filename);
    return;
  }
  AudioFile<T> audio_file;
  audio_file.setAudioBufferSize(1, (int)samples.size());
  audio_file.setSampleRate(sample_rate);
  audio_file.setBitDepth(32);
  audio_file.samples[0] = samples;
  audio_file.save(filename, AudioFileFormat::Wave);
  LOG_INFO("Wrote {} samples to {}", samples.size(), filename);
}

template <typename T = float>
void write_txt(const std::string& filename,
               const typename std::vector<T>& samples) {
  if (samples.size() == 0) {
    LOG_ERROR("Error writing to file {}, sample count is 0.", filename);
    return;
  }
  // should be numpy readable
  // how to read in python:
  // import numpy as np
  // wave = np.loadtxt("wave.txt")
  std::ofstream ofs(filename);
  for (auto e : samples) {
    ofs << e << std::endl;
  }
  ofs.close();
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
template <typename T = float>
constexpr std::vector<T> zeros(size_t n) {
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
    result.push_back(
        static_cast<T>(std::sin(2 * std::numbers::pi * freq * t[i] + phase)));
  }
  return result;
}

}  // namespace Signal

inline int calculateBestReorder(int size) {
  int bestP = 1;
  int minDifference = size;

  for (int i = 1; i <= std::sqrt(size); i++) {
    if (size % i == 0) {
      int q = size / i;
      int difference = std::abs(i - q);
      if (difference < minDifference) {
        minDifference = difference;
        bestP = i;
      }
    }
  }

  return bestP;
}

inline Bits reorderBits(BitView bits) {
  int p = calculateBestReorder((int)bits.size());
  int q = (int)bits.size() / p;
  Bits reorderedBits(bits.size());

  for (int i = 0; i < p; i++) {
    for (int j = 0; j < q; j++) {
      reorderedBits[j * p + i] = bits[i * q + j];
    }
  }

  return reorderedBits;
}

inline Bits dereorderBits(BitView reorderedBits) {
  int p = calculateBestReorder((int)reorderedBits.size());
  int q = (int)reorderedBits.size() / p;
  Bits bits(reorderedBits.size());

  for (int i = 0; i < p; i++) {
    for (int j = 0; j < q; j++) {
      bits[i * q + j] = reorderedBits[j * p + i];
    }
  }

  return bits;
}

class Rand {
 public:
  Rand(unsigned int seed = 0) : seed(seed) { setSeed(seed); }

  void setSeed(unsigned int newSeed) {
    seed = newSeed;
    state = seed;
  }

  int nextInt(int min, int max) { return min + (generate() % (max - min + 1)); }

 private:
  unsigned int seed;
  unsigned int state;

  unsigned int generate() {
    // Simple LCG parameters
    state = (1103515245 * state + 12345) & 0x7fffffff;
    return state;
  }
};

inline std::vector<int> permutation(int n) {
  std::vector<int> result(n);
  for (int i = 0; i < n; i++) {
    result[i] = i;
  }
  Rand r(114514);
  // Fisher–Yates shuffle Algorithm to shuffle the array
  for (int i = n - 1; i > 0; i--) {
    int j = r.nextInt(0, i);
    std::swap(result[i], result[j]);
  }
  return result;
}

inline Bits uniform_reorder(BitView bits) {
  auto perm = permutation((int)bits.size());
  Bits reorderedBits(bits.size());
  for (size_t i = 0; i < bits.size(); i++) {
    reorderedBits[perm[i]] = bits[i];
  }
  return reorderedBits;
}

inline Bits uniform_dereorder(BitView reorderedBits) {
  auto perm = permutation((int)reorderedBits.size());
  Bits bits(reorderedBits.size());
  for (size_t i = 0; i < reorderedBits.size(); i++) {
    bits[i] = reorderedBits[perm[i]];
  }
  return bits;
}

inline Bits int2Bits(size_t n, int size) {
  Bits bits(size);
  for (int i = 0; i < size; i++) {
    bits[i] = static_cast<uint8_t>((n >> i) & 1);
  }
  return bits;
}
inline void int2Bits(size_t n, MutBitView bits) {
  for (int i = 0; i < bits.size(); i++) {
    bits[i] = static_cast<uint8_t>((n >> i) & 1);
  }
}
inline int bits2Int(BitView bits) {
  int n = 0;
  for (size_t i = 0; i < bits.size(); i++) {
    n |= bits[i] << i;
  }
  return n;
}

}  // namespace SuperSonic