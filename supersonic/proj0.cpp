#include <cxxopts.hpp>

#include "supersonic.h"
#include "utils.h"

using SuperSonic::Saudio;

// Objective 1 (1.5 points): NODE1 should record the TA’s voice for 10 seconds
// and accurately replay the recorded sound.
struct Objective1 {
  void run(Saudio* supersonic, std::atomic_flag& stop_flag) {
    constexpr int seconds = 10;

    // Record the TA's voice for 10 seconds
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<float> record_stream;
    while (1) {
      if (stop_flag.test()) {
        break;
      }
      auto current_time = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> elapsed_time = current_time - start_time;
      if (elapsed_time.count() >= seconds) {
        break;
      }
      supersonic->rx_buffer.consume_all(
          [&](float e) { record_stream.push_back(e); });
    }
    LOG_INFO("Recorded {} samples", record_stream.size());

    // Replay the recorded sound
    std::atomic_flag token;
    supersonic->tx_buffer.push({record_stream, &token});
    while (!token.test() && !stop_flag.test()) {
    }
    LOG_INFO("Replay finished");
  }
};

// Objective 2 (1.5 points): NODE1 must simultaneously play a predefined sound
// wave (e.g., a song) and record the playing sound. The TA may speak during the
// recording. After 10 seconds, the playback and recording should stop. Then,
// NODE1 must accurately replay the recorded sound.
struct Objective2 {
  void run(Saudio* supersonic, std::atomic_flag& stop_flag) {
    // Load the predefined sound wave to vector
    const std::string filePath = "test-audio.wav";
    auto samples = read_wav<float>(filePath);
    if (!samples) {
      LOG_ERROR("Failed to load audio file.");
      return;
    }
    std::vector<float> sample = samples.value()[0];

    constexpr int seconds = 10;

    constexpr int nframes = 128;

    // First 10 seconds: Play the predefined sound wave and record the playing
    std::vector<float> record_stream;
    LOG_INFO("start playing the predefined sound wave");

    auto sample_count =
        std::min((size_t)SuperSonic::kSampleRate * seconds, sample.size());
    std::span<float> sample_span(sample.data(), sample_count);
    std::atomic_flag token;
    supersonic->tx_buffer.push({sample_span, &token});
    while (1) {
      if (token.test() || stop_flag.test()) {
        break;
      }
      // record the playing sound
      for (int i = 0; i < nframes; i++) {
        supersonic->rx_buffer.consume_one(
            [&](float e) { record_stream.push_back(e); });
      }
    }
    LOG_INFO("Recorded {} samples", record_stream.size());

    // Last 10 seconds: Replay the recorded sound
    LOG_INFO("Replaying the recorded sound");
    token.clear();
    supersonic->tx_buffer.push({record_stream, &token});
    while (!token.test() && !stop_flag.test()) {
    }
    LOG_INFO("Replay finished");
  }
};

int main(int argc, char** argv) {
  cxxopts::Options options("supersonic", "Supersonic Project 0");
  // clang-format off
  options.add_options()
    ("h,help", "Print usage")
    ("i,input", "Input sink", cxxopts::value<std::string>()->default_value("system:capture_1"))
    ("o,output", "Output sink", cxxopts::value<std::string>()->default_value("system:playback_1"))
    ("t,task", "Task to run", cxxopts::value<int>(), "1: Objective 1, 2: Objective 2");
  // clang-format on
  auto result = options.parse(argc, argv);

  if (result.count("help") || result.count("task") == 0) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  Saudio::SaudioOption opt;
  opt.input_port = result["input"].as<std::string>();
  opt.output_port = result["output"].as<std::string>();
  opt.ringbuffer_size = 128 * 10;

  auto supersonic = std::make_unique<Saudio>(opt);

  std::atomic_flag stop_flag = ATOMIC_FLAG_INIT;
  std::jthread work_thread;

  if (result["task"].as<int>() == 1) {
    Objective1 obj1;
    work_thread =
        std::jthread([&]() { obj1.run(supersonic.get(), stop_flag); });
  } else if (result["task"].as<int>() == 2) {
    Objective2 obj2;
    work_thread =
        std::jthread([&]() { obj2.run(supersonic.get(), stop_flag); });
  } else {
    LOG_ERROR("Invalid task number");
    return 1;
  }

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
