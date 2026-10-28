#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sm {

class WmiSession;

struct BatteryInfo {
  bool statusOk = false;
  std::optional<bool> present;      // nullopt: Windows reports "unknown"
  std::optional<bool> acOnline;     // nullopt: unknown
  std::optional<bool> charging;
  std::optional<int> percent;
  // Windows' own estimate (GetSystemPowerStatus.BatteryLifeTime); an estimate, not a
  // measurement, and unavailable while charging or on AC.
  std::optional<uint32_t> estimatedSecondsRemaining;
  // Health: only when the battery driver exposes capacities through WMI.
  std::optional<uint32_t> designCapacityMWh, fullChargeCapacityMWh;
  std::optional<double> healthPct;
  std::string healthNote;
  std::vector<std::string> notes;
};

BatteryInfo queryBatteryStatus();
// Slow tier: fills design/full-charge capacity and health using ROOT\WMI.
void queryBatteryHealth(WmiSession& wmiRootWmi, BatteryInfo& info);

}  // namespace sm
