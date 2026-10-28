#include "util/strings.h"

#include <windows.h>

#include <algorithm>
#include <cctype>

namespace sm {

std::string toUtf8(const wchar_t* w, size_t len) {
  if (!w || len == 0) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return std::string();
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), &out[0], n, nullptr, nullptr);
  return out;
}

std::string toUtf8(const std::wstring& w) { return toUtf8(w.data(), w.size()); }

std::wstring toWide(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return std::wstring();
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
  return out;
}

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::string toLower(std::string s) {
  for (auto& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

bool iequals(const std::string& a, const std::string& b) { return toLower(a) == toLower(b); }

bool icontains(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

bool startsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string sanitize(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool lastSpace = false;
  for (unsigned char c : s) {
    bool space = c < 0x20 || c == 0x7f || c == ' ';
    if (space) {
      if (!lastSpace && !out.empty()) out.push_back(' ');
      lastSpace = true;
    } else {
      out.push_back(static_cast<char>(c));
      lastSpace = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

size_t displayWidth(const std::string& utf8) {
  size_t n = 0;
  for (unsigned char c : utf8) {
    if ((c & 0xC0) != 0x80) ++n;  // count lead bytes only
  }
  return n;
}

std::string truncateDisplay(const std::string& utf8, size_t maxCols) {
  if (displayWidth(utf8) <= maxCols) return utf8;
  if (maxCols == 0) return std::string();
  size_t cols = 0;
  size_t i = 0;
  for (; i < utf8.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(utf8[i]);
    if ((c & 0xC0) != 0x80) {
      if (cols == maxCols - 1) break;
      ++cols;
    }
  }
  return utf8.substr(0, i) + "~";
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    size_t p = s.find(sep, start);
    if (p == std::string::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, p - start));
    start = p + 1;
  }
  return out;
}

}  // namespace sm
