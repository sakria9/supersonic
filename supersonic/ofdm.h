#pragma once

#include <kiss_fft.h>

#include "supersonic.h"
#include "utils.h"

namespace SuperSonic {

// static constexpr float symbol_freq = 1000;
// static constexpr std::array<int, 2> channels = {3, 7};
// static constexpr int opt.symbol_bits = 1;

// static const float symbol_freq = 1000;
// static const std::vector<int> channels = {3, 7};
// static const int opt.symbol_bits = 1;

// static constexpr float symbol_freq = 4000;
// static constexpr std::array<int, 2> channels = {1, 2};
// static constexpr int opt.symbol_bits = 1;

class OFDM {
 public:
  const Config::OFDMOption opt;

  OFDM(Config::OFDMOption opt) : opt(opt) {}

  inline Samples modulate(Bits bits) {
    if (bits.size() % opt.channels.size() != 0) {
      LOG_ERROR("Invalid bits size: {}", bits.size());
      return {};
    }

    auto cfg = kiss_fft_alloc(opt.real_symbol_samples, 1, 0, 0);
    std::vector<kiss_fft_cpx> in(opt.real_symbol_samples),
        out(opt.real_symbol_samples);

    Samples wave;
    wave.resize(bits.size() / opt.channels.size() * opt.symbol_samples);
    for (size_t i = 0; i < bits.size(); i += opt.channels.size()) {
      auto cur_bits = BitView(bits).subspan(i, opt.channels.size());

      // reset in
      for (int j = 0; j < opt.real_symbol_samples; j++) {
        in[j].r = 0;
        in[j].i = 0;
      }

      float A = 0.5 / opt.channels.size();
      for (size_t j = 0; j < opt.channels.size(); j++) {
        auto channel = opt.channels[j];
        if (cur_bits[j]) {
          // one => sin wave
          in[channel].i = -A;
          in[opt.real_symbol_samples - channel].i = A;
        } else {
          // zero => cos wave
          in[channel].r = A;
          in[opt.real_symbol_samples - channel].r = A;
        }
      }

      kiss_fft(cfg, in.data(), out.data());

      auto cur_wave = MutSampleView(wave).subspan(
          i / opt.channels.size() * opt.symbol_samples, opt.symbol_samples);
      auto core_wave =
          cur_wave.subspan(opt.cp_samples, opt.real_symbol_samples);
      for (int j = 0; j < opt.real_symbol_samples; j++) {
        core_wave[j] = out[j].r;
      }
      std::copy(core_wave.end() - opt.cp_samples, core_wave.end(),
                cur_wave.begin());
    }
    return wave;
  }

  inline Bits demodulate(SampleView wave) {
    if (wave.size() % opt.symbol_samples != 0) {
      LOG_ERROR("Invalid wave size: {}", wave.size());
      return {};
    }

    auto cfg = kiss_fft_alloc(opt.real_symbol_samples, 0, 0, 0);
    std::vector<kiss_fft_cpx> in(opt.real_symbol_samples),
        out(opt.real_symbol_samples);

    Bits bits;
    bits.reserve(wave.size() / opt.symbol_samples * opt.channels.size());
    for (size_t i = 0; i < wave.size(); i += opt.symbol_samples) {
      SampleView symbol(wave.begin() + i + opt.cp_samples,
                        opt.real_symbol_samples);
      for (int j = 0; j < opt.real_symbol_samples; j++) {
        in[j].r = symbol[j];
        in[j].i = 0;
      }

      kiss_fft(cfg, in.data(), out.data());

      for (size_t j = 0; j < opt.channels.size(); j++) {
        auto channel = opt.channels[j];
        auto one = fabs(out[channel].i);
        auto zero = fabs(out[channel].r);
        bits.push_back(one > zero);
      }
    }

    free(cfg);

    return bits;
  }
};

}  // namespace SuperSonic