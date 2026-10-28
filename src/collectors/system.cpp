#include "collectors/system.h"

#include <windows.h>

#include "platform/win.h"
#include "util/format.h"
#include "util/strings.h"

namespace sm {

namespace {
struct OsVersionInfoExW {
  ULONG dwOSVersionInfoSize;
  ULONG dwMajorVersion;
  ULONG dwMinorVersion;
  ULONG dwBuildNumber;
  ULONG dwPlatformId;
  WCHAR szCSDVersion[128];
  USHORT wServicePackMajor;
  USHORT wServicePackMinor;
  USHORT wSuiteMask;
  UCHAR wProductType;
  UCHAR wReserved;
};
using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfoExW*);

std::string archName(WORD a) {
  switch (a) {
    case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
    case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
    case 12 /* ARM64 */: return "ARM64";
    case PROCESSOR_ARCHITECTURE_ARM: return "ARM";
    case PROCESSOR_ARCHITECTURE_IA64: return "IA64";
    default: return "unknown";
  }
}
}  // namespace

void refreshUptime(SystemInfo& s) {
  uint64_t ms = GetTickCount64();
  s.uptimeSeconds = ms / 1000;
  uint64_t now = nowFileTime();
  s.bootTimeUtc = fileTimeToIsoUtc(now - ms * 10000ull);
}

SystemInfo querySystemInfo() {
  SystemInfo s;
  const std::wstring cv = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  OsVersionInfoExW v;
  ZeroMemory(&v, sizeof(v));
  v.dwOSVersionInfoSize = sizeof(v);
  auto rtl = win::ntdllProc<RtlGetVersionFn>("RtlGetVersion");
  if (rtl && rtl(&v) == 0) {
    s.major = v.dwMajorVersion;
    s.minor = v.dwMinorVersion;
    s.build = v.dwBuildNumber;
    s.servicePack = sanitize(toUtf8(std::wstring(v.szCSDVersion)));
  }
  if (auto p = win::regString(HKEY_LOCAL_MACHINE, cv, L"ProductName")) s.productName = sanitize(toUtf8(*p));
  if (auto e = win::regString(HKEY_LOCAL_MACHINE, cv, L"EditionID")) s.edition = sanitize(toUtf8(*e));
  if (auto u = win::regDword(HKEY_LOCAL_MACHINE, cv, L"UBR")) s.ubr = *u;
  // Windows 11 still reports "Windows 10" in ProductName; build 22000+ is Windows 11.
  if (s.major == 10 && s.build >= 22000 && startsWith(s.productName, "Windows 10"))
    s.productName = "Windows 11" + s.productName.substr(10);
  if (s.productName.empty()) s.productName = "Windows " + std::to_string(s.major) + "." + std::to_string(s.minor);
  s.displayVersion = std::to_string(s.major) + "." + std::to_string(s.minor) + "." + std::to_string(s.build);
  if (s.ubr) s.displayVersion += "." + std::to_string(*s.ubr);
  s.isWindows7 = s.major == 6 && s.minor == 1;
  // Windows 7 / 8 / 8.1 no longer receive security updates from Microsoft (8.1 ended Jan 2023).
  s.outOfSupport = s.major < 10;

  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  s.osArch = archName(si.wProcessorArchitecture);
#if defined(_WIN64)
  s.processArch = "x64";
#else
  s.processArch = "x86";
#endif
  s.wow64 = win::isWow64Process();

  wchar_t name[MAX_COMPUTERNAME_LENGTH + 64];
  DWORD len = MAX_COMPUTERNAME_LENGTH + 63;
  if (GetComputerNameExW(ComputerNameDnsHostname, name, &len) || (len = MAX_COMPUTERNAME_LENGTH + 63, GetComputerNameW(name, &len)))
    s.computerName = toUtf8(std::wstring(name, len));
  refreshUptime(s);
  s.elevated = win::isElevated();
  s.smbios = querySmbios();
  return s;
}

}  // namespace sm
