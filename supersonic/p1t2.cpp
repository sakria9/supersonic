#include <cxxopts.hpp>

#include "supersonic.h"

// Play sine wave
struct Objective1 {
  void run(SuperSonic* supersonic, std::atomic_flag& stop_flag) {
    constexpr float freq = 1000.0;
    constexpr float amplitude = 0.5;
    constexpr float phase = 0.0;
    auto sine_wave = [&freq, &amplitude, &phase](int i) {
      float t = (float)i / kSampleRate;
      return amplitude * std::sin(2 * M_PI * 1000 * t) +
             amplitude * std::sin(2 * M_PI * 10000 * t);
    };

    // Play the sine wave
    size_t play_index = 0;
    while (1) {
      if (stop_flag.test()) {
        break;
      }
      int nframes = 128;
      for (int i = 0; i < nframes; i++) {
        if (supersonic->tx_buffer.write_available()) {
          supersonic->tx_buffer.push(sine_wave(play_index++));
          play_index %= 48;
        }
      }
    }
  }
};

int main(int argc, char** argv) {
  cxxopts::Options options("supersonic", "Supersonic Project 1 Task 2");
  // clang-format off
  options.add_options()
    ("h,help", "Print usage")
    ("i,input", "Input sink", cxxopts::value<std::string>()->default_value("system:capture_1"))
    ("o,output", "Output sink", cxxopts::value<std::string>()->default_value("system:playback_1"));
  // clang-format on
  auto result = options.parse(argc, argv);

  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  SuperSonic::SuperSonicOption opt;
  opt.input_port = result["input"].as<std::string>();
  opt.output_port = result["output"].as<std::string>();
  opt.ringbuffer_size = 128 * 50;
  opt.enable_raw_log = true;

  auto supersonic = std::make_unique<SuperSonic>(opt);

  std::atomic_flag stop_flag = ATOMIC_FLAG_INIT;
  std::jthread work_thread;

  Objective1 obj1;
  work_thread = std::jthread([&]() { obj1.run(supersonic.get(), stop_flag); });

  int rc = supersonic->run();
  if (rc) {
    return 1;
  }

  std::cout << "Press any key to quit." << std::endl;
  std::cin.get();
  stop_flag.test_and_set();

  work_thread.join();

  supersonic.reset();

  return 0;
}
