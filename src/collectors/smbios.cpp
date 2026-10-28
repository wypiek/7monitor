#include "collectors/smbios.h"

#include <windows.h>

#include "util/strings.h"

namespace sm {

namespace {

// Returns string number idx (1-based) from the string-set following a structure.
std::optional<std::string> smbiosString(const uint8_t* strings, const uint8_t* end, uint8_t idx) {
  if (idx == 0) return std::nullopt;
  const uint8_t* p = strings;
  for (uint8_t i = 1; p < end && *p; ++i) {
    const uint8_t* s = p;
    while (p < end && *p) ++p;
    if (i == idx) {
      std::string v = sanitize(trim(std::string(reinterpret_cast<const char*>(s), static_cast<size_t>(p - s))));
      // Placeholder strings OEMs leave in firmware are not information.
      std::string l = toLower(v);
      if (v.empty() || l == "to be filled by o.e.m." || l == "default string" || l == "system product name" ||
          l == "system manufacturer" || l == "not applicable" || l == "none")
        return std::nullopt;
      return v;
    }
    ++p;
  }
  return std::nullopt;
}

}  // namespace

SmbiosInfo parseSmbios(const std::vector<uint8_t>& raw) {
  SmbiosInfo info;
  if (raw.size() < 8) {
    info.note = "SMBIOS table too small";
    return info;
  }
  uint8_t major = raw[1], minor = raw[2];
  uint32_t len = static_cast<uint32_t>(raw[4]) | (static_cast<uint32_t>(raw[5]) << 8) |
                 (static_cast<uint32_t>(raw[6]) << 16) | (static_cast<uint32_t>(raw[7]) << 24);
  if (len > raw.size() - 8) len = static_cast<uint32_t>(raw.size() - 8);
  info.version = std::to_string(major) + "." + std::to_string(minor);
  const uint8_t* p = raw.data() + 8;
  const uint8_t* end = p + len;
  while (p + 4 <= end) {
    uint8_t type = p[0], hlen = p[1];
    if (hlen < 4 || p + hlen > end) break;
    const uint8_t* strings = p + hlen;
    // Find the double-NUL terminating the string set.
    const uint8_t* q = strings;
    while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) ++q;
    const uint8_t* next = q + 2;
    auto str = [&](size_t off) -> std::optional<std::string> {
      if (off >= hlen) return std::nullopt;
      return smbiosString(strings, end, p[off]);
    };
    if (type == 0) {
      info.biosVendor = str(4);
      info.biosVersion = str(5);
      info.biosDate = str(8);
    } else if (type == 1) {
      info.systemManufacturer = str(4);
      info.systemProduct = str(5);
      info.systemVersion = str(6);
    } else if (type == 2) {
      info.boardManufacturer = str(4);
      info.boardProduct = str(5);
      info.boardVersion = str(6);
    } else if (type == 127) {
      break;  // end-of-table
    }
    if (next > end) break;
    p = next;
  }
  info.ok = true;
  return info;
}

SmbiosInfo querySmbios() {
  const DWORD rsmb = 'R' << 24 | 'S' << 16 | 'M' << 8 | 'B';
  UINT size = GetSystemFirmwareTable(rsmb, 0, nullptr, 0);
  if (size == 0) {
    SmbiosInfo i;
    i.note = "GetSystemFirmwareTable('RSMB') unavailable";
    return i;
  }
  std::vector<uint8_t> buf(size);
  if (GetSystemFirmwareTable(rsmb, 0, buf.data(), size) != size) {
    SmbiosInfo i;
    i.note = "GetSystemFirmwareTable('RSMB') failed";
    return i;
  }
  return parseSmbios(buf);
}

}  // namespace sm
