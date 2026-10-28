#pragma once
// Thin Win32 helpers: RAII handles, safe system-DLL loading, registry access and
// error text. Nothing here depends on APIs newer than Windows 7 SP1.
#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sm {
namespace win {

class Handle {
 public:
  Handle() = default;
  explicit Handle(HANDLE h) : h_(h) {}
  ~Handle() { reset(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
  Handle& operator=(Handle&& o) noexcept {
    if (this != &o) {
      reset();
      h_ = o.h_;
      o.h_ = nullptr;
    }
    return *this;
  }
  HANDLE get() const { return h_; }
  bool valid() const { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }
  void reset(HANDLE h = nullptr) {
    if (valid()) CloseHandle(h_);
    h_ = h;
  }

 private:
  HANDLE h_ = nullptr;
};

// %windir%\System32 (the native one for 64-bit processes, SysWOW64 view for 32-bit).
std::wstring systemDirectory();

// Loads a DLL from the system directory by full path (never from the current
// directory or PATH). Returns nullptr when absent. Used for every optional API.
HMODULE loadSystemLibrary(const wchar_t* fileName);
// Loads a DLL from an absolute path (vendor libraries in Program Files).
HMODULE loadLibraryAbsolute(const std::wstring& fullPath);

template <typename F>
F procAddress(HMODULE module, const char* name) {
  if (!module) return nullptr;
  FARPROC p = GetProcAddress(module, name);
  return reinterpret_cast<F>(reinterpret_cast<void*>(p));
}

// ntdll is always loaded in every process.
template <typename F>
F ntdllProc(const char* name) {
  return procAddress<F>(GetModuleHandleW(L"ntdll.dll"), name);
}

// Registry reads always use the native (64-bit) view so 32-bit builds on 64-bit
// Windows report the real system configuration.
std::optional<std::wstring> regString(HKEY root, const std::wstring& subKey, const wchar_t* value);
std::optional<DWORD> regDword(HKEY root, const std::wstring& subKey, const wchar_t* value);
std::optional<uint64_t> regQword(HKEY root, const std::wstring& subKey, const wchar_t* value);
std::vector<std::wstring> regSubKeys(HKEY root, const std::wstring& subKey);

bool isElevated();
bool isWow64Process();

std::string errorText(DWORD err);

inline uint64_t ft64(const FILETIME& ft) {
  return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// Monotonic clock in seconds based on QueryPerformanceCounter.
double monotonicSeconds();

std::optional<std::wstring> environmentVariable(const wchar_t* name);

}  // namespace win
}  // namespace sm
