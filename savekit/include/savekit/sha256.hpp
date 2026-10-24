#pragma once

#include <string>

#include "savekit/bytes.hpp"

namespace savekit {

// Lowercase hex SHA-256 of `data`. Used for change detection and info.json checksums.
std::string sha256_hex(const Bytes& data);

}  // namespace savekit
