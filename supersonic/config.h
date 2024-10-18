#include <fmt/ranges.h>
#include <boost/json/src.hpp>

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
        real_symbol_samples(kSampleRate / symbol_freq),
        shift_samples(shift_samples),
        symbol_samples(shift_samples + real_symbol_samples + shift_samples),
        symbol_time(1.0 / symbol_freq) {
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

  SphyOption(SaudioOption saudio_option,
             OFDMOption ofdm_option,
             size_t bin_payload_size = 40,
             size_t frame_gap_size = 48)
      : saudio_option(saudio_option),
        ofdm_option(ofdm_option),
        bin_payload_size(bin_payload_size),
        phy_payload_size(bin_payload_size * ofdm_option.symbol_samples),
        frame_gap_size(frame_gap_size),
        frame_size(Signal::CHIRP1_LEN + phy_payload_size + frame_gap_size) {
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

inline Option load_option(std::string filename) {
  try {
    std::ifstream file(filename);
    if (!file.is_open()) {
      LOG_ERROR("Failed to open file: {}", filename);
      throw std::runtime_error("Failed to open file");
    }
    boost::json::value j;
    file >> j;

    auto value_opt =
        [](const boost::json::object& j,
           const std::string& key) -> std::optional<boost::json::value> {
      if (j.contains(key)) {
        return j.at(key);
      } else {
        return std::nullopt;
      }
    };
    auto to_string = [](const boost::json::value& v) { return v.as_string(); };
    auto to_int = [](const boost::json::value& v) { return v.as_int64(); };

    SaudioOption saudio_opt;

    auto saudio_option = j.at("saudio_option").as_object();
    auto input_port =
        value_opt(saudio_option, "input_port").transform(to_string);
    auto output_port =
        value_opt(saudio_option, "output_port").transform(to_string);
    auto ringbuffer_size =
        value_opt(saudio_option, "ringbuffer_size").transform(to_int);
    if (input_port) {
      saudio_opt.input_port = *input_port;
    }
    if (output_port) {
      saudio_opt.output_port = *output_port;
    }
    if (ringbuffer_size) {
      saudio_opt.ringbuffer_size = *ringbuffer_size;
    }

    auto ofdm_opt = [&]() {
      auto ofdm_option = j.at("ofdm_option").as_object();
      auto symbol_freq =
          value_opt(ofdm_option, "symbol_freq").transform(to_int);
      auto symbol_bits =
          value_opt(ofdm_option, "symbol_bits").transform(to_int);
      auto channels = value_opt(ofdm_option, "channels")
                          .transform([](const boost::json::value& v) {
                            std::vector<int> result;
                            for (const auto& e : v.as_array()) {
                              result.push_back(e.as_int64());
                            }
                            return result;
                          });
      auto shift_samples =
          value_opt(ofdm_option, "shift_samples").transform(to_int);
      if (symbol_freq || symbol_bits || channels || shift_samples) {
        if (!(symbol_bits && channels && shift_samples)) {
          throw std::runtime_error(
              "symbol_freq, symbol_bits, channels, "
              "shift_samples must be specified together");
        }
        return OFDMOption(*symbol_freq, *symbol_bits, *channels,
                          *shift_samples);
      } else {
        return OFDMOption();
      }
    }();

    auto sphy_opt = [&]() {
      auto sphy_option = j.at("sphy_option").as_object();
      auto bin_payload_size =
          value_opt(sphy_option, "bin_payload_size").transform(to_int);
      auto frame_gap_size =

          value_opt(sphy_option, "frame_gap_size").transform(to_int);
      if (bin_payload_size || frame_gap_size) {
        return SphyOption(saudio_opt, ofdm_opt, *bin_payload_size,
                          *frame_gap_size);
      } else {
        return SphyOption(saudio_opt, ofdm_opt);
      }
    }();

    auto project1_opt = [&]() {
      auto project1_option = j.at("project1_option").as_object();
      auto payload_size =
          value_opt(project1_option, "payload_size").transform(to_int);
      if (payload_size) {
        return Project1Option{static_cast<size_t>(*payload_size)};
      } else {
        throw std::runtime_error("payload_size must be specified");
      }
    }();

    return {
        .sphy_option = sphy_opt,
        .project1_option = project1_opt,
    };
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to load option: {}", e.what());
    throw;
  }
}

}  // namespace Config

}  // namespace SuperSonic