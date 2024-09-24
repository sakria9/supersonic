#pragma once

#include <jack/jack.h>
#include <boost/lockfree/spsc_queue.hpp>

#include "log.h"
#include "utils.h"

using RingBuffer = boost::lockfree::spsc_queue<float>;

constexpr int kSampleRate = 48000;

class SuperSonic {
 public:
  struct SuperSonicOption {
    std::string client_name = "supersonic";

    std::string input_sink = "system:capture_1";
    std::string output_sink = "system:playback_1";

    size_t ringbuffer_size = kSampleRate * 5;
  };
  SuperSonic(SuperSonicOption& opt)
      : opt_(opt),
        input_buffer(opt.ringbuffer_size),
        output_buffer(opt.ringbuffer_size) {}

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

    log_thread = std::jthread([this]() {
      while (!stop_log_thread.test()) {
        log_input_buffer.consume_one(
            [&](float e) { log_input_buffer_data.push_back(e); });
        log_output_buffer.consume_one(
            [&](float e) { log_output_buffer_data.push_back(e); });
      }
    });

    // activate the client
    if (jack_activate(client_)) {
      LOG_ERROR("jack_activate failed.");
      return 1;
    }

    // bind input_port to system:capture_1
    if (jack_connect(client_, opt_.input_sink.c_str(),
                     jack_port_name(input_port_))) {
      LOG_ERROR("jack_connect failed.");
      return 1;
    }
    // bind output_port to system:playback_1
    if (jack_connect(client_, jack_port_name(output_port_),
                     opt_.output_sink.c_str())) {
      LOG_ERROR("jack_connect failed.");
      return 1;
    }

    return 0;
  }

  ~SuperSonic() {
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

      stop_log_thread.test_and_set();
      log_thread.join();
      // write to wav file
      write_wav("raw_input.wav", log_input_buffer_data, kSampleRate);
      write_wav("raw_output.wav", log_output_buffer_data, kSampleRate);
    }
  }

 private:
  SuperSonicOption opt_;

  jack_client_t* client_ = nullptr;
  jack_port_t *input_port_ = nullptr, *output_port_ = nullptr;

  RingBuffer log_input_buffer{kSampleRate * 10};
  RingBuffer log_output_buffer{kSampleRate * 10};
  std::vector<float> log_input_buffer_data;
  std::vector<float> log_output_buffer_data;
  std::jthread log_thread;
  std::atomic_flag stop_log_thread = ATOMIC_FLAG_INIT;

 public:
  RingBuffer input_buffer, output_buffer;

 private:
  bool warned_input_buffer_ = false, warned_output_buffer_ = false;

  // this is called in jack thread
  static int jack_process_callback_handler(jack_nframes_t nframes, void* arg) {
    return reinterpret_cast<SuperSonic*>(arg)->process_callback(nframes);
  }
  // this is called in jack thread
  int process_callback(jack_nframes_t nframes) {
    // read from input port
    auto* input = (jack_default_audio_sample_t*)jack_port_get_buffer(
        input_port_, nframes);
    auto pushed = input_buffer.push(input, nframes);
    if (pushed != nframes) {
      if (!warned_input_buffer_) {
        LOG_WARN(
            "Push input_buffer failed. Expected to push {} frames, but pushed "
            "{} frames.",
            nframes, pushed);
        warned_input_buffer_ = true;
      }
    } else {
      warned_input_buffer_ = false;
    }

    if (log_input_buffer.push(input, nframes) != nframes) {
      LOG_ERROR("log_input_buffer.push failed.");
    }

    // write to output port
    auto output = (jack_default_audio_sample_t*)jack_port_get_buffer(
        output_port_, nframes);
    auto wrote = output_buffer.pop(output, nframes);
    if (wrote != nframes) {
      // fill 0 if not enough data
      std::fill(output + wrote, output + nframes, .0f);
      if (!warned_output_buffer_) {
        LOG_WARN(
            "Write output failed. Expected to write {} frames, but wrote {} "
            "frames.",
            nframes, wrote);
        warned_output_buffer_ = true;
      }
    } else {
      warned_output_buffer_ = false;
    }

    if (log_output_buffer.push(output, nframes) != nframes) {
      LOG_ERROR("log_output_buffer.push failed.");
    }

    return 0;
  }
};
