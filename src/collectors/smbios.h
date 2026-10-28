#pragma once
// Raw SMBIOS table parsing (GetSystemFirmwareTable 'RSMB', available since Vista).
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sm {

struct SmbiosInfo {
  bool ok = false;
  std::string version;  // "2.7"
  std::optional<std::string> biosVendor, biosVersion, biosDate;
  std::optional<std::string> systemManufacturer, systemProduct, systemVersion;
  std::optional<std::string> boardManufacturer, boardProduct, boardVersion;
  std::string note;
};

// Parses the RawSMBIOSData blob returned by GetSystemFirmwareTable. Pure function.
SmbiosInfo parseSmbios(const std::vector<uint8_t>& raw);
SmbiosInfo querySmbios();

}  // namespace sm
