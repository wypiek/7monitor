#include "util/csv.h"

#include <algorithm>
#include <map>

namespace sm {

std::string csvEscape(const std::string& f) {
  bool quote = f.find_first_of(",\"\r\n") != std::string::npos || (!f.empty() && (f.front() == ' ' || f.back() == ' '));
  if (!quote) return f;
  std::string out = "\"";
  for (char c : f) {
    if (c == '"') out += "\"\"";
    else out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string csvLine(const std::vector<std::string>& fields) {
  std::string out;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i) out.push_back(',');
    out += csvEscape(fields[i]);
  }
  out += "\r\n";
  return out;
}

static void flattenInto(const JValue& v, const std::string& prefix, std::vector<std::pair<std::string, std::string>>& out) {
  switch (v.type()) {
    case JValue::Type::Object:
      for (const auto& m : v.members()) flattenInto(m.second, prefix.empty() ? m.first : prefix + "." + m.first, out);
      break;
    case JValue::Type::Array: {
      bool scalars = std::all_of(v.items().begin(), v.items().end(), [](const JValue& x) {
        return x.type() != JValue::Type::Object && x.type() != JValue::Type::Array;
      });
      if (!scalars) break;
      std::string joined;
      for (size_t i = 0; i < v.items().size(); ++i) {
        if (i) joined.push_back(';');
        joined += v.items()[i].scalarText();
      }
      out.emplace_back(prefix, joined);
      break;
    }
    default:
      out.emplace_back(prefix, v.scalarText());
  }
}

std::vector<std::pair<std::string, std::string>> flatten(const JValue& obj) {
  std::vector<std::pair<std::string, std::string>> out;
  flattenInto(obj, std::string(), out);
  return out;
}

CsvTable csvFromObjects(const std::vector<JValue>& rows) {
  CsvTable t;
  std::map<std::string, size_t> index;
  std::vector<std::vector<std::pair<std::string, std::string>>> flat;
  for (const auto& r : rows) {
    flat.push_back(flatten(r));
    for (const auto& kv : flat.back()) {
      if (index.emplace(kv.first, t.header.size()).second) t.header.push_back(kv.first);
    }
  }
  for (const auto& f : flat) {
    std::vector<std::string> row(t.header.size());
    for (const auto& kv : f) row[index[kv.first]] = kv.second;
    t.rows.push_back(std::move(row));
  }
  return t;
}

std::string CsvTable::toString(bool includeHeader) const {
  std::string out;
  if (includeHeader) out += csvLine(header);
  for (const auto& r : rows) out += csvLine(r);
  return out;
}

}  // namespace sm
