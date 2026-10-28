#include "collectors/process.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>

#include "platform/win.h"
#include "util/rates.h"
#include "util/strings.h"

namespace sm {

bool parseProcessSort(const std::string& s, ProcessSort& out) {
  std::string l = toLower(s);
  if (l == "cpu") out = ProcessSort::Cpu;
  else if (l == "memory" || l == "mem") out = ProcessSort::Memory;
  else if (l == "pid") out = ProcessSort::Pid;
  else if (l == "name") out = ProcessSort::Name;
  else return false;
  return true;
}

const char* processSortName(ProcessSort s) {
  switch (s) {
    case ProcessSort::Cpu: return "cpu";
    case ProcessSort::Memory: return "memory";
    case ProcessSort::Pid: return "pid";
    case ProcessSort::Name: return "name";
  }
  return "cpu";
}

void sortProcesses(std::vector<ProcessInfo>& p, ProcessSort by) {
  auto byPid = [](const ProcessInfo& a, const ProcessInfo& b) { return a.pid < b.pid; };
  switch (by) {
    case ProcessSort::Pid: std::sort(p.begin(), p.end(), byPid); break;
    case ProcessSort::Name:
      std::stable_sort(p.begin(), p.end(), [&](const ProcessInfo& a, const ProcessInfo& b) {
        std::string la = toLower(a.name), lb = toLower(b.name);
        return la != lb ? la < lb : a.pid < b.pid;
      });
      break;
    case ProcessSort::Cpu:
      std::stable_sort(p.begin(), p.end(), [&](const ProcessInfo& a, const ProcessInfo& b) {
        double ca = a.cpuPct.value_or(-1), cb = b.cpuPct.value_or(-1);
        if (ca != cb) return ca > cb;
        uint64_t wa = a.workingSet.value_or(0), wb = b.workingSet.value_or(0);
        return wa != wb ? wa > wb : a.pid < b.pid;
      });
      break;
    case ProcessSort::Memory:
      std::stable_sort(p.begin(), p.end(), [&](const ProcessInfo& a, const ProcessInfo& b) {
        uint64_t wa = a.workingSet.value_or(0), wb = b.workingSet.value_or(0);
        return wa != wb ? wa > wb : a.pid < b.pid;
      });
      break;
  }
}

std::string priorityClassName(unsigned long c) {
  switch (c) {
    case IDLE_PRIORITY_CLASS: return "Idle";
    case BELOW_NORMAL_PRIORITY_CLASS: return "Below normal";
    case NORMAL_PRIORITY_CLASS: return "Normal";
    case ABOVE_NORMAL_PRIORITY_CLASS: return "Above normal";
    case HIGH_PRIORITY_CLASS: return "High";
    case REALTIME_PRIORITY_CLASS: return "Realtime";
    default: return "Unknown";
  }
}

namespace {

// Fills details obtainable through a limited-information handle.
void fillDetails(ProcessInfo& p) {
  if (p.pid == 0) {
    p.accessDenied = true;  // System Idle Process is not a real process object
    return;
  }
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
  if (!h) {
    p.accessDenied = true;
    return;
  }
  win::Handle guard(h);
  FILETIME c, e, k, u;
  if (GetProcessTimes(h, &c, &e, &k, &u)) {
    p.createTime = win::ft64(c);
    p.cpuTime100ns = win::ft64(k) + win::ft64(u);
  }
  PROCESS_MEMORY_COUNTERS_EX mc;
  ZeroMemory(&mc, sizeof(mc));
  mc.cb = sizeof(mc);
  if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mc), sizeof(mc))) {
    p.workingSet = mc.WorkingSetSize;
    p.peakWorkingSet = mc.PeakWorkingSetSize;
    p.privateBytes = mc.PrivateUsage;
  }
  DWORD hc = 0;
  if (GetProcessHandleCount(h, &hc)) p.handles = hc;
  DWORD cls = GetPriorityClass(h);
  if (cls) p.priorityClass = priorityClassName(cls);
  IO_COUNTERS io;
  if (GetProcessIoCounters(h, &io)) {
    p.ioReadBytes = io.ReadTransferCount;
    p.ioWriteBytes = io.WriteTransferCount;
  }
  wchar_t path[MAX_PATH * 4];
  DWORD len = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
  if (QueryFullProcessImageNameW(h, 0, path, &len)) p.path = toUtf8(std::wstring(path, len));
}

bool snapshot(std::vector<ProcessInfo>& out) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  win::Handle guard(snap);
  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  if (!Process32FirstW(snap, &pe)) return false;
  do {
    ProcessInfo p;
    p.pid = pe.th32ProcessID;
    p.parentPid = pe.th32ParentProcessID;
    p.name = toUtf8(std::wstring(pe.szExeFile));
    p.threads = pe.cntThreads;
    p.basePriority = static_cast<int>(pe.pcPriClassBase);
    out.push_back(std::move(p));
  } while (Process32NextW(snap, &pe));
  return true;
}

}  // namespace

ProcessCollector::ProcessCollector(unsigned logicalCpus) : cpus_(logicalCpus ? logicalCpus : 1) {}

std::vector<ProcessInfo> ProcessCollector::sample() {
  std::vector<ProcessInfo> procs;
  if (!snapshot(procs)) return procs;
  double now = win::monotonicSeconds();
  uint64_t wall100ns = prevT_ > 0 ? static_cast<uint64_t>((now - prevT_) * 1e7) : 0;
  std::map<std::pair<uint32_t, uint64_t>, uint64_t> cur;
  for (auto& p : procs) {
    fillDetails(p);
    if (!p.cpuTime100ns || !p.createTime) continue;
    auto key = std::make_pair(p.pid, *p.createTime);
    cur[key] = *p.cpuTime100ns;
    auto it = prevCpu_.find(key);
    if (it != prevCpu_.end() && wall100ns > 0) {
      auto d = counterDelta64(it->second, *p.cpuTime100ns);
      if (d) p.cpuPct = processCpuPercent(*d, wall100ns, cpus_);
    }
  }
  prevCpu_.swap(cur);  // exited processes drop out automatically
  prevT_ = now;
  return procs;
}

std::optional<ProcessInfo> queryProcess(uint32_t pid) {
  std::vector<ProcessInfo> procs;
  if (!snapshot(procs)) return std::nullopt;
  for (auto& p : procs) {
    if (p.pid == pid) {
      fillDetails(p);
      return p;
    }
  }
  return std::nullopt;
}

std::string terminateProcessById(uint32_t pid) {
  if (pid == 0 || pid == 4) return "refusing to terminate a system process";
  if (pid == GetCurrentProcessId()) return "refusing to terminate sevenmon itself";
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
  if (!h) return win::errorText(GetLastError());
  win::Handle guard(h);
  if (!TerminateProcess(h, 1)) return win::errorText(GetLastError());
  return std::string();
}

}  // namespace sm
