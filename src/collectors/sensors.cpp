#include "collectors/sensors.h"

#include "platform/wmi.h"
#include "util/strings.h"

namespace sm {

AcpiZones queryAcpiThermalZones(WmiSession& wmi) {
  AcpiZones z;
  std::string err;
  if (!wmi.connected() && !wmi.connect(L"ROOT\\WMI", &err)) {
    z.status = "WMI ROOT\\WMI unavailable: " + err;
    return z;
  }
  std::vector<WmiRow> rows;
  if (!wmi.query(L"SELECT InstanceName, CurrentTemperature FROM MSAcpi_ThermalZoneTemperature", rows, &err)) {
    z.status = "ACPI thermal zones unavailable: " + err;
    return z;
  }
  z.queried = true;
  for (auto& r : rows) {
    SensorReading s;
    s.name = r["InstanceName"].s.empty() ? "ACPI thermal zone" : r["InstanceName"].s;
    s.kind = "temperature";
    s.unit = "C";
    s.source = "ACPI thermal zone (WMI)";
    s.note = "ACPI thermal zone, not CPU core temperature";
    uint64_t tenthsKelvin = 0;
    if (r["CurrentTemperature"].toUInt(tenthsKelvin) && tenthsKelvin > 0) {
      double c = static_cast<double>(tenthsKelvin) / 10.0 - 273.15;
      if (c > -50 && c < 150) s.value = c;  // reject obviously bogus firmware values
      else s.note += "; firmware reported an implausible value";
    }
    z.zones.push_back(s);
  }
  if (z.zones.empty()) z.status = "firmware exposes no ACPI thermal zones";
  return z;
}

std::vector<SensorReading> buildSensorList(const std::vector<GpuAdapter>& gpus,
                                           const std::vector<PhysicalDiskInfo>& disks, const AcpiZones& acpi) {
  std::vector<SensorReading> out;
  SensorReading cpu;
  cpu.name = "CPU core temperature";
  cpu.kind = "temperature";
  cpu.unit = "C";
  cpu.source = "none";
  cpu.note = "no documented Windows 7 API; would require a kernel driver, which sevenmon does not install";
  out.push_back(cpu);
  for (const auto& g : gpus) {
    if (g.software) continue;
    std::string src = g.sensorBackend == "none" ? "none" : g.sensorBackend;
    std::string note = g.sensorBackend == "none" && !g.notes.empty() ? g.notes.front() : std::string();
    out.push_back({g.name + " temperature", "temperature", g.temperatureC, "C", src, note});
    out.push_back({g.name + " load", "load", g.utilizationPct, "%", src, note});
    if (g.coreClockMhz) out.push_back({g.name + " core clock", "clock", static_cast<double>(*g.coreClockMhz), "MHz", src, ""});
    if (g.memoryClockMhz) out.push_back({g.name + " memory clock", "clock", static_cast<double>(*g.memoryClockMhz), "MHz", src, ""});
    if (g.powerWatts) out.push_back({g.name + " power", "power", g.powerWatts, "W", src, ""});
  }
  for (const auto& d : disks) {
    SensorReading s{"Disk " + std::to_string(d.number) + " (" + d.model + ") temperature", "temperature",
                    d.smartTemperatureC, "C", "SMART (WMI)", d.smartTemperatureC ? "" : d.smartNote};
    if (!d.smartTemperatureC && s.note.empty()) s.note = "temperature attribute not reported";
    out.push_back(s);
  }
  if (acpi.queried && !acpi.zones.empty()) {
    for (const auto& z : acpi.zones) out.push_back(z);
  } else {
    out.push_back({"ACPI thermal zone", "temperature", std::nullopt, "C", "ACPI thermal zone (WMI)", acpi.status});
  }
  return out;
}

}  // namespace sm
