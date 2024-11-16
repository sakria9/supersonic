#include <boost/asio/experimental/awaitable_operators.hpp>
#include <cxxopts.hpp>

#include "mac.h"
#include "phy.h"
#include "utils.h"

awaitable<void> expiry(int seconds) {
  steady_timer timer(co_await this_coro::executor);
  timer.expires_after(std::chrono::seconds(seconds));
  co_await timer.async_wait(use_awaitable);
}

awaitable<void> async_main(boost::asio::io_context& ctx,
                           SuperSonic::Sphy& phy,
                           SuperSonic::Smac& mac,
                           SuperSonic::Config::Option& option) {
  co_await phy.init();

  auto ex = co_await this_coro::executor;
  co_spawn(ex, mac.run(), detached);

  if (option.project2_option.task == 1) {
    auto start_ts = std::chrono::high_resolution_clock::now();
    constexpr int rounds = 100;
    std::vector<double> frame_times;
    for (int i = 0; i < rounds; i++) {
      SuperSonic::Bits bits(option.project2_option.payload_size);
      LOG_INFO("Kicked frame {}", i);

      auto start = std::chrono::high_resolution_clock::now();
      co_await mac.tx(bits);
      auto end = std::chrono::high_resolution_clock::now();
      frame_times.push_back(
          std::chrono::duration_cast<std::chrono::microseconds>(end - start)
              .count() *
          1e-6);
    }
    auto end_ts = std::chrono::high_resolution_clock::now();
    auto sec_float =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_ts - start_ts)
            .count() /
        1000.0;
    LOG_INFO("Sent {} frames in {} s, {} bps", rounds, sec_float,
             (rounds * option.project2_option.payload_size) / sec_float);

    // print frame times in one line using fmt
    LOG_INFO("Frame times: {}", fmt::join(frame_times, " "));
  } else if (option.project2_option.task == 2) {
    LOG_INFO("RX only mode");
  }
}

int main(int argc, char** argv) {
  cxxopts::Options options("supersonic", "Supersonic Project 2");
  // clang-format off
  options.add_options()
    ("h,help", "Print usage")
    ("c,config", "Config file", cxxopts::value<std::string>())
  ;
  // clang-format on
  auto result = options.parse(argc, argv);

  if (result.count("help") || result.count("config") == 0) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  auto config_file = result["config"].as<std::string>();
  auto option = SuperSonic::Config::load_option(config_file);

  SuperSonic::Sphy phy(option.sphy_option);
  SuperSonic::Smac mac(option.smac_option, phy);

  try {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    co_spawn(io_context, async_main(io_context, phy, mac, option), detached);

    io_context.run();
  } catch (std::exception& e) {
    std::printf("Exception: %s\n", e.what());
  }

  return 0;
}
