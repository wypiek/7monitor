#pragma once
// Snapshot -> JSON documents (schema documented in docs/json-schema.md). The same
// documents feed CSV output, so both formats always carry identical fields.
#include <string>
#include <vector>

#include "collectors/process.h"
#include "sampler/sampler.h"
#include "util/json.h"

namespace sm {

constexpr int kSchemaVersion = 1;

// Wraps data in the common envelope:
// {"schema_version":1,"tool":"sevenmon","tool_version":"x","type":T,"timestamp":...,"data":D}
JValue envelope(const std::string& type, const std::string& timestamp, JValue data);

JValue jsonSystem(const SystemInfo& s);
JValue jsonCpu(const Snapshot& s);
JValue jsonMemory(const Snapshot& s);
JValue jsonGpu(const Snapshot& s);
JValue jsonDisk(const Snapshot& s);
JValue jsonNetwork(const Snapshot& s);
JValue jsonBattery(const BatteryInfo& b);
JValue jsonProcess(const ProcessInfo& p);
JValue jsonProcesses(const std::vector<ProcessInfo>& procs, ProcessSort sort, size_t total);
JValue jsonSensors(const Snapshot& s);
JValue jsonHardware(const Snapshot& s);
JValue jsonSummary(const Snapshot& s);
// Flat, fixed-field record used by export/watch streaming (stable CSV columns).
JValue jsonSample(const Snapshot& s);

// Aggregates
struct Totals {
  std::optional<double> diskReadBps, diskWriteBps, netRxBps, netTxBps;
  unsigned interfacesUp = 0;
};
Totals computeTotals(const Snapshot& s);

std::string hex4(unsigned v);

}  // namespace sm
