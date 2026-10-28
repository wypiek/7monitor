// AMD Display Library backend (Overdrive 5 API, the generation that matches Windows 7
// era Radeon drivers). atiadlxx.dll is the native library; 32-bit processes on 64-bit
// Windows use atiadlxy.dll. Loaded from System32 only. Newer GPUs that only support
// Overdrive N/8 report no data here and are shown as N/A.
#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <set>

#include "collectors/gpu.h"
#include "platform/win.h"
#include "util/strings.h"

namespace sm {
namespace {

constexpr int ADL_OK = 0;
constexpr int kAdlMaxPath = 256;

struct AdapterInfo {
  int iSize;
  int iAdapterIndex;
  char strUDID[kAdlMaxPath];
  int iBusNumber;
  int iDeviceNumber;
  int iFunctionNumber;
  int iVendorID;
  char strAdapterName[kAdlMaxPath];
  char strDisplayName[kAdlMaxPath];
  int iPresent;
  int iExist;
  char strDriverPath[kAdlMaxPath];
  char strDriverPathExt[kAdlMaxPath];
  char strPNPString[kAdlMaxPath];
  int iOSDisplayIndex;
};
struct ADLTemperature {
  int iSize;
  int iTemperature;  // millidegrees Celsius
};
struct ADLPMActivity {
  int iSize;
  int iEngineClock;  // 10 kHz units
  int iMemoryClock;  // 10 kHz units
  int iVddc;
  int iActivityPercent;
  int iCurrentPerformanceLevel;
  int iCurrentBusSpeed;
  int iCurrentBusLanes;
  int iMaximumBusLanes;
  int iReserved;
};

using MallocCb = void*(__stdcall*)(int);
using CreateFn = int (*)(MallocCb, int);
using DestroyFn = int (*)();
using NumFn = int (*)(int*);
using InfoFn = int (*)(AdapterInfo*, int);
using ActiveFn = int (*)(int, int*);
using TempFn = int (*)(int, int, ADLTemperature*);
using ActFn = int (*)(int, ADLPMActivity*);

void* __stdcall adlMalloc(int size) { return std::malloc(static_cast<size_t>(size)); }

class AdlBackend : public GpuSensorBackend {
 public:
  ~AdlBackend() override {
    if (initialized_ && destroy_) destroy_();
  }
  const char* name() const override { return "AMD ADL (Overdrive 5)"; }
  std::string status() const override { return status_; }

  bool init() override {
#if defined(_WIN64)
    lib_ = win::loadSystemLibrary(L"atiadlxx.dll");
#else
    lib_ = win::isWow64Process() ? win::loadSystemLibrary(L"atiadlxy.dll") : win::loadSystemLibrary(L"atiadlxx.dll");
    if (!lib_) lib_ = win::loadSystemLibrary(L"atiadlxy.dll");
#endif
    if (!lib_) {
      status_ = "ADL library not found (no AMD display driver installed)";
      return false;
    }
    auto create = win::procAddress<CreateFn>(lib_, "ADL_Main_Control_Create");
    destroy_ = win::procAddress<DestroyFn>(lib_, "ADL_Main_Control_Destroy");
    num_ = win::procAddress<NumFn>(lib_, "ADL_Adapter_NumberOfAdapters_Get");
    info_ = win::procAddress<InfoFn>(lib_, "ADL_Adapter_AdapterInfo_Get");
    active_ = win::procAddress<ActiveFn>(lib_, "ADL_Adapter_Active_Get");
    temp_ = win::procAddress<TempFn>(lib_, "ADL_Overdrive5_Temperature_Get");
    act_ = win::procAddress<ActFn>(lib_, "ADL_Overdrive5_CurrentActivity_Get");
    if (!create || !num_ || !info_) {
      status_ = "ADL library found but required exports are missing";
      return false;
    }
    if (create(adlMalloc, 1) != ADL_OK) {
      status_ = "ADL_Main_Control_Create failed";
      return false;
    }
    initialized_ = true;
    status_ = "loaded";
    if (!temp_ && !act_) status_ += " (Overdrive 5 not exported: no sensor data)";
    return true;
  }

  void update(std::vector<GpuAdapter>& adapters) override {
    if (!initialized_) return;
    int n = 0;
    if (num_(&n) != ADL_OK || n <= 0 || n > 256) return;
    std::vector<AdapterInfo> infos(static_cast<size_t>(n));
    memset(infos.data(), 0, infos.size() * sizeof(AdapterInfo));
    for (auto& i : infos) i.iSize = sizeof(AdapterInfo);
    if (info_(infos.data(), static_cast<int>(infos.size() * sizeof(AdapterInfo))) != ADL_OK) return;
    std::set<int> buses;  // ADL lists one entry per display output; keep one per GPU
    std::vector<bool> used(adapters.size(), false);
    for (const auto& ai : infos) {
      if (ai.iVendorID != 0x1002) continue;
      if (active_) {
        int on = 0;
        if (active_(ai.iAdapterIndex, &on) != ADL_OK || !on) continue;
      }
      if (!buses.insert(ai.iBusNumber).second) continue;
      std::string nm(ai.strAdapterName, strnlen(ai.strAdapterName, sizeof(ai.strAdapterName)));
      GpuAdapter* target = nullptr;
      for (size_t k = 0; k < adapters.size(); ++k) {
        if (!used[k] && (adapters[k].vendorId == 0x1002 || adapters[k].vendorId == 0x1022) &&
            gpuNamesMatch(adapters[k].name, nm)) {
          target = &adapters[k];
          used[k] = true;
          break;
        }
      }
      if (!target) continue;
      target->sensorBackend = "ADL";
      if (temp_) {
        ADLTemperature t = {sizeof(ADLTemperature), 0};
        if (temp_(ai.iAdapterIndex, 0, &t) == ADL_OK) target->temperatureC = t.iTemperature / 1000.0;
      }
      if (act_) {
        ADLPMActivity a;
        memset(&a, 0, sizeof(a));
        a.iSize = sizeof(a);
        if (act_(ai.iAdapterIndex, &a) == ADL_OK) {
          target->utilizationPct = a.iActivityPercent;
          if (a.iEngineClock > 0) target->coreClockMhz = static_cast<unsigned>(a.iEngineClock / 100);
          if (a.iMemoryClock > 0) target->memoryClockMhz = static_cast<unsigned>(a.iMemoryClock / 100);
        }
      }
      if (!target->temperatureC && !target->utilizationPct)
        target->notes.push_back("adapter does not support ADL Overdrive 5 sensors");
      target->notes.push_back("VRAM usage and power are not exposed by ADL Overdrive 5");
    }
  }

 private:
  HMODULE lib_ = nullptr;
  bool initialized_ = false;
  std::string status_ = "not initialized";
  DestroyFn destroy_ = nullptr;
  NumFn num_ = nullptr;
  InfoFn info_ = nullptr;
  ActiveFn active_ = nullptr;
  TempFn temp_ = nullptr;
  ActFn act_ = nullptr;
};

}  // namespace

std::unique_ptr<GpuSensorBackend> makeAdlBackend() { return std::unique_ptr<GpuSensorBackend>(new AdlBackend()); }

}  // namespace sm
