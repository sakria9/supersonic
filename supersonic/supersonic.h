#pragma once

#include <jack/jack.h>
#include <boost/lockfree/spsc_queue.hpp>

#include "log.h"
#include "utils.h"

namespace SuperSonic {

struct TxTask {
  std::span<const float> data;
  size_t played_index;
  std::atomic_flag* completed;
  TxTask(std::span<const float> data,
         size_t played_index,
         std::atomic_flag* completed)
      : data(data), played_index(played_index), completed(completed) {}
  TxTask(std::span<const float> data, std::atomic_flag* completed)
      : data(data), played_index(0), completed(completed) {}
  TxTask(std::span<const float> data, size_t played_index)
      : data(data), played_index(played_index), completed(nullptr) {}
  TxTask(std::span<const float> data)
      : data(data), played_index(0), completed(nullptr) {}
};

using RingBuffer = boost::lockfree::spsc_queue<float>;
using TxRingBuffer = boost::lockfree::spsc_queue<TxTask>;
using RxRingBuffer = RingBuffer;

constexpr int kSampleRate = 48000;

class Saudio {
 public:
  struct SaudioOption {
    std::string client_name = "supersonic";

    std::string input_port = "system:capture_1";
    std::string output_port = "system:playback_1";

    size_t ringbuffer_size = kSampleRate * 5;

    bool enable_raw_log = true;
  };
  Saudio(SaudioOption& opt)
      : opt_(opt),
        rx_buffer(opt.ringbuffer_size),
        tx_buffer(opt.ringbuffer_size) {}

