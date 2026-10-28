#pragma once
// Pure arithmetic for utilization and throughput. Everything here is derived from the
// difference between two real samples; there is deliberately no way to obtain a value
// from a single sample.
#include <cstdint>
#include <optional>

namespace sm {

// Difference of a monotonically increasing 64-bit counter. Returns nullopt when the
// counter went backwards (adapter reset, device re-enumeration, driver reload) because
// no trustworthy delta exists for that interval.
inline std::optional<uint64_t> counterDelta64(uint64_t prev, uint64_t cur) {
  if (cur < prev) return std::nullopt;
  return cur - prev;
}

// Difference of a 32-bit counter that may wrap. A decrease is treated as a single wrap
// only when the implied delta is below half the counter range; otherwise it is a reset.
inline std::optional<uint64_t> counterDelta32(uint32_t prev, uint32_t cur) {
  if (cur >= prev) return static_cast<uint64_t>(cur - prev);
  uint64_t wrapped = (0x100000000ull - prev) + cur;
  if (wrapped < 0x80000000ull) return wrapped;
  return std::nullopt;
}

// Rate per second for a counter delta over an elapsed time in seconds.
inline std::optional<double> perSecond(std::optional<uint64_t> delta, double elapsedSeconds) {
  if (!delta || !(elapsedSeconds > 0.0)) return std::nullopt;
  return static_cast<double>(*delta) / elapsedSeconds;
}

inline double clampPercent(double v) {
  if (v < 0.0) return 0.0;
  if (v > 100.0) return 100.0;
  return v;
}

// CPU busy percentage from deltas of idle, kernel and user time. Windows reports kernel
// time *including* idle time for both GetSystemTimes and per-processor counters.
inline std::optional<double> cpuBusyPercent(uint64_t idleDelta, uint64_t kernelDelta, uint64_t userDelta) {
  uint64_t total = kernelDelta + userDelta;
  if (total == 0 || idleDelta > total) return std::nullopt;
  return clampPercent(100.0 * static_cast<double>(total - idleDelta) / static_cast<double>(total));
}

inline std::optional<double> sharePercent(uint64_t part, uint64_t total) {
  if (total == 0) return std::nullopt;
  return clampPercent(100.0 * static_cast<double>(part) / static_cast<double>(total));
}

// Process CPU usage normalized to the whole machine (0..100 across all logical CPUs),
// as Task Manager does. cpuDelta100ns and wallDelta100ns are in 100 ns units.
inline std::optional<double> processCpuPercent(uint64_t cpuDelta100ns, uint64_t wallDelta100ns, unsigned logicalCpus) {
  if (wallDelta100ns == 0 || logicalCpus == 0) return std::nullopt;
  double pct = 100.0 * static_cast<double>(cpuDelta100ns) /
               (static_cast<double>(wallDelta100ns) * static_cast<double>(logicalCpus));
  return clampPercent(pct);
}

// Disk activity from IOCTL_DISK_PERFORMANCE idle/query times (100 ns units).
inline std::optional<double> diskActivePercent(std::optional<uint64_t> idleDelta, std::optional<uint64_t> queryDelta) {
  if (!idleDelta || !queryDelta || *queryDelta == 0) return std::nullopt;
  double idle = static_cast<double>(*idleDelta) / static_cast<double>(*queryDelta);
  return clampPercent(100.0 * (1.0 - idle));
}

}  // namespace sm
