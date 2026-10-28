#include "sampler/sampler.h"

#include <chrono>

#include "platform/win.h"
#include "platform/wmi.h"
#include "util/format.h"

namespace sm {

Sampler::Sampler(const Options& opts) : opts_(opts) {
  data_.system = querySystemInfo();
  data_.cpuInfo = queryCpuInfo();
  data_.disks = queryPhysicalDisks();
  lastDiskEnum_ = win::monotonicSeconds();
  net_.reset(new NetworkCollector());
  procs_.reset(new ProcessCollector(data_.cpuInfo.logicalProcessors));
  gpuMon_.reset(new GpuMonitor());
  wmi_.reset(new WmiSession());
}

Sampler::~Sampler() { stopBackground(); }

void Sampler::sampleFast() {
  CpuRaw raw = sampleCpu();
  CpuUsage usage;
  if (havePrevCpu_) usage = computeCpuUsage(prevCpu_, raw);
  else usage.notes.push_back("first sample: utilization needs a second sample");
  prevCpu_ = raw;
  havePrevCpu_ = true;
  unsigned cur = 0, mx = 0;
  if (queryCpuFrequency(cur, mx)) {
    usage.currentMhz = cur;
    if (mx) usage.maxMhz = mx;
  }

  MemoryInfo mem = queryMemory();

  std::vector<PhysicalDiskInfo> disks;
  {
    std::lock_guard<std::mutex> lk(mu_);
    disks = data_.disks;
  }
  double now = win::monotonicSeconds();
  if (now - lastDiskEnum_ > 30.0) {  // pick up hot-plugged disks, keep previous counters
    auto fresh = queryPhysicalDisks();
    for (auto& f : fresh) {
      for (const auto& old : disks) {
        if (old.number == f.number && old.model == f.model) {
          f.raw = old.raw;
          break;
        }
      }
    }
    disks.swap(fresh);
    lastDiskEnum_ = now;
  }
  refreshDiskPerf(disks);

  auto network = net_->sample(opts_.includeAllInterfaces);
  BatteryInfo battery = queryBatteryStatus();

  std::vector<ProcessInfo> procs;
  if (opts_.processes) {
    procs = procs_->sample();
    uint64_t ws = 0, priv = 0;
    for (const auto& p : procs) {
      ws += p.workingSet.value_or(0);
      priv += p.privateBytes.value_or(0);
    }
    mem.processWorkingSetTotal = ws;
    mem.processPrivateTotal = priv;
  }

  std::lock_guard<std::mutex> lk(mu_);
  data_.fastSeq++;
  data_.ratesValid = data_.fastSeq >= 2;
  data_.timestamp = nowIsoUtc();
  refreshUptime(data_.system);
  data_.cpu = usage;
  data_.memory = mem;
  // Preserve SMART fields that the slow tier attached to the previous disk list.
  for (auto& d : disks) {
    for (const auto& old : data_.disks) {
      if (old.number == d.number) {
        d.smartPredictFailure = old.smartPredictFailure;
        d.smartTemperatureC = old.smartTemperatureC;
        d.smartNote = old.smartNote;
      }
    }
  }
  data_.disks = disks;
  data_.network = network;
  // Keep slow-tier health fields.
  battery.designCapacityMWh = data_.battery.designCapacityMWh;
  battery.fullChargeCapacityMWh = data_.battery.fullChargeCapacityMWh;
  battery.healthPct = data_.battery.healthPct;
  battery.healthNote = data_.battery.healthNote;
  data_.battery = battery;
  data_.processesCollected = opts_.processes;
  data_.processes.swap(procs);
}

void Sampler::collectSlow() {
  auto volumes = queryVolumes();

  std::vector<GpuAdapter> gpus;
  std::vector<std::string> gpuNotes;
  {
    std::lock_guard<std::mutex> lk(mu_);
    gpus = data_.gpus;
    gpuNotes = data_.gpuNotes;
  }
  if (!gpusDetected_) {
    gpus = detectGpus(&gpuNotes);
    gpusDetected_ = true;
  }
  std::vector<GpuMonitor::BackendStatus> backends;
  if (opts_.gpuSensors) {
    gpuMon_->update(gpus);
    backends = gpuMon_->backendStatus();
  }

  SmartResult smart;
  AcpiZones acpi;
  BatteryInfo health;
  {
    std::lock_guard<std::mutex> lk(mu_);
    health = data_.battery;
  }
  if (!health.statusOk) health = queryBatteryStatus();
  if (opts_.smart) smart = querySmart(*wmi_);
  else smart.status = "SMART query disabled";
  if (opts_.acpi) acpi = queryAcpiThermalZones(*wmi_);
  else acpi.status = "ACPI query disabled";
  queryBatteryHealth(*wmi_, health);

  std::lock_guard<std::mutex> lk(mu_);
  data_.volumes = volumes;
  data_.gpus = gpus;
  data_.gpuNotes = gpuNotes;
  data_.gpuBackends = backends;
  data_.smart = smart;
  data_.acpi = acpi;
  attachSmart(smart, data_.disks);
  data_.battery.designCapacityMWh = health.designCapacityMWh;
  data_.battery.fullChargeCapacityMWh = health.fullChargeCapacityMWh;
  data_.battery.healthPct = health.healthPct;
  data_.battery.healthNote = health.healthNote;
  data_.slowReady = true;
  data_.slowTimestamp = nowIsoUtc();
}

void Sampler::startBackground() {
  if (thread_.joinable()) return;
  {
    std::lock_guard<std::mutex> lk(threadMu_);
    stop_ = false;
  }
  thread_ = std::thread([this] { slowLoop(); });
}

void Sampler::stopBackground() {
  if (!thread_.joinable()) return;
  {
    std::lock_guard<std::mutex> lk(threadMu_);
    stop_ = true;
  }
  cv_.notify_all();
  thread_.join();
}

void Sampler::requestSlowRefresh() {
  {
    std::lock_guard<std::mutex> lk(threadMu_);
    refreshRequested_ = true;
  }
  cv_.notify_all();
}

void Sampler::slowLoop() {
  ComScope com;  // WMI and DXGI are used from this thread only while it runs
  for (;;) {
    collectSlow();
    std::unique_lock<std::mutex> lk(threadMu_);
    cv_.wait_for(lk, std::chrono::milliseconds(opts_.slowIntervalMs), [this] { return stop_ || refreshRequested_; });
    refreshRequested_ = false;
    if (stop_) break;
  }
  // Release COM objects on the thread that created them.
  wmi_.reset(new WmiSession());
}

Snapshot Sampler::snapshot() const {
  std::lock_guard<std::mutex> lk(mu_);
  Snapshot s = data_;
  s.sensors = buildSensorList(s.gpus, s.disks, s.acpi);
  return s;
}

}  // namespace sm
