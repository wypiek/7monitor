#include "platform/console.h"

#include <algorithm>

#include "util/strings.h"

namespace sm {

bool handleIsConsole(HANDLE h) {
  if (h == nullptr || h == INVALID_HANDLE_VALUE) return false;
  DWORD mode = 0;
  return GetConsoleMode(h, &mode) != 0;
}

Out::Out(DWORD stdHandle, bool allowColor) {
  h_ = GetStdHandle(stdHandle);
  console_ = handleIsConsole(h_);
  if (console_) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(h_, &info)) {
      defaultAttr_ = info.wAttributes;
      color_ = allowColor;
    }
  }
}

Out::Out(HANDLE file) : h_(file) {}

Out::~Out() { reset(); }

void Out::write(const std::string& utf8) {
  if (utf8.empty() || h_ == nullptr || h_ == INVALID_HANDLE_VALUE) return;
  if (console_) {
    std::wstring w = toWide(utf8);
    // Windows 7 conhost rejects very large WriteConsoleW requests (shared 64 KiB
    // heap), so write in modest chunks.
    const size_t chunk = 4096;
    for (size_t off = 0; off < w.size(); off += chunk) {
      DWORD n = static_cast<DWORD>(std::min(chunk, w.size() - off));
      DWORD written = 0;
      if (!WriteConsoleW(h_, w.data() + off, n, &written, nullptr)) failed_ = true;
    }
  } else {
    size_t off = 0;
    while (off < utf8.size()) {
      DWORD written = 0;
      DWORD n = static_cast<DWORD>(std::min<size_t>(utf8.size() - off, 1u << 20));
      if (!WriteFile(h_, utf8.data() + off, n, &written, nullptr) || written == 0) {
        failed_ = true;  // e.g. closed pipe: stop quietly instead of spinning
        return;
      }
      off += written;
    }
  }
}

static WORD attrFor(Color c, WORD def) {
  WORD bg = def & 0xF0;
  switch (c) {
    case Color::Default: return def;
    case Color::Dim: return bg | FOREGROUND_INTENSITY;
    case Color::Title: return bg | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case Color::Accent: return bg | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case Color::Good: return bg | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case Color::Warn: return bg | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case Color::Bad: return bg | FOREGROUND_RED | FOREGROUND_INTENSITY;
    case Color::Header: return bg | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
  }
  return def;
}

void Out::color(Color c) {
  if (!color_) return;
  SetConsoleTextAttribute(h_, attrFor(c, defaultAttr_));
}

void Out::reset() {
  if (!color_) return;
  SetConsoleTextAttribute(h_, defaultAttr_);
}

static bool oemPageHasBoxGlyphs(unsigned cp) {
  // OEM code pages whose raster-font glyph sets include the box-drawing and
  // shade/block characters used by the dashboard.
  switch (cp) {
    case 437: case 737: case 775: case 850: case 852: case 855: case 857: case 860:
    case 861: case 862: case 863: case 865: case 866: case 869:
      return true;
    default:
      return false;
  }
}

ConsoleCaps detectConsoleCaps(HANDLE out) {
  ConsoleCaps caps;
  caps.isConsole = handleIsConsole(out);
  if (!caps.isConsole) {
    caps.reason = "not a console";
    return caps;
  }
  caps.outputCodePage = GetConsoleOutputCP();
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(out, &info)) {
    caps.width = static_cast<short>(info.srWindow.Right - info.srWindow.Left + 1);
    caps.height = static_cast<short>(info.srWindow.Bottom - info.srWindow.Top + 1);
  }
  CONSOLE_FONT_INFOEX font;
  ZeroMemory(&font, sizeof(font));
  font.cbSize = sizeof(font);
  if (GetCurrentConsoleFontEx(out, FALSE, &font)) {
    caps.fontKnown = true;
    caps.fontFace = toUtf8(std::wstring(font.FaceName));
    caps.trueTypeFont = (font.FontFamily & TMPF_TRUETYPE) != 0;
  }
  if (caps.trueTypeFont) {
    caps.unicodeBlocks = true;
    caps.reason = "TrueType console font";
  } else if (oemPageHasBoxGlyphs(caps.outputCodePage)) {
    caps.unicodeBlocks = true;
    caps.reason = "raster font with OEM code page " + std::to_string(caps.outputCodePage);
  } else {
    caps.unicodeBlocks = false;
    caps.reason = "raster font or unknown font with code page " + std::to_string(caps.outputCodePage) +
                  "; using ASCII";
  }
  return caps;
}

}  // namespace sm
