#pragma once

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>

#include <map>
#include <utility>

#include "crc.h"
#include "phy.h"
#include "utils.h"

using boost::asio::awaitable;
using boost::asio::detached;
using boost::asio::steady_timer;
using boost::asio::use_awaitable;
namespace this_coro = boost::asio::this_coro;
#if defined(BOOST_ASIO_ENABLE_HANDLER_TRACKING)
#define use_awaitable \
  boost::asio::use_awaitable_t(__FILE__, __LINE__, __PRETTY_FUNCTION__)
#endif

namespace SuperSonic {

class Smac {
 public:
  static constexpr int src_bits = 2;
  static constexpr int dest_bits = 2;
  static constexpr int type_bits = 2;
  static constexpr int seq_bits = 4;
  static constexpr int header_bits =
      src_bits + dest_bits + type_bits + seq_bits;
  static constexpr int crc_bits = 16;

  enum class FrameType : uint8_t {
    Data = 0,
    Ack = 1,
  };

  struct Frame {
    uint8_t src;
    uint8_t dest;
    FrameType type;
    uint8_t seq;
    Bits payload;
  };

  uint8_t get_frame_src(BitView frame);
  void set_frame_src(MutBitView frame, uint8_t src);
  uint8_t get_frame_dest(BitView frame);
  void set_frame_dest(MutBitView frame, uint8_t dest);
  FrameType get_frame_type(BitView frame);
  void set_frame_type(MutBitView frame, FrameType type);
  uint8_t get_frame_seq(BitView frame);
  void set_frame_seq(MutBitView frame, uint8_t seq);

  Frame parse_frame(BitView frame);
  Bits make_frame(const Frame& frame);

  Smac(const Config::SmacOption& opt, Sphy& phy) : opt_(opt), phy_(phy) {
    if (!(0 <= opt_.mac_addr && opt_.mac_addr < 4)) {
      LOG_ERROR("Invalid mac addr {}", opt_.mac_addr);
      throw std::runtime_error("Invalid mac addr");
    }
    LOG_INFO("Smac mac addr {}", opt_.mac_addr);
  }

  awaitable<void> run();
  awaitable<void> listen();

  awaitable<void> tx(Bits bits);
  awaitable<Bits> rx();

  awaitable<void> tx_frame(Frame frame) {
    auto bits = make_frame(frame);
    co_await phy_.tx(bits);
    co_await phy_.tx_finish();
  }

  using TxChannel =
      boost::asio::experimental::channel<void(boost::system::error_code, Bits)>;
  using TxCompChannel =
      boost::asio::experimental::channel<void(boost::system::error_code,
                                              size_t)>;
  using RxChannel =
      boost::asio::experimental::channel<void(boost::system::error_code,
                                              Frame)>;
  using RxDataChannel =
      boost::asio::experimental::channel<void(boost::system::error_code,
                                              Bits)>;

  std::unique_ptr<RxChannel> rx_channel_;
  std::unique_ptr<RxDataChannel> rx_data_channel_;
  std::unique_ptr<TxChannel> tx_channel_;
  std::unique_ptr<TxCompChannel> tx_comp_channel_;

  const Config::SmacOption opt_;
  Sphy& phy_;
};

}  // namespace SuperSonic