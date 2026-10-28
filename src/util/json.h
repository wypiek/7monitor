#pragma once
// Minimal ordered JSON document model and serializer used for --json output.
// Missing measurements are represented as null, never as 0 or a placeholder.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sm {

class JValue {
 public:
  enum class Type { Null, Bool, Int, UInt, Double, String, Array, Object };

  JValue() = default;
  JValue(std::nullptr_t) {}
  JValue(bool b) : type_(Type::Bool), b_(b) {}
  JValue(int v) : type_(Type::Int), i_(v) {}
  JValue(long v) : type_(Type::Int), i_(v) {}
  JValue(long long v) : type_(Type::Int), i_(v) {}
  JValue(unsigned v) : type_(Type::UInt), u_(v) {}
  JValue(unsigned long v) : type_(Type::UInt), u_(v) {}
  JValue(unsigned long long v) : type_(Type::UInt), u_(v) {}
  JValue(double v, int decimals = 2) : type_(Type::Double), d_(v), decimals_(decimals) {}
  JValue(const char* s) : type_(Type::String), s_(s ? s : "") {}
  JValue(std::string s) : type_(Type::String), s_(std::move(s)) {}

  template <typename T>
  JValue(const std::optional<T>& o) {
    if (o) *this = JValue(*o);
  }

  static JValue object() {
    JValue v;
    v.type_ = Type::Object;
    return v;
  }
  static JValue array() {
    JValue v;
    v.type_ = Type::Array;
    return v;
  }
  static JValue number(const std::optional<double>& v, int decimals) {
    return v ? JValue(*v, decimals) : JValue();
  }

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }

  // Object: insertion-ordered keys. Setting an existing key replaces its value.
  JValue& set(const std::string& key, JValue v);
  const JValue* get(const std::string& key) const;
  const std::vector<std::pair<std::string, JValue>>& members() const { return members_; }

  // Array
  JValue& push(JValue v);
  const std::vector<JValue>& items() const { return items_; }

  bool asBool() const { return b_; }
  long long asInt() const { return i_; }
  unsigned long long asUInt() const { return u_; }
  double asDouble() const { return d_; }
  int decimals() const { return decimals_; }
  const std::string& asString() const { return s_; }

  // Scalar to plain text (used for CSV cells): null -> "".
  std::string scalarText() const;

 private:
  Type type_ = Type::Null;
  bool b_ = false;
  long long i_ = 0;
  unsigned long long u_ = 0;
  double d_ = 0;
  int decimals_ = 2;
  std::string s_;
  std::vector<JValue> items_;
  std::vector<std::pair<std::string, JValue>> members_;
};

std::string jsonEscape(const std::string& utf8);
// pretty=false produces a single line (used for NDJSON streaming).
std::string toJson(const JValue& v, bool pretty);

}  // namespace sm
