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

#include "config.h"
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

class Tun {
 public:
  Tun(Config::TunOption& tun_option);
  awaitable<void> init();
  void ReceivePackets();
  ~Tun(){};

  using Channel =
      boost::asio::experimental::channel<void(boost::system::error_code, Bits)>;

  awaitable<Bits> tx() {
    co_return (co_await TxFromSystem->async_receive(use_awaitable));
  };

  void rx(BitView);

  // system want to send a packet
  std::unique_ptr<Channel> TxFromSystem;
};

}  // namespace SuperSonic