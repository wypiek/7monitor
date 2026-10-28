// NVIDIA Management Library backend. nvml.dll ships with the NVIDIA display driver
// (older drivers: %ProgramFiles%\NVIDIA Corporation\NVSMI, newer: System32). It is
// loaded dynamically by absolute path; if it is missing nothing else changes.
#include <windows.h>

#include <cstdio>

#include "collectors/gpu.h"
#include "platform/win.h"
#include "util/strings.h"

namespace sm {
namespace {

using nvmlReturn_t = int;
using nvmlDevice_t = void*;
struct nvmlUtilization_t {
  unsigned int gpu;
  unsigned int memory;
};
struct nvmlMemory_t {
  unsigned long long total;
  unsigned long long free;
  unsigned long long used;
};
constexpr int NVML_SUCCESS = 0;
constexpr int NVML_TEMPERATURE_GPU = 0;
constexpr int NVML_CLOCK_GRAPHICS = 0;
constexpr int NVML_CLOCK_MEM = 2;

using InitFn = nvmlReturn_t (*)();
using ShutdownFn = nvmlReturn_t (*)();
using CountFn = nvmlReturn_t (*)(unsigned int*);
using HandleFn = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
using NameFn = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
using UtilFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
using MemFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);
using TempFn = nvmlReturn_t (*)(nvmlDevice_t, int, unsigned int*);
using ClockFn = nvmlReturn_t (*)(nvmlDevice_t, int, unsigned int*);
using PowerFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
using DrvVerFn = nvmlReturn_t (*)(char*, unsigned int);

class NvmlBackend : public GpuSensorBackend {
 public:
  ~NvmlBackend() override {
    if (initialized_ && shutdown_) shutdown_();
    // The library is intentionally not unloaded: NVML may own driver threads.
  }
  const char* name() const override { return "NVIDIA NVML"; }
  std::string status() const override { return status_; }

