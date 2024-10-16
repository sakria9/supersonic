#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/channel.hpp>
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

#include "utils.h"
#include "chirp.h"
#include "fsk.h"
#include "supersonic.h"

namespace SuperSonic {

class Sphy {
 public:
  // interval and timieout
  static constexpr auto PUSH_INTERVAL = std::chrono::milliseconds(1);
  static constexpr auto POLL_INTERVAL = std::chrono::milliseconds(10);
  static constexpr auto RX_POLL_INTERVAL = std::chrono::milliseconds(0);
  static constexpr auto TIMEOUT = std::chrono::seconds(1);

  // buffer
  static constexpr size_t TX_BUFFER_SIZE = 100;

  // FSK
  static constexpr float FSK_FREQ = 2000;
  static constexpr auto FSK_OPTION = Signal::FSKOption(FSK_FREQ);
  static constexpr size_t BIN_PAYLOAD_SIZE = 50;
  static constexpr size_t PHY_PAYLOAD_SIZE =
      FSK_OPTION.symbol_samples * BIN_PAYLOAD_SIZE;
  static constexpr size_t FRAME_GAP_SIZE = 48;
  static constexpr size_t FRAME_SIZE =
      Signal::CHIRP1_LEN + PHY_PAYLOAD_SIZE + FRAME_GAP_SIZE;

  const std::vector<float> chirp = Signal::generate_chirp1();

  struct SphyOption {
    Saudio::SaudioOption supersonic_option;
  };

  Sphy(SphyOption& opt) : opt_(opt) {}

  awaitable<void> init() {
    supersonic_ = std::make_unique<Saudio>(opt_.supersonic_option);
    supersonic_->run();
    auto ex = co_await this_coro::executor;

    tx_channel_ = std::make_unique<TxChannel>(ex, TX_BUFFER_SIZE);

    using namespace Signal;
    auto t = linspace(0, .5, kSampleRate / 2);
    auto wave = sine_wave(440, t);
    co_await send_audio(wave);
    co_await send_audio(zeros(kSampleRate / 2));

    // spawn async_main
    co_spawn(ex, async_main(), detached);
  }

  using TxChannel =
      boost::asio::experimental::channel<void(boost::system::error_code, Bits)>;

  awaitable<Bits> rx() {
    auto phy_payload = co_await receive_frame();
    auto bits = Signal::fsk_demodulate(phy_payload, FSK_OPTION);
    co_return bits;
  }

  awaitable<void> tx(Bits bits) {
    if (tx_channel_ == nullptr) {
      LOG_ERROR("Tx channel not initialized. This should not happen.");
      co_return;
    }
    co_await tx_channel_->async_send({}, std::move(bits));
  }

 private:
  awaitable<void> async_main() {
    // handling tx
    if (!tx_channel_) {
      LOG_ERROR("Tx channel not initialized. This should not happen.");
      co_return;
    }
    while (1) {
      auto bits = co_await tx_channel_->async_receive(use_awaitable);
      co_await send_bits(bits);
    }
  }

  awaitable<void> send_bits(BitView bits) {
    if (bits.size() != BIN_PAYLOAD_SIZE) {
      LOG_ERROR("Invalid bits size: {}", bits.size());
      co_return;
    }
    auto wave = Signal::fsk_modulate(bits, FSK_OPTION);
    co_await send_frame(wave);
  }

  awaitable<void> send_frame(SampleView phy_payload) {
    auto frame =
        Signal::concatenate(chirp, phy_payload, Signal::zeros(FRAME_GAP_SIZE));
    co_await send_audio(std::move(frame));
  }

  awaitable<void> send_audio(Samples data) {
    auto start_time = std::chrono::high_resolution_clock::now();

    steady_timer timer(co_await this_coro::executor);

    if (!supersonic_->tx_buffer.write_available()) {
      LOG_WARN("Tx buffer is full, waiting for space.");
    }
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

    supersonic_->tx_buffer.push({data});
  }

  awaitable<float> rx_pop() {
    static constexpr float magic = -1;
    // fast path
    if (supersonic_->rx_buffer.read_available()) {
      float e;
      supersonic_->rx_buffer.pop(e);
      co_return (magic * e);
    }

    while (!supersonic_->rx_buffer.read_available()) {
      steady_timer timer(co_await this_coro::executor);
      timer.expires_after(RX_POLL_INTERVAL);
      co_await timer.async_wait(use_awaitable);
    }
    float e;
    supersonic_->rx_buffer.pop(e);
    co_return (magic * e);
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
  std::unique_ptr<TxChannel> tx_channel_;
};

}  // namespace SuperSonic