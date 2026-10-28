#include "collectors/gpu.h"

#include <windows.h>
#include <dxgi.h>

#include <cstdio>
#include <set>

#include "platform/win.h"
#include "util/strings.h"

namespace sm {

std::string gpuVendorName(unsigned id) {
  switch (id) {
    case 0x10DE: return "NVIDIA";
    case 0x1002: case 0x1022: return "AMD";
    case 0x8086: case 0x8087: return "Intel";
    case 0x1414: return "Microsoft";
    case 0x15AD: return "VMware";
    case 0x80EE: return "VirtualBox";
    case 0x1AB8: return "Parallels";
    case 0x1234: return "QEMU";
    case 0x1B36: return "Red Hat (QXL)";
    case 0x5333: return "S3";
    case 0x1106: return "VIA";
    case 0x102B: return "Matrox";
    case 0x0000: return "Unknown";
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "Unknown (0x%04X)", id);
  return buf;
}

namespace {

const wchar_t* kDisplayClassKey =
    L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";

struct DriverEntry {
  std::string desc, version, date, provider, matchingId;
  std::optional<uint64_t> memorySize;
};

std::vector<DriverEntry> readDisplayDrivers() {
  std::vector<DriverEntry> out;
  for (const auto& sub : win::regSubKeys(HKEY_LOCAL_MACHINE, kDisplayClassKey)) {
    if (sub.size() != 4) continue;  // 0000, 0001, ... (skip "Properties")
    std::wstring path = std::wstring(kDisplayClassKey) + L"\\" + sub;
    DriverEntry e;
    if (auto v = win::regString(HKEY_LOCAL_MACHINE, path, L"DriverDesc")) e.desc = sanitize(toUtf8(*v));
    if (auto v = win::regString(HKEY_LOCAL_MACHINE, path, L"DriverVersion")) e.version = sanitize(toUtf8(*v));
    if (auto v = win::regString(HKEY_LOCAL_MACHINE, path, L"DriverDate")) e.date = sanitize(toUtf8(*v));
    if (auto v = win::regString(HKEY_LOCAL_MACHINE, path, L"ProviderName")) e.provider = sanitize(toUtf8(*v));
    if (auto v = win::regString(HKEY_LOCAL_MACHINE, path, L"MatchingDeviceId")) e.matchingId = toLower(toUtf8(*v));
    if (auto q = win::regQword(HKEY_LOCAL_MACHINE, path, L"HardwareInformation.qwMemorySize")) e.memorySize = *q;
    else if (auto d = win::regQword(HKEY_LOCAL_MACHINE, path, L"HardwareInformation.MemorySize")) e.memorySize = *d;
    if (!e.desc.empty() || !e.version.empty()) out.push_back(e);
  }
  return out;
}

void attachDriver(GpuAdapter& a, const std::vector<DriverEntry>& drivers) {
  char want[64];
  std::snprintf(want, sizeof(want), "ven_%04x&dev_%04x", a.vendorId, a.deviceId);
  const DriverEntry* best = nullptr;
  for (const auto& d : drivers) {
    if (a.vendorId && d.matchingId.find(want) != std::string::npos) {
      best = &d;
      break;
    }
  }
  if (!best) {
    for (const auto& d : drivers) {
      if (!d.desc.empty() && iequals(d.desc, a.name)) {
        best = &d;
        break;
      }
    }
  }
  if (!best) return;
  if (!best->version.empty()) a.driverVersion = best->version;
  if (!best->date.empty()) a.driverDate = best->date;
  if (!best->provider.empty()) a.driverProvider = best->provider;
  if (!a.dedicatedVideoMemory && best->memorySize) a.dedicatedVideoMemory = best->memorySize;
}

// IID_IDXGIFactory1 {770aae78-f26f-4dba-a829-253c83d1b387}
const GUID kIidDxgiFactory1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};

bool detectViaDxgi(std::vector<GpuAdapter>& out, std::string& why) {
  HMODULE dxgi = win::loadSystemLibrary(L"dxgi.dll");
  if (!dxgi) {
    why = "dxgi.dll not present";
    return false;
  }
  using CreateFn = HRESULT(WINAPI*)(REFIID, void**);
  auto create = win::procAddress<CreateFn>(dxgi, "CreateDXGIFactory1");
  if (!create) {
    why = "CreateDXGIFactory1 not exported (DXGI 1.1 requires Windows 7 or Vista with the Platform Update)";
    FreeLibrary(dxgi);
    return false;
  }
  IDXGIFactory1* factory = nullptr;
  HRESULT hr = create(kIidDxgiFactory1, reinterpret_cast<void**>(&factory));
  if (FAILED(hr) || !factory) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "CreateDXGIFactory1 failed (0x%08lX)", static_cast<unsigned long>(hr));
    why = buf;
    FreeLibrary(dxgi);
    return false;
  }
  std::set<std::pair<unsigned, unsigned long long>> seen;  // (deviceId, LUID) dedupe
  for (UINT i = 0;; ++i) {
    IDXGIAdapter1* ad = nullptr;
    if (factory->EnumAdapters1(i, &ad) == DXGI_ERROR_NOT_FOUND || !ad) break;
    DXGI_ADAPTER_DESC1 d;
    if (SUCCEEDED(ad->GetDesc1(&d))) {
      unsigned long long luid = (static_cast<unsigned long long>(static_cast<uint32_t>(d.AdapterLuid.HighPart)) << 32) |
                                d.AdapterLuid.LowPart;
      if (seen.insert({d.DeviceId, luid}).second) {
        GpuAdapter a;
        a.name = sanitize(toUtf8(std::wstring(d.Description)));
        a.vendorId = d.VendorId;
        a.deviceId = d.DeviceId;
        a.vendor = gpuVendorName(d.VendorId);
        a.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || (d.VendorId == 0x1414 && d.DeviceId == 0x8c);
        a.detectionSource = "DXGI 1.1";
        if (d.DedicatedVideoMemory) a.dedicatedVideoMemory = d.DedicatedVideoMemory;
        if (d.DedicatedSystemMemory) a.dedicatedSystemMemory = d.DedicatedSystemMemory;
        if (d.SharedSystemMemory) a.sharedSystemMemory = d.SharedSystemMemory;
        out.push_back(a);
      }
    }
    ad->Release();
  }
  factory->Release();
  // dxgi.dll stays loaded: unloading it while driver threads may linger is unsafe.
  return true;
}

