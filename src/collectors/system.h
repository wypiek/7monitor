#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "collectors/smbios.h"

namespace sm {

struct SystemInfo {
  std::string productName;  // "Windows 7 Professional"
  std::string edition;      // EditionID
  std::string servicePack;  // "Service Pack 1"
  unsigned major = 0, minor = 0, build = 0;
  std::optional<unsigned> ubr;  // update build revision (Windows 10+)
  std::string displayVersion;   // "6.1.7601" / "10.0.19045.1234"
  std::string osArch;           // x64, x86, ARM64
  std::string processArch;      // architecture of this sevenmon.exe
  bool wow64 = false;
  std::string computerName;
  uint64_t uptimeSeconds = 0;
  std::string bootTimeUtc;  // derived: now - GetTickCount64 (includes time spent asleep)
  bool elevated = false;
  bool isWindows7 = false;
  bool outOfSupport = false;
  SmbiosInfo smbios;
};

SystemInfo querySystemInfo();
// Cheap refresh of uptime only.
void refreshUptime(SystemInfo& s);

}  // namespace sm
