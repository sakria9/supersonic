#include <utility>
#include "boost/asio/awaitable.hpp"
#include "mac.h"
#include "phy.h"

static awaitable<void> expiry_ms(int ms) {
  steady_timer timer(co_await this_coro::executor);
  timer.expires_after(std::chrono::milliseconds(ms));
  co_await timer.async_wait(use_awaitable);
}

namespace SuperSonic {

awaitable<void> Smac::tx(Bits bits) {
  co_await tx_channel_->async_send({}, std::move(bits));
  co_await tx_comp_channel_->async_receive();
}

awaitable<void> Smac::run() {
  using namespace boost::asio::experimental::awaitable_operators;

  auto ex = co_await this_coro::executor;

  srand(opt_.mac_addr);

  static constexpr int RX_BUFFER_SIZE = 1000;
  rx_channel_ = std::make_unique<RxChannel>(ex, RX_BUFFER_SIZE);
  static constexpr int TX_BUFFER_SIZE = 0;
  tx_channel_ = std::make_unique<TxChannel>(ex, TX_BUFFER_SIZE);
  tx_comp_channel_ = std::make_unique<TxCompChannel>(ex, TX_BUFFER_SIZE);

  // spawn listen
  co_spawn(ex, listen(), detached);

  std::map<uint8_t, uint8_t> tx_seq_map;
  std::map<uint8_t, uint8_t> rx_seq_map;
  auto next_seq = [](uint8_t seq) -> uint8_t { return (seq + 1) & 0xf; };

  struct TxState {
    enum class State {
      Idle,
      Sending,
      WaitingAck,
    } state = State::Idle;

    // valid if state == Sending
    Bits bits;

    // valid if state == WaitingAck
    Frame frame;

    // valid if state == Sending or WaitingAck
    std::chrono::time_point<std::chrono::high_resolution_clock> timeout_ts;

    // valid if state = Sending or WaitingAck
    int resend;  // ack timeout

    // valid if state == Sending or WaitingAck
    int retries;  // channel busy
  } tx_state;

  auto tx_data = [&]() -> awaitable<void> {
    LOG_INFO("tx_data");
    if (tx_state.state != TxState::State::Sending) {
      LOG_ERROR("Invalid state, this should not happen");
      throw std::runtime_error("Invalid state");
    }

    if (phy_.supersonic_->rx_power() > opt_.busy_power_threshold) {
      if (tx_state.retries >= opt_.max_retries) {
        LOG_ERROR("Channel busy. Max retries reached, LINK ERROR");
        throw std::runtime_error("LINK ERROR");
      }
      auto backoff_ms = int((float)rand() * opt_.backoff_ms / RAND_MAX);
      LOG_WARN("Channel busy, wait {} ms, retry {}, {} > {}", backoff_ms,
               tx_state.retries, phy_.supersonic_->rx_power(),
               opt_.busy_power_threshold);
      tx_state.retries++;
      tx_state.timeout_ts = std::chrono::high_resolution_clock::now() +
                            std::chrono::milliseconds(backoff_ms);
      co_return;
    }
    LOG_INFO("Channel idle, start sending");
    tx_state.retries = 0;

    uint8_t dest = 1 ^ (opt_.mac_addr);

    // new dest, set seq to 0
    if (tx_seq_map.find(dest) == tx_seq_map.end()) {
      tx_seq_map[dest] = 0;
    }

    Frame frame{opt_.mac_addr, dest, FrameType::Data, tx_seq_map[dest],
                tx_state.bits};
    co_await tx_frame(frame);
    LOG_INFO("Sent frame dest {} seq {} payload size {}, wait ack", frame.dest,
             frame.seq, frame.payload.size());

    tx_state.state = TxState::State::WaitingAck;
    tx_state.frame = frame;
    tx_state.timeout_ts = std::chrono::high_resolution_clock::now() +
                          std::chrono::milliseconds(opt_.timeout_ms);
  };

  auto tx_data_resend = [&]() -> awaitable<void> {
    LOG_WARN("tx_data_resend");
    if (tx_state.state != TxState::State::WaitingAck) {
      LOG_ERROR("Invalid state, this should not happen");
      throw std::runtime_error("Invalid state");
    }

    if (tx_state.resend >= opt_.max_retries) {
      LOG_ERROR("Max resend reached, LINK ERROR");
      throw std::runtime_error("LINK ERROR");
    }

    tx_state.state = TxState::State::Sending;
    tx_state.resend++;
    tx_state.timeout_ts = std::chrono::high_resolution_clock::now() +
                          std::chrono::milliseconds(opt_.timeout_ms);
    co_return;
  };

  auto rx_ack = [&](Frame& ack_frame) -> awaitable<void> {
    LOG_INFO("rx_ack");
    if (tx_state.state != TxState::State::WaitingAck) {
      LOG_ERROR("Invalid state, this should not happen");
      throw std::runtime_error("Invalid state");
    }
    if (ack_frame.type != FrameType::Ack) {
      LOG_WARN("Received frame type {} is not Ack, skip",
               std::to_underlying(ack_frame.type));
      co_return;
    }

    auto expect_seq = next_seq(tx_state.frame.seq);
    if (ack_frame.seq != expect_seq) {
      LOG_WARN(
          "Received ACK frame src {} dst {} seq {}, seq is not expected "
          "{}, retry {}",
          ack_frame.src, ack_frame.dest, ack_frame.seq, expect_seq,
          tx_state.retries);
      co_await tx_data_resend();
      co_return;
    }

    // Correctly received ACK
    LOG_INFO("Correctly received ACK src {} dst {} seq {}", ack_frame.src,
             ack_frame.dest, ack_frame.seq);
    co_await tx_comp_channel_->async_send({}, 0);

    tx_seq_map[tx_state.frame.dest] = next_seq(tx_seq_map[tx_state.frame.dest]);

    tx_state.state = TxState::State::Idle;
  };

  auto rx_data = [&](Frame& frame) -> awaitable<void> {
    LOG_INFO("rx_data");
    if (frame.type != FrameType::Data) {
      LOG_WARN("Received frame type {} is not Data, skip",
               std::to_underlying(frame.type));
      co_return;
    }

    // new src, set seq to 0
    if (rx_seq_map.find(frame.src) == rx_seq_map.end()) {
      rx_seq_map[frame.src] = 0;
    }

    // check seq
    if (frame.seq != rx_seq_map[frame.src]) {
      LOG_WARN("Received frame seq {} is not expected {}, send another ACK",
               frame.seq, rx_seq_map[frame.src]);
      Frame ack_frame{
          opt_.mac_addr, frame.src, FrameType::Ack, rx_seq_map[frame.src], {}};
      co_await tx_frame(ack_frame);
      co_return;
    }
    rx_seq_map[frame.src] = next_seq(rx_seq_map[frame.src]);

    // Correctly received frame
    // push payload to somewhere, here we just print it
    LOG_INFO(
        "Received frame src {} seq {} payload size {}, "
        "Sending ACK src {} dest {} seq {}",
        frame.src, frame.seq, frame.payload.size(), opt_.mac_addr, frame.src,
        rx_seq_map[frame.src]);

    // Send ACK with next seq
    Frame ack_frame{
        opt_.mac_addr, frame.src, FrameType::Ack, rx_seq_map[frame.src], {}};
    co_await tx_frame(ack_frame);
    LOG_INFO("ACK sent");
  };

  while (1) {
    LOG_INFO("State {}", std::to_underlying(tx_state.state));
    if (tx_state.state == TxState::State::Idle) {
      // get a tx task or recv data
      auto result = co_await (tx_channel_->async_receive(use_awaitable) ||
                              rx_channel_->async_receive(use_awaitable));
      if (result.index() == 0) {
        // TX
        Bits& bits = std::get<0>(result);

        tx_state.state = TxState::State::Sending;
        tx_state.bits = bits;
        tx_state.retries = 0;
        tx_state.resend = 0;
        co_await tx_data();
      } else if (result.index() == 1) {
        // RX
        auto frame = std::get<1>(result);
        co_await rx_data(frame);
      }
      continue;
    }

    if (tx_state.state == TxState::State::Sending) {
      // wait backoff time or recv data
      auto now = std::chrono::high_resolution_clock::now();
      auto timeout = tx_state.timeout_ts - now;
      auto timeout_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(timeout)
              .count();
      if (timeout_ms <= 0) {
        // retry
        co_await tx_data();
        continue;
      }

      auto result = co_await (rx_channel_->async_receive(use_awaitable) ||
                              expiry_ms(timeout_ms));
      if (result.index() == 0) {
        // recv data
        auto& frame = std::get<0>(result);
        co_await rx_data(frame);
      } else if (result.index() == 1) {
        // retry
        co_await tx_data();
      }
      continue;
    }

    if (tx_state.state == TxState::State::WaitingAck) {
      // wait recv ack or ack timeout or recv data
      auto now = std::chrono::high_resolution_clock::now();
      auto timeout = tx_state.timeout_ts - now;
      auto timeout_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(timeout)
              .count();
      if (timeout_ms <= 0) {
        // resend
        co_await tx_data_resend();
        continue;
      }

      auto result = co_await (rx_channel_->async_receive(use_awaitable) ||
                              expiry_ms(timeout_ms));
      if (result.index() == 0) {
        // recv ack or recv data
        auto& frame = std::get<0>(result);
        if (frame.type == FrameType::Ack) {
          co_await rx_ack(frame);
        } else {
          co_await rx_data(frame);
        }
      } else if (result.index() == 1) {
        // resend
        co_await tx_data_resend();
      }
      continue;
    }
  }
}

awaitable<void> Smac::listen() {
  while (1) {
    auto rx_bits = co_await phy_.rx();
    if (rx_bits.size() < header_bits + 16) {
      LOG_WARN("Received frame too short, drop the frame");
      continue;
    }
    auto crc_result = validate_crc16(rx_bits);
    if (!crc_result) {
      LOG_WARN("CRC check failed, drop the frame");
      continue;
    }
    auto rx_frame = parse_frame(rx_bits);
    if (rx_frame.dest != opt_.mac_addr) {
      // LOG_INFO("Frame dest {} is not me {}", rx_frame.dest, opt_.mac_addr);
      continue;
    }
    LOG_INFO("Received frame type {} payload size {}",
             std::to_underlying(rx_frame.type), rx_frame.payload.size());
    co_await rx_channel_->async_send({}, std::move(rx_frame));
    LOG_INFO("Frame pushed to rx channel");
  }
}

uint8_t Smac::get_frame_src(BitView frame) {
  return bits2Int(frame.subspan(0, src_bits));
}
void Smac::set_frame_src(MutBitView frame, uint8_t src) {
  int2Bits(src, frame.subspan(0, src_bits));
}
uint8_t Smac::get_frame_dest(BitView frame) {
  return bits2Int(frame.subspan(src_bits, dest_bits));
}
void Smac::set_frame_dest(MutBitView frame, uint8_t dest) {
  int2Bits(dest, frame.subspan(src_bits, dest_bits));
}
Smac::FrameType Smac::get_frame_type(BitView frame) {
  return static_cast<FrameType>(
      bits2Int(frame.subspan(src_bits + dest_bits, type_bits)));
}
void Smac::set_frame_type(MutBitView frame, FrameType type) {
  int2Bits(std::to_underlying(type),
           frame.subspan(src_bits + dest_bits, type_bits));
}
uint8_t Smac::get_frame_seq(BitView frame) {
  return bits2Int(frame.subspan(src_bits + dest_bits + type_bits, seq_bits));
}
void Smac::set_frame_seq(MutBitView frame, uint8_t seq) {
  int2Bits(seq, frame.subspan(src_bits + dest_bits + type_bits, seq_bits));
}

Smac::Frame Smac::parse_frame(BitView frame) {
  const int payload_bits = frame.size() - header_bits - crc_bits;
  auto payload = frame.subspan(dest_bits + type_bits + seq_bits, payload_bits);
  auto payload_vec = std::vector<uint8_t>(payload.begin(), payload.end());
  return Frame{get_frame_src(frame), get_frame_dest(frame),
               get_frame_type(frame), get_frame_seq(frame),
               std::move(payload_vec)};
}
Bits Smac::make_frame(const Frame& frame) {
  Bits bits(header_bits + frame.payload.size());
  set_frame_src(bits, frame.src);
  set_frame_dest(bits, frame.dest);
  set_frame_type(bits, frame.type);
  set_frame_seq(bits, frame.seq);
  std::copy(frame.payload.begin(), frame.payload.end(),
            bits.begin() + header_bits);
  return crc16(bits);
}

}  // namespace SuperSonic