#include <boost/asio/experimental/awaitable_operators.hpp>
#include <cxxopts.hpp>

#include "phy.h"
#include "rs.h"
#include "supersonic.h"
#include "utils.h"

static constexpr int warm_up_rounds = 50;
static constexpr int rounds = 100;

awaitable<void> async_send(SuperSonic::Sphy& phy) {
  using namespace SuperSonic;
  LOG_INFO("Total {} warmup rounds", warm_up_rounds);

  for (int i = 0; i < warm_up_rounds; i++) {
    SuperSonic::Bits bits(phy.opt_.bin_payload_size);
    for (size_t i = 0; i < bits.size(); i++) {
      bits[i] = rand() % 2;
    }
    co_await phy.tx(std::move(bits));
  }

  LOG_INFO("Total {} data rounds", rounds);

  std::ofstream ofs("input.txt");
  for (size_t i = 0; i < rounds; i++) {
    Bits bits(phy.opt_.bin_payload_size);
    for (size_t i = 0; i < bits.size(); i++) {
      bits[i] = rand() % 2;
      ofs << (int)bits[i];
    }
    ofs << std::endl;
    co_await phy.tx(bits);
  }

  LOG_INFO("Send finished");
}

awaitable<void> expiry(int seconds) {
  steady_timer timer(co_await this_coro::executor);
  timer.expires_after(std::chrono::seconds(seconds));
  co_await timer.async_wait(use_awaitable);
}

awaitable<void> async_recv(boost::asio::io_context& ctx,
                           SuperSonic::Sphy& phy) {
  using namespace boost::asio::experimental::awaitable_operators;
  using namespace SuperSonic;

  auto timeout = [&]() {
    LOG_ERROR("Timeout");
    ctx.stop();
  };

  for (int i = 0; i < warm_up_rounds; i++) {
    auto frame = co_await (phy.rx() || expiry(10));
    if (frame.index() == 1) {
      timeout();
      co_return;
    }
  }

  LOG_INFO("Warmup finished");
  LOG_INFO("Start receiving");

  Bits bits;

  auto start = std::chrono::high_resolution_clock::now();
  std::ofstream ofs("output.txt");
  for (size_t i = 0; i < rounds; i++) {
    LOG_INFO("Round {}", i);
    auto frame = co_await (phy.rx() || expiry(10));
    if (frame.index() == 1) {
      timeout();
      co_return;
    }

    auto& phy_frame = std::get<0>(frame);
    bits.insert(bits.end(), phy_frame.begin(), phy_frame.end());

    for (size_t i = 0; i < phy_frame.size(); i++) {
      ofs << (int)phy_frame[i];
    }
    ofs << std::endl;
  }
  auto end = std::chrono::high_resolution_clock::now();

  float s =
      (float)std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count() /
      1000;
  LOG_INFO("Recv finished, {} bits received in {} s, {} bps", bits.size(), s,
           (float)bits.size() / s);

  LOG_INFO("Successfully decoded");

  ctx.stop();
}

awaitable<void> async_main(boost::asio::io_context& ctx,
                           SuperSonic::Sphy& phy,
                           SuperSonic::Config::Option& option,
                           int task) {
  co_await phy.init();

  auto ex = co_await this_coro::executor;

  if (task == 1 || task == 3) {
    co_spawn(ex, async_send(phy), detached);
  }
  if (task == 2 || task == 3) {
    co_spawn(ex, async_recv(ctx, phy), detached);
  }
}

int main(int argc, char** argv) {
  cxxopts::Options options("supersonic", "Supersonic Project 1");
  // clang-format off
  options.add_options()
    ("h,help", "Print usage")
    ("t,task", "Task to run", cxxopts::value<int>(), "1: Send, 2: Receive, 3: Both");
  // clang-format on
  auto result = options.parse(argc, argv);

  if (result.count("help") || result.count("task") == 0) {
    std::cout << options.help() << std::endl;
    return 0;
  }
  auto task = result["task"].as<int>();

  auto option = SuperSonic::Config::load_option("config.json");

  SuperSonic::Sphy phy(option.sphy_option);

  try {
    boost::asio::io_context io_context(1);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    co_spawn(io_context, async_main(io_context, phy, option, task), detached);

    io_context.run();
  } catch (std::exception& e) {
    std::printf("Exception: %s\n", e.what());
  }

  return 0;
}
