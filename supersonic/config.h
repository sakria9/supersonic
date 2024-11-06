#pragma once

#include <fmt/ranges.h>

#include "chirp.h"
#include "log.h"
#include "magic.h"

namespace SuperSonic {

namespace Config {

struct SaudioOption {
  std::string client_name = "supersonic";

  std::string input_port = "system:capture_1";
  std::string output_port = "system:playback_1";

  size_t ringbuffer_size = kSampleRate * 5;

  bool enable_raw_log = true;
};

struct OFDMOption {
  const float symbol_freq;
  const int symbol_bits;
  const std::vector<int> channels;

  const int real_symbol_samples;
  const int shift_samples;
  const int symbol_samples;
  const float symbol_time;
  OFDMOption(float symbol_freq,
             int symbol_bits,
             std::vector<int> channels,
             int shift_samples)

      : symbol_freq(symbol_freq),
        symbol_bits(symbol_bits),
        channels(channels),
        real_symbol_samples(int(kSampleRate / symbol_freq)),
        shift_samples(shift_samples),
        symbol_samples(shift_samples + real_symbol_samples + shift_samples),
        symbol_time(1.0f / symbol_freq) {
    LOG_INFO("OFDMOption: symbol_freq = {}, symbol_bits = {}, channels = {}",
             symbol_freq, symbol_bits, fmt::join(channels, ", "));

    if (symbol_bits != 1 && symbol_bits != 2 && symbol_bits != 3) {
      throw std::runtime_error("symbol_bits must be 1, 2, or 3");
    }
    if (pow(2, symbol_bits) != channels.size()) {
      throw std::runtime_error("channels size must be 2^symbol_bits");
    }
  }
  OFDMOption() : OFDMOption(1000, 1, {3, 7}, 0) {}
};

struct SphyOption {
  SaudioOption saudio_option;
  const OFDMOption ofdm_option;
  const size_t bin_payload_size;
  const size_t phy_payload_size;
  const size_t frame_gap_size;
  const size_t frame_size;
  const float magic_factor;

  SphyOption(SaudioOption saudio_option,
             OFDMOption ofdm_option,
             size_t bin_payload_size = 40,
             size_t frame_gap_size = 48,
             float magic_factor = -1.0f)
      : saudio_option(saudio_option),
        ofdm_option(ofdm_option),
        bin_payload_size(bin_payload_size),
        phy_payload_size(bin_payload_size * ofdm_option.symbol_samples),
        frame_gap_size(frame_gap_size),
        frame_size(Signal::CHIRP1_LEN + phy_payload_size + frame_gap_size),
        magic_factor(magic_factor) {
    LOG_INFO(
        "SphyOption: bin_payload_size = {}, phy_payload_size = {}, "
        "frame_gap_size = {}, frame_size = {}",
        bin_payload_size, phy_payload_size, frame_gap_size, frame_size);
    if (bin_payload_size % ofdm_option.symbol_bits != 0) {
      throw std::runtime_error(
          "bin_payload_size must be a multiple of symbol_bits");
    }
  }
};

struct Project1Option {
  size_t payload_size;
};

struct Option {
  SphyOption sphy_option;
  Project1Option project1_option;
};

Option load_option(std::string filename);

}  // namespace Config

}  // namespace SuperSonic