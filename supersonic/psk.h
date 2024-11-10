#pragma once

#include "utils.h"

namespace SuperSonic {

class PSK {
 public:
  // static constexpr size_t symbol_len = 2;
  // static constexpr std::array<int, symbol_len> one{1, -1};
  // static constexpr std::array<int, symbol_len> zero{-1, 1};

  static constexpr size_t symbol_len = 4;
  static constexpr std::array<int, symbol_len> one{1, -1, 1, -1};
  static constexpr std::array<int, symbol_len> zero{-1, 1, -1, 1};

  static inline Samples modulate(Bits raw_bits) {
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

  static inline Bits demodulate(SampleView wave) {
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
};

}  // namespace SuperSonic