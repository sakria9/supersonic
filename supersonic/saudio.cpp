// #define USE_MA

#ifdef USE_MA
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#endif

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include "supersonic.h"

namespace SuperSonic {

int Saudio::run() {
#ifdef USE_MA
  return run_ma();
#else
  return run_jack();
#endif
}

#ifdef USE_MA
static void data_callback(ma_device* pDevice,
                          void* pOutput,
                          const void* pInput,
                          uint32_t frameCount) {
  Saudio* saudio = reinterpret_cast<Saudio*>(pDevice->pUserData);
  saudio->process_callback(pInput, pOutput, frameCount);
}

int Saudio::run_ma() {
  ma_result result;

  LOG_INFO("supersonic::run");

  ma_context context;
  ma_device_info* pPlaybackDeviceInfos;
  ma_uint32 playbackDeviceCount;
  ma_device_info* pCaptureDeviceInfos;
  ma_uint32 captureDeviceCount;
  if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
    LOG_ERROR("Failed to initialize context.");
    return -2;
  }

  result = ma_context_get_devices(&context, &pPlaybackDeviceInfos,
                                  &playbackDeviceCount, &pCaptureDeviceInfos,
                                  &captureDeviceCount);
  if (result != MA_SUCCESS) {
    LOG_ERROR("Failed to retrieve device information.");
    return -3;
  }

  LOG_INFO("Playback Devices");
  for (ma_uint32 iDevice = 0; iDevice < playbackDeviceCount; ++iDevice) {
    LOG_INFO("    {}: {}", iDevice, pPlaybackDeviceInfos[iDevice].name);
  }

  LOG_INFO("Capture Devices");
  for (ma_uint32 iDevice = 0; iDevice < captureDeviceCount; ++iDevice) {
    LOG_INFO("    {}: {}", iDevice, pCaptureDeviceInfos[iDevice].name);
  }

  ma_device_config deviceConfig;
  deviceConfig = ma_device_config_init(ma_device_type_duplex);
  deviceConfig.playback.format = ma_format_f32;
  deviceConfig.playback.channels = 1;
  deviceConfig.capture.format = ma_format_f32;
  deviceConfig.capture.channels = 1;
  deviceConfig.sampleRate = kSampleRate;
  deviceConfig.dataCallback = data_callback;
  deviceConfig.pUserData = this;

  bool found_output = false;
  if (opt_.output_port.index() == 1) {
    if (std::get<int>(opt_.output_port) >= playbackDeviceCount ||
        std::get<int>(opt_.output_port) < 0) {
      LOG_ERROR("Output port index out of range.");
      throw std::runtime_error("Output port index out of range.");
    }
    auto& dev = pPlaybackDeviceInfos[std::get<int>(opt_.output_port)];
    deviceConfig.playback.pDeviceID = &dev.id;
    LOG_INFO("Output port: {}", dev.name);
    found_output = true;
  } else {
    auto output_port = std::get<std::string>(opt_.output_port);
    for (ma_uint32 iDevice = 0; iDevice < playbackDeviceCount; ++iDevice) {
      auto& dev = pPlaybackDeviceInfos[iDevice];
      if (std::string(dev.name).contains(output_port)) {
        deviceConfig.playback.pDeviceID = &dev.id;
        LOG_INFO("Output port: {} {}", iDevice, dev.name);
        found_output = true;
        break;
      } else {
        LOG_INFO("{} != {}", dev.name, output_port);
      }
    }
  }

  bool found_input = false;
  if (opt_.input_port.index() == 1) {
    if (std::get<int>(opt_.input_port) >= captureDeviceCount ||
        std::get<int>(opt_.input_port) < 0) {
      LOG_ERROR("Input port index out of range.");
      throw std::runtime_error("Input port index out of range.");
    }
    auto& dev = pCaptureDeviceInfos[std::get<int>(opt_.input_port)];
    deviceConfig.capture.pDeviceID = &dev.id;
    LOG_INFO("Input port: {}", dev.name);
    found_input = true;
  } else {
    auto input_port = std::get<std::string>(opt_.input_port);
    for (ma_uint32 iDevice = 0; iDevice < captureDeviceCount; ++iDevice) {
      auto& dev = pCaptureDeviceInfos[iDevice];
      if (std::string(dev.name).contains(input_port)) {
        deviceConfig.capture.pDeviceID = &dev.id;
        LOG_INFO("Input port: {} {}", iDevice, dev.name);
        found_input = true;
        break;
      } else {
        LOG_INFO("{} != {}", dev.name, input_port);
      }
    }
  }
  if (!found_output) {
    LOG_ERROR("Output port not found.");
    throw std::runtime_error("Output port not found.");
  }
  if (!found_input) {
    LOG_ERROR("Input port not found.");
    throw std::runtime_error("Input port not found.");
  }

  device = new ma_device;

  result = ma_device_init(NULL, &deviceConfig, (ma_device*)device);
  if (result != MA_SUCCESS) {
    LOG_ERROR("Failed to initialize playback device.");
    throw std::runtime_error("Failed to initialize playback device.");
  }

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

  result = ma_device_start((ma_device*)device);
  if (result != MA_SUCCESS) {
    LOG_ERROR("Failed to start playback device.");
    ma_device_uninit((ma_device*)device);
    throw std::runtime_error("Failed to start playback device.");
  }

  return 0;
}
#endif

