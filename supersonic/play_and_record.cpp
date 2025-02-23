#include <cxxopts.hpp>

#include "supersonic.h"
#include "utils.h"

using SuperSonic::Saudio;

struct Objective1 {
  void run(std::string wav_file,
           Saudio* supersonic,
           std::atomic_flag& stop_flag) {
    // Load the predefined sound wave to vector
    auto samples = read_wav<float>(wav_file);
    if (!samples) {
      LOG_ERROR("Failed to load audio file.");
      return;
    }
    std::vector<float> sample = samples.value()[0];

    LOG_INFO("start playing the predefined sound wave");
    std::atomic_flag token = ATOMIC_FLAG_INIT;
    supersonic->tx_buffer.push({sample, &token});
    while (!token.test() && !stop_flag.test()) {
    }
    LOG_INFO("Play finished");
  }
};

int main(int argc, char** argv) {
  cxxopts::Options options("supersonic", "Supersonic Project 0");
  // clang-format off
  options.add_options()
    ("h,help", "Print usage")
    ("i,input", "Input sink", cxxopts::value<std::string>()->default_value("system:capture_1"))
    ("o,output", "Output sink", cxxopts::value<std::string>()->default_value("system:playback_1"))
    ("f,file", "Wav file", cxxopts::value<std::string>()->default_value("test-audio.wav"))
    ("ii", "Input index", cxxopts::value<int>()->default_value("0"))
    ("oi", "Output index", cxxopts::value<int>()->default_value("0"));
  // clang-format on
  auto result = options.parse(argc, argv);

  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  SuperSonic::Config::SaudioOption opt;
  if (result.count("ii")) {
    opt.input_port = result["ii"].as<int>();
  } else {
    opt.input_port = result["input"].as<std::string>();
  }
  if (result.count("oi")) {
    opt.output_port = result["oi"].as<int>();
  } else {
    opt.output_port = result["output"].as<std::string>();
  }
  opt.ringbuffer_size = 128 * 100;
  opt.enable_raw_log = true;

  auto supersonic = std::make_unique<Saudio>(opt);

  std::atomic_flag stop_flag = ATOMIC_FLAG_INIT;
  std::jthread work_thread;

  Objective1 obj1;
  work_thread = std::jthread([&]() {
    obj1.run(result["file"].as<std::string>(), supersonic.get(), stop_flag);
  });

  int rc = supersonic->run();
  if (rc) {
    return 1;
  }

  // std::cout << "Press any key to quit." << std::endl;
  // std::cin.get();
  // stop_flag.test_and_set();

  work_thread.join();

  supersonic.reset();

  return 0;
}
