#pragma once
// Process enumeration (Tool Help snapshot) and per-process details using
// PROCESS_QUERY_LIMITED_INFORMATION (Vista+), which a standard user can obtain for
// most processes. Protected/system processes that refuse access keep the fields the
// snapshot provides (PID, name, threads, base priority) and are flagged accessDenied.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sm {

struct ProcessInfo {
  uint32_t pid = 0, parentPid = 0;
  std::string name;
  std::optional<std::string> path;
  uint32_t threads = 0;
  int basePriority = 0;
  std::optional<std::string> priorityClass;  // Normal, High, ...
  std::optional<double> cpuPct;              // share of total machine capacity
  std::optional<uint64_t> cpuTime100ns;      // kernel + user since start
  std::optional<uint64_t> workingSet, peakWorkingSet, privateBytes;
  std::optional<uint32_t> handles;
  std::optional<uint64_t> createTime;  // FILETIME value
  std::optional<uint64_t> ioReadBytes, ioWriteBytes;
  bool accessDenied = false;
};

enum class ProcessSort { Cpu, Memory, Pid, Name };
bool parseProcessSort(const std::string& s, ProcessSort& out);
const char* processSortName(ProcessSort s);
void sortProcesses(std::vector<ProcessInfo>& procs, ProcessSort by);

class ProcessCollector {
 public:
  explicit ProcessCollector(unsigned logicalCpus);
  std::vector<ProcessInfo> sample();

 private:
  unsigned cpus_;
  double prevT_ = 0;
  // Keyed by (pid, creation time) so a recycled PID never inherits another process's CPU time.
  std::map<std::pair<uint32_t, uint64_t>, uint64_t> prevCpu_;
};

// One-off detailed query for a single PID (no CPU percentage).
std::optional<ProcessInfo> queryProcess(uint32_t pid);

std::string priorityClassName(unsigned long cls);

// Requests termination. Returns an empty string on success or an error description.
std::string terminateProcessById(uint32_t pid);

}  // namespace sm
