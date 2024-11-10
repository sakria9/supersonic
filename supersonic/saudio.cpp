#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#include "supersonic.h"

namespace SuperSonic {

static void data_callback(ma_device* pDevice,
                          void* pOutput,
                          const void* pInput,
                          uint32_t frameCount) {
  Saudio* saudio = reinterpret_cast<Saudio*>(pDevice->pUserData);
  saudio->process_callback(pInput, pOutput, frameCount);
}

int Saudio::run() {
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

Saudio::~Saudio() {
  if (device) {
    ma_device_uninit((ma_device*)device);
    delete (ma_device*)device;
  }

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
    if (warned_rx_buffer_ == false) {
      LOG_WARN("Rx buffer is full. Going to reset RX buffer.");
      warned_rx_buffer_ = true;
    }
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
