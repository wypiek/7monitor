#include "collectors/cpu.h"
#include "collectors/disk.h"
#include "collectors/network.h"
#include "test.h"
#include "util/rates.h"

using namespace sm;

TEST(cpu_busy_percent_basic) {
  // 1 s of a 2-CPU machine: kernel 1.0 s (incl. idle 0.6 s), user 1.0 s -> busy 1.4/2.0
  auto v = cpuBusyPercent(6000000, 10000000, 10000000);
  CHECK(v.has_value());
  CHECK_NEAR(*v, 70.0, 1e-9);
}

TEST(cpu_busy_percent_zero_elapsed_is_unavailable) { CHECK(!cpuBusyPercent(0, 0, 0).has_value()); }

TEST(cpu_busy_percent_rejects_idle_above_total) { CHECK(!cpuBusyPercent(50, 10, 10).has_value()); }

TEST(cpu_usage_needs_two_samples) {
  CpuRaw a;
  a.totalOk = true;
  a.total = {1000, 3000, 1000};
  CpuRaw b = a;
  b.total = {1600, 4000, 2000};  // idle +600, kernel +1000, user +1000 -> busy 1400/2000
  auto u = computeCpuUsage(a, b);
  CHECK(u.totalPct.has_value());
  CHECK_NEAR(*u.totalPct, 70.0, 1e-9);
  CHECK_NEAR(*u.userPct, 50.0, 1e-9);
  CHECK_NEAR(*u.systemPct, 20.0, 1e-9);
  CHECK_NEAR(*u.idlePct, 30.0, 1e-9);
  // identical samples: nothing elapsed -> no value rather than 0%
  auto same = computeCpuUsage(a, a);
  CHECK(!same.totalPct.has_value());
}

TEST(cpu_usage_per_core_and_counter_reset) {
  CpuRaw a, b;
  a.coresOk = b.coresOk = true;
  a.cores = {{100, 200, 100}, {500, 600, 0}};
  b.cores = {{150, 300, 200}, {400, 600, 0}};  // core 1 went backwards
  auto u = computeCpuUsage(a, b);
  CHECK_EQ(u.corePct.size(), static_cast<size_t>(2));
  CHECK(u.corePct[0].has_value());
  CHECK_NEAR(*u.corePct[0], 75.0, 1e-9);  // idle 50 of 200
  CHECK(!u.corePct[1].has_value());
}

TEST(counter_delta64_reset) {
  CHECK_EQ(*counterDelta64(10, 15), 5ull);
  CHECK(!counterDelta64(15, 10).has_value());
}

TEST(counter_delta32_wrap) {
  auto d = counterDelta32(0xFFFFFFF0u, 0x10u);
  CHECK(d.has_value());
  CHECK_EQ(*d, 0x20ull);
  CHECK(!counterDelta32(0x10000000u, 0x10u).has_value());  // implausible wrap -> reset
}

TEST(per_second_rate) {
  CHECK_NEAR(*perSecond(counterDelta64(0, 2048), 2.0), 1024.0, 1e-9);
  CHECK(!perSecond(counterDelta64(0, 10), 0.0).has_value());
  CHECK(!perSecond(std::nullopt, 1.0).has_value());
}

TEST(process_cpu_percent_normalized) {
  // 0.5 s CPU in 1 s wall on 4 CPUs = 12.5% of machine
  CHECK_NEAR(*processCpuPercent(5000000, 10000000, 4), 12.5, 1e-9);
  CHECK_NEAR(*processCpuPercent(90000000, 10000000, 4), 100.0, 1e-9);  // clamped
  CHECK(!processCpuPercent(1, 0, 4).has_value());
}

TEST(disk_rates_from_two_samples) {
  DiskPerfRaw a, b;
  a.ok = b.ok = true;
  a.t = 10.0;
  b.t = 12.0;
  a.bytesRead = 1000;
  b.bytesRead = 1000 + 4 * 1048576;
  a.bytesWritten = b.bytesWritten = 5;
  a.readCount = 0xFFFFFFFE;
  b.readCount = 2;  // wrapped by 4
  a.idleTime = 0;
  b.idleTime = 15000000;
  a.queryTime = 0;
  b.queryTime = 20000000;
  PhysicalDiskInfo d;
  computeDiskRates(a, b, d);
  CHECK_NEAR(*d.readBps, 2097152.0, 1e-6);
  CHECK_NEAR(*d.writeBps, 0.0, 1e-9);
  CHECK_NEAR(*d.readIops, 2.0, 1e-9);
  CHECK_NEAR(*d.activePct, 25.0, 1e-9);
  PhysicalDiskInfo none;
  computeDiskRates(DiskPerfRaw(), b, none);
  CHECK(!none.readBps.has_value());
}

TEST(network_rates_first_sample_and_reset) {
  NetInterface n;
  n.rxBytes = 1000;
  n.txBytes = 500;
  computeNetRates(false, 0, 0, 0, n, 1.0);
  CHECK(!n.rxBytesPerSec.has_value());
  CHECK_EQ(n.rateNote, std::string("first sample"));
  n.rxBytes = 3000;
  n.txBytes = 1500;
  computeNetRates(true, 1000, 500, 1.0, n, 3.0);
  CHECK_NEAR(*n.rxBytesPerSec, 1000.0, 1e-9);
  CHECK_NEAR(*n.txBytesPerSec, 500.0, 1e-9);
  n.rxBytes = 10;  // adapter reset its counters
  computeNetRates(true, 3000, 1500, 3.0, n, 4.0);
  CHECK(!n.rxBytesPerSec.has_value());
  CHECK_EQ(n.rateNote, std::string("counter reset"));
}
