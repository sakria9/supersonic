#pragma once

#include <fmt/ranges.h>
#include <variant>

#include "chirp.h"
#include "log.h"
#include "magic.h"

// #include "psk.h"

namespace SuperSonic {

namespace Config {

struct SaudioOption {
  std::string client_name = "supersonic";

  std::variant<std::string, int> input_port = "system:capture_1";
  std::variant<std::string, int> output_port = "system:playback_1";

  size_t ringbuffer_size = kSampleRate * 5;

  bool enable_raw_log = true;
};

struct OFDMOption {
  const float symbol_freq;
  const std::vector<int> channels;

  const int real_symbol_samples;
  const int cp_samples;
  const int symbol_samples;
  const float symbol_time;

  OFDMOption(float symbol_freq, std::vector<int> channels, int cp_samples)
      : symbol_freq(symbol_freq),
        channels(channels),
        real_symbol_samples(int(kSampleRate / symbol_freq)),
        cp_samples(cp_samples),
        symbol_samples(cp_samples + real_symbol_samples),
        symbol_time(1.0f / symbol_freq) {
    LOG_INFO(
        "OFDMOption: symbol_freq={}, channels={}, symbol_samples={}, "
        "cp_samples={}",
        symbol_freq, fmt::join(channels, ", "), symbol_samples, cp_samples);
  }
  OFDMOption()
      : OFDMOption(1000, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, 12) {}

  size_t phy_payload_size(size_t bin_payload_size) const {
    return (bin_payload_size + channels.size() - 1) / channels.size() *
           symbol_samples;
  }
};

struct SphyOption {
  SaudioOption saudio_option;
  const size_t bin_payload_size;
  const size_t frame_gap_size;
  const float magic_factor;
  const float preamble_threshold;
  const size_t max_payload_size;
  const OFDMOption ofdm_option;

  SphyOption(SaudioOption saudio_option,
             size_t bin_payload_size = 40,
             size_t frame_gap_size = 48,
             float magic_factor = -1.0f,
             float preamble_threshold = 0.1f,
             size_t max_payload_size = 2048,
             OFDMOption ofdm_option = OFDMOption())
      : saudio_option(saudio_option),
        bin_payload_size(bin_payload_size),
        frame_gap_size(frame_gap_size),
        magic_factor(magic_factor),
        preamble_threshold(preamble_threshold),
        max_payload_size(max_payload_size),
        ofdm_option(ofdm_option) {}
};

struct SmacOption {
  uint8_t mac_addr;
  int timeout_ms;
  int backoff_ms;
  int max_backoff_ms;
  int max_retries;
  float busy_power_threshold;
};

struct Project1Option {
  size_t payload_size;
};

struct Project2Option {
  int task;
  size_t payload_size;
  size_t bin_size;
};


struct Option {
  SphyOption sphy_option;
  SmacOption smac_option;
  Project1Option project1_option;
  Project2Option project2_option;
};

Option load_option(std::string filename);

}  // namespace Config

}  // namespace SuperSonic