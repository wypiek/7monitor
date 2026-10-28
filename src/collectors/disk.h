#pragma once
// Volumes (logical drives) and physical disks. Everything here works for a standard
// user: physical disks are opened with zero access rights, which is sufficient for
// IOCTL_DISK_PERFORMANCE, IOCTL_STORAGE_QUERY_PROPERTY and drive geometry. Nothing is
// ever written to a device.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sm {

struct VolumeInfo {
  std::string root;        // "C:\"
  std::string label;
  std::string fileSystem;  // NTFS, FAT32, ...
  std::string driveType;   // fixed, removable, network, cdrom, ramdisk, unknown
  bool ready = false;      // false for empty card readers / optical drives
  std::optional<uint64_t> totalBytes, freeBytes, availableToUserBytes;
  std::optional<double> usedPct;
  std::vector<unsigned> diskNumbers;  // physical disks backing the volume
  std::string note;
};

struct DiskPerfRaw {
  bool ok = false;
  uint64_t bytesRead = 0, bytesWritten = 0;
  uint32_t readCount = 0, writeCount = 0;
  uint64_t idleTime = 0, queryTime = 0;  // 100 ns
  double t = 0;                          // monotonic seconds
};

struct PhysicalDiskInfo {
  unsigned number = 0;
  std::string model;    // vendor + product id from the storage descriptor
  std::string busType;  // SATA, USB, NVMe, ...
  bool removable = false;
  std::optional<uint64_t> sizeBytes;
  std::string mediaType = "unknown";  // SSD, HDD, unknown
  std::string mediaTypeSource;        // how mediaType was determined or why unknown
  DiskPerfRaw raw;
  std::string perfNote;
  // Rates (need two samples)
  std::optional<double> readBps, writeBps, activePct;
  std::optional<double> readIops, writeIops;
  // SMART (slow tier, via WMI; usually requires administrator rights)
  std::optional<bool> smartPredictFailure;
  std::optional<double> smartTemperatureC;
  std::string smartNote;
};

std::vector<VolumeInfo> queryVolumes();
// Enumerates \\.\PhysicalDrive0..N and reads static properties + one raw perf sample.
std::vector<PhysicalDiskInfo> queryPhysicalDisks();
// Refreshes raw perf counters for the given disks and computes rates from the previous
// raw values held in each entry.
void refreshDiskPerf(std::vector<PhysicalDiskInfo>& disks);

// Pure: computes rates from two raw samples.
void computeDiskRates(const DiskPerfRaw& prev, const DiskPerfRaw& cur, PhysicalDiskInfo& out);

std::string busTypeName(int busType);

}  // namespace sm
