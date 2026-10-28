// Live tests: read the real machine. They check structural sanity only (never exact
// values), so they pass on any hardware where the APIs work.
#include <windows.h>

#include "collectors/battery.h"
#include "collectors/cpu.h"
#include "collectors/disk.h"
#include "collectors/gpu.h"
#include "collectors/memory.h"
#include "collectors/network.h"
#include "collectors/process.h"
#include "collectors/system.h"
#include "output/report_json.h"
#include "sampler/sampler.h"
#include "test.h"
#include "util/csv.h"
#include "util/strings.h"

using namespace sm;

LIVE_TEST(live_cpu) {
  CpuInfo i = queryCpuInfo();
  CHECK(i.logicalProcessors > 0);
  CHECK(!i.brand.empty());
  CpuRaw a = sampleCpu();
  CHECK(a.totalOk);
  Sleep(200);
  CpuRaw b = sampleCpu();
  auto u = computeCpuUsage(a, b);
  if (u.totalPct) CHECK(*u.totalPct >= 0 && *u.totalPct <= 100);
}

LIVE_TEST(live_memory) {
  MemoryInfo m = queryMemory();
  CHECK(m.ok);
  CHECK(m.totalPhys > 0);
  CHECK(m.availPhys <= m.totalPhys);
  CHECK(m.loadPct >= 0 && m.loadPct <= 100);
}

LIVE_TEST(live_processes_include_self) {
  ProcessCollector pc(1);
  auto first = pc.sample();
  Sleep(100);
  auto procs = pc.sample();
  CHECK(!procs.empty());
  bool found = false;
  for (const auto& p : procs) {
    if (p.pid == GetCurrentProcessId()) {
      found = true;
      CHECK(p.path.has_value());
      CHECK(p.workingSet.has_value() && *p.workingSet > 0);
      CHECK(p.cpuPct.has_value());
      CHECK(icontains(p.name, "sevenmon_tests"));
    }
  }
  CHECK(found);
  auto self = queryProcess(GetCurrentProcessId());
  CHECK(self.has_value());
  CHECK(!queryProcess(0xFFFFFFF0u).has_value());
  CHECK(!terminateProcessById(0).empty());
  CHECK(!terminateProcessById(GetCurrentProcessId()).empty());
}

LIVE_TEST(live_system_and_volumes) {
  SystemInfo s = querySystemInfo();
  CHECK(!s.productName.empty());
  CHECK(s.major >= 6);
  CHECK(!s.osArch.empty());
  auto vols = queryVolumes();
  CHECK(!vols.empty());
  for (const auto& v : vols) CHECK(v.root.size() == 3);
}

LIVE_TEST(live_network_and_gpu_do_not_crash) {
  NetworkCollector nc;
  auto a = nc.sample(true);
  Sleep(100);
  auto b = nc.sample(true);
  for (const auto& n : b) {
    if (n.rxBytesPerSec) CHECK(*n.rxBytesPerSec >= 0);
  }
  auto gpus = detectGpus(nullptr);
  GpuMonitor gm;
  gm.update(gpus);  // must not crash with or without vendor libraries
  for (const auto& g : gpus) {
    if (g.sensorBackend == "none") CHECK(!g.temperatureC.has_value() && !g.utilizationPct.has_value());
  }
  queryPhysicalDisks();
  queryBatteryStatus();
}

LIVE_TEST(live_snapshot_json_csv_roundtrip) {
  Sampler::Options o;
  Sampler s(o);
  s.sampleFast();
  s.collectSlow();
  Sleep(150);
  s.sampleFast();
  Snapshot snap = s.snapshot();
  CHECK(snap.ratesValid);
  std::string j = toJson(envelope("summary", snap.timestamp, jsonSummary(snap)), true);
  CHECK(j.find("\"schema_version\": 1") != std::string::npos);
  CHECK(j.find("nan") == std::string::npos && j.find("inf") == std::string::npos);
  CsvTable t = csvFromObjects({jsonSample(snap)});
  CHECK_EQ(t.header.size(), t.rows[0].size());
  CHECK_EQ(t.header[0], std::string("timestamp"));
}
