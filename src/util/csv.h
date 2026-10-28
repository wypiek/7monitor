#pragma once
// RFC 4180 CSV helpers. CSV rows are produced by flattening JSON objects so the CSV
// and JSON outputs always carry the same fields (nested keys joined with '.').
#include <string>
#include <utility>
#include <vector>

#include "util/json.h"

namespace sm {

std::string csvEscape(const std::string& field);
std::string csvLine(const std::vector<std::string>& fields);  // includes trailing CRLF

// Flattens an object into ordered (key, text) pairs. Arrays of scalars are joined
// with ';', arrays of objects are skipped (they are emitted as separate CSV tables).
std::vector<std::pair<std::string, std::string>> flatten(const JValue& obj);

struct CsvTable {
  std::vector<std::string> header;
  std::vector<std::vector<std::string>> rows;
  std::string toString(bool includeHeader = true) const;
};

// Builds a table from a list of objects; the header is the union of flattened keys in
// first-seen order. Missing cells are empty.
CsvTable csvFromObjects(const std::vector<JValue>& rows);

}  // namespace sm
