#include "tui/canvas.h"

#include <algorithm>
#include <cmath>

#include "util/strings.h"

namespace sm {

Theme Theme::make(bool color, bool unicode) {
  Theme t;
  t.color = color;
  t.unicode = unicode;
  if (!color) {
    // Monochrome: grey on black, intensity for emphasis, reverse video for selection.
    t.normal = 0x07;
    t.dim = 0x07;
    t.label = 0x07;
    t.value = 0x0F;
    t.title = 0x0F;
    t.header = 0x70;
    t.footer = 0x70;
    t.footerKey = 0x70;
    t.good = t.warn = t.bad = 0x0F;
    t.selected = 0x70;
    t.border = 0x07;
    t.tableHeader = 0x0F;
  }
  if (unicode) {
    t.hz = 0x2500;
    t.vt = 0x2502;
    t.tl = 0x250C;
    t.tr = 0x2510;
    t.bl = 0x2514;
    t.br = 0x2518;
    t.barFull = 0x2588;
    t.barEmpty = 0x2591;
  }
  return t;
}

uint16_t levelAttr(const Theme& t, double pct) {
  if (pct >= 90) return t.bad;
  if (pct >= 70) return t.warn;
  return t.good;
}

Canvas::Canvas(int w, int h, uint16_t attr, bool asciiOnly)
    : w_(std::max(0, w)), h_(std::max(0, h)), ascii_(asciiOnly), cells_(static_cast<size_t>(w_) * h_) {
  clear(attr);
}

void Canvas::clear(uint16_t attr) {
  for (auto& c : cells_) {
    c.ch = L' ';
    c.attr = attr;
  }
}

void Canvas::put(int x, int y, wchar_t ch, uint16_t attr) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  if (ch < 0x20 || (ascii_ && ch > 0x7E)) ch = ch < 0x20 ? L' ' : L'?';
  Cell& c = cells_[static_cast<size_t>(y) * w_ + x];
  c.ch = ch;
  c.attr = attr;
}

int Canvas::text(int x, int y, const std::wstring& s, uint16_t attr, int maxWidth) {
  int n = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (maxWidth >= 0 && n >= maxWidth) break;
    wchar_t ch = s[i];
    if (ch >= 0xD800 && ch <= 0xDBFF) {  // surrogate pair: the console cannot show it in one cell
      ch = L'?';
      ++i;
    }
    put(x + n, y, ch, attr);
    ++n;
  }
  return n;
}

int Canvas::text(int x, int y, const std::string& utf8, uint16_t attr, int maxWidth) {
  return text(x, y, toWide(utf8), attr, maxWidth);
}

void Canvas::fill(int x, int y, int w, int h, wchar_t ch, uint16_t attr) {
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) put(x + i, y + j, ch, attr);
}

void Canvas::box(int x, int y, int w, int h, const Theme& t, const std::string& title) {
  if (w < 2 || h < 2) return;
  put(x, y, t.tl, t.border);
  put(x + w - 1, y, t.tr, t.border);
  put(x, y + h - 1, t.bl, t.border);
  put(x + w - 1, y + h - 1, t.br, t.border);
  for (int i = 1; i < w - 1; ++i) {
    put(x + i, y, t.hz, t.border);
    put(x + i, y + h - 1, t.hz, t.border);
  }
  for (int j = 1; j < h - 1; ++j) {
    put(x, y + j, t.vt, t.border);
    put(x + w - 1, y + j, t.vt, t.border);
  }
  if (!title.empty() && w > 6) text(x + 2, y, " " + title + " ", t.title, w - 4);
}

void Canvas::bar(int x, int y, int width, const std::optional<double>& pct, const Theme& t) {
  if (width <= 0) return;
  int filled = 0;
  uint16_t a = t.dim;
  if (pct && std::isfinite(*pct)) {
    double p = std::max(0.0, std::min(100.0, *pct));
    filled = static_cast<int>(std::lround(p / 100.0 * width));
    a = levelAttr(t, p);
  }
  for (int i = 0; i < width; ++i) put(x + i, y, i < filled ? t.barFull : t.barEmpty, i < filled ? a : t.dim);
}

std::wstring Canvas::rowText(int y) const {
  std::wstring s;
  if (y < 0 || y >= h_) return s;
  for (int x = 0; x < w_; ++x) s.push_back(at(x, y).ch);
  return s;
}

std::string Canvas::allText() const {
  std::string out;
  for (int y = 0; y < h_; ++y) {
    out += toUtf8(rowText(y));
    out.push_back('\n');
  }
  return out;
}

}  // namespace sm
