#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sm {

struct PageFileInfo {
  std::string path;
  uint64_t totalBytes = 0;
  uint64_t inUseBytes = 0;
  uint64_t peakBytes = 0;
};

struct MemoryInfo {
  bool ok = false;
  // Physical RAM
  uint64_t totalPhys = 0, availPhys = 0, usedPhys = 0;
  double loadPct = 0;  // used / total * 100 (computed, not dwMemoryLoad, for precision)
  // Commit charge (virtual memory backed by RAM + page files)
  std::optional<uint64_t> commitTotal, commitLimit, commitPeak;
  std::optional<uint64_t> systemCache, kernelPaged, kernelNonpaged;
  std::optional<uint32_t> processCount, threadCount, handleCount;
  std::vector<PageFileInfo> pageFiles;
  bool pageFilesKnown = false;
  // Sum of process working sets / private bytes, filled by the sampler from the
  // process collector (accessible processes only).
  std::optional<uint64_t> processWorkingSetTotal, processPrivateTotal;
  std::vector<std::string> notes;
};

MemoryInfo queryMemory();

}  // namespace sm
