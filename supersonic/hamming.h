#pragma once

#include "utils.h"

namespace SuperSonic {

// Hamming 7,4 code encode and decode

namespace Hamming {

constexpr inline size_t hamming_encoded_length(size_t data_length) {
  return data_length / 4 * 7;
}

inline void encode(BitView data, MutBitView encoded) {
  if (data.size() != 4) {
    LOG_ERROR("Input must be a vector of 4 bits.");
    return;
  }
  if (encoded.size() != 7) {
    LOG_ERROR("Output must be a vector of 7 bits.");
    return;
  }

  // Assign data bits
  encoded[2] = data[0];  // d1
  encoded[4] = data[1];  // d2
  encoded[5] = data[2];  // d3
  encoded[6] = data[3];  // d4

  // Calculate parity bits
  encoded[0] = static_cast<int>(data[0] + data[1] + data[3]) % 2;  // p1
  encoded[1] = static_cast<int>(data[0] + data[2] + data[3]) % 2;  // p2
  encoded[3] = static_cast<int>(data[1] + data[2] + data[3]) % 2;  // p3
}

inline void decode(MutBitView received, MutBitView decoded) {
  if (received.size() != 7) {
    LOG_ERROR("Input must be a vector of 7 bits.");
  }
  if (decoded.size() != 4) {
    LOG_ERROR("Input must be a vector of 4 bits.");
  }

  // Calculate syndrome bits
  int s1 =
      static_cast<int>(received[0] + received[2] + received[4] + received[6]) %
      2;
  int s2 =
      static_cast<int>(received[1] + received[2] + received[5] + received[6]) %
      2;
  int s3 =
      static_cast<int>(received[3] + received[4] + received[5] + received[6]) %
      2;

  // Determine error position
  int error_position = s1 + (s2 << 1) + (s3 << 2);

  // Correct the error if there is one
  if (error_position != 0) {
    received[error_position - 1] = 1 - received[error_position - 1];
  }

  // Extract data bits
  decoded[0] = received[2];  // d1
  decoded[1] = received[4];  // d2
  decoded[2] = received[5];  // d3
  decoded[3] = received[6];  // d4
}

inline Bits hamming_encode(BitView bits) {
  if (bits.size() % 4 != 0) {
    LOG_ERROR("Invalid bits size: {}", bits.size());
    return {};
  }

  Bits encoded;
  encoded.resize(bits.size() / 4 * 7);
  for (size_t i = 0; i < bits.size() / 4; i++) {
    auto data = bits.subspan(i * 4, 4);
    auto encoded_data = MutBitView(encoded.data() + i * 7, 7);
    encode(data, encoded_data);
  }
  return encoded;
}

inline Bits hamming_decode(MutBitView bits) {
  if (bits.size() % 7 != 0) {
    LOG_ERROR("Invalid bits size: {}", bits.size());
    return {};
  }

  Bits decoded;
  decoded.resize(bits.size() / 7 * 4);
  for (size_t i = 0; i < bits.size() / 7; i++) {
    auto received = bits.subspan(i * 7, 7);
    auto decoded_data = MutBitView(decoded.data() + i * 4, 4);
    decode(received, decoded_data);
  }
  return decoded;
}

}  // namespace Hamming

}  // namespace SuperSonic