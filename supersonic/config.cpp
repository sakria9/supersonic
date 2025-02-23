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
    auto to_string = [](const boost::json::value& v) {
      return std::string(v.as_string());
    };
    auto to_int = [](const boost::json::value& v) { return v.as_int64(); };
    auto to_float = [](const boost::json::value& v) {
      return static_cast<float>(v.as_double());
    };

    // Saudio
    SaudioOption saudio_opt;

    auto saudio_option = j.at("saudio_option").as_object();

    auto input_port =
        value_opt(saudio_option, "input_port").transform(to_string);
    auto output_port =
        value_opt(saudio_option, "output_port").transform(to_string);
    if (input_port) {
      saudio_opt.input_port = *input_port;
    }
    if (output_port) {
      saudio_opt.output_port = *output_port;
    }

    auto input_idx = value_opt(saudio_option, "input_idx").transform(to_int);
    auto output_idx = value_opt(saudio_option, "output_idx").transform(to_int);
    if (input_idx) {
      saudio_opt.input_port = int(*input_idx);
    }
    if (output_idx) {
      saudio_opt.output_port = int(*output_idx);
    }

    auto ringbuffer_size =
        value_opt(saudio_option, "ringbuffer_size").transform(to_int);

    if (ringbuffer_size) {
      saudio_opt.ringbuffer_size = *ringbuffer_size;
    }

    // OFDM
    auto ofdm_opt = [&]() {
      if (!j.as_object().contains("ofdm_option")) {
        return OFDMOption();
      }
      auto ofdm_option = j.at("ofdm_option").as_object();
      auto symbol_freq =
          value_opt(ofdm_option, "symbol_freq").transform(to_int);
      auto channels = value_opt(ofdm_option, "channels")
                          .transform([](const boost::json::value& v) {
                            std::vector<int> result;
                            for (const auto& e : v.as_array()) {
                              result.push_back((int)e.as_int64());
                            }
                            return result;
                          });
      auto cp_samples = value_opt(ofdm_option, "cp_samples").transform(to_int);
      if (symbol_freq || channels || cp_samples) {
        if (!(symbol_freq && channels && cp_samples)) {
          throw std::runtime_error(
              "symbol_freq, channels, "
              "cp_samples must be specified together");
        }
        return OFDMOption((float)*symbol_freq, *channels, (int)*cp_samples);
      } else {
        return OFDMOption();
      }
    }();

    // Sphy
    auto sphy_opt = [&]() {
      auto sphy_option = j.at("sphy_option").as_object();
      auto bin_payload_size =
          value_opt(sphy_option, "bin_payload_size").transform(to_int);
      auto frame_gap_size =
          value_opt(sphy_option, "frame_gap_size").transform(to_int);
      auto magic_factor =
          value_opt(sphy_option, "magic_factor").transform(to_float);
      auto preamble_threshold =
          value_opt(sphy_option, "preamble_threshold").transform(to_float);
      auto max_payload_size =
          value_opt(sphy_option, "max_payload_size").transform(to_int);
      if (bin_payload_size || frame_gap_size) {
        return SphyOption(saudio_opt, *bin_payload_size, *frame_gap_size,
                          *magic_factor, *preamble_threshold, *max_payload_size,
                          ofdm_opt);
      } else {
        return SphyOption(saudio_opt);
      }
    }();

    // Smac
    auto smac_opt = [&]() -> SmacOption {
      SmacOption result;
      if (j.as_object().contains("smac_option")) {
        auto smac_option = j.at("smac_option").as_object();
        auto mac_addr = value_opt(smac_option, "mac_addr").transform(to_int);
        if (mac_addr) {
          result.mac_addr = static_cast<uint8_t>(*mac_addr);
        } else {
          throw std::runtime_error("mac_addr must be specified");
        }
        auto timeout_ms = value_opt(smac_option, "timeout_ms")
                              .transform(to_int)
                              .value_or(200);
        auto max_retries = value_opt(smac_option, "max_retries")
                               .transform(to_int)
                               .value_or(20);
        auto busy_power_threshold =
            value_opt(smac_option, "busy_power_threshold")
                .transform(to_float)
                .value_or(1e-4);
        auto backoff_ms = value_opt(smac_option, "backoff_ms")
                              .transform(to_int)
                              .value_or(100);
        auto max_backoff_ms = value_opt(smac_option, "max_backoff_ms")
                                  .transform(to_int)
                                  .value_or(1000);
        result.timeout_ms = timeout_ms;
        result.max_retries = max_retries;
        result.busy_power_threshold = busy_power_threshold;
        result.backoff_ms = backoff_ms;
        result.max_backoff_ms = max_backoff_ms;
        return result;
      } else {
        return {};
      }
    }();

    // TUN
    auto tun_opt = [&]() -> TunOption {
      if (j.as_object().contains("tun_option")) {
        auto tun_option = j.at("tun_option").as_object();
        auto ip_suffix = value_opt(tun_option, "ip_suffix").transform(to_int);

        TunOption r;
        if (ip_suffix) {
          r.ip_suffix = static_cast<uint8_t>(*ip_suffix);
        } else {
          throw std::runtime_error("ip_suffix must be specified");
        }

        if (tun_option.contains("allow_ips")) {
          auto allow_ips = tun_option.at("allow_ips").as_array();
          for (const auto& e : allow_ips) {
            std::string ip_str{e.as_string()};
            // Parse IP address
            auto parseIpToUint32 = [](const std::string& ip) -> uint32_t {
              uint32_t result = 0;
              int shift = 24;  // Start with the most significant byte
              std::istringstream ss(ip);
              std::string token;

              for (int i = 0; i < 4; ++i) {
                if (!std::getline(ss, token, '.')) {
                  throw std::invalid_argument("Invalid IP address format");
                }

                int octet = std::stoi(token);
                if (octet < 0 || octet > 255) {
                  throw std::out_of_range("IP address octet out of range");
                }

                result |= static_cast<uint32_t>(octet) << shift;
                shift -= 8;
              }

              if (ss.rdbuf()->in_avail() != 0) {
                throw std::invalid_argument("Invalid IP address format");
              }

              return result;
            };
            auto ip = parseIpToUint32(ip_str);
            LOG_INFO("Allow IP: {} {}", ip_str, ip);
            r.allow_ips.push_back(ip);
          }
        }

        return r;
      } else {
        return TunOption{.ip_suffix = 1};
      }
    }();

    // Project1
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

    // Project2
    auto project2_opt = [&]() {
      if (!j.as_object().contains("project2_option")) {
        return Project2Option{-1, 0};
      }

      auto project2_option = j.at("project2_option").as_object();
      auto task = value_opt(project2_option, "task").transform(to_int);
      auto payload_size =
          value_opt(project2_option, "payload_size").transform(to_int);
      auto bin_size = value_opt(project2_option, "bin_size").transform(to_int);
      if (task && payload_size && bin_size) {
        return Project2Option{int(*task), static_cast<size_t>(*payload_size),
                              static_cast<size_t>(*bin_size)};
      } else {
        throw std::runtime_error("payload_size must be specified");
      }
    }();

    return {
        .sphy_option = sphy_opt,
        .smac_option = smac_opt,
        .tun_option = tun_opt,
        .project1_option = project1_opt,
        .project2_option = project2_opt,
    };
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to load option: {}", e.what());
    throw;
  }
}

}  // namespace Config

}  // namespace SuperSonic