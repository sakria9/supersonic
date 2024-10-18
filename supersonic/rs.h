#pragma once

#include <cstdint>
#include <utility>
#include <galois_field.hh>
#include <reed_solomon_decoder.hh>
#include <reed_solomon_encoder.hh>

#include "utils.h"

namespace SuperSonic {

// Hamming 7,4 code encode and decode

template <int N, int K, int Width>
class RS {
 public:
  static_assert(Width == 4 || Width == 8, "Width must be 4 or 8.");

  static constexpr int RS_N = N;
  static constexpr int RS_K = K;
  static_assert((RS_N - RS_K) % 2 == 0);
  static constexpr int RS_T = (N - K) / 2;
  static constexpr int RS_WIDTH = Width;
  static constexpr int PAYLOAD_SIZE = RS_K * RS_WIDTH;

  static constexpr int POLY =
      Width == 4 ? 0b10011 : (Width == 8 ? 0b100011101 : 0);

  typedef CODE::GaloisField<Width, POLY, uint8_t> GF;
  typedef CODE::ReedSolomonEncoder<RS_T * 2, 0, GF> RS_ENC;
  typedef CODE::ReedSolomonDecoder<RS_T * 2, 0, GF> RS_DEC;
  GF instance;
  RS_ENC rs_encoder;
  RS_DEC rs_decoder;
  static_assert(RS_ENC::N == RS_N);
  static_assert(RS_ENC::K == RS_K);
  static_assert(RS_DEC::N == RS_N);
  static_assert(RS_DEC::K == RS_K);

  static inline void bitsToGF(BitView bits, unsigned char* bytes) {
    for (size_t i = 0; i < bits.size() / RS_WIDTH; i++) {
      bytes[i] = 0;
      for (size_t j = 0; j < RS_WIDTH; j++) {
        bytes[i] |= bits[i * RS_WIDTH + j] << j;
      }
    }
  }
  static inline void GFToBits(unsigned char* bytes, MutBitView bits) {
    for (size_t i = 0; i < bits.size() / RS_WIDTH; i++) {
      for (size_t j = 0; j < RS_WIDTH; j++) {
        bits[i * RS_WIDTH + j] = (bytes[i] >> j) & 1;
      }
    }
  }

  inline Bits encode(BitView bits) {
    if (bits.size() != PAYLOAD_SIZE) {
      LOG_ERROR("Input must be a vector of {} bits.", RS_K);
      return {};
    }

    typename RS_ENC::value_type message[RS_N];
    bitsToGF(bits, message);
    rs_encoder(message, message + RS_K);
    Bits encoded(RS_N * RS_WIDTH);
    GFToBits(message, encoded);

    return encoded;
  }

  inline Bits encode_many(BitView bits) {
    Bits raw(PAYLOAD_SIZE);
    Bits encoded;
    for (size_t i = 0; i < bits.size(); i += PAYLOAD_SIZE) {
      // Copy the bits to raw, and pad with 0 if necessary
      size_t j = 0;
      for (; j < PAYLOAD_SIZE && i + j < bits.size(); j++) {
        raw[j] = bits[i + j];
      }
      for (; j < PAYLOAD_SIZE; j++) {
        raw[j] = 0;
      }
      auto encoded_part = encode(raw);
      encoded.insert(encoded.end(), encoded_part.begin(), encoded_part.end());
    }
    return encoded;
  }

  inline static size_t encoded_size(size_t bits_size) {
    return (bits_size + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE * RS_N * RS_WIDTH;
  }

  inline Bits decode(MutBitView received) {
    typename RS_DEC::value_type message[RS_N];
    bitsToGF(received, message);
    rs_decoder(message, message + RS_K);
    Bits decoded(RS_K * RS_WIDTH);
    GFToBits(message, decoded);
    return decoded;
  }

  inline Bits decode_many(MutBitView received) {
    if (received.size() % (RS_N * RS_WIDTH) != 0) {
      LOG_ERROR("Invalid bits size: {}", received.size());
      return {};
    }
    Bits decoded;
    for (size_t i = 0; i < received.size(); i += RS_N * RS_WIDTH) {
      Bits received_part(received.begin() + i,
                         received.begin() + i + RS_N * RS_WIDTH);
      auto decoded_part = decode(received_part);
      decoded.insert(decoded.end(), decoded_part.begin(), decoded_part.end());
    }
    return decoded;
  }
};

using RS1511 = RS<15, 11, 4>;
using RS255223 = RS<255, 223, 8>;

}  // namespace SuperSonic