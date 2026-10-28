#pragma once
// UTF-8 / UTF-16 conversion and small string helpers. All internal text is UTF-8;
// conversion to UTF-16 happens only at Win32 API and console boundaries.
#include <string>
#include <vector>

namespace sm {

std::string toUtf8(const std::wstring& w);
std::string toUtf8(const wchar_t* w, size_t len);
std::wstring toWide(const std::string& s);

std::string trim(const std::string& s);
std::string toLower(std::string s);
bool iequals(const std::string& a, const std::string& b);
bool icontains(const std::string& haystack, const std::string& needle);
bool startsWith(const std::string& s, const std::string& prefix);

// Replaces control characters with spaces and collapses runs of whitespace.
std::string sanitize(const std::string& s);

// Number of display columns, counted as Unicode code points (wide CJK glyphs are
// counted as one column; documented limitation).
size_t displayWidth(const std::string& utf8);
// Truncates to at most maxCols code points, adding "~" when truncated (ASCII-safe).
std::string truncateDisplay(const std::string& utf8, size_t maxCols);

std::vector<std::string> split(const std::string& s, char sep);

}  // namespace sm
