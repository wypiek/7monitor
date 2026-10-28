#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sm {

// Static processor description (collected once).
struct CpuInfo {
  std::string vendor;  // e.g. "GenuineIntel" (CPUID leaf 0)
  std::string brand;   // CPUID brand string, registry fallback
  unsigned logicalProcessors = 0;
  std::optional<unsigned> physicalCores;  // GetLogicalProcessorInformation
  std::optional<unsigned> packages;
  std::optional<unsigned> l2CacheKiB, l3CacheKiB;
  std::optional<unsigned> maxMhz;  // CallNtPowerInformation, registry ~MHz fallback
  std::vector<std::string> notes;
};

struct CpuCoreTimes {
  uint64_t idle = 0, kernel = 0, user = 0;  // 100 ns units; kernel includes idle
};

// One raw sample of cumulative counters. Utilization is only derivable from two.
struct CpuRaw {
  bool totalOk = false;
  CpuCoreTimes total;
  bool coresOk = false;
  std::vector<CpuCoreTimes> cores;
};

struct CpuUsage {
  std::optional<double> totalPct, userPct, systemPct, idlePct;
  std::vector<std::optional<double>> corePct;
  std::optional<unsigned> currentMhz;  // average across processors, as reported by the power manager
  std::optional<unsigned> maxMhz;
  std::vector<std::string> notes;
};

CpuInfo queryCpuInfo();
CpuRaw sampleCpu();
// Pure function: utilization from the difference of two samples.
CpuUsage computeCpuUsage(const CpuRaw& prev, const CpuRaw& cur);
// Current/max MHz reported by CallNtPowerInformation(ProcessorInformation).
bool queryCpuFrequency(unsigned& currentMhzAvg, unsigned& maxMhz);

}  // namespace sm
