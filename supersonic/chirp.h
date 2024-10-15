#pragma once

#include "supersonic.h"
#include "utils.h"

namespace SuperSonic {

namespace Signal {

inline constexpr std::vector<float> generate_chirp(float f0,
                                                   float c,
                                                   float duration) {
  auto f1 = c * duration + f0;

  auto phi = [f0, c](float t) { return 2 * M_PI * (c / 2 * t * t + f0 * t); };

  auto t = linspace(0, duration, int(kSampleRate * duration));
  auto chirp = zeros(t.size());
  for (int i = 0; i < t.size(); i++) {
    chirp[i] = sin(phi(t[i]));
  }

  auto chirp_rev = scale(flip(chirp), -1);
  chirp = concatenate(chirp, chirp_rev);

  chirp = concatenate(chirp, zeros(48));
  return chirp;
}

inline auto generate_chirp1() {
  return generate_chirp(5000, 5000000, 0.001);
}

}  // namespace Signal

}  // namespace SuperSonic