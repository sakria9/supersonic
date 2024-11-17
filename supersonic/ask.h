#pragma once

#include "modulator.h"
#include "utils.h"

namespace SuperSonic {

class ASK : public Modulator {
 public:
  static constexpr size_t symbol_len = 2;
  static constexpr std::array<float, symbol_len> one{0, 1};
  static constexpr std::array<float, symbol_len> zero{0, -1};

  size_t symbol_samples() const override { return symbol_len; }
  size_t bits_per_symbol() const override { return 1; }

  Samples modulate(Bits raw_bits) override {
    Samples wave(raw_bits.size() * symbol_len);
    for (size_t i = 0; i < raw_bits.size(); i++) {
      if (raw_bits[i]) {
        for (size_t j = 0; j < symbol_len; j++) {
          wave[i * symbol_len + j] = one[j];
        }
      } else {
        for (size_t j = 0; j < symbol_len; j++) {
          wave[i * symbol_len + j] = zero[j];
        }
      }
    }
    return wave;
  }

  Bits demodulate(SampleView wave) override {
    using SuperSonic::Signal::dot;
    if (wave.size() % symbol_len != 0) {
      LOG_ERROR("Invalid wave size: {}", wave.size());
      return {};
    }
    Bits bits(wave.size() / symbol_len);
    for (size_t i = 0; i < wave.size(); i += symbol_len) {
      auto symbol = wave.subspan(i, symbol_len);
      auto one_dot = dot(symbol, one);
      auto zero_dot = dot(symbol, zero);
      bits[i / symbol_len] = one_dot > zero_dot;
    }
    return bits;
  }

  size_t phy_payload_size(size_t bin_payload_size) const override {
    return bin_payload_size * symbol_len;
  }
};

}  // namespace SuperSonic