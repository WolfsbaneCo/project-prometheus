// A small JSON reader for the relay's messages and Cloudflare's API answers. Reading only; the
// app writes its few JSON bodies by hand.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace p64::json {

struct Value {
  enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string string;
  std::vector<Value> array;
  std::map<std::string, Value> object;

  // Missing keys and wrong types read as null, "" or 0, so lookups chain safely.
  const Value& operator[](const std::string& key) const;
  const Value& operator[](std::size_t index) const;
  std::string str() const { return type == Type::String ? string : std::string(); }
  double num() const { return type == Type::Number ? number : 0; }
  bool truthy() const { return type == Type::Bool ? boolean : type != Type::Null; }
  bool is_null() const { return type == Type::Null; }
};

// Returns false (and leaves `out` null) when `text` is not valid JSON.
bool parse(const std::string& text, Value& out);

// "text" with quotes and escapes, for writing JSON by hand.
std::string quote(const std::string& text);

}  // namespace p64::json
