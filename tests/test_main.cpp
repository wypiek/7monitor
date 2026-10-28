#include <windows.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "platform/wmi.h"
#include "test.h"

namespace smtest {

std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

namespace {
struct Failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};
}  // namespace

void fail(const char* file, int line, const std::string& msg) {
  throw Failure(std::string(file) + ":" + std::to_string(line) + ": " + msg);
}

}  // namespace smtest

int wmain(int argc, wchar_t** argv) {
  std::wstring onlyTag, excludeTag, onlyName;
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    if (a == L"--tag" && i + 1 < argc) onlyTag = argv[++i];
    else if (a == L"--exclude-tag" && i + 1 < argc) excludeTag = argv[++i];
    else onlyName = a;
  }
  sm::ComScope com;
  sm::initComSecurity();
  int passed = 0, failed = 0, skipped = 0;
  for (auto& t : smtest::registry()) {
    std::wstring tag(t.tag, t.tag + std::strlen(t.tag));
    std::wstring name(t.name, t.name + std::strlen(t.name));
    if ((!onlyTag.empty() && tag != onlyTag) || (!excludeTag.empty() && tag == excludeTag) ||
        (!onlyName.empty() && name.find(onlyName) == std::wstring::npos)) {
      ++skipped;
      continue;
    }
    try {
      t.fn();
      ++passed;
      std::printf("[ OK ] %s\n", t.name);
    } catch (const std::exception& e) {
      ++failed;
      std::printf("[FAIL] %s\n       %s\n", t.name, e.what());
    }
    std::fflush(stdout);
  }
  std::printf("\n%d passed, %d failed, %d skipped\n", passed, failed, skipped);
  return failed ? 1 : 0;
}
