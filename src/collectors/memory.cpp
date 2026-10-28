#include "collectors/memory.h"

#include <windows.h>
#include <psapi.h>

#include "platform/win.h"
#include "util/strings.h"

namespace sm {

namespace {
struct SysPageFileInfo {
  ULONG NextEntryOffset;
  ULONG TotalSize;   // pages
  ULONG TotalInUse;  // pages
  ULONG PeakUsage;   // pages
  USHORT NameLength;
  USHORT NameMaximumLength;
  PWSTR NameBuffer;
};
using NtQuerySystemInformationFn = LONG(WINAPI*)(ULONG, PVOID, ULONG, PULONG);

std::string cleanPageFilePath(std::wstring p) {
  // NT paths look like "\??\C:\pagefile.sys".
  if (p.compare(0, 4, L"\\??\\") == 0) p = p.substr(4);
  return toUtf8(p);
}
}  // namespace

MemoryInfo queryMemory() {
  MemoryInfo m;
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) {
    m.ok = true;
    m.totalPhys = ms.ullTotalPhys;
    m.availPhys = ms.ullAvailPhys;
    m.usedPhys = ms.ullTotalPhys - ms.ullAvailPhys;
    m.loadPct = ms.ullTotalPhys ? 100.0 * static_cast<double>(m.usedPhys) / static_cast<double>(ms.ullTotalPhys) : 0;
  } else {
    m.notes.push_back("GlobalMemoryStatusEx failed: " + win::errorText(GetLastError()));
  }

  PERFORMANCE_INFORMATION pi;
  pi.cb = sizeof(pi);
  if (GetPerformanceInfo(&pi, sizeof(pi))) {
    uint64_t page = pi.PageSize;
    m.commitTotal = static_cast<uint64_t>(pi.CommitTotal) * page;
    m.commitLimit = static_cast<uint64_t>(pi.CommitLimit) * page;
    m.commitPeak = static_cast<uint64_t>(pi.CommitPeak) * page;
    m.systemCache = static_cast<uint64_t>(pi.SystemCache) * page;
    m.kernelPaged = static_cast<uint64_t>(pi.KernelPaged) * page;
    m.kernelNonpaged = static_cast<uint64_t>(pi.KernelNonpaged) * page;
    m.processCount = pi.ProcessCount;
    m.threadCount = pi.ThreadCount;
    m.handleCount = pi.HandleCount;
  } else {
    m.notes.push_back("GetPerformanceInfo failed: commit figures unavailable");
  }

  static auto nqsi = win::ntdllProc<NtQuerySystemInformationFn>("NtQuerySystemInformation");
  if (nqsi) {
    std::vector<unsigned char> buf(4096);
    ULONG ret = 0;
    LONG st = 0;
    for (int tries = 0; tries < 4; ++tries) {
      st = nqsi(18 /* SystemPageFileInformation */, buf.data(), static_cast<ULONG>(buf.size()), &ret);
      if (st != static_cast<LONG>(0xC0000004L) /* STATUS_INFO_LENGTH_MISMATCH */) break;
      buf.resize(buf.size() * 4);
    }
    if (st == 0) {
      m.pageFilesKnown = true;
      SYSTEM_INFO si;
      GetSystemInfo(&si);
      uint64_t page = si.dwPageSize;
      size_t off = 0;
      while (ret > 0 && off + sizeof(SysPageFileInfo) <= buf.size()) {
        const auto* e = reinterpret_cast<const SysPageFileInfo*>(buf.data() + off);
        PageFileInfo pf;
        if (e->NameBuffer && e->NameLength)
          pf.path = cleanPageFilePath(std::wstring(e->NameBuffer, e->NameLength / sizeof(wchar_t)));
        pf.totalBytes = e->TotalSize * page;
        pf.inUseBytes = e->TotalInUse * page;
        pf.peakBytes = e->PeakUsage * page;
        m.pageFiles.push_back(pf);
        if (e->NextEntryOffset == 0) break;
        off += e->NextEntryOffset;
      }
    } else {
      m.notes.push_back("page file details unavailable");
    }
  }
  return m;
}

}  // namespace sm
