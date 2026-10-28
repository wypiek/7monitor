#include "collectors/battery.h"

#include <windows.h>

#include "platform/wmi.h"

namespace sm {

BatteryInfo queryBatteryStatus() {
  BatteryInfo b;
  SYSTEM_POWER_STATUS s;
  if (!GetSystemPowerStatus(&s)) {
    b.notes.push_back("GetSystemPowerStatus failed");
    return b;
  }
  b.statusOk = true;
  if (s.ACLineStatus == 0) b.acOnline = false;
  else if (s.ACLineStatus == 1) b.acOnline = true;
  if (s.BatteryFlag == 255) {
    b.notes.push_back("battery status unknown to Windows");
  } else if (s.BatteryFlag & 128) {
    b.present = false;
  } else {
    b.present = true;
    b.charging = (s.BatteryFlag & 8) != 0;
    if (s.BatteryLifePercent <= 100) b.percent = s.BatteryLifePercent;
    if (s.BatteryLifeTime != static_cast<DWORD>(-1)) b.estimatedSecondsRemaining = s.BatteryLifeTime;
  }
  return b;
}

void queryBatteryHealth(WmiSession& wmi, BatteryInfo& info) {
  info.designCapacityMWh.reset();
  info.fullChargeCapacityMWh.reset();
  info.healthPct.reset();
  if (!info.present || !*info.present) {
    info.healthNote = "no battery";
    return;
  }
  std::string err;
  if (!wmi.connected() && !wmi.connect(L"ROOT\\WMI", &err)) {
    info.healthNote = "WMI ROOT\\WMI unavailable: " + err;
    return;
  }
  std::vector<WmiRow> rows;
  uint64_t design = 0, full = 0;
  if (wmi.query(L"SELECT DesignedCapacity FROM BatteryStaticData", rows, &err) && !rows.empty())
    rows[0]["DesignedCapacity"].toUInt(design);
  if (wmi.query(L"SELECT FullChargedCapacity FROM BatteryFullChargedCapacity", rows, &err) && !rows.empty())
    rows[0]["FullChargedCapacity"].toUInt(full);
  if (design) info.designCapacityMWh = static_cast<uint32_t>(design);
  if (full) info.fullChargeCapacityMWh = static_cast<uint32_t>(full);
  if (design && full) {
    info.healthPct = 100.0 * static_cast<double>(full) / static_cast<double>(design);
    info.healthNote = "full-charge capacity / design capacity, as reported by the battery driver";
  } else {
    info.healthNote = "battery driver does not expose capacity data" + (err.empty() ? std::string() : " (" + err + ")");
  }
}

}  // namespace sm
