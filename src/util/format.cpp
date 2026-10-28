#include "util/format.h"

#include <windows.h>

#include <cmath>
#include <cstdio>

namespace sm {

const char* const kNA = "N/A";

std::string fmtFixed(double v, int decimals) {
  if (!std::isfinite(v)) return kNA;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
  return buf;
}

std::string fmtBytes(uint64_t bytes) {
  static const char* const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
  if (bytes < 1024) return std::to_string(bytes) + " B";
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1024.0 && u < 5) {
    v /= 1024.0;
    ++u;
  }
  int decimals = v < 10.0 ? 2 : (v < 100.0 ? 1 : 0);
  return fmtFixed(v, decimals) + " " + units[u];
}

std::string fmtBytes(const std::optional<uint64_t>& bytes) { return bytes ? fmtBytes(*bytes) : kNA; }

std::string fmtRate(const std::optional<double>& bps) {
  if (!bps || !std::isfinite(*bps) || *bps < 0) return kNA;
  double v = *bps;
  if (v < 1024.0) return fmtFixed(v, 0) + " B/s";
  static const char* const units[] = {"KiB/s", "MiB/s", "GiB/s", "TiB/s"};
  int u = -1;
  while (v >= 1024.0 && u < 3) {
    v /= 1024.0;
    ++u;
  }
  int decimals = v < 10.0 ? 2 : (v < 100.0 ? 1 : 0);
  return fmtFixed(v, decimals) + " " + units[u];
}

std::string fmtBitRate(uint64_t bps) {
  if (bps >= 1000000000ull) {
    double g = static_cast<double>(bps) / 1e9;
    return fmtFixed(g, std::fmod(g, 1.0) == 0.0 ? 0 : 1) + " Gbps";
  }
  if (bps >= 1000000ull) {
    double m = static_cast<double>(bps) / 1e6;
    return fmtFixed(m, std::fmod(m, 1.0) == 0.0 ? 0 : 1) + " Mbps";
  }
  if (bps >= 1000ull) return fmtFixed(static_cast<double>(bps) / 1e3, 0) + " Kbps";
  return std::to_string(bps) + " bps";
}

std::string fmtPercent(const std::optional<double>& pct, int decimals) {
  if (!pct || !std::isfinite(*pct)) return kNA;
  return fmtFixed(*pct, decimals) + "%";
}

std::string fmtCount(uint64_t n) {
  std::string s = std::to_string(n);
  std::string out;
  int c = 0;
  for (size_t i = s.size(); i > 0; --i) {
    out.insert(out.begin(), s[i - 1]);
    if (++c % 3 == 0 && i > 1) out.insert(out.begin(), ',');
  }
  return out;
}

std::string fmtDuration(uint64_t seconds) {
  uint64_t d = seconds / 86400, h = (seconds / 3600) % 24, m = (seconds / 60) % 60, s = seconds % 60;
  char buf[64];
  if (d > 0)
    std::snprintf(buf, sizeof(buf), "%llud %02llu:%02llu:%02llu", static_cast<unsigned long long>(d),
                  static_cast<unsigned long long>(h), static_cast<unsigned long long>(m),
                  static_cast<unsigned long long>(s));
  else
    std::snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu", static_cast<unsigned long long>(h),
                  static_cast<unsigned long long>(m), static_cast<unsigned long long>(s));
  return buf;
}

std::string fmtMhz(const std::optional<unsigned>& mhz) {
  if (!mhz || *mhz == 0) return kNA;
  return std::to_string(*mhz) + " MHz";
}

std::string fmtCelsius(const std::optional<double>& c) {
  if (!c || !std::isfinite(*c)) return kNA;
  return fmtFixed(*c, 1) + " C";
}

std::string fmtWatts(const std::optional<double>& w) {
  if (!w || !std::isfinite(*w)) return kNA;
  return fmtFixed(*w, 1) + " W";
}

static FILETIME toFt(uint64_t v) {
  FILETIME ft;
  ft.dwLowDateTime = static_cast<DWORD>(v & 0xffffffffu);
  ft.dwHighDateTime = static_cast<DWORD>(v >> 32);
  return ft;
}

std::string fileTimeToIsoUtc(uint64_t v) {
  if (v == 0) return std::string();
  FILETIME ft = toFt(v);
  SYSTEMTIME st;
  if (!FileTimeToSystemTime(&ft, &st)) return std::string();
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour,
                st.wMinute, st.wSecond);
  return buf;
}

std::string fileTimeToLocal(uint64_t v) {
  if (v == 0) return std::string();
  FILETIME ft = toFt(v), local;
  SYSTEMTIME st;
  if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) return std::string();
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
                st.wMinute, st.wSecond);
  return buf;
}

uint64_t nowFileTime() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

std::string nowIsoUtc() { return fileTimeToIsoUtc(nowFileTime()); }

}  // namespace sm
