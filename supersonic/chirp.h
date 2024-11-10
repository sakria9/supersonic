#pragma once

#include <numbers>

#include "magic.h"
#include "utils.h"

namespace SuperSonic {

namespace Signal {

inline std::vector<float> generate_chirp(float f0,
                                                   float c,
                                                   float duration) {
  // auto f1 = c * duration + f0;

  auto phi = [f0, c](float t) {
    return 2 * std::numbers::pi * (c / 2 * t * t + f0 * t);
  };

  auto t = linspace(0, duration, int(kSampleRate * duration));
  auto chirp = zeros(t.size());
  for (size_t i = 0; i < t.size(); i++) {
    chirp[i] = static_cast<float>(sin(phi(t[i])));
  }

  auto chirp_rev = scale(flip(chirp), -1);
  chirp = concatenate(chirp, chirp_rev);

  chirp = concatenate(chirp, zeros(6));
  return chirp;
}

static constexpr size_t CHIRP1_LEN = 96 + 6;
inline auto generate_chirp1() {
  return generate_chirp(5000, 5000000, 0.001f);
}

}  // namespace Signal

}  // namespace SuperSonic