#include "collectors/gpu.h"
#include "collectors/smart.h"
#include "collectors/smbios.h"
#include "test.h"

using namespace sm;

static void addStruct(std::vector<uint8_t>& t, uint8_t type, std::vector<uint8_t> fields, std::vector<std::string> strs) {
  std::vector<uint8_t> s = {type, static_cast<uint8_t>(4 + fields.size()), 0, 0};
  s.insert(s.end(), fields.begin(), fields.end());
  t.insert(t.end(), s.begin(), s.end());
  for (const auto& x : strs) {
    t.insert(t.end(), x.begin(), x.end());
    t.push_back(0);
  }
  if (strs.empty()) t.push_back(0);
  t.push_back(0);
}

TEST(smbios_parse_bios_system_board) {
  std::vector<uint8_t> table;
  // type 0: vendor=1 version=2 (offset 4,5), start seg (6,7), date=3 at offset 8
  addStruct(table, 0, {1, 2, 0, 0, 3}, {"Acme BIOS", "1.2.3", "01/02/2015"});
  addStruct(table, 1, {1, 2, 3, 0}, {"Contoso", "Laptop 7", "To Be Filled By O.E.M."});
  addStruct(table, 2, {1, 2, 0}, {"Contoso", "Board X"});
  addStruct(table, 127, {}, {});
  std::vector<uint8_t> raw = {0, 2, 7, 0};
  uint32_t len = static_cast<uint32_t>(table.size());
  for (int i = 0; i < 4; ++i) raw.push_back(static_cast<uint8_t>(len >> (8 * i)));
  raw.insert(raw.end(), table.begin(), table.end());
  auto info = parseSmbios(raw);
  CHECK(info.ok);
  CHECK_EQ(info.version, std::string("2.7"));
  CHECK_EQ(*info.biosVendor, std::string("Acme BIOS"));
  CHECK_EQ(*info.biosVersion, std::string("1.2.3"));
  CHECK_EQ(*info.biosDate, std::string("01/02/2015"));
  CHECK_EQ(*info.systemManufacturer, std::string("Contoso"));
  CHECK_EQ(*info.systemProduct, std::string("Laptop 7"));
  CHECK(!info.systemVersion.has_value());  // OEM placeholder is not information
  CHECK_EQ(*info.boardProduct, std::string("Board X"));
  CHECK(!info.boardVersion.has_value());  // string index 0 = none
}

TEST(smbios_malformed_input_is_safe) {
  CHECK(!parseSmbios({}).ok);
  std::vector<uint8_t> raw = {0, 2, 7, 0, 0xFF, 0xFF, 0, 0, 1, 200, 0};  // length overflows, header length overflows
  auto info = parseSmbios(raw);
  CHECK(!info.biosVendor.has_value());
  std::vector<uint8_t> raw2 = {0, 2, 7, 0, 6, 0, 0, 0, 0, 2, 0, 0, 1, 1};  // header length < 4
  CHECK(!parseSmbios(raw2).biosVendor.has_value());
}

TEST(smart_attribute_parsing) {
  std::vector<uint8_t> v(512, 0);
  auto put = [&](int slot, uint8_t id, std::vector<uint8_t> raw) {
    size_t off = 2 + static_cast<size_t>(slot) * 12;
    v[off] = id;
    for (size_t i = 0; i < raw.size(); ++i) v[off + 5 + i] = raw[i];
  };
  put(0, 5, {3, 0, 0, 0, 0, 0});
  put(1, 9, {0x10, 0x27, 0, 0, 0, 0});  // 10000 hours
  put(2, 194, {38, 0, 20, 0, 50, 0});   // current 38 C, min/max in upper bytes
  auto a = parseSmartAttributes(v);
  CHECK_NEAR(*a.temperatureC, 38.0, 1e-9);
  CHECK_EQ(*a.powerOnHours, 10000ull);
  CHECK_EQ(*a.reallocatedSectors, 3ull);
  std::vector<uint8_t> bogus(512, 0);
  bogus[2] = 194;
  bogus[7] = 200;  // implausible
  CHECK(!parseSmartAttributes(bogus).temperatureC.has_value());
  CHECK(!parseSmartAttributes({1, 2, 3}).temperatureC.has_value());
}

TEST(gpu_vendor_and_name_matching) {
  CHECK_EQ(gpuVendorName(0x10DE), std::string("NVIDIA"));
  CHECK_EQ(gpuVendorName(0x1002), std::string("AMD"));
  CHECK_EQ(gpuVendorName(0x8086), std::string("Intel"));
  CHECK_EQ(gpuVendorName(0x1414), std::string("Microsoft"));
  CHECK_EQ(gpuVendorName(0xABCD), std::string("Unknown (0xABCD)"));
  CHECK(gpuNamesMatch("NVIDIA GeForce GTX 1650", "GeForce GTX 1650"));
  CHECK(gpuNamesMatch("AMD Radeon HD 7700 Series", "AMD Radeon HD 7700 Series"));
  CHECK(!gpuNamesMatch("NVIDIA GeForce GTX 1650", "GeForce GTX 1060"));
  CHECK(!gpuNamesMatch("", "x"));
}
