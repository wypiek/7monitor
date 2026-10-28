#include "output/table.h"

#include <algorithm>
#include <cmath>

#include "util/strings.h"

namespace sm {

std::vector<std::string> Table::lines() const {
  std::vector<size_t> w(cols_.size(), 0);
  auto cell = [&](const std::vector<std::string>& row, size_t c) -> std::string {
    std::string v = c < row.size() ? row[c] : std::string();
    if (cols_[c].maxWidth) v = truncateDisplay(v, cols_[c].maxWidth);
    return v;
  };
  for (size_t c = 0; c < cols_.size(); ++c) w[c] = displayWidth(cols_[c].header);
  for (const auto& r : rows_)
    for (size_t c = 0; c < cols_.size(); ++c) w[c] = std::max(w[c], displayWidth(cell(r, c)));
  auto fmt = [&](const std::vector<std::string>& row, bool header) {
    std::string line;
    for (size_t c = 0; c < cols_.size(); ++c) {
      std::string v = header ? cols_[c].header : cell(row, c);
      size_t pad = w[c] - displayWidth(v);
      bool last = c + 1 == cols_.size();
      if (cols_[c].right) line += std::string(pad, ' ') + v;
      else line += last ? v : v + std::string(pad, ' ');
      if (!last) line += "  ";
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    return line;
  };
  std::vector<std::string> out;
  out.push_back(fmt({}, true));
  for (const auto& r : rows_) out.push_back(fmt(r, false));
  return out;
}

void Table::print(Out& out) const {
  auto ls = lines();
  for (size_t i = 0; i < ls.size(); ++i) {
    if (i == 0) out.colored(Color::Header, ls[i]);
    else out.write(ls[i]);
    out.write("\r\n");
  }
}

std::string asciiBar(double pct, int width) {
  if (width < 1) return std::string();
  if (!std::isfinite(pct)) pct = 0;
  pct = std::max(0.0, std::min(100.0, pct));
  int filled = static_cast<int>(std::lround(pct / 100.0 * width));
  return "[" + std::string(static_cast<size_t>(filled), '#') + std::string(static_cast<size_t>(width - filled), '-') + "]";
}

}  // namespace sm
