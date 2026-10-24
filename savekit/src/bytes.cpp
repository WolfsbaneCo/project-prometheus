#include "savekit/bytes.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace savekit {

Bytes swap_words(const Bytes& data) {
  if (data.size() % 4 != 0) {
    throw std::invalid_argument("swap_words: length is not a multiple of 4");
  }
  Bytes out(data.size());
  for (std::size_t i = 0; i < data.size(); i += 4) {
    out[i + 0] = data[i + 3];
    out[i + 1] = data[i + 2];
    out[i + 2] = data[i + 1];
    out[i + 3] = data[i + 0];
  }
  return out;
}

bool is_uniform(const Bytes& data, std::uint8_t value) {
  return std::all_of(data.begin(), data.end(), [value](std::uint8_t b) { return b == value; });
}

Bytes read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open for reading: " + path.string());
  }
  Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) {
    throw std::runtime_error("read failed: " + path.string());
  }
  return data;
}

void write_new_file(const std::filesystem::path& path, const Bytes& data) {
  if (std::filesystem::exists(path)) {
    throw std::runtime_error("refusing to overwrite existing file: " + path.string());
  }
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
      throw std::runtime_error("cannot open for writing: " + path.string());
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) {
      throw std::runtime_error("write failed: " + path.string());
    }
  }
  if (read_file(path) != data) {
    throw std::runtime_error("verification after write failed: " + path.string());
  }
}

std::string to_hex(const std::uint8_t* data, std::size_t size) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    out.push_back(digits[data[i] >> 4]);
    out.push_back(digits[data[i] & 0xF]);
  }
  return out;
}

}  // namespace savekit
