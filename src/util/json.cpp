#include "util/json.h"

#include <cmath>
#include <cstdio>

#include "util/format.h"

namespace sm {

JValue& JValue::set(const std::string& key, JValue v) {
  if (type_ != Type::Object) {
    *this = object();
  }
  for (auto& m : members_) {
    if (m.first == key) {
      m.second = std::move(v);
      return m.second;
    }
  }
  members_.emplace_back(key, std::move(v));
  return members_.back().second;
}

const JValue* JValue::get(const std::string& key) const {
  for (const auto& m : members_) {
    if (m.first == key) return &m.second;
  }
  return nullptr;
}

JValue& JValue::push(JValue v) {
  if (type_ != Type::Array) {
    *this = array();
  }
  items_.push_back(std::move(v));
  return items_.back();
}

static std::string doubleText(double d, int decimals) {
  if (!std::isfinite(d)) return std::string();
  std::string s = fmtFixed(d, decimals);
  if (s == "-0" || s.rfind("-0.", 0) == 0) {
    bool allZero = true;
    for (char c : s.substr(1))
      if (c != '0' && c != '.') allZero = false;
    if (allZero) s = s.substr(1);
  }
  return s;
}

std::string JValue::scalarText() const {
  switch (type_) {
    case Type::Null: return std::string();
    case Type::Bool: return b_ ? "true" : "false";
    case Type::Int: return std::to_string(i_);
    case Type::UInt: return std::to_string(u_);
    case Type::Double: return doubleText(d_, decimals_);
    case Type::String: return s_;
    default: return std::string();
  }
}

std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7f) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));  // UTF-8 passes through unchanged
        }
    }
  }
  return out;
}

static void write(const JValue& v, bool pretty, int indent, std::string& out) {
  auto nl = [&](int level) {
    if (!pretty) return;
    out.push_back('\n');
    out.append(static_cast<size_t>(level) * 2, ' ');
  };
  switch (v.type()) {
    case JValue::Type::Null: out += "null"; break;
    case JValue::Type::Bool: out += v.asBool() ? "true" : "false"; break;
    case JValue::Type::Int: out += std::to_string(v.asInt()); break;
    case JValue::Type::UInt: out += std::to_string(v.asUInt()); break;
    case JValue::Type::Double: {
      std::string t = doubleText(v.asDouble(), v.decimals());
      out += t.empty() ? "null" : t;
      break;
    }
    case JValue::Type::String:
      out.push_back('"');
      out += jsonEscape(v.asString());
      out.push_back('"');
      break;
    case JValue::Type::Array: {
      if (v.items().empty()) {
        out += "[]";
        break;
      }
      out.push_back('[');
      bool first = true;
      for (const auto& it : v.items()) {
        if (!first) out.push_back(',');
        first = false;
        nl(indent + 1);
        write(it, pretty, indent + 1, out);
      }
      nl(indent);
      out.push_back(']');
      break;
    }
    case JValue::Type::Object: {
      if (v.members().empty()) {
        out += "{}";
        break;
      }
      out.push_back('{');
      bool first = true;
      for (const auto& m : v.members()) {
        if (!first) out.push_back(',');
        first = false;
        nl(indent + 1);
        out.push_back('"');
        out += jsonEscape(m.first);
        out += pretty ? "\": " : "\":";
        write(m.second, pretty, indent + 1, out);
      }
      nl(indent);
      out.push_back('}');
      break;
    }
  }
}

std::string toJson(const JValue& v, bool pretty) {
  std::string out;
  write(v, pretty, 0, out);
  return out;
}

}  // namespace sm
