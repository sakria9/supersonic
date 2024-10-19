#pragma once

#include <jack/jack.h>
#include <boost/lockfree/spsc_queue.hpp>

#include "config.h"
#include "log.h"
#include "utils.h"

namespace SuperSonic {

struct TxTask {
  Samples data;
  size_t played_index;
  std::atomic_flag* completed;
  TxTask(SampleView data, size_t played_index, std::atomic_flag* completed)
      : data(data.begin(), data.end()),
        played_index(played_index),
        completed(completed) {}

  TxTask(SampleView data, std::atomic_flag* completed)
      : TxTask(data, 0, completed) {}
  TxTask(SampleView data, size_t played_index)
      : TxTask(data, played_index, nullptr) {}
  TxTask(SampleView data) : TxTask(data, 0, nullptr) {}
};

using RingBuffer = boost::lockfree::spsc_queue<float>;
using TxRingBuffer = boost::lockfree::spsc_queue<TxTask>;
using RxRingBuffer = RingBuffer;

class Saudio {
 public:
  Saudio(Config::SaudioOption& opt)
      : opt_(opt),
        rx_buffer(opt.ringbuffer_size),
        tx_buffer(opt.ringbuffer_size) {}

  int run();

  ~Saudio();

 private:
  Config::SaudioOption opt_;

  jack_client_t* client_ = nullptr;
  jack_port_t *input_port_ = nullptr, *output_port_ = nullptr;

  RingBuffer log_rx_buffer{kSampleRate * 10};
  RingBuffer log_tx_buffer{kSampleRate * 10};
  std::vector<float> log_rx_buffer_data;
  std::vector<float> log_tx_buffer_data;
  std::jthread log_thread;
  std::atomic_flag stop_log_thread = ATOMIC_FLAG_INIT;

  bool warned_rx_buffer_ = false;

  void* device;

 public:
  RxRingBuffer rx_buffer;
  TxRingBuffer tx_buffer;

 public:
  // this is called in audio thread
  void process_callback(const void* pInput, void* pOutput, uint32_t frameCount);
};

}  // namespace SuperSonic