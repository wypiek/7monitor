#pragma once
// Two-tier sampling scheduler.
//
//  fast tier  (every refresh interval, caller's thread): CPU, memory, physical disk
//             throughput, network throughput, processes, battery status.
//  slow tier  (default every 5 s, background thread when interactive): volumes (which
//             can block on disconnected network shares), GPU vendor telemetry, SMART,
//             battery health and ACPI thermal zones through WMI.
//
// The slow tier never blocks the fast tier or the UI: results are published under a
// mutex when complete. One-shot CLI commands call collectSlow() synchronously instead.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "collectors/battery.h"
#include "collectors/cpu.h"
#include "collectors/disk.h"
#include "collectors/gpu.h"
#include "collectors/memory.h"
#include "collectors/network.h"
#include "collectors/process.h"
#include "collectors/sensors.h"
#include "collectors/smart.h"
#include "collectors/system.h"

namespace sm {

struct Snapshot {
  uint64_t fastSeq = 0;  // number of fast samples taken; rates need >= 2
  std::string timestamp;  // UTC ISO 8601 of the last fast sample
  bool ratesValid = false;
  SystemInfo system;
  CpuInfo cpuInfo;
  CpuUsage cpu;
  MemoryInfo memory;
  std::vector<PhysicalDiskInfo> disks;
  std::vector<NetInterface> network;
  BatteryInfo battery;
  bool processesCollected = false;
  std::vector<ProcessInfo> processes;
  // slow tier
  bool slowReady = false;
  std::string slowTimestamp;
  std::vector<VolumeInfo> volumes;
  std::vector<GpuAdapter> gpus;
  std::vector<std::string> gpuNotes;
  std::vector<GpuMonitor::BackendStatus> gpuBackends;
  SmartResult smart;
  AcpiZones acpi;
  std::vector<SensorReading> sensors;
};

class Sampler {
 public:
  struct Options {
    bool processes = true;
    bool includeAllInterfaces = false;
    bool smart = true;        // WMI SMART queries (slow tier)
    bool acpi = true;         // WMI ACPI thermal zones (slow tier)
    bool gpuSensors = true;   // NVML / ADL
    unsigned slowIntervalMs = 5000;
  };

  explicit Sampler(const Options& opts);
  ~Sampler();
  Sampler(const Sampler&) = delete;
  Sampler& operator=(const Sampler&) = delete;

  void sampleFast();
  void collectSlow();  // synchronous, on the calling thread (needs COM initialized)
  void startBackground();
  void stopBackground();
  void requestSlowRefresh();

  Snapshot snapshot() const;

 private:
  void slowLoop();

  Options opts_;
  mutable std::mutex mu_;
  Snapshot data_;
  CpuRaw prevCpu_;
  bool havePrevCpu_ = false;
  std::unique_ptr<NetworkCollector> net_;
  std::unique_ptr<ProcessCollector> procs_;
  double lastDiskEnum_ = 0;

  // Slow tier state (used by exactly one thread at a time)
  std::unique_ptr<GpuMonitor> gpuMon_;
  std::unique_ptr<WmiSession> wmi_;
  bool gpusDetected_ = false;

  std::thread thread_;
  std::mutex threadMu_;
  std::condition_variable cv_;
  bool stop_ = false;
  bool refreshRequested_ = false;
};

}  // namespace sm
