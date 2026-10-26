#include "json.hpp"

#include <cctype>
#include <cstdlib>

namespace p64::json {
namespace {

const Value kNull;

struct Parser {
  const std::string& s;
  std::size_t i = 0;

  void skip() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  }

  bool literal(const char* word) {
    std::size_t n = 0;
    while (word[n]) ++n;
    if (s.compare(i, n, word) != 0) return false;
    i += n;
    return true;
  }

  static void put_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool hex4(unsigned& out) {
    if (i + 4 > s.size()) return false;
    out = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s[i++];
      out <<= 4;
      if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
      else return false;
    }
    return true;
  }

  bool string(std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    while (i < s.size()) {
      const char c = s[i++];
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i >= s.size()) return false;
      const char e = s[i++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp = 0;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            i += 2;
            unsigned lo = 0;
            if (!hex4(lo)) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          put_utf8(out, cp);
          break;
        }
        default: return false;
      }
    }
    return false;
  }

  bool value(Value& v, int depth) {
    if (depth > 64) return false;
    skip();
    if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '{') {
      ++i;
      v.type = Value::Type::Object;
      skip();
      if (i < s.size() && s[i] == '}') {
        ++i;
        return true;
      }
      for (;;) {
        skip();
        std::string key;
        if (!string(key)) return false;
        skip();
        if (i >= s.size() || s[i++] != ':') return false;
        if (!value(v.object[key], depth + 1)) return false;
        skip();
        if (i < s.size() && s[i] == ',') {
          ++i;
          continue;
        }
        if (i < s.size() && s[i] == '}') {
          ++i;
          return true;
        }
        return false;
      }
    }
    if (c == '[') {
      ++i;
      v.type = Value::Type::Array;
      skip();
      if (i < s.size() && s[i] == ']') {
        ++i;
        return true;
      }
      for (;;) {
        v.array.emplace_back();
        if (!value(v.array.back(), depth + 1)) return false;
        skip();
        if (i < s.size() && s[i] == ',') {
          ++i;
          continue;
        }
        if (i < s.size() && s[i] == ']') {
          ++i;
          return true;
        }
        return false;
      }
    }
    if (c == '"') {
      v.type = Value::Type::String;
      return string(v.string);
    }
    if (literal("true")) {
      v.type = Value::Type::Bool;
      v.boolean = true;
      return true;
    }
    if (literal("false")) {
      v.type = Value::Type::Bool;
      return true;
    }
    if (literal("null")) return true;
    const char* begin = s.c_str() + i;
    char* end = nullptr;
    v.number = std::strtod(begin, &end);
    if (end == begin) return false;
    v.type = Value::Type::Number;
    i += static_cast<std::size_t>(end - begin);
    return true;
  }
};

}  // namespace

const Value& Value::operator[](const std::string& key) const {
  if (type != Type::Object) return kNull;
  auto it = object.find(key);
  return it == object.end() ? kNull : it->second;
}

const Value& Value::operator[](std::size_t index) const {
  return type == Type::Array && index < array.size() ? array[index] : kNull;
}

bool parse(const std::string& text, Value& out) {
  out = Value();
  Parser p{text};
  if (!p.value(out, 0)) {
    out = Value();
    return false;
  }
  p.skip();
  if (p.i != text.size()) {
    out = Value();
    return false;
  }
  return true;
}

std::string quote(const std::string& text) {
  std::string out = "\"";
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          static const char* hex = "0123456789abcdef";
          out += "\\u00";
          out += hex[c >> 4];
          out += hex[c & 15];
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out + "\"";
}

}  // namespace p64::json
