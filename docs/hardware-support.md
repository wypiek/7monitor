# Hardware support and sensor limitations

SevenMonitor reports a value only when it comes from a documented interface that
measures it. Everything else is `N/A` (JSON `null`) with a note explaining why.
`sevenmon doctor` runs every probe below on the current machine.

## CPU

| Metric | Source | Notes |
|--------|--------|-------|
| Vendor, model | `CPUID` leaves 0 and 0x80000002-4; registry `ProcessorNameString` fallback | |
| Logical processors | `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)` | Windows 7 API |
| Physical cores, packages, L2/L3 | `GetLogicalProcessorInformation` | |
| Total, user, system, idle % | `GetSystemTimes` deltas | first sample has no value |
| Per-core % | `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` deltas | covers processor group 0 (up to 64 logical CPUs) |
| Current / max MHz | `CallNtPowerInformation(ProcessorInformation)` | Windows reports the nominal/P-state frequency; Turbo Boost / Precision Boost is not reflected. Labeled accordingly. |
| Temperature | **not available** | Windows 7 has no documented API for on-die sensors. Tools that show it install a kernel driver to read MSRs; SevenMonitor deliberately does not. ACPI thermal zones are *not* used as a substitute. |

## Memory

`GlobalMemoryStatusEx` (physical), `GetPerformanceInfo` (commit total/limit/peak,
system cache, kernel pools, process/thread/handle counts) and
`NtQuerySystemInformation(SystemPageFileInformation)` (page file paths, size, usage,
peak). Physical RAM usage and commit charge are always shown separately: commit is
virtual memory backed by RAM *plus* page files and can exceed physical RAM.

"Working sets" and "private bytes" totals are sums over processes SevenMonitor can open;
protected processes are not included.

## GPU

Detection: DXGI 1.1 `CreateDXGIFactory1` (loaded at run time from System32) gives the
adapter name, PCI vendor/device ID, dedicated/shared memory sizes and whether the adapter
is a software adapter (Microsoft Basic Render Driver). If DXGI is unavailable,
`EnumDisplayDevices` is used. The driver version, date and provider come from the
display class registry key matched by PCI ID.

Windows 7 uses WDDM 1.1, which has **no generic GPU utilization or VRAM-usage counters**
(those arrived with WDDM 2.0 in Windows 10). Live readings therefore exist only through
vendor libraries shipped with the installed driver:

| Vendor | Backend | Readings | Requirements / limitations |
|--------|---------|----------|----------------------------|
| NVIDIA | NVML (`nvml.dll` from System32 or `%ProgramFiles%\NVIDIA Corporation\NVSMI`) | utilization, memory-controller load, VRAM used/total, temperature, core and memory clock, power | NVML ships with the driver. GeForce cards may not report utilization or power on older drivers (shown as N/A with a note). The x86 build usually cannot load the 64-bit-only NVML of newer drivers. |
| AMD | ADL Overdrive 5 (`atiadlxx.dll`, `atiadlxy.dll` for 32-bit on 64-bit Windows) | activity %, temperature, engine and memory clock | Matches Radeon generations that had Windows 7 drivers (up to roughly GCN 4). Overdrive N/8-only GPUs return no data. VRAM usage and power are not exposed by Overdrive 5. |
| Intel | none | detection, driver version, VRAM size | Intel's telemetry APIs (IGCL) require Windows 10. |
| Microsoft Basic Display Adapter / VMs | none | detection only | |

Vendor libraries are loaded by absolute path only, never from the current directory, and
are optional: if they are missing or fail, nothing else is affected.

## Disks and volumes

| Metric | Source | Permissions |
|--------|--------|-------------|
| Drive letters, label, file system, type | `GetLogicalDriveStrings`, `GetVolumeInformation`, `GetDriveType` | none |
| Size / free / free-to-user | `GetDiskFreeSpaceEx` | none |
| Volume -> disk mapping | `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` | none (zero-access handle) |
| Disk model, bus type, removable | `IOCTL_STORAGE_QUERY_PROPERTY(StorageDeviceProperty)` | none |
| Disk size | `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX` | none |
| Read/write throughput, IOPS, active % | `IOCTL_DISK_PERFORMANCE` deltas; active % = 1 - idle/elapsed | none |
| SSD vs HDD | `StorageDeviceSeekPenaltyProperty` (added in Windows 7) | Some Windows 7 storage drivers do not implement it (reported as `unknown`). Ignored for USB and virtual disks, where it is unreliable. |
| SMART failure prediction, temperature | WMI `ROOT\WMI` `MSStorageDriver_FailurePredictStatus` / `_FailurePredictData` (attributes 194/190) | usually requires **administrator**. Works for ATA/SATA disks whose driver passes SMART through; USB bridges, many RAID controllers and NVMe drivers on Windows 7 do not. |

