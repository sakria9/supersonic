#include <iostream>
#include <thread>

#include <fmt/printf.h>
#include <boost/lockfree/spsc_queue.hpp>

#include "jack/jack.h"

#include "AudioFile.h"

int main() {
  jack_status_t status;
  int rc;

  std::cout << "Hello CS120." << std::endl;

  // create a new Jack client
  std::string client_name = "supersonic";
  if (client_name.size() > jack_client_name_size()) {
    std::cerr << "Client name is too long." << std::endl;
    return 1;
  }
  jack_client_t* client =
      jack_client_open("supersonic", JackNullOption, &status);
  if (client == nullptr) {
    std::cerr << "jack_client_open failed, status = " << status << std::endl;
    return 1;
  }

  // register input and output ports
  jack_port_t* input_port = jack_port_register(
      client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  if (input_port == nullptr) {
    std::cerr << "jack_port_register failed." << std::endl;
    return 1;
  }
  jack_port_t* output_port = jack_port_register(
      client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  if (output_port == nullptr) {
    std::cerr << "jack_port_register failed." << std::endl;
    return 1;
  }

  std::atomic_bool stop_flag = false;

  using RingBuffer = boost::lockfree::spsc_queue<float>;
  constexpr size_t buffer_size = 352800;
  // constexpr size_t buffer_size = 128 * 100;
  //  ringbuffer for input
  RingBuffer input_buffer(buffer_size);
  // ringbuffer for output
  RingBuffer output_buffer(buffer_size);

  const std::string filePath = "C:\\Users\\synic\\Downloads\\test-audio.wav";
  AudioFile<float> audioFile;
  bool loaded = audioFile.load(filePath);
  if (!loaded) {
    // std::cerr << "Failed to load audio file." << std::endl;
    return 1;
  }
  fmt::println("Bit Depth: {}", audioFile.getBitDepth());
  fmt::println("Sample Rate: {}", audioFile.getSampleRate());
  fmt::println("Channels: {}", audioFile.getNumChannels());
  fmt::println("Samples: {}", audioFile.getNumSamplesPerChannel());
  fmt::println("Length in Seconds: {}", audioFile.getLengthInSeconds());
  std::vector<float>& sample = audioFile.samples[0];
  std::jthread write_wave([&]() {
    for (auto e : sample) {
      if (stop_flag.load()) {
        return;
      }
      while (not output_buffer.write_available()) {
      }
      auto succ = output_buffer.push(e);
      if (not succ) {
        std::cerr << "Failed to push to output buffer." << std::endl;
        return;
      }
    }
  });

  std::jthread process_input_wave([&]() {
    std::vector<float> stream;
    while (not stop_flag.load()) {
      input_buffer.consume_all([&](float e) { stream.push_back(e); });
    }
    fmt::println("Stream size: {}", stream.size());
    AudioFile<float> audioFile;
    audioFile.setNumChannels(1);
    audioFile.setNumSamplesPerChannel(stream.size());
    audioFile.setSampleRate(48000);
    audioFile.setBitDepth(24);
    audioFile.samples[0] = stream;
    audioFile.save("C:\\Users\\synic\\Downloads\\output.wav");
    fmt::println("Saved output.wav");
  });

  // register a process callback
  struct CallBackCtx {
    jack_port_t* input_port;
    jack_port_t* output_port;
    RingBuffer& input_buffer;
    RingBuffer& output_buffer;
  };
  auto callback_ctx = CallBackCtx{
      .input_port = input_port,
      .output_port = output_port,
      .input_buffer = input_buffer,
      .output_buffer = output_buffer,
  };
  jack_set_process_callback(
      client,
      [](jack_nframes_t nframes, void* arg) {
        auto ctx = static_cast<CallBackCtx*>(arg);
        // read from input port
        auto* input = (jack_default_audio_sample_t*)jack_port_get_buffer(
            ctx->input_port, nframes);
        auto pushed = ctx->input_buffer.push(input, nframes);
        if (pushed != nframes) {
          fmt::println("Expected to push {} frames, but pushed {} frames.",
                       nframes, pushed);
        }

        // write to output port
        auto output = (jack_default_audio_sample_t*)jack_port_get_buffer(
            ctx->output_port, nframes);
        auto writed = ctx->output_buffer.pop(output, nframes);
        if (writed != nframes) {
          //fmt::println("Expected to write {} frames, but wrote {} frames.",
          //             nframes, writed);
        }
        return 0;
      },
      &callback_ctx);

  // activate the client
  if (jack_activate(client)) {
    std::cerr << "jack_activate failed." << std::endl;
    return 1;
  }

  // bind input_port to system:capture_1
  if (jack_connect(client, "system:capture_1", jack_port_name(input_port))) {
    std::cerr << "jack_connect failed." << std::endl;
    return 1;
  }
  // bind output_port to system:playback_1
  if (jack_connect(client, jack_port_name(output_port), "system:playback_1")) {
    std::cerr << "jack_connect failed." << std::endl;
    return 1;
  }

  // keep the client running until the user presses a key
  std::cout << "Press any key to quit." << std::endl;
  std::cin.get();

  // deactivate the client
  if (jack_deactivate(client)) {
    std::cerr << "jack_deactivate failed." << std::endl;
    return 1;
  }

  // unregister the port
  if (jack_port_unregister(client, input_port)) {
    std::cerr << "jack_port_unregister failed." << std::endl;
    return 1;
  }
  if (jack_port_unregister(client, output_port)) {
    std::cerr << "jack_port_unregister failed." << std::endl;
    return 1;
  }

  // close the client
  rc = jack_client_close(client);
  if (rc) {
    std::cerr << "jack_client_close failed, rc = " << rc << std::endl;
    return 1;
  }

  stop_flag = true;
  write_wave.join();
  process_input_wave.join();

  std::cout << "Goodbye CS120." << std::endl;

  return 0;
}
