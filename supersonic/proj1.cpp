#include <boost/asio/experimental/awaitable_operators.hpp>
#include <cxxopts.hpp>

#include "phy.h"
#include "rs.h"
#include "supersonic.h"
#include "utils.h"

static constexpr int warm_up_rounds = 50;

SuperSonic::RS255223 rs;

awaitable<void> async_send(SuperSonic::Sphy& phy) {
  using namespace SuperSonic;

  // read bits from input.txt
  std::ifstream ifs("input.txt");
  if (!ifs.is_open()) {
    LOG_ERROR("Failed to open input.txt");
    co_return;
  }
  Bits bits;
  while (true) {
    char c;
    ifs >> c;
    if (ifs.eof()) {
      break;
    }
    bits.push_back(c - '0');
  }

  LOG_INFO("Read {} bits from input.txt", bits.size());

  bits = rs.encode_many(bits);
  bits = uniform_reorder(bits);
  const size_t rounds =
      (bits.size() + phy.opt_.bin_payload_size - 1) / phy.opt_.bin_payload_size;
  bits.resize(rounds * phy.opt_.bin_payload_size);

  LOG_INFO("Total {} warmup rounds", warm_up_rounds);

  for (int i = 0; i < warm_up_rounds; i++) {
    SuperSonic::Bits bits(phy.opt_.bin_payload_size);
    for (size_t i = 0; i < bits.size(); i++) {
      bits[i] = rand() % 2;
    }
    co_await phy.tx(std::move(bits));
  }

  LOG_INFO("Total {} data rounds", rounds);

  for (size_t i = 0; i < rounds; i++) {
    BitView view(bits.data() + i * phy.opt_.bin_payload_size,
                 phy.opt_.bin_payload_size);
    co_await phy.tx(view);
  }

  LOG_INFO("Send finished");
}

awaitable<void> expiry(int seconds) {
  steady_timer timer(co_await this_coro::executor);
  timer.expires_after(std::chrono::seconds(seconds));
  co_await timer.async_wait(use_awaitable);
}

awaitable<void> async_recv(boost::asio::io_context& ctx,
                           SuperSonic::Sphy& phy,
                           size_t recv_size) {
  using namespace boost::asio::experimental::awaitable_operators;
  using namespace SuperSonic;

  auto rs_size = rs.encoded_size(recv_size);
  size_t rounds =
      (rs_size + phy.opt_.bin_payload_size - 1) / phy.opt_.bin_payload_size;
  SuperSonic::Bits bits;

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

  auto start = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < rounds; i++) {
    LOG_INFO("Round {}", i);
    auto frame = co_await (phy.rx() || expiry(10));
    if (frame.index() == 1) {
      timeout();
      co_return;
    }

    auto& phy_frame = std::get<0>(frame);
    bits.insert(bits.end(), phy_frame.begin(), phy_frame.end());
  }
  auto end = std::chrono::high_resolution_clock::now();

  float s =
      (float)std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count() /
      1000;
  LOG_INFO("Recv finished, {} bits received in {} s, {} bps (include FEC)",
           bits.size(), s, (float)bits.size() / s);
  LOG_INFO("Recv finished, {} bits received in {} s, {} bps (exclude FEC)",
           recv_size, s, (float)recv_size / s);

  bits.resize(rs_size);
  bits = uniform_dereorder(bits);
  bits = rs.decode_many(bits);
  bits.resize(recv_size);

  LOG_INFO("Successfully decoded");

  {
    std::ofstream ofs("output.txt");
    for (auto e : bits) {
      ofs << (int)e;
    }
  }

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
    co_spawn(ex, async_recv(ctx, phy, option.project1_option.payload_size),
             detached);
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
