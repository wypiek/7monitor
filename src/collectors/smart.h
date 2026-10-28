#pragma once
// SMART status through the storage driver's WMI classes (ROOT\WMI
// MSStorageDriver_FailurePredictStatus / _FailurePredictData). These classes generally
// require administrator rights and are only implemented for ATA/SATA disks behind
// drivers that pass SMART through (USB bridges and many RAID/NVMe drivers do not).
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sm {

class WmiSession;
struct PhysicalDiskInfo;

struct SmartEntry {
  std::string instanceName;  // WMI instance, e.g. IDE\DiskST3500418AS...\5&...&0_0
  std::optional<bool> predictFailure;
  std::optional<double> temperatureC;  // attribute 194, else 190
  std::optional<uint64_t> powerOnHours;  // attribute 9
  std::optional<uint64_t> reallocatedSectors;  // attribute 5
};

struct SmartResult {
  bool queried = false;
  std::vector<SmartEntry> entries;
  std::string status;  // explanation when unavailable
};

SmartResult querySmart(WmiSession& wmiRootWmi);

// Pure parser for the 512-byte ATA SMART data block (VendorSpecific array).
struct SmartAttributes {
  std::optional<double> temperatureC;
  std::optional<uint64_t> powerOnHours;
  std::optional<uint64_t> reallocatedSectors;
};
SmartAttributes parseSmartAttributes(const std::vector<uint8_t>& vendorSpecific);

// Attaches SMART entries to physical disks by model-name matching.
void attachSmart(const SmartResult& smart, std::vector<PhysicalDiskInfo>& disks);

}  // namespace sm