  bool init() override {
    std::vector<std::wstring> candidates;
    candidates.push_back(win::systemDirectory() + L"\\nvml.dll");
    if (auto pf = win::environmentVariable(L"ProgramW6432"))
      candidates.push_back(*pf + L"\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    if (auto pf = win::environmentVariable(L"ProgramFiles"))
      candidates.push_back(*pf + L"\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    for (const auto& c : candidates) {
      lib_ = win::loadLibraryAbsolute(c);
      if (lib_) {
        path_ = toUtf8(c);
        break;
      }
    }
    if (!lib_) {
      status_ = "nvml.dll not found (no NVIDIA driver with NVML installed)";
      return false;
    }
    auto init = win::procAddress<InitFn>(lib_, "nvmlInit_v2");
    if (!init) init = win::procAddress<InitFn>(lib_, "nvmlInit");
    shutdown_ = win::procAddress<ShutdownFn>(lib_, "nvmlShutdown");
    count_ = win::procAddress<CountFn>(lib_, "nvmlDeviceGetCount_v2");
    if (!count_) count_ = win::procAddress<CountFn>(lib_, "nvmlDeviceGetCount");
    handle_ = win::procAddress<HandleFn>(lib_, "nvmlDeviceGetHandleByIndex_v2");
    if (!handle_) handle_ = win::procAddress<HandleFn>(lib_, "nvmlDeviceGetHandleByIndex");
    name_ = win::procAddress<NameFn>(lib_, "nvmlDeviceGetName");
    util_ = win::procAddress<UtilFn>(lib_, "nvmlDeviceGetUtilizationRates");
    mem_ = win::procAddress<MemFn>(lib_, "nvmlDeviceGetMemoryInfo");
    temp_ = win::procAddress<TempFn>(lib_, "nvmlDeviceGetTemperature");
    clock_ = win::procAddress<ClockFn>(lib_, "nvmlDeviceGetClockInfo");
    power_ = win::procAddress<PowerFn>(lib_, "nvmlDeviceGetPowerUsage");
    auto drv = win::procAddress<DrvVerFn>(lib_, "nvmlSystemGetDriverVersion");
    if (!init || !count_ || !handle_ || !name_) {
      status_ = "nvml.dll found at " + path_ + " but required exports are missing";
      return false;
    }
    nvmlReturn_t r = init();
    if (r != NVML_SUCCESS) {
      status_ = "nvmlInit failed (code " + std::to_string(r) + ")";
      return false;
    }
    initialized_ = true;
    char ver[96] = {};
    if (drv && drv(ver, sizeof(ver)) == NVML_SUCCESS) driverVersion_ = ver;
    unsigned int n = 0;
    count_(&n);
    status_ = "loaded " + path_ + ", " + std::to_string(n) + " device(s)" +
              (driverVersion_.empty() ? "" : ", driver " + driverVersion_);
    return true;
  }

  void update(std::vector<GpuAdapter>& adapters) override {
    if (!initialized_) return;
    unsigned int n = 0;
    if (count_(&n) != NVML_SUCCESS) return;
    std::vector<bool> used(adapters.size(), false);
    for (unsigned int i = 0; i < n; ++i) {
      nvmlDevice_t dev = nullptr;
      if (handle_(i, &dev) != NVML_SUCCESS) continue;
      char nm[96] = {};
      if (name_(dev, nm, sizeof(nm)) != NVML_SUCCESS) continue;
      // Pair with the first unused NVIDIA adapter whose name matches.
      GpuAdapter* target = nullptr;
      for (size_t k = 0; k < adapters.size(); ++k) {
        if (!used[k] && adapters[k].vendorId == 0x10DE && gpuNamesMatch(adapters[k].name, nm)) {
          target = &adapters[k];
          used[k] = true;
          break;
        }
      }
      if (!target) continue;
      target->sensorBackend = "NVML";
      nvmlUtilization_t u;
      if (util_ && util_(dev, &u) == NVML_SUCCESS) {
        target->utilizationPct = u.gpu;
        target->memoryControllerPct = u.memory;
      }
      nvmlMemory_t m;
      if (mem_ && mem_(dev, &m) == NVML_SUCCESS) {
        target->vramUsed = m.used;
        target->vramTotal = m.total;
      }
      unsigned int v = 0;
      if (temp_ && temp_(dev, NVML_TEMPERATURE_GPU, &v) == NVML_SUCCESS) target->temperatureC = v;
      if (clock_ && clock_(dev, NVML_CLOCK_GRAPHICS, &v) == NVML_SUCCESS) target->coreClockMhz = v;
      if (clock_ && clock_(dev, NVML_CLOCK_MEM, &v) == NVML_SUCCESS) target->memoryClockMhz = v;
      if (power_ && power_(dev, &v) == NVML_SUCCESS) target->powerWatts = v / 1000.0;
      if (!driverVersion_.empty() && !target->driverVersion) target->driverVersion = driverVersion_;
      if (!target->utilizationPct) target->notes.push_back("utilization not supported by NVML for this GPU");
      if (!target->powerWatts) target->notes.push_back("power reading not supported by NVML for this GPU");
    }
  }

 private:
  HMODULE lib_ = nullptr;
  std::string path_;
  std::string status_ = "not initialized";
  std::string driverVersion_;
  bool initialized_ = false;
  ShutdownFn shutdown_ = nullptr;
  CountFn count_ = nullptr;
  HandleFn handle_ = nullptr;
  NameFn name_ = nullptr;
  UtilFn util_ = nullptr;
  MemFn mem_ = nullptr;
  TempFn temp_ = nullptr;
  ClockFn clock_ = nullptr;
  PowerFn power_ = nullptr;
};

}  // namespace

std::unique_ptr<GpuSensorBackend> makeNvmlBackend() { return std::unique_ptr<GpuSensorBackend>(new NvmlBackend()); }

}  // namespace sm
