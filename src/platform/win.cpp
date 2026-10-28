#include "platform/win.h"

#include <cstring>

#include "util/strings.h"

namespace sm {
namespace win {

std::wstring systemDirectory() {
  wchar_t buf[MAX_PATH];
  UINT n = GetSystemDirectoryW(buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return L"C:\\Windows\\System32";
  return std::wstring(buf, n);
}

HMODULE loadSystemLibrary(const wchar_t* fileName) {
  std::wstring path = systemDirectory() + L"\\" + fileName;
  return LoadLibraryW(path.c_str());
}

HMODULE loadLibraryAbsolute(const std::wstring& fullPath) {
  if (fullPath.size() < 3 || fullPath[1] != L':') return nullptr;  // absolute paths only
  if (GetFileAttributesW(fullPath.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
  return LoadLibraryExW(fullPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

namespace {
struct RegKey {
  HKEY h = nullptr;
  ~RegKey() {
    if (h) RegCloseKey(h);
  }
};

bool openKey(HKEY root, const std::wstring& sub, RegKey& key) {
  return RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key.h) == ERROR_SUCCESS;
}
}  // namespace

std::optional<std::wstring> regString(HKEY root, const std::wstring& subKey, const wchar_t* value) {
  RegKey key;
  if (!openKey(root, subKey, key)) return std::nullopt;
  DWORD type = 0, size = 0;
  if (RegQueryValueExW(key.h, value, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return std::nullopt;
  if ((type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ) || size == 0) return std::nullopt;
  std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2, L'\0');
  DWORD cb = static_cast<DWORD>((buf.size() - 1) * sizeof(wchar_t));
  if (RegQueryValueExW(key.h, value, nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &cb) != ERROR_SUCCESS)
    return std::nullopt;
  buf[cb / sizeof(wchar_t)] = L'\0';  // registry strings are not guaranteed to be terminated
  return std::wstring(buf.data());
}

std::optional<DWORD> regDword(HKEY root, const std::wstring& subKey, const wchar_t* value) {
  RegKey key;
  if (!openKey(root, subKey, key)) return std::nullopt;
  DWORD type = 0, data = 0, cb = sizeof(data);
  if (RegQueryValueExW(key.h, value, nullptr, &type, reinterpret_cast<BYTE*>(&data), &cb) != ERROR_SUCCESS)
    return std::nullopt;
  if (type != REG_DWORD) return std::nullopt;
  return data;
}

std::optional<uint64_t> regQword(HKEY root, const std::wstring& subKey, const wchar_t* value) {
  RegKey key;
  if (!openKey(root, subKey, key)) return std::nullopt;
  DWORD type = 0;
  BYTE data[8] = {};
  DWORD cb = sizeof(data);
  if (RegQueryValueExW(key.h, value, nullptr, &type, data, &cb) != ERROR_SUCCESS) return std::nullopt;
  uint64_t v = 0;
  if ((type == REG_QWORD && cb == 8) || (type == REG_BINARY && cb == 8)) {
    memcpy(&v, data, 8);
    return v;
  }
  if ((type == REG_DWORD || type == REG_BINARY) && cb == 4) {
    uint32_t d;
    memcpy(&d, data, 4);
    return d;
  }
  return std::nullopt;
}

std::vector<std::wstring> regSubKeys(HKEY root, const std::wstring& subKey) {
  std::vector<std::wstring> out;
  RegKey key;
  if (!openKey(root, subKey, key)) return out;
  for (DWORD i = 0;; ++i) {
    wchar_t name[256];
    DWORD len = 256;
    LONG r = RegEnumKeyExW(key.h, i, name, &len, nullptr, nullptr, nullptr, nullptr);
    if (r == ERROR_NO_MORE_ITEMS) break;
    if (r != ERROR_SUCCESS) continue;
    out.emplace_back(name, len);
  }
  return out;
}

bool isElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elev = {};
  DWORD cb = 0;
  BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &cb);
  CloseHandle(token);
  return ok && elev.TokenIsElevated != 0;
}

bool isWow64Process() {
  BOOL wow = FALSE;
  using Fn = BOOL(WINAPI*)(HANDLE, PBOOL);
  auto fn = procAddress<Fn>(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process");
  if (fn && fn(GetCurrentProcess(), &wow)) return wow != FALSE;
  return false;
}

std::string errorText(DWORD err) {
  wchar_t* buf = nullptr;
  DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buf), 0,
                           nullptr);
  std::string msg;
  if (n && buf) msg = sanitize(toUtf8(std::wstring(buf, n)));
  if (buf) LocalFree(buf);
  if (msg.empty()) msg = "Windows error";
  while (!msg.empty() && msg.back() == '.') msg.pop_back();
  return msg + " (" + std::to_string(err) + ")";
}

double monotonicSeconds() {
  static LARGE_INTEGER freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f;
  }();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
}

std::optional<std::wstring> environmentVariable(const wchar_t* name) {
  DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
  if (n == 0) return std::nullopt;
  std::wstring v(n, L'\0');
  DWORD m = GetEnvironmentVariableW(name, &v[0], n);
  if (m == 0 || m >= n) return std::nullopt;
  v.resize(m);
  return v;
}

}  // namespace win
}  // namespace sm
