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
  static constexpr int REORDER_BITS = false;

 public:
  const Config::OFDMOption opt;

  OFDM(Config::OFDMOption opt) : opt(opt) {}

  inline Samples modulate(Bits raw_bits) {
    if (raw_bits.size() % opt.symbol_bits != 0) {
      LOG_ERROR("Invalid bits size: {}", raw_bits.size());
      return {};
    }

    Bits bits;
    if (REORDER_BITS) {
      bits = reorderBits(raw_bits);
    } else {
      bits = std::move(raw_bits);
    }

    auto cfg = kiss_fft_alloc(opt.real_symbol_samples, 1, 0, 0);
    std::vector<kiss_fft_cpx> in(opt.real_symbol_samples),
        out(opt.real_symbol_samples);

    Samples wave;
    wave.resize(bits.size() / opt.symbol_bits * opt.symbol_samples);
    for (size_t i = 0; i < bits.size(); i += opt.symbol_bits) {
      auto channel_idx = [&]() -> int {
        if (opt.symbol_bits == 1) {
          return bits[i];
        } else if (opt.symbol_bits == 2) {
          return bits[i] * 2 + bits[i + 1];
        } else if (opt.symbol_bits == 3) {
          return bits[i] * 4 + bits[i + 1] * 2 + bits[i + 2];
        } else {
          throw std::runtime_error("symbol_bits must be 1, 2, or 3");
        }
      }();

      auto channel = opt.channels[channel_idx];
      // reset in
      for (int j = 0; j < opt.real_symbol_samples; j++) {
        in[j].r = 0;
        in[j].i = 0;
      }
      in[channel].r = 0;
      // in[channel].i = -channel_mags[channel_idx] * 0.5;
      in[channel].i = -0.5;
      in[opt.real_symbol_samples - channel].r = 0;
      // in[real_symbol_samples - channel].i = channel_mags[channel_idx] * 0.5;
      in[opt.real_symbol_samples - channel].i = 0.5;

      kiss_fft(cfg, in.data(), out.data());

      auto cur_wave = MutSampleView(wave).subspan(
          i / opt.symbol_bits * opt.symbol_samples, opt.symbol_samples);
      auto core_wave =
          cur_wave.subspan(opt.shift_samples, opt.real_symbol_samples);
      for (int j = 0; j < opt.real_symbol_samples; j++) {
        core_wave[j] = out[j].r;
      }
      std::copy(core_wave.end() - opt.shift_samples, core_wave.end(),
                cur_wave.begin());
      std::copy(core_wave.begin(), core_wave.begin() + opt.shift_samples,
                cur_wave.end() - opt.shift_samples);
    }
    return wave;
#undef opt
  }

  inline Bits demodulate(SampleView wave) {
    if (wave.size() % opt.real_symbol_samples != 0) {
      LOG_ERROR("Invalid wave size: {}", wave.size());
      return {};
    }

    auto cfg = kiss_fft_alloc(opt.real_symbol_samples, 0, 0, 0);
    std::vector<kiss_fft_cpx> in(opt.real_symbol_samples),
        out(opt.real_symbol_samples);

    Bits bits;
    bits.reserve(wave.size() / opt.symbol_samples);
    for (size_t i = 0; i < wave.size(); i += opt.symbol_samples) {
      SampleView symbol(wave.begin() + i + opt.shift_samples,
                        opt.real_symbol_samples);
      for (int j = 0; j < opt.real_symbol_samples; j++) {
        in[j].r = symbol[j];
        in[j].i = 0;
      }

      kiss_fft(cfg, in.data(), out.data());

      std::vector<float> channel_mags(opt.channels.size());
      for (size_t j = 0; j < opt.channels.size(); j++) {
        auto channel = opt.channels[j];
        float mag = std::sqrt(out[channel].r * out[channel].r +
                              out[channel].i * out[channel].i);
        channel_mags[j] = mag;
      }

      auto max_channel_idx = Signal::argmax(channel_mags);
      if (opt.symbol_bits == 1) {
        bits.push_back(max_channel_idx % 2);
      } else if (opt.symbol_bits == 2) {
        bits.push_back(max_channel_idx / 2);
        bits.push_back(max_channel_idx % 2);
      } else if (opt.symbol_bits == 3) {
        bits.push_back(max_channel_idx / 4);
        bits.push_back(max_channel_idx / 2 % 2);
        bits.push_back(max_channel_idx % 2);
      } else {
        throw std::runtime_error("symbol_bits must be 1, 2, or 3");
      }
    }

    free(cfg);

    if (REORDER_BITS) {
      return dereorderBits(bits);
    } else {
      return bits;
    }
#undef opt
  }
};

}  // namespace SuperSonic