Devices are opened with **zero access rights** (no read or write permission requested).
SevenMonitor never writes to, formats, or reconfigures any disk.

Mapped network drives are queried from the slow tier only; a disconnected share may
take Windows several seconds to answer, which delays one-shot `disk`/`summary` commands
but never the dashboard.

## Network

`GetIfTable2` provides 64-bit octet counters, link speeds, operational/media state,
interface type and MAC; `GetAdaptersAddresses` provides IPv4/IPv6 unicast addresses
(link-local IPv6 addresses include the zone index). Throughput is computed per
interface from counter deltas keyed by the interface LUID:

* a new interface shows `N/A` until its second sample ("first sample"),
* a counter that goes backwards (adapter reset, driver reload) yields `N/A` for that
  interval ("counter reset") and restarts from the new value,
* interfaces that disappear are forgotten.

By default filter/loopback interfaces and non-hardware interfaces that are down (Teredo,
ISATAP and similar tunnels) are hidden; `--all` shows them. No network traffic is
generated and no connectivity is required.

## Battery and power

`GetSystemPowerStatus` gives AC status, charge %, charging flag and **Windows' own
estimate** of remaining time (reported as an estimate, not a measurement, and only
available on battery power). Design and full-charge capacity come from WMI
`BatteryStaticData` and `BatteryFullChargedCapacity`; health = full-charge / design.
Many drivers do not implement these classes - health is then `N/A`.

## System and firmware

Windows version via `RtlGetVersion` (immune to manifest-based version lies) and the
`CurrentVersion` registry key (product name, edition, UBR on Windows 10+); Windows 11 is
detected by build >= 22000. Uptime via `GetTickCount64`; boot time is *derived* (now -
uptime) and labeled as such. SMBIOS system, baseboard and BIOS strings via
`GetSystemFirmwareTable('RSMB')`; OEM placeholders ("To Be Filled By O.E.M.", "Default
string") are treated as missing. Serial numbers are intentionally not collected.

## Processes

Tool Help snapshot (`CreateToolhelp32Snapshot`) for PID, parent, name, threads and base
priority; `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` for times, memory, handles,
priority class, I/O counters and image path. A standard user can open most processes on
Windows 7; protected and some SYSTEM processes refuse access and are flagged
`access_denied` with the fields that need a handle shown as N/A. Running elevated
reveals more. CPU % is normalized to the whole machine (100 % = all logical CPUs busy),
computed from kernel+user time deltas, and keyed by (PID, creation time) so a recycled
PID never inherits another process's history. Command lines are not collected.

## Sensors summary

| Sensor | Available when |
|--------|---------------|
| CPU core temperature | never (see CPU) |
| GPU temperature / load / clocks / power | NVML or ADL present and supported by the GPU |
| Disk temperature | SMART through WMI works (usually admin, SATA) |
| ACPI thermal zones | firmware implements `_TMP` and WMI exposes it (usually admin). These measure a firmware-defined zone (often motherboard or chassis), **not** the CPU cores, and some firmware returns constant values. |
| Fan speeds, voltages | not available: no documented Windows 7 interface (Super I/O chips need a driver) |

## Resource usage

* Fast tier every refresh interval: a handful of system calls plus one
  `OpenProcess`/query set per process (the main cost; roughly proportional to the number
  of processes).
* Slow tier every 5 s on a background thread: volumes, GPU libraries, WMI queries with a
  4 s timeout each.
* Frames are written only when their content changed.
* No files are written except `--output`/`export` targets; no registry writes; no network.

Measured in the development environment (Wine 9 on Linux, not Windows): ~9.5 MiB private
memory for the test binary after 400 sampling+render iterations with no growth beyond
noise (handle counts are not reported by Wine). The dashboard process showed a 28 MiB
working set under Wine, which includes Wine's own DLLs. Figures on a real Windows 7
installation still need to be recorded (see windows7-testing.md).
