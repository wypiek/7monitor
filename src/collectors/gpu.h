#pragma once
// GPU detection and optional vendor telemetry.
//
// Detection uses DXGI 1.1 (loaded dynamically; present on Windows 7 SP1) with an
// EnumDisplayDevices fallback, plus the display class registry keys for driver data.
// Live sensors come only from vendor libraries that the installed driver ships:
//   NVIDIA: NVML (nvml.dll), AMD: ADL (atiadlxx.dll / atiadlxy.dll).
// Windows 7 has no WDDM 2.x GPU performance counters, so without a vendor backend the
// sensor fields stay empty (shown as N/A). Values are never estimated.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sm {

struct GpuAdapter {
  std::string name;
  std::string vendor;  // NVIDIA, AMD, Intel, Microsoft, VMware, ... or "Unknown (0xNNNN)"
  unsigned vendorId = 0, deviceId = 0;
  bool software = false;  // Microsoft Basic Render Driver and similar
  std::string detectionSource;  // "DXGI 1.1" or "EnumDisplayDevices"
  std::optional<std::string> driverVersion, driverDate, driverProvider;
  std::optional<uint64_t> dedicatedVideoMemory, dedicatedSystemMemory, sharedSystemMemory;

  // Vendor telemetry (empty when no backend provides it)
  std::string sensorBackend = "none";
  std::optional<double> utilizationPct;
  std::optional<double> memoryControllerPct;
  std::optional<uint64_t> vramUsed, vramTotal;
  std::optional<double> temperatureC;
  std::optional<unsigned> coreClockMhz, memoryClockMhz;
  std::optional<double> powerWatts;
  std::vector<std::string> notes;
};

std::string gpuVendorName(unsigned vendorId);

// Static detection (no vendor libraries involved).
std::vector<GpuAdapter> detectGpus(std::vector<std::string>* notes = nullptr);

class GpuSensorBackend {
 public:
  virtual ~GpuSensorBackend() = default;
  virtual const char* name() const = 0;
  // Attempts to load the vendor library. Returns false (with status text) when absent.
  virtual bool init() = 0;
  virtual std::string status() const = 0;
  // Fills sensor fields of matching adapters.
  virtual void update(std::vector<GpuAdapter>& adapters) = 0;
};

std::unique_ptr<GpuSensorBackend> makeNvmlBackend();
std::unique_ptr<GpuSensorBackend> makeAdlBackend();

// Owns the vendor backends; initializes them lazily on first update().
class GpuMonitor {
 public:
  GpuMonitor();
  ~GpuMonitor();
  void update(std::vector<GpuAdapter>& adapters);
  struct BackendStatus {
    std::string name;
    bool available;
    std::string status;
  };
  std::vector<BackendStatus> backendStatus();

 private:
  void ensureInit();
  bool initialized_ = false;
  std::vector<std::unique_ptr<GpuSensorBackend>> backends_;
  std::vector<bool> active_;
};

// Case-insensitive fuzzy name match used to pair vendor-library devices with adapters.
bool gpuNamesMatch(const std::string& adapterName, const std::string& vendorName);

}  // namespace sm
