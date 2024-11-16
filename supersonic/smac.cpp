#include <utility>
#include "mac.h"

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

  while (1) {
    auto result = co_await (rx_channel_->async_receive(use_awaitable) ||
                            tx_channel_->async_receive(use_awaitable));
    if (result.index() == 0) {
      // RX
      auto frame = std::get<0>(result);
      if (frame.type != FrameType::Data) {
        LOG_WARN("Received frame type {} is not Data, skip",
                 std::to_underlying(frame.type));
        continue;
      }
      if (rx_seq_map.find(frame.src) == rx_seq_map.end()) {
        rx_seq_map[frame.src] = 0;
      }
      if (frame.seq != rx_seq_map[frame.src]) {
        LOG_WARN("Received frame seq {} is not expected {}, send another ACK",
                 frame.seq, rx_seq_map[frame.src]);
        Frame ack_frame{opt_.mac_addr,
                        frame.src,
                        FrameType::Ack,
                        rx_seq_map[frame.src],
                        {}};
        co_await tx_frame(ack_frame);
        continue;
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
    } else if (result.index() == 1) {
      // TX
      auto bits = std::get<1>(result);
      uint8_t dest = 1 ^ (opt_.mac_addr);
      if (tx_seq_map.find(dest) == tx_seq_map.end()) {
        tx_seq_map[dest] = 0;
      }

      Frame frame{opt_.mac_addr, dest, FrameType::Data, tx_seq_map[dest], bits};
      tx_seq_map[dest] = next_seq(tx_seq_map[dest]);

      int retries = 0;

      while (1) {
        if (retries >= opt_.max_retries) {
          LOG_ERROR("Max retries reached, LINK ERROR");
          throw std::runtime_error("LINK ERROR");
        }
        retries++;

        co_await tx_frame(frame);
        LOG_INFO("Sent frame dest {} seq {} payload size {}, wait ack",
                 frame.dest, frame.seq, frame.payload.size());

        auto ack_result = co_await (rx_channel_->async_receive(use_awaitable) ||
                                    expiry_ms(opt_.timeout_ms));
        while (ack_result.index() == 0 &&
               std::get<0>(ack_result).type != FrameType::Ack) {
          // filter out non-ack frames
          ack_result = co_await (rx_channel_->async_receive(use_awaitable) ||
                                 expiry_ms(opt_.timeout_ms));
        }

        if (ack_result.index() == 0) {
          // get a frame
          auto& ack_frame = std::get<0>(ack_result);

          if (ack_frame.type != FrameType::Ack) {
            LOG_WARN("Received frame type {} is not Ack, retry {}",
                     std::to_underlying(ack_frame.type), retries);
            continue;
          }
          if (ack_frame.seq != next_seq(frame.seq)) {
            LOG_WARN(
                "Received ACK frame src {} dst {} seq {}, seq is not expected "
                "{}, retry {}",
                ack_frame.src, ack_frame.dest, ack_frame.seq,
                next_seq(frame.seq), retries);
            continue;
          }
          // Correctly received ACK
          LOG_INFO("Correctly received ACK src {} dst {} seq {}", ack_frame.src,
                   ack_frame.dest, ack_frame.seq);
          co_await tx_comp_channel_->async_send({}, 0);
          break;
        } else {
          // timeout
          LOG_WARN("Timeout, retry {}", retries);
        }
      }
    }
  }
}

awaitable<void> Smac::listen() {
  while (1) {
    auto rx_bits = co_await phy_.rx();
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
    // LOG_INFO("Received frame type {} payload size {}",
    //          std::to_underlying(rx_frame.type), rx_frame.payload.size());
    co_await rx_channel_->async_send({}, std::move(rx_frame));
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