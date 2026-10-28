#pragma once
// Aggregated sensor readings from trustworthy sources only. CPU core temperature is
// deliberately not reported: Windows 7 offers no documented API for it and reading the
// on-die sensors would require installing a kernel driver.
#include <optional>
#include <string>
#include <vector>

#include "collectors/disk.h"
#include "collectors/gpu.h"

namespace sm {

class WmiSession;

struct SensorReading {
  std::string name;
  std::string kind;  // temperature, power, clock, load
  std::optional<double> value;
  std::string unit;    // C, W, MHz, %
  std::string source;  // NVML, ADL, SMART (WMI), ACPI thermal zone (WMI)
  std::string note;
};

struct AcpiZones {
  bool queried = false;
  std::vector<SensorReading> zones;
  std::string status;
};

// ACPI thermal zones via ROOT\WMI MSAcpi_ThermalZoneTemperature. Usually requires
// administrator rights; many firmwares do not implement it or report constant values.
AcpiZones queryAcpiThermalZones(WmiSession& wmiRootWmi);

std::vector<SensorReading> buildSensorList(const std::vector<GpuAdapter>& gpus,
                                           const std::vector<PhysicalDiskInfo>& disks, const AcpiZones& acpi);

}  // namespace sm
