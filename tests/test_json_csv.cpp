#include <limits>

#include "test.h"
#include "util/csv.h"
#include "util/json.h"

using namespace sm;

TEST(json_escaping) {
  CHECK_EQ(jsonEscape("a\"b\\c"), std::string("a\\\"b\\\\c"));
  CHECK_EQ(jsonEscape("line\nnext\ttab"), std::string("line\\nnext\\ttab"));
  CHECK_EQ(jsonEscape(std::string("\x01", 1)), std::string("\\u0001"));
  CHECK_EQ(jsonEscape("\xC3\xBC"), std::string("\xC3\xBC"));  // UTF-8 passes through
}

TEST(json_missing_values_are_null) {
  JValue o = JValue::object();
  o.set("a", std::optional<double>());
  o.set("b", JValue::number(std::nullopt, 1));
  o.set("c", std::numeric_limits<double>::quiet_NaN());
  o.set("d", std::optional<unsigned>(7u));
  CHECK_EQ(toJson(o, false), std::string("{\"a\":null,\"b\":null,\"c\":null,\"d\":7}"));
}

TEST(json_numbers_and_nesting) {
  JValue o = JValue::object();
  o.set("pct", JValue(42.345, 1));
  o.set("neg0", JValue(-0.0001, 2));
  o.set("big", 18446744073709551615ull);
  o.set("int", -3);
  JValue arr = JValue::array();
  arr.push(true);
  arr.push("x");
  o.set("arr", arr);
  o.set("empty", JValue::array());
  CHECK_EQ(toJson(o, false),
           std::string("{\"pct\":42.3,\"neg0\":0.00,\"big\":18446744073709551615,\"int\":-3,\"arr\":[true,\"x\"],\"empty\":[]}"));
  // set() replaces existing keys while keeping order
  o.set("pct", 1);
  CHECK(toJson(o, false).find("{\"pct\":1,") == 0);
  std::string pretty = toJson(o, true);
  CHECK(pretty.find("\n  \"pct\": 1") != std::string::npos);
}

TEST(csv_escaping) {
  CHECK_EQ(csvEscape("plain"), std::string("plain"));
  CHECK_EQ(csvEscape("a,b"), std::string("\"a,b\""));
  CHECK_EQ(csvEscape("say \"hi\""), std::string("\"say \"\"hi\"\"\""));
  CHECK_EQ(csvEscape("two\nlines"), std::string("\"two\nlines\""));
  CHECK_EQ(csvEscape(" lead"), std::string("\" lead\""));
  CHECK_EQ(csvLine({"a", "", "c,d"}), std::string("a,,\"c,d\"\r\n"));
}

TEST(csv_flatten_and_union_header) {
  JValue a = JValue::object();
  a.set("name", "eth0");
  JValue nested = JValue::object();
  nested.set("rx", 10);
  nested.set("tx", std::optional<int>());
  a.set("rate", nested);
  JValue ips = JValue::array();
  ips.push("10.0.0.1");
  ips.push("10.0.0.2");
  a.set("ipv4", ips);
  JValue objs = JValue::array();
  objs.push(JValue::object());
  a.set("skipped", objs);
  JValue b = JValue::object();
  b.set("name", "wifi");
  b.set("extra", true);
  CsvTable t = csvFromObjects({a, b});
  CHECK_EQ(t.header.size(), static_cast<size_t>(5));
  CHECK_EQ(t.header[1], std::string("rate.rx"));
  CHECK_EQ(t.toString(),
           std::string("name,rate.rx,rate.tx,ipv4,extra\r\neth0,10,,10.0.0.1;10.0.0.2,\r\nwifi,,,,true\r\n"));
  CHECK_EQ(t.toString(false), std::string("eth0,10,,10.0.0.1;10.0.0.2,\r\nwifi,,,,true\r\n"));
}
