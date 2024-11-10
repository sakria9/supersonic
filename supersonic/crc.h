#pragma once

#include "utils.h"

namespace SuperSonic {

Bits crc16(BitView bits);
bool validate_crc16(BitView bits);

}  // namespace SuperSonic
