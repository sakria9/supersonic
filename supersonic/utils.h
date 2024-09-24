#pragma once

#include <optional>
#include "AudioFile.h"

#include "log.h"

template <typename T = float>
void write_wav(const std::string& filename,
               const typename std::vector<float>& samples,
               int sample_rate) {
  if (samples.size() == 0) {
    LOG_ERROR("Error writing to wav file {}, sample count is 0.", filename);
    return;
  }
  AudioFile<T> audio_file;
  audio_file.setAudioBufferSize(1, samples.size());
  audio_file.setSampleRate(sample_rate);
  audio_file.setBitDepth(32);
  audio_file.samples[0] = samples;
  audio_file.save(filename, AudioFileFormat::Wave);
  LOG_INFO("Wrote {} samples to {}", samples.size(), filename);
}

template <typename T = float>
typename std::optional<typename AudioFile<T>::AudioBuffer> read_wav(
    const std::string& filename) {
  AudioFile<float> audioFile;
  bool loaded = audioFile.load(filename);
  if (!loaded) {
    LOG_ERROR("Failed to load audio file.");
    return std::nullopt;
  }
  LOG_INFO("Read audio file: {}", filename);
  LOG_INFO("Bit Depth: {}", audioFile.getBitDepth());
  LOG_INFO("Sample Rate: {}", audioFile.getSampleRate());
  LOG_INFO("Channels: {}", audioFile.getNumChannels());
  LOG_INFO("Samples: {}", audioFile.getNumSamplesPerChannel());
  LOG_INFO("Length in Seconds: {}", audioFile.getLengthInSeconds());
  return audioFile.samples;
}