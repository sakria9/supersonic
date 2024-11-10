#include <boost/crc.hpp>

#include "crc.h"
#include "utils.h"

namespace SuperSonic {

static uint16_t calc_crc16(BitView bits) {
  // bits[0] represents the first bit of the data
  // bits[1] represents the second bit of the data
  // ...
  // boost only supports byte-wise crc calculation
  // so we need to convert bits to bytes
  Bytes bytes((bits.size() + 7) / 8);
  for (size_t i = 0; i < bits.size(); i++) {
    bytes[i / 8] |= bits[i] << (i % 8);
  }
  boost::crc_16_type result;
  result.process_bytes(bytes.data(), bytes.size());
  return result.checksum();
}

Bits crc16(BitView bits) {
  // bits[0] represents the first bit of the data
  // bits[1] represents the second bit of the data
  // ...
  // return bits + crc
  auto crc = calc_crc16(bits);
  Bits crc_bits(16);
  for (int i = 0; i < 16; i++) {
    crc_bits[i] = (crc >> i) & 1;
  }
  return SuperSonic::Signal::concatenate(bits, crc_bits);
}
bool validate_crc16(BitView bits) {
  // bits[0] represents the first bit of the data
  // bits[1] represents the second bit of the data
  // ...
  // the last 16 bits are crc
  // return true if crc is correct
  auto crc = calc_crc16(bits.subspan(0, bits.size() - 16));
  uint16_t recv_crc = 0;
  for (int i = 0; i < 16; i++) {
    recv_crc |= bits[bits.size() - 16 + i] << i;
  }
  return crc == recv_crc;
}

}  // namespace SuperSonic
