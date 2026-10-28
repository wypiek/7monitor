#pragma once
// In-memory character-cell canvas. The dashboard is rendered entirely into a Canvas and
// then copied to the console with a single WriteConsoleOutputW call (no flicker). The
// same canvas is used by the layout tests, so layouts are verified without a console.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sm {

struct Cell {
  wchar_t ch = L' ';
  uint16_t attr = 0x07;
  bool operator==(const Cell& o) const { return ch == o.ch && attr == o.attr; }
  bool operator!=(const Cell& o) const { return !(*this == o); }
};

struct Theme {
  bool color = true;
  bool unicode = true;
  uint16_t normal = 0x07, dim = 0x08, label = 0x0B, value = 0x0F, title = 0x0F, header = 0x1F, footer = 0x30,
           footerKey = 0x3F, good = 0x0A, warn = 0x0E, bad = 0x0C, selected = 0x70, border = 0x08, tableHeader = 0x0B;
  wchar_t hz = L'-', vt = L'|', tl = L'+', tr = L'+', bl = L'+', br = L'+';
  wchar_t barFull = L'#', barEmpty = L'-';
  static Theme make(bool color, bool unicode);
};

class Canvas {
 public:
  Canvas(int w, int h, uint16_t attr = 0x07, bool asciiOnly = false);
  int width() const { return w_; }
  int height() const { return h_; }
  void clear(uint16_t attr);
  void put(int x, int y, wchar_t ch, uint16_t attr);
  // Draws text clipped to the canvas and to maxWidth columns (if >= 0). Returns columns used.
  int text(int x, int y, const std::wstring& s, uint16_t attr, int maxWidth = -1);
  int text(int x, int y, const std::string& utf8, uint16_t attr, int maxWidth = -1);
  void fill(int x, int y, int w, int h, wchar_t ch, uint16_t attr);
  void box(int x, int y, int w, int h, const Theme& t, const std::string& title);
  // Horizontal bar of width cells; pct nullopt draws an empty bar.
  void bar(int x, int y, int width, const std::optional<double>& pct, const Theme& t);
  const Cell& at(int x, int y) const { return cells_[static_cast<size_t>(y) * w_ + x]; }
  const std::vector<Cell>& cells() const { return cells_; }
  std::wstring rowText(int y) const;
  std::string allText() const;  // UTF-8, rows separated by '\n' (tests)

 private:
  int w_, h_;
  bool ascii_;
  std::vector<Cell> cells_;
};

uint16_t levelAttr(const Theme& t, double pct);

}  // namespace sm
