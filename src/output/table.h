#pragma once
// Aligned plain-text tables. Widths are computed from content; long cells are
// truncated to a column's maxWidth with a "~" marker.
#include <string>
#include <vector>

#include "platform/console.h"

namespace sm {

struct Column {
  std::string header;
  bool right = false;
  size_t maxWidth = 0;  // 0 = unlimited
};

class Table {
 public:
  explicit Table(std::vector<Column> cols) : cols_(std::move(cols)) {}
  void add(std::vector<std::string> row) { rows_.push_back(std::move(row)); }
  size_t size() const { return rows_.size(); }
  // Rendered lines without trailing newlines (header first).
  std::vector<std::string> lines() const;
  void print(Out& out) const;

 private:
  std::vector<Column> cols_;
  std::vector<std::vector<std::string>> rows_;
};

// "[#####-----]" style bar using ASCII only (safe for every console and file).
std::string asciiBar(double pct, int width);

}  // namespace sm