void detectViaEnumDisplayDevices(std::vector<GpuAdapter>& out) {
  std::set<std::string> seen;
  for (DWORD i = 0;; ++i) {
    DISPLAY_DEVICEW dd;
    ZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);
    if (!EnumDisplayDevicesW(nullptr, i, &dd, 0)) break;
    if (dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) continue;
    std::string name = sanitize(toUtf8(std::wstring(dd.DeviceString)));
    std::string id = toLower(toUtf8(std::wstring(dd.DeviceID)));
    if (name.empty() || !seen.insert(name + "|" + id).second) continue;
    GpuAdapter a;
    a.name = name;
    a.detectionSource = "EnumDisplayDevices";
    unsigned ven = 0, dev = 0;
    size_t p = id.find("ven_");
    if (p != std::string::npos) ven = static_cast<unsigned>(strtoul(id.substr(p + 4, 4).c_str(), nullptr, 16));
    p = id.find("dev_");
    if (p != std::string::npos) dev = static_cast<unsigned>(strtoul(id.substr(p + 4, 4).c_str(), nullptr, 16));
    a.vendorId = ven;
    a.deviceId = dev;
    a.vendor = gpuVendorName(ven);
    out.push_back(a);
  }
}

}  // namespace

std::vector<GpuAdapter> detectGpus(std::vector<std::string>* notes) {
  std::vector<GpuAdapter> out;
  std::string why;
  if (!detectViaDxgi(out, why)) {
    if (notes) notes->push_back("DXGI unavailable: " + why + "; using EnumDisplayDevices");
    detectViaEnumDisplayDevices(out);
  } else if (out.empty()) {
    detectViaEnumDisplayDevices(out);
  }
  auto drivers = readDisplayDrivers();
  for (auto& a : out) {
    attachDriver(a, drivers);
    if (a.vendor.rfind("Unknown", 0) == 0 && a.name.find("Basic Display") != std::string::npos) a.vendor = "Microsoft";
  }
  return out;
}

static std::string normalizeName(const std::string& s) {
  std::string out;
  for (char c : toLower(s)) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(c);
  }
  for (const char* prefix : {"nvidia", "amd", "ati"}) {
    std::string p(prefix);
    if (out.compare(0, p.size(), p) == 0) out = out.substr(p.size());
  }
  return out;
}

bool gpuNamesMatch(const std::string& adapterName, const std::string& vendorName) {
  std::string a = normalizeName(adapterName), b = normalizeName(vendorName);
  if (a.empty() || b.empty()) return false;
  return a.find(b) != std::string::npos || b.find(a) != std::string::npos;
}

GpuMonitor::GpuMonitor() = default;
GpuMonitor::~GpuMonitor() = default;

void GpuMonitor::ensureInit() {
  if (initialized_) return;
  initialized_ = true;
  backends_.push_back(makeNvmlBackend());
  backends_.push_back(makeAdlBackend());
  for (auto& b : backends_) active_.push_back(b->init());
}

void GpuMonitor::update(std::vector<GpuAdapter>& adapters) {
  ensureInit();
  for (auto& a : adapters) {  // never carry stale readings over from a previous sample
    a.sensorBackend = "none";
    a.utilizationPct.reset();
    a.memoryControllerPct.reset();
    a.vramUsed.reset();
    a.vramTotal.reset();
    a.temperatureC.reset();
    a.coreClockMhz.reset();
    a.memoryClockMhz.reset();
    a.powerWatts.reset();
    a.notes.clear();
  }
  for (size_t i = 0; i < backends_.size(); ++i) {
    if (active_[i]) backends_[i]->update(adapters);
  }
  for (auto& a : adapters) {
    if (a.sensorBackend != "none") continue;
    if (a.software) a.notes.push_back("software adapter: no hardware sensors");
    else if (a.vendor == "NVIDIA") a.notes.push_back("NVML not available from the installed driver");
    else if (a.vendor == "AMD") a.notes.push_back("ADL not available or adapter not supported by Overdrive 5");
    else if (a.vendor == "Intel") a.notes.push_back("no Windows 7-compatible Intel telemetry API");
    else a.notes.push_back("no supported telemetry backend for this vendor");
  }
}

std::vector<GpuMonitor::BackendStatus> GpuMonitor::backendStatus() {
  ensureInit();
  std::vector<BackendStatus> out;
  for (size_t i = 0; i < backends_.size(); ++i)
    out.push_back({backends_[i]->name(), static_cast<bool>(active_[i]), backends_[i]->status()});
  return out;
}

}  // namespace sm
