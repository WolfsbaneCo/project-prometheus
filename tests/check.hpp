// A minimal test harness so the save tooling builds and tests with no downloads.
#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace check {

struct Case {
  const char* name;
  std::function<void()> body;
};

inline std::vector<Case>& cases() {
  static std::vector<Case> all;
  return all;
}

inline int& failures() {
  static int n = 0;
  return n;
}

struct Register {
  Register(const char* name, std::function<void()> body) { cases().push_back({name, std::move(body)}); }
};

inline int run_all() {
  for (const auto& c : cases()) {
    const int before = failures();
    try {
      c.body();
    } catch (const std::exception& e) {
      std::cerr << c.name << ": unexpected exception: " << e.what() << "\n";
      ++failures();
    }
    std::cout << (failures() == before ? "PASS " : "FAIL ") << c.name << "\n";
  }
  std::cout << cases().size() << " tests, " << failures() << " failed checks\n";
  return failures() == 0 ? 0 : 1;
}

}  // namespace check

#define CHECK_CONCAT2(a, b) a##b
#define CHECK_CONCAT(a, b) CHECK_CONCAT2(a, b)
#define TEST_CASE(name)                                                              \
  static void CHECK_CONCAT(test_fn_, __LINE__)();                                    \
  static check::Register CHECK_CONCAT(test_reg_, __LINE__)(name, CHECK_CONCAT(test_fn_, __LINE__)); \
  static void CHECK_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                   \
  do {                                                                                \
    if (!(cond)) {                                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n";     \
      ++check::failures();                                                            \
    }                                                                                 \
  } while (0)

#define CHECK_THROWS(expr)                                                            \
  do {                                                                                \
    bool threw_ = false;                                                              \
    try {                                                                             \
      (void)(expr);                                                                   \
    } catch (...) {                                                                   \
      threw_ = true;                                                                  \
    }                                                                                 \
    if (!threw_) {                                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << ": expected exception: " #expr "\n"; \
      ++check::failures();                                                            \
    }                                                                                 \
  } while (0)