  int run() {
    jack_status_t status;

    LOG_INFO("supersonic::run");

    // create a new Jack client
    if (opt_.client_name.size() > jack_client_name_size()) {
      LOG_ERROR("Client name is too long.");
      return 1;
    }
    client_ =
        jack_client_open(opt_.client_name.c_str(), JackNullOption, &status);
    if (client_ == nullptr) {
      // LOG_ERROR("jack_client_open failed, status = {}", status);
      return 1;
    }

    // register input and output ports
    input_port_ = jack_port_register(client_, "input", JACK_DEFAULT_AUDIO_TYPE,
                                     JackPortIsInput, 0);
    if (input_port_ == nullptr) {
      LOG_ERROR("jack_port_register failed.");
      return 1;
    }
    output_port_ = jack_port_register(
        client_, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (output_port_ == nullptr) {
      LOG_ERROR("jack_port_register failed.");
      return 1;
    }

    // register a process callback
    jack_set_process_callback(client_, jack_process_callback_handler, this);

    if (opt_.enable_raw_log) {
      log_thread = std::jthread([this]() {
        while (!stop_log_thread.test()) {
          log_rx_buffer.consume_one(
              [&](float e) { log_rx_buffer_data.push_back(e); });
          log_tx_buffer.consume_one(
              [&](float e) { log_tx_buffer_data.push_back(e); });
        }
      });
    }

    // activate the client
    if (jack_activate(client_)) {
      LOG_ERROR("jack_activate failed.");
      return 1;
    }

    // bind input_port to system:capture_1
    if (jack_connect(client_, opt_.input_port.c_str(),
                     jack_port_name(input_port_))) {
      LOG_ERROR("jack_connect failed.");
      return 1;
    }
    // bind output_port to system:playback_1
    if (jack_connect(client_, jack_port_name(output_port_),
                     opt_.output_port.c_str())) {
      LOG_ERROR("jack_connect failed.");
      return 1;
    }

    return 0;
  }

  ~Saudio() {
    if (client_) {
      int rc;
      // deactivate the client
      rc = jack_deactivate(client_);
      if (rc) {
        LOG_ERROR("jack_deactivate failed, rc = {}", rc);
      }

      // unregister the port
      if (input_port_) {
        rc = jack_port_unregister(client_, input_port_);
        if (rc) {
          LOG_ERROR("jack_port_unregister failed, rc = {}", rc);
        }
      }
      if (output_port_) {
        rc = jack_port_unregister(client_, output_port_);
        if (rc) {
          LOG_ERROR("jack_port_unregister failed, rc = {}", rc);
        }
      }

      // close the client
      rc = jack_client_close(client_);
      if (rc) {
        LOG_ERROR("jack_client_close failed, rc = {}", rc);
      }

      if (opt_.enable_raw_log) {
        stop_log_thread.test_and_set();
        log_thread.join();
        // write to wav file
        write_wav("raw_input.wav", log_rx_buffer_data, kSampleRate);
        write_wav("raw_output.wav", log_tx_buffer_data, kSampleRate);
      }
    }
  }

 private:
  SaudioOption opt_;

  jack_client_t* client_ = nullptr;
  jack_port_t *input_port_ = nullptr, *output_port_ = nullptr;

  RingBuffer log_rx_buffer{kSampleRate * 10};
  RingBuffer log_tx_buffer{kSampleRate * 10};
  std::vector<float> log_rx_buffer_data;
  std::vector<float> log_tx_buffer_data;
  std::jthread log_thread;
  std::atomic_flag stop_log_thread = ATOMIC_FLAG_INIT;

 public:
  RxRingBuffer rx_buffer;
  TxRingBuffer tx_buffer;

 private:
  bool warned_tx_buffer_ = false;

  // this is called in jack thread
  static int jack_process_callback_handler(jack_nframes_t nframes, void* arg) {
    return reinterpret_cast<Saudio*>(arg)->process_callback(nframes);
  }
  // this is called in jack thread
  int process_callback(jack_nframes_t nframes) {
    // read from input port
    auto rx = (jack_default_audio_sample_t*)jack_port_get_buffer(input_port_,
                                                                 nframes);
    if (rx_buffer.write_available() < nframes) {
      auto to_drop = nframes - rx_buffer.write_available();
      LOG_WARN("Rx buffer is full. Going to reset RX buffer.");
      rx_buffer.consume_all([](float e) {});
    }
    auto pushed = rx_buffer.push(rx, nframes);
    if (pushed != nframes) {
      LOG_ERROR(
          "rx_buffer.push failed. Expected to push {}, but pushed {}."
          "This should not happen.",
          nframes, pushed);
    }

    if (opt_.enable_raw_log) {
      if (log_rx_buffer.push(rx, nframes) != nframes) {
        LOG_ERROR("log_rx_buffer.push failed.");
      }
    }

    // write to output port
    auto tx = (jack_default_audio_sample_t*)jack_port_get_buffer(output_port_,
                                                                 nframes);

    size_t wrote = 0;
    while (wrote < nframes && tx_buffer.read_available()) {
      auto& task = tx_buffer.front();
      auto& data = task.data;
      auto& played_index = task.played_index;
      auto to_play = std::min(nframes - wrote, data.size() - played_index);
      std::copy(data.begin() + played_index,
                data.begin() + played_index + to_play, tx + wrote);
      wrote += to_play;
      played_index += to_play;
      if (played_index == data.size()) {
        tx_buffer.pop();
        if (task.completed != nullptr) {
          task.completed->test_and_set();
        }
      }
    }
    if (wrote != nframes) {
      // fill 0 if not enough data
      std::fill(tx + wrote, tx + nframes, .0f);
      if (!warned_tx_buffer_) {
        LOG_WARN(
            "Write TX failed. Expected to write {} frames, but wrote {} "
            "frames.",
            nframes, wrote);
        warned_tx_buffer_ = true;
      }
    } else {
      warned_tx_buffer_ = false;
    }

    if (opt_.enable_raw_log) {
      if (log_tx_buffer.push(tx, nframes) != nframes) {
        LOG_ERROR("log_tx_buffer.push failed.");
      }
    }

    return 0;
  }
};

}  // namespace SuperSonic