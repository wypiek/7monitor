#include <limits>

#include "test.h"
#include "util/format.h"
#include "util/strings.h"

using namespace sm;

TEST(format_bytes) {
  CHECK_EQ(fmtBytes(0ull), std::string("0 B"));
  CHECK_EQ(fmtBytes(1023ull), std::string("1023 B"));
  CHECK_EQ(fmtBytes(1536ull), std::string("1.50 KiB"));
  CHECK_EQ(fmtBytes(8ull * 1024 * 1024 * 1024), std::string("8.00 GiB"));
  CHECK_EQ(fmtBytes(std::optional<uint64_t>()), std::string("N/A"));
}

TEST(format_rates_and_missing) {
  CHECK_EQ(fmtRate(std::nullopt), std::string("N/A"));
  CHECK_EQ(fmtRate(512.0), std::string("512 B/s"));
  CHECK_EQ(fmtRate(1.2 * 1048576), std::string("1.20 MiB/s"));
  CHECK_EQ(fmtRate(-1.0), std::string("N/A"));
  CHECK_EQ(fmtBitRate(1000000000ull), std::string("1 Gbps"));
  CHECK_EQ(fmtBitRate(100000000ull), std::string("100 Mbps"));
}

TEST(format_percent_and_sensors_missing) {
  CHECK_EQ(fmtPercent(40.0), std::string("40.0%"));
  CHECK_EQ(fmtPercent(std::nullopt), std::string("N/A"));
  CHECK_EQ(fmtPercent(std::numeric_limits<double>::quiet_NaN()), std::string("N/A"));
  CHECK_EQ(fmtCelsius(std::nullopt), std::string("N/A"));
  CHECK_EQ(fmtCelsius(45.0), std::string("45.0 C"));
  CHECK_EQ(fmtWatts(std::nullopt), std::string("N/A"));
  CHECK_EQ(fmtMhz(std::nullopt), std::string("N/A"));
  CHECK_EQ(fmtMhz(std::optional<unsigned>(0u)), std::string("N/A"));  // 0 MHz is not a reading
}

TEST(format_duration_and_count) {
  CHECK_EQ(fmtDuration(59), std::string("00:00:59"));
  CHECK_EQ(fmtDuration(3 * 86400 + 4 * 3600 + 5 * 60 + 6), std::string("3d 04:05:06"));
  CHECK_EQ(fmtCount(1234567), std::string("1,234,567"));
  CHECK_EQ(fmtCount(12), std::string("12"));
}

TEST(format_iso_time) {
  // 2001-01-01T00:00:00Z as FILETIME
  CHECK_EQ(fileTimeToIsoUtc(126227808000000000ull), std::string("2001-01-01T00:00:00Z"));
  CHECK_EQ(fileTimeToIsoUtc(0), std::string());
}

TEST(strings_utf8_roundtrip_and_width) {
  std::string s = "Z\xC3\xBCrich \xE6\x97\xA5";  // Zürich 日
  CHECK_EQ(toUtf8(toWide(s)), s);
  CHECK_EQ(displayWidth(s), static_cast<size_t>(8));
  CHECK_EQ(truncateDisplay("abcdef", 4), std::string("abc~"));
  CHECK_EQ(truncateDisplay(s, 3), std::string("Z\xC3\xBC~"));
  CHECK_EQ(sanitize("a\tb\r\n  c "), std::string("a b c"));
}
