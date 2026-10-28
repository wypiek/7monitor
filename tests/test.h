#pragma once
// Minimal self-contained test framework (no external dependencies).
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace smtest {

struct TestCase {
  const char* name;
  const char* tag;  // "unit" or "live" (live tests read the real machine)
  std::function<void()> fn;
};

std::vector<TestCase>& registry();
void fail(const char* file, int line, const std::string& msg);

struct Registrar {
  Registrar(const char* name, const char* tag, std::function<void()> fn) { registry().push_back({name, tag, fn}); }
};

template <typename A, typename B>
std::string describe(const A& a, const B& b) {
  std::ostringstream os;
  os << "expected [" << b << "] got [" << a << "]";
  return os.str();
}

}  // namespace smtest

#define SM_CAT2(a, b) a##b
#define SM_CAT(a, b) SM_CAT2(a, b)
#define TEST_TAGGED(name, tag)                                                         \
  static void SM_CAT(test_fn_, name)();                                                \
  static smtest::Registrar SM_CAT(test_reg_, name)(#name, tag, SM_CAT(test_fn_, name)); \
  static void SM_CAT(test_fn_, name)()
#define TEST(name) TEST_TAGGED(name, "unit")
#define LIVE_TEST(name) TEST_TAGGED(name, "live")

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) smtest::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
  } while (0)
#define CHECK_EQ(a, b)                                                                                 \
  do {                                                                                                 \
    auto _va = (a);                                                                                    \
    auto _vb = (b);                                                                                    \
    if (!(_va == _vb)) smtest::fail(__FILE__, __LINE__, #a " == " #b ": " + smtest::describe(_va, _vb)); \
  } while (0)
#define CHECK_NEAR(a, b, eps)                                                                     \
  do {                                                                                            \
    double _va = (a), _vb = (b);                                                                  \
    if (!(std::fabs(_va - _vb) <= (eps)))                                                         \
      smtest::fail(__FILE__, __LINE__, #a " ~= " #b ": " + smtest::describe(_va, _vb));           \
  } while (0)
