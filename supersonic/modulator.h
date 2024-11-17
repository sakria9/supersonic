#pragma once
#include "utils.h"

namespace SuperSonic {
class Modulator {
 public:
  virtual Samples modulate(Bits raw_bits) = 0;
  virtual Bits demodulate(SampleView wave) = 0;
  virtual size_t phy_payload_size(size_t bin_payload_size) const = 0;
  virtual size_t symbol_samples() const = 0;
  virtual size_t bits_per_symbol() const = 0;
};
}  // namespace SuperSonic