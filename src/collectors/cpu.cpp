#include "collectors/cpu.h"

#include <windows.h>
#include <powrprof.h>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

#include <cstring>

#include "platform/win.h"
#include "util/rates.h"
#include "util/strings.h"

namespace sm {

namespace {

void cpuid(unsigned leaf, unsigned regs[4]) {
#if defined(_MSC_VER)
  int r[4];
  __cpuid(r, static_cast<int>(leaf));
  for (int i = 0; i < 4; ++i) regs[i] = static_cast<unsigned>(r[i]);
#else
  __cpuid(leaf, regs[0], regs[1], regs[2], regs[3]);
#endif
}

// Layout of SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION (NtQuerySystemInformation class 8).
struct SppiEntry {
  LARGE_INTEGER IdleTime;
  LARGE_INTEGER KernelTime;
  LARGE_INTEGER UserTime;
  LARGE_INTEGER DpcTime;
  LARGE_INTEGER InterruptTime;
  ULONG InterruptCount;
};

// Layout of PROCESSOR_POWER_INFORMATION (not declared by every SDK).
struct PpiEntry {
  ULONG Number;
  ULONG MaxMhz;
  ULONG CurrentMhz;
  ULONG MhzLimit;
  ULONG MaxIdleState;
  ULONG CurrentIdleState;
};

using NtQuerySystemInformationFn = LONG(WINAPI*)(ULONG, PVOID, ULONG, PULONG);

unsigned logicalCount() {
  // GetActiveProcessorCount exists from Windows 7; it covers all processor groups.
  using Fn = DWORD(WINAPI*)(WORD);
  auto fn = win::procAddress<Fn>(GetModuleHandleW(L"kernel32.dll"), "GetActiveProcessorCount");
  if (fn) {
    DWORD n = fn(0xFFFF /* ALL_PROCESSOR_GROUPS */);
    if (n) return n;
  }
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwNumberOfProcessors;
}

}  // namespace

CpuInfo queryCpuInfo() {
  CpuInfo info;
  unsigned r[4] = {};
  cpuid(0, r);
  char vendor[13] = {};
  memcpy(vendor + 0, &r[1], 4);
  memcpy(vendor + 4, &r[3], 4);
  memcpy(vendor + 8, &r[2], 4);
  info.vendor = trim(vendor);

  cpuid(0x80000000u, r);
  if (r[0] >= 0x80000004u) {
    char brand[49] = {};
    for (unsigned i = 0; i < 3; ++i) {
      cpuid(0x80000002u + i, r);
      memcpy(brand + i * 16, r, 16);
    }
    info.brand = sanitize(trim(brand));
  }
  const std::wstring cpuKey = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
  if (info.brand.empty()) {
    if (auto name = win::regString(HKEY_LOCAL_MACHINE, cpuKey, L"ProcessorNameString"))
      info.brand = sanitize(trim(toUtf8(*name)));
  }
  if (info.brand.empty()) info.brand = "Unknown processor";

  info.logicalProcessors = logicalCount();

  DWORD len = 0;
  GetLogicalProcessorInformation(nullptr, &len);
  if (len > 0) {
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
    len = static_cast<DWORD>(buf.size() * sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    if (GetLogicalProcessorInformation(buf.data(), &len)) {
      unsigned cores = 0, packages = 0, l2 = 0, l3 = 0;
      size_t count = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
      for (size_t i = 0; i < count; ++i) {
        const auto& e = buf[i];
        if (e.Relationship == RelationProcessorCore) ++cores;
        else if (e.Relationship == RelationProcessorPackage) ++packages;
        else if (e.Relationship == RelationCache) {
          if (e.Cache.Level == 2) l2 += e.Cache.Size / 1024;
          if (e.Cache.Level == 3) l3 += e.Cache.Size / 1024;
        }
      }
      if (cores) info.physicalCores = cores;
      if (packages) info.packages = packages;
      if (l2) info.l2CacheKiB = l2;
      if (l3) info.l3CacheKiB = l3;
    }
  }
  if (!info.physicalCores) info.notes.push_back("physical core count unavailable (GetLogicalProcessorInformation failed)");

  unsigned cur = 0, mx = 0;
  if (queryCpuFrequency(cur, mx) && mx) {
    info.maxMhz = mx;
  } else if (auto mhz = win::regDword(HKEY_LOCAL_MACHINE, cpuKey, L"~MHz")) {
    if (*mhz) info.maxMhz = *mhz;
  }
  if (info.logicalProcessors > 64)
    info.notes.push_back("more than 64 logical processors: per-core figures cover processor group 0 only");
  return info;
}

bool queryCpuFrequency(unsigned& currentAvg, unsigned& maxMhz) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  unsigned n = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
  std::vector<PpiEntry> buf(n);
  LONG st = CallNtPowerInformation(ProcessorInformation, nullptr, 0, buf.data(),
                                   static_cast<ULONG>(buf.size() * sizeof(PpiEntry)));
  if (st != 0) return false;
  uint64_t sum = 0;
  unsigned cnt = 0, mx = 0;
  for (const auto& e : buf) {
    if (e.CurrentMhz == 0) continue;
    sum += e.CurrentMhz;
    ++cnt;
    if (e.MaxMhz > mx) mx = e.MaxMhz;
  }
  if (cnt == 0) return false;
  currentAvg = static_cast<unsigned>(sum / cnt);
  maxMhz = mx;
  return true;
}

CpuRaw sampleCpu() {
  CpuRaw raw;
  FILETIME idle, kernel, user;
  if (GetSystemTimes(&idle, &kernel, &user)) {
    raw.totalOk = true;
    raw.total.idle = win::ft64(idle);
    raw.total.kernel = win::ft64(kernel);
    raw.total.user = win::ft64(user);
  }
  static auto nqsi = win::ntdllProc<NtQuerySystemInformationFn>("NtQuerySystemInformation");
  if (nqsi) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    std::vector<SppiEntry> buf(si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1);
    ULONG ret = 0;
    LONG st = nqsi(8 /* SystemProcessorPerformanceInformation */, buf.data(),
                   static_cast<ULONG>(buf.size() * sizeof(SppiEntry)), &ret);
    if (st == 0 && ret >= sizeof(SppiEntry)) {
      raw.coresOk = true;
      size_t n = ret / sizeof(SppiEntry);
      raw.cores.resize(n);
      for (size_t i = 0; i < n; ++i) {
        raw.cores[i].idle = static_cast<uint64_t>(buf[i].IdleTime.QuadPart);
        raw.cores[i].kernel = static_cast<uint64_t>(buf[i].KernelTime.QuadPart);
        raw.cores[i].user = static_cast<uint64_t>(buf[i].UserTime.QuadPart);
      }
    }
  }
  return raw;
}

CpuUsage computeCpuUsage(const CpuRaw& prev, const CpuRaw& cur) {
  CpuUsage u;
  if (prev.totalOk && cur.totalOk) {
    auto di = counterDelta64(prev.total.idle, cur.total.idle);
    auto dk = counterDelta64(prev.total.kernel, cur.total.kernel);
    auto du = counterDelta64(prev.total.user, cur.total.user);
    if (di && dk && du && *di <= *dk) {
      u.totalPct = cpuBusyPercent(*di, *dk, *du);
      uint64_t total = *dk + *du;
      u.userPct = sharePercent(*du, total);
      u.systemPct = sharePercent(*dk - *di, total);
      u.idlePct = sharePercent(*di, total);
    }
    if (!u.totalPct) u.notes.push_back("no CPU time elapsed between samples");
  } else if (!cur.totalOk) {
    u.notes.push_back("GetSystemTimes failed");
  }
  if (prev.coresOk && cur.coresOk && prev.cores.size() == cur.cores.size()) {
    for (size_t i = 0; i < cur.cores.size(); ++i) {
      const auto& a = prev.cores[i];
      const auto& b = cur.cores[i];
      auto di = counterDelta64(a.idle, b.idle);
      auto dk = counterDelta64(a.kernel, b.kernel);
      auto du = counterDelta64(a.user, b.user);
      if (di && dk && du) u.corePct.push_back(cpuBusyPercent(*di, *dk, *du));
      else u.corePct.push_back(std::nullopt);
    }
  } else if (!cur.coresOk) {
    u.notes.push_back("per-core counters unavailable (NtQuerySystemInformation)");
  }
  return u;
}

}  // namespace sm
