#pragma once
// Human-readable formatting. All functions are locale independent (always '.' as the
// decimal separator) so text output is stable and parseable.
#include <cstdint>
#include <optional>
#include <string>

namespace sm {

extern const char* const kNA;  // "N/A"

std::string fmtFixed(double v, int decimals);
std::string fmtBytes(uint64_t bytes);                 // 1023 B, 1.5 KiB, 3.21 GiB
std::string fmtBytes(const std::optional<uint64_t>& bytes);
std::string fmtRate(const std::optional<double>& bytesPerSecond);  // 1.2 MiB/s / N/A
std::string fmtBitRate(uint64_t bitsPerSecond);       // 100 Mbps, 1 Gbps
std::string fmtPercent(const std::optional<double>& pct, int decimals = 1);
std::string fmtCount(uint64_t n);                     // 1,234,567
std::string fmtDuration(uint64_t seconds);            // 3d 04:05:06 / 04:05:06
std::string fmtMhz(const std::optional<unsigned>& mhz);
std::string fmtCelsius(const std::optional<double>& c);
std::string fmtWatts(const std::optional<double>& w);

// FILETIME (100 ns since 1601 UTC) to ISO 8601 "2026-01-02T03:04:05Z" and local
// "2026-01-02 04:04:05". Return empty string on conversion failure.
std::string fileTimeToIsoUtc(uint64_t ft);
std::string fileTimeToLocal(uint64_t ft);
std::string nowIsoUtc();
uint64_t nowFileTime();

}  // namespace sm
