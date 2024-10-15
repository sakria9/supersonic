#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include "log.h"

using boost::asio::awaitable;
using boost::asio::detached;
using boost::asio::steady_timer;
using boost::asio::use_awaitable;
namespace this_coro = boost::asio::this_coro;
#if defined(BOOST_ASIO_ENABLE_HANDLER_TRACKING)
#define use_awaitable \
  boost::asio::use_awaitable_t(__FILE__, __LINE__, __PRETTY_FUNCTION__)
#endif

#include "chirp.h"
#include "fsk.h"
#include "supersonic.h"

namespace SuperSonic {

class Sphy {
 public:
  static constexpr auto PUSH_INTERVAL = std::chrono::milliseconds(1);
  static constexpr auto POLL_INTERVAL = std::chrono::milliseconds(10);
  static constexpr auto RX_POLL_INTERVAL = std::chrono::milliseconds(0);
  static constexpr auto TIMEOUT = std::chrono::seconds(1);

  static constexpr size_t PAYLOAD_SIZE = 50;
  static constexpr size_t PHY_PAYLOAD_SIZE = 2400;

  const std::vector<float> chirp = Signal::generate_chirp1();

  struct SphyOption {
    Saudio::SaudioOption supersonic_option;
  };

  Sphy(SphyOption& opt) : opt_(opt) {}

  void init() {
    supersonic_ = std::make_unique<Saudio>(opt_.supersonic_option);
    supersonic_->run();
  }

 public:
  awaitable<void> send_bits(Bits bits) {
    if (bits.size() != PAYLOAD_SIZE) {
      LOG_ERROR("Invalid bits size: {}", bits.size());
      co_return;
    }
    Signal::FSKOption opt(1000);
    auto wave = Signal::fsk_modulate(bits, opt);
    co_await send_frame(wave);
  }

  awaitable<Bits> receive_bits() {
    auto phy_payload = co_await receive_frame();
    Signal::FSKOption opt(1000);
    auto bits = Signal::fsk_demodulate(phy_payload, opt);
    co_return bits;
  }

 private:
  awaitable<void> send_frame(Samples phy_payload) {
    auto frame = Signal::concatenate(chirp, phy_payload);
    co_await send_audio(frame);
  }

  awaitable<void> send_audio(std::span<const float> data) {
    std::atomic_flag token;
    auto start_time = std::chrono::high_resolution_clock::now();

    steady_timer timer(co_await this_coro::executor);

    while (!supersonic_->tx_buffer.write_available()) {
      timer.expires_after(PUSH_INTERVAL);
      co_await timer.async_wait(use_awaitable);
      auto now = std::chrono::high_resolution_clock::now();
      if (now - start_time > TIMEOUT) {
        LOG_ERROR(
            "Tx buffer is full after {} seconds,"
            "probably something bad happens",
            std::chrono::duration_cast<std::chrono::seconds>(now - start_time)
                .count());
        co_return;
      }
    }

    supersonic_->tx_buffer.push({data, &token});

    while (!token.test()) {
      timer.expires_after(POLL_INTERVAL);
      co_await timer.async_wait(use_awaitable);
      auto now = std::chrono::high_resolution_clock::now();
      if (now - start_time > TIMEOUT) {
        LOG_ERROR(
            "Audio data pushed to Tx buffer but not played after {} seconds, "
            "probably something bad happens",
            std::chrono::duration_cast<std::chrono::seconds>(now - start_time)
                .count());
        break;
      }
    }
  }

  awaitable<float> rx_pop() {
    static constexpr float magic = -1;
    // fast path
    if (supersonic_->rx_buffer.read_available()) {
      float e;
      supersonic_->rx_buffer.pop(e);
      co_return magic* e;
    }

    while (!supersonic_->rx_buffer.read_available()) {
      steady_timer timer(co_await this_coro::executor);
      timer.expires_after(RX_POLL_INTERVAL);
      co_await timer.async_wait(use_awaitable);
    }
    float e;
    supersonic_->rx_buffer.pop(e);
    co_return magic* e;
  }

  awaitable<std::vector<float>> receive_frame() {
    using namespace Signal;
    enum class State { PREAMBLE, PAYLOAD } state = State::PREAMBLE;

    static const size_t chirp_len = chirp.size();
    static constexpr size_t PREMABLE_PEEK_SIZE = 64;
    static constexpr size_t PREMABLE_WINDOW_SIZE = PREMABLE_PEEK_SIZE * 2 + 1;

    // find preamble
    auto preamble = zeros<float>(chirp_len);
    auto corr = zeros<float>(PREMABLE_WINDOW_SIZE);

    auto move_left = [](auto& arr) {
      std::copy(arr.begin() + 1, arr.end(), arr.begin());
    };
    auto calc_preamble_corr = [&]() {
      auto& wave = preamble;
      auto max = maxv(absv(wave));
      if (max < 1e-6) {
        return 0.0f;
      }

      auto wave_norm = wave;
      for (auto& e : wave_norm) {
        e /= max;
      }

      auto correlation = dot(wave_norm, chirp) / (float)chirp_len;
      return correlation;
    };

    auto add_one = [&](float a) {
      move_left(preamble);
      preamble[chirp_len - 1] = a;
      move_left(corr);
      corr[PREMABLE_WINDOW_SIZE - 1] = calc_preamble_corr();
    };

    while (true) {
      float e = co_await rx_pop();
      add_one(e);
      auto max_idx = argmax(corr);
      if (max_idx == PREMABLE_PEEK_SIZE && corr[max_idx] > 0.1) {
        // preamble found
        LOG_INFO("Preamble found with corr = {}", corr[max_idx]);
        break;
      }
    }

    auto phy_payload = zeros(PHY_PAYLOAD_SIZE);
    std::span<float> peek_wave{preamble.data() + chirp_len - PREMABLE_PEEK_SIZE,
                               PREMABLE_PEEK_SIZE};
    // copy to phy_payload
    std::copy(peek_wave.begin(), peek_wave.end(), phy_payload.begin());
    auto to_read = PHY_PAYLOAD_SIZE - PREMABLE_PEEK_SIZE;
    for (size_t i = 0; i < to_read; i++) {
      phy_payload[PREMABLE_PEEK_SIZE + i] = co_await rx_pop();
    }

    co_return phy_payload;
  };

  SphyOption opt_;
  std::unique_ptr<Saudio> supersonic_;
};

}  // namespace SuperSonic