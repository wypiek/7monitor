#include "collectors/smart.h"

#include "collectors/disk.h"
#include "platform/wmi.h"
#include "util/strings.h"

namespace sm {

SmartAttributes parseSmartAttributes(const std::vector<uint8_t>& v) {
  SmartAttributes out;
  // Layout: 2-byte revision, then 30 attributes of 12 bytes:
  // id, flags(2), current, worst, raw[6], reserved.
  if (v.size() < 2 + 12) return out;
  std::optional<double> t194, t190;
  for (size_t i = 0; i < 30; ++i) {
    size_t off = 2 + i * 12;
    if (off + 12 > v.size()) break;
    uint8_t id = v[off];
    if (id == 0) continue;
    const uint8_t* raw = &v[off + 5];
    uint64_t raw48 = 0;
    for (int b = 5; b >= 0; --b) raw48 = (raw48 << 8) | raw[b];
    switch (id) {
      case 194:  // Temperature: lowest raw byte is the current temperature on virtually all drives
        if (raw[0] > 0 && raw[0] < 120) t194 = raw[0];
        break;
      case 190:  // Airflow temperature
        if (raw[0] > 0 && raw[0] < 120) t190 = raw[0];
        break;
      case 9: out.powerOnHours = raw48 & 0xFFFFFFFFull; break;
      case 5: out.reallocatedSectors = raw48 & 0xFFFFFFFFull; break;
      default: break;
    }
  }
  out.temperatureC = t194 ? t194 : t190;
  return out;
}

SmartResult querySmart(WmiSession& wmi) {
  SmartResult r;
  std::string err;
  if (!wmi.connected() && !wmi.connect(L"ROOT\\WMI", &err)) {
    r.status = "WMI ROOT\\WMI unavailable: " + err;
    return r;
  }
  std::vector<WmiRow> rows;
  if (!wmi.query(L"SELECT InstanceName, PredictFailure FROM MSStorageDriver_FailurePredictStatus", rows, &err)) {
    r.status = "SMART status unavailable: " + err;
    return r;
  }
  r.queried = true;
  for (auto& row : rows) {
    SmartEntry e;
    e.instanceName = row["InstanceName"].s;
    if (row["PredictFailure"].kind == WmiValue::Kind::Bool) e.predictFailure = row["PredictFailure"].b;
    r.entries.push_back(e);
  }
  std::vector<WmiRow> data;
  if (wmi.query(L"SELECT InstanceName, VendorSpecific FROM MSStorageDriver_FailurePredictData", data, &err)) {
    for (auto& row : data) {
      auto attrs = parseSmartAttributes(row["VendorSpecific"].bytes);
      for (auto& e : r.entries) {
        if (e.instanceName == row["InstanceName"].s) {
          e.temperatureC = attrs.temperatureC;
          e.powerOnHours = attrs.powerOnHours;
          e.reallocatedSectors = attrs.reallocatedSectors;
        }
      }
    }
  }
  if (r.entries.empty()) r.status = "no disks report SMART through WMI";
  return r;
}

static std::string alnumLower(const std::string& s) {
  std::string out;
  for (char c : toLower(s))
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(c);
  return out;
}

void attachSmart(const SmartResult& smart, std::vector<PhysicalDiskInfo>& disks) {
  for (auto& d : disks) {
    d.smartPredictFailure.reset();
    d.smartTemperatureC.reset();
    d.smartNote = smart.status;
    std::string model = alnumLower(d.model);
    if (model.size() < 4) continue;
    for (const auto& e : smart.entries) {
      if (alnumLower(e.instanceName).find(model) != std::string::npos) {
        d.smartPredictFailure = e.predictFailure;
        d.smartTemperatureC = e.temperatureC;
        d.smartNote.clear();
        break;
      }
    }
    if (smart.queried && !d.smartPredictFailure && d.smartNote.empty())
      d.smartNote = "disk not reported by the SMART WMI provider";
  }
}

}  // namespace sm
