#include <boost/asio/experimental/awaitable_operators.hpp>
#include <cxxopts.hpp>

#include "mac.h"
#include "phy.h"
#include "tun.h"
#include "utils.h"

bool icmp_fast_path(SuperSonic::BitView bits) {
  auto bytes = SuperSonic::bitsToBytes(bits);
  // verify the packet is ICMP

  // 1. check the IP version
  if (bytes[0] != 0x45) {
    return false;
  }
  // 2. check the protocol
  if (bytes[9] != 0x01) {
    return false;
  }
  // 3. check the ICMP type
  if (bytes[20] != 0x08) {
    return false;
  }
  // 4. check the ICMP code
  if (bytes[21] != 0x00) {
    return false;
  }

  LOG_INFO("ICMP packet received");
  return true;
}

awaitable<void> async_rx(boost::asio::io_context& ctx,
                         SuperSonic::Tun& tun,
                         SuperSonic::Sphy& phy,
                         SuperSonic::Smac& mac,
                         SuperSonic::Config::Option& option) {
  while (true) {
    auto rx_bits = co_await mac.rx();
    if (icmp_fast_path(rx_bits)) {
      LOG_INFO("ICMP fast path, but not implemented");
      tun.rx(rx_bits);
    } else {
      tun.rx(rx_bits);
    }
  }
}

awaitable<void> async_tx(boost::asio::io_context& ctx,
                         SuperSonic::Tun& tun,
                         SuperSonic::Sphy& phy,
                         SuperSonic::Smac& mac,
                         SuperSonic::Config::Option& option) {
  while (true) {
    auto tx_bits = co_await tun.tx();
    co_await mac.tx(tx_bits);
  }
}

awaitable<void> async_main(boost::asio::io_context& ctx,
                           SuperSonic::Tun& tun,
                           SuperSonic::Sphy& phy,
                           SuperSonic::Smac& mac,
                           SuperSonic::Config::Option& option) {
  co_await tun.init();
  co_await phy.init();

  auto ex = co_await this_coro::executor;
  co_spawn(ex, mac.run(), detached);

  co_spawn(ex, async_rx(ctx, tun, phy, mac, option), detached);
  co_spawn(ex, async_tx(ctx, tun, phy, mac, option), detached);
}

int main(int argc, char** argv) {
  cxxopts::Options options("supersonic", "Supersonic Project 3");
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
  SuperSonic::Tun tun(option.tun_option);

  try {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    co_spawn(io_context, async_main(io_context, tun, phy, mac, option),
             detached);

    io_context.run();
  } catch (std::exception& e) {
    std::printf("Exception: %s\n", e.what());
  }

  return 0;
}