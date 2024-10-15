#pragma once

#include "supersonic.h"
#include "utils.h"

namespace SuperSonic {
namespace Signal {

struct FSKOption {
  float freq;
  int symbol_samples;
  float symbol_time;
  FSKOption(float freq)
      : freq(freq),
        symbol_samples(kSampleRate / freq),
        symbol_time(1.0 / freq) {}
};

inline Samples fsk_modulate(Bits bits, FSKOption opt) {
  auto t = linspace(0, opt.symbol_time, opt.symbol_samples);
  auto one = sine_wave(opt.freq, t);
  auto zero = sine_wave(opt.freq * 2, t);

  Samples wave;
  for (auto e : bits) {
    if (e) {
      wave.insert(wave.end(), one.begin(), one.end());
    } else {
      wave.insert(wave.end(), zero.begin(), zero.end());
    }
  }
  return wave;
}

inline Bits fsk_demodulate(Samples wave, FSKOption opt) {
  if (wave.size() % opt.symbol_samples != 0) {
    LOG_ERROR("Invalid wave size: {}", wave.size());
    return {};
  }

  auto t = linspace(0, opt.symbol_time, opt.symbol_samples);
  auto one = sine_wave(opt.freq, t);
  auto zero = sine_wave(opt.freq * 2, t);

  Bits bits;
  bits.reserve(wave.size() / opt.symbol_samples);
  for (size_t i = 0; i < wave.size(); i += opt.symbol_samples) {
    std::span<float> symbol(wave.begin() + i, opt.symbol_samples);
    auto corr_one = std::abs(dot(symbol, one));
    auto corr_zero = std::abs(dot(symbol, zero));
    bits.push_back(corr_one > corr_zero);
  }
  return bits;
}

}  // namespace Signal
}  // namespace SuperSonic