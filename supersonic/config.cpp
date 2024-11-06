#include <boost/json/src.hpp>

#include "chirp.h"
#include "config.h"
#include "log.h"
#include "magic.h"

namespace SuperSonic {

namespace Config {

Option load_option(std::string filename) {
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
    auto to_float = [](const boost::json::value& v) {
      return static_cast<float>(v.as_double());
    };

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
                              result.push_back((int)e.as_int64());
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
        return OFDMOption((float)*symbol_freq, (int)*symbol_bits, *channels,
                          (int)*shift_samples);
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
      auto magic_factor =
          value_opt(sphy_option, "magic_factor").transform(to_float);
      if (bin_payload_size || frame_gap_size) {
        return SphyOption(saudio_opt, ofdm_opt, *bin_payload_size,
                          *frame_gap_size, *magic_factor);
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