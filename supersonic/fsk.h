#pragma once

#include <kiss_fft.h>

#include "supersonic.h"
#include "utils.h"

namespace SuperSonic {
namespace Signal {

struct FSKOption {
  float freq;
  int symbol_samples;
  float symbol_time;
  constexpr FSKOption(float freq)
      : freq(freq),
        symbol_samples(kSampleRate / freq),
        symbol_time(1.0 / freq) {}
};

inline Samples fsk_modulate(BitView bits, FSKOption opt) {
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

inline Bits fsk_demodulate(SampleView wave, FSKOption opt) {
  if (wave.size() % opt.symbol_samples != 0) {
    LOG_ERROR("Invalid wave size: {}", wave.size());
    return {};
  }

  auto t = linspace(0, opt.symbol_time, opt.symbol_samples);

  auto cfg = kiss_fft_alloc(opt.symbol_samples, 0, 0, 0);
  kiss_fft_cpx in[opt.symbol_samples], out[opt.symbol_samples];

  Bits bits;
  bits.reserve(wave.size() / opt.symbol_samples);
  for (size_t i = 0; i < wave.size(); i += opt.symbol_samples) {
    SampleView symbol(wave.begin() + i, opt.symbol_samples);
    for (size_t j = 0; j < opt.symbol_samples; j++) {
      in[j].r = symbol[j];
      in[j].i = 0;
    }

    kiss_fft(cfg, in, out);

    float f1_mag = std::sqrt(out[1].r * out[1].r + out[1].i * out[1].i);
    float f2_mag = std::sqrt(out[2].r * out[2].r + out[2].i * out[2].i);
    bits.push_back(f1_mag > f2_mag);
  }

  free(cfg);

  return bits;
}

}  // namespace Signal
}  // namespace SuperSonic