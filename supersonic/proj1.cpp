#include <cxxopts.hpp>

#include "phy.h"
#include "supersonic.h"
#include "utils.h"

void print_bits(SuperSonic::Bits bits, std::string prefix = "") {
  std::cout << prefix << "Bits: ";
  for (auto b : bits) {
    std::cout << (bool)b;
  }
  std::cout << std::endl;
}

awaitable<void> async_send(SuperSonic::Sphy& phy) {
  static constexpr int rounds = 100;

  std::ofstream ofs("input.txt");

  for (int i = 0; i < rounds; i++) {
    SuperSonic::Bits bits(SuperSonic::Sphy::BIN_PAYLOAD_SIZE);
    for (int i = 0; i < bits.size(); i++) {
      bits[i] = rand() % 2;
    }

    for (auto b : bits) {
      ofs << (bool)b;
    }
    ofs << std::endl;

    // LOG_INFO("Sending bits");
    co_await phy.tx(std::move(bits));
    // print_bits(bits, "Sent ");

    // steady_timer timer(co_await this_coro::executor);
    // timer.expires_after(std::chrono::seconds(1));
    // co_await timer.async_wait(use_awaitable);
  }

  LOG_INFO("Send finished");
}

awaitable<void> async_recv(SuperSonic::Sphy& phy) {
  std::ofstream ofs("output.txt");
  while (1) {
    auto frame = co_await phy.rx();
    LOG_INFO("Frame received with size {}", frame.size());
    // print_bits(frame, "Recv ");
    for (auto b : frame) {
      ofs << (bool)b;
    }
    ofs << std::endl;
    ofs.flush();
  }
}

awaitable<void> async_main(SuperSonic::Sphy& phy) {
  co_await phy.init();

  auto ex = co_await this_coro::executor;
  co_spawn(ex, async_recv(phy), detached);
  co_spawn(ex, async_send(phy), detached);
}

int main(int argc, char** argv) {
  cxxopts::Options options("supersonic", "Supersonic Project 0");
  // clang-format off
  options.add_options()
    ("h,help", "Print usage")
    ("i,input", "Input sink", cxxopts::value<std::string>()->default_value("system:capture_1"))
    ("o,output", "Output sink", cxxopts::value<std::string>()->default_value("system:playback_1"))
    ("t,task", "Task to run", cxxopts::value<int>(), "1: Send, 2: Receive");
  // clang-format on
  auto result = options.parse(argc, argv);

  if (result.count("help") || result.count("task") == 0) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  SuperSonic::Saudio::SaudioOption opt;
  opt.input_port = result["input"].as<std::string>();
  opt.output_port = result["output"].as<std::string>();
  opt.ringbuffer_size = 128 * 10;

  SuperSonic::Sphy::SphyOption phy_opt;
  phy_opt.supersonic_option = opt;

  SuperSonic::Sphy phy(phy_opt);

  try {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    co_spawn(io_context, async_main(phy), detached);

    io_context.run();
  } catch (std::exception& e) {
    std::printf("Exception: %s\n", e.what());
  }

  return 0;
}
