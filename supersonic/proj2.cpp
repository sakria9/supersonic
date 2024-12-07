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

awaitable<void> async_tx(boost::asio::io_context& ctx,
                         SuperSonic::Sphy& phy,
                         SuperSonic::Smac& mac,
                         SuperSonic::Config::Option& option) {
  // read bits from input.txt
  std::ifstream ifs("input.txt");
  if (!ifs.is_open()) {
    LOG_ERROR("Failed to open input.txt");
    co_return;
  }
  SuperSonic::Bits input_bits;
  while (true) {
    char c;
    ifs >> c;
    if (ifs.eof()) {
      break;
    }
    input_bits.push_back(c - '0');
  }
  LOG_INFO("Read {} bits from input.txt", input_bits.size());
  if (input_bits.size() != option.project2_option.bin_size) {
    LOG_ERROR("Invalid bits size: {} != {}", input_bits.size(),
              option.project2_option.bin_size);
    throw std::runtime_error("Invalid bits size");
  }

  auto payload_size = option.project2_option.payload_size;

  std::vector<SuperSonic::Bits> frames;
  frames.push_back({});
  for (size_t i = 0; i < input_bits.size(); i++) {
    if (frames.back().size() == payload_size) {
      frames.push_back({});
    }
    frames.back().push_back(input_bits[i]);
  }
  size_t rounds = frames.size();

  auto start_ts = std::chrono::high_resolution_clock::now();
  std::vector<double> frame_times;
  for (size_t i = 0; i < rounds; i++) {
    LOG_INFO("Kicked frame {}", i);

    auto start = std::chrono::high_resolution_clock::now();
    co_await mac.tx(frames[i]);
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
           (option.project2_option.bin_size) / sec_float);

  // print frame times in one line using fmt
  LOG_INFO("Frame times: {}", fmt::join(frame_times, " "));
}

awaitable<void> async_rx(boost::asio::io_context& ctx,
                           SuperSonic::Sphy& phy,
                           SuperSonic::Smac& mac,
                           SuperSonic::Config::Option& option) {

  std::ofstream ofs("output.txt");

  SuperSonic::Bits bits;
  while (bits.size() < option.project2_option.bin_size) {
    LOG_INFO("Received {} bits, expect {} bits", bits.size(),
             option.project2_option.bin_size);
    auto rx = co_await mac.rx();
    bits.insert(bits.end(), rx.begin(), rx.end());
    for (auto e : rx) {
      ofs << (int)e;
    }
    ofs.flush();
    LOG_INFO("Wrote {} bits to output.txt", bits.size());
  }

  LOG_INFO("Received {} bits", bits.size());
}

awaitable<void> async_main(boost::asio::io_context& ctx,
                           SuperSonic::Sphy& phy,
                           SuperSonic::Smac& mac,
                           SuperSonic::Config::Option& option) {
  co_await phy.init();

  auto ex = co_await this_coro::executor;
  co_spawn(ex, mac.run(), detached);

  co_spawn(ex, async_rx(ctx, phy, mac, option), detached);

  if (option.project2_option.task == 1) {
    co_await async_tx(ctx, phy, mac, option);
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