#ifndef USE_MA
int Saudio::jack_process_callback_handler(jack_nframes_t nframes, void* arg) {
  auto saudio = reinterpret_cast<Saudio*>(arg);

  auto rx = (jack_default_audio_sample_t*)jack_port_get_buffer(
      saudio->input_port_, nframes);

  auto tx = (jack_default_audio_sample_t*)jack_port_get_buffer(
      saudio->output_port_, nframes);

  saudio->process_callback(rx, tx, nframes);
  return 0;
}

int Saudio::run_jack() {
  jack_status_t status;

  LOG_INFO("supersonic::run");

  // create a new Jack client
  if (opt_.client_name.size() > (size_t)jack_client_name_size()) {
    LOG_ERROR("Client name is too long.");
    throw std::runtime_error("Client name is too long.");
  }
  client_ = jack_client_open(opt_.client_name.c_str(), JackNullOption, &status);
  if (client_ == nullptr) {
    LOG_ERROR("jack_client_open failed, status = {}", (int)status);
    throw std::runtime_error("jack_client_open failed.");
  }

  // List all playback ports
  printf("Playback Ports:\n");
  const char** playback_ports =
      jack_get_ports(client_, NULL, NULL, JackPortIsOutput);
  if (playback_ports) {
    for (int i = 0; playback_ports[i] != NULL; i++) {
      printf("  %s\n", playback_ports[i]);
    }
    jack_free(playback_ports);
  } else {
    printf("No playback ports found.\n");
  }

  // List all capture ports
  printf("\nCapture Ports:\n");
  const char** capture_ports =
      jack_get_ports(client_, NULL, NULL, JackPortIsInput);
  if (capture_ports) {
    for (int i = 0; capture_ports[i] != NULL; i++) {
      printf("  %s\n", capture_ports[i]);
    }
    jack_free(capture_ports);
  } else {
    printf("No capture ports found.\n");
  }

  // register input and output ports
  input_port_ = jack_port_register(client_, "input", JACK_DEFAULT_AUDIO_TYPE,
                                   JackPortIsInput, 0);
  if (input_port_ == nullptr) {
    LOG_ERROR("jack_port_register failed.");
    throw std::runtime_error("jack_port_register failed.");
  }
  output_port_ = jack_port_register(client_, "output", JACK_DEFAULT_AUDIO_TYPE,
                                    JackPortIsOutput, 0);
  if (output_port_ == nullptr) {
    LOG_ERROR("jack_port_register failed.");
    throw std::runtime_error("jack_port_register failed.");
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
  if (opt_.input_port.index() != 0) {
    LOG_ERROR("Invalid input_port.");
    throw std::runtime_error("Invalid input_port.");
  }
  auto capture = std::get<0>(opt_.input_port);
  if (jack_connect(client_, capture.c_str(), jack_port_name(input_port_))) {
    LOG_ERROR("jack_connect failed.");
    throw std::runtime_error("jack_connect failed.");
  }

  // bind output_port to system:playback_1
  if (opt_.output_port.index() != 0) {
    LOG_ERROR("Invalid output_port.");
    throw std::runtime_error("Invalid output_port.");
  }
  auto playback = std::get<0>(opt_.output_port);
  if (jack_connect(client_, jack_port_name(output_port_), playback.c_str())) {
    LOG_ERROR("jack_connect failed.");
    throw std::runtime_error("jack_connect failed.");
  }

  return 0;
}
#endif

Saudio::~Saudio() {
#ifdef USE_MA
  if (device) {
    ma_device_uninit((ma_device*)device);
    delete (ma_device*)device;
  }
#endif

#ifndef USE_MA
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
  }
#endif

  if (opt_.enable_raw_log) {
    stop_log_thread.test_and_set();
    log_thread.join();
    // write to wav file
    write_wav("raw_input.wav", log_rx_buffer_data, kSampleRate);
    write_wav("raw_output.wav", log_tx_buffer_data, kSampleRate);
  }
}

void Saudio::process_callback(const void* pInput,
                              void* pOutput,
                              uint32_t frameCount) {
  auto rx = (const float*)pInput;
  auto tx = (float*)pOutput;

  if (rx_buffer.write_available() < frameCount) {
    LOG_WARN("Rx buffer is full. Going to reset RX buffer.");
    rx_buffer.consume_all([](float) {});
  }
  auto pushed = rx_buffer.push(rx, frameCount);
  if (pushed != frameCount) {
    LOG_ERROR(
        "rx_buffer.push failed. Expected to push {}, but pushed {}."
        "This should not happen.",
        frameCount, pushed);
  }

  if (opt_.enable_raw_log) {
    if (log_rx_buffer.push(rx, frameCount) != frameCount) {
      LOG_ERROR("log_rx_buffer.push failed.");
    }
  }

  size_t wrote = 0;
  while (wrote < frameCount && tx_buffer.read_available()) {
    auto& task = tx_buffer.front();
    auto& data = task.data;
    auto& played_index = task.played_index;
    auto to_play = std::min(frameCount - wrote, data.size() - played_index);
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
  std::fill(tx + wrote, tx + frameCount, .0f);

  if (opt_.enable_raw_log) {
    if (log_tx_buffer.push(tx, frameCount) != frameCount) {
      LOG_ERROR("log_tx_buffer.push failed.");
    }
  }
}

}  // namespace SuperSonic
