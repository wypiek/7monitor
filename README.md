# SevenMonitor (`sevenmon.exe`)

A lightweight, native terminal system monitor for **Windows 7 SP1** (x86 and x64) that
also runs on Windows 8.1, 10 and 11. Think `htop`/`glances`, written in C++17 against the
classic Win32 API, running in the original Windows 7 Command Prompt.

* One portable `.exe`, statically linked: no .NET, Python, Java, VC++ redistributable,
  service, driver or installer.
* Two modes: one-shot commands for scripts (text, `--json`, `--csv`) and a live,
  flicker-free dashboard (`sevenmon watch`) built on `WriteConsoleOutputW`.
* Real measurements only. Rates (CPU %, disk and network throughput, per-process CPU)
  come from the difference between two real samples. Anything without a trustworthy
  source is shown as `N/A` (JSON `null`) - never estimated, never randomized.
* No administrator rights needed for ordinary statistics. No network access, no
  telemetry, no configuration changes, no disk writes except files you ask for.

> Windows 7 has been out of support since January 2020. SevenMonitor reports on the
> system; it does not make Windows 7 any safer. `sevenmon doctor` says so explicitly.

## Quick start

```
sevenmon                       system summary (same as "sevenmon summary")
sevenmon watch                 interactive dashboard (Q to quit)
sevenmon cpu                   processor details and per-core utilization
sevenmon processes --sort memory --top 10
sevenmon process 1234          one process in detail
sevenmon disk                  volumes, physical disks, throughput, SMART
sevenmon network --verbose     interfaces with MAC, IPv4 and IPv6 addresses
sevenmon gpu                   adapters, driver versions, NVML/ADL sensors
sevenmon sensors               every temperature/load sensor with its source
sevenmon hardware              Windows edition, SMBIOS system/board/BIOS, inventory
sevenmon doctor                which data sources work here and why others do not
```

Machine-readable output:

```
sevenmon summary --json
sevenmon cpu --json
sevenmon processes --top 20 --csv > procs.csv
sevenmon export --csv --output stats.csv --interval 1000 --count 60
sevenmon export --json --output C:\logs\ --duration 3600          (JSON lines file)
sevenmon watch --json --interval 2000 | findstr cpu_total_pct     (NDJSON stream)
```

`--json` and `--csv` output contains no colors, progress messages or decorations;
progress and errors go to stderr. See [docs/json-schema.md](docs/json-schema.md).

## What is measured, and how

| Area | Source (all available on Windows 7 SP1) | Admin needed? |
|------|------------------------------------------|---------------|
| CPU utilization | `GetSystemTimes`, `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` per core | no |
| CPU model/topology | `CPUID`, `GetLogicalProcessorInformation`, registry | no |
| CPU frequency | `CallNtPowerInformation(ProcessorInformation)` (does not show turbo) | no |
| CPU temperature | **not available** - no documented API; would need a kernel driver | - |
| Memory | `GlobalMemoryStatusEx`, `GetPerformanceInfo`, page files via `NtQuerySystemInformation` | no |
| GPU detection | DXGI 1.1 (`CreateDXGIFactory1`, loaded dynamically), display-class registry keys | no |
| GPU sensors | NVIDIA NVML (`nvml.dll`), AMD ADL Overdrive 5 (`atiadlxx.dll`) when the driver ships them | no |
| Volumes | `GetLogicalDriveStrings`, `GetVolumeInformation`, `GetDiskFreeSpaceEx` | no |
| Disk throughput | `IOCTL_DISK_PERFORMANCE` (device opened with zero access rights) | no |
| SSD/HDD | `IOCTL_STORAGE_QUERY_PROPERTY` seek-penalty descriptor | no (falls back to "unknown") |
| SMART | WMI `MSStorageDriver_FailurePredictStatus/Data` | usually yes |
| Network | `GetIfTable2` (64-bit counters), `GetAdaptersAddresses` | no |
| Battery | `GetSystemPowerStatus`; health via WMI `BatteryStaticData`/`BatteryFullChargedCapacity` | no |
| System/firmware | `RtlGetVersion`, registry, `GetSystemFirmwareTable('RSMB')` | no |
| Processes | Tool Help snapshot, `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` | no (protected processes show fewer fields) |
| ACPI thermal zones | WMI `MSAcpi_ThermalZoneTemperature`, labeled "not CPU core temperature" | usually yes |

Details and limitations: [docs/hardware-support.md](docs/hardware-support.md).

## The dashboard

`sevenmon watch` switches to a private console screen buffer (your scrollback is
untouched and restored on exit, including after Ctrl+C), renders each frame into memory
and writes it with a single `WriteConsoleOutputW` call, only when something changed.

Keys: `Q` quit, `R` refresh, `O` overview, `P` processes, `C` CPU, `M` memory, `G` GPU,
`D` disk, `N` network, `H` help, `Up/Down/PgUp/PgDn/Home/End` select, `Enter` process
details, `S` cycle sort, `/` filter, `K` terminate the selected process (asks `Y/N`),
`+`/`-` refresh interval, `Space` pause, `Esc` back.

It adapts to the window size (compact single-column layout below 100 columns, "window
too small" below 40x10), uses box-drawing characters with TrueType fonts or OEM code
pages that contain them and falls back to ASCII otherwise (`--plain` forces ASCII and
no color; `--no-color` keeps Unicode but drops color). Slow sources (WMI, vendor GPU
libraries, volumes) are collected on a background thread every 5 s so they can never
freeze the interface.

On Windows 7 the console window cannot be dragged larger than its buffer: to enlarge the
dashboard use the window menu > Properties > Layout (or start `sevenmon` in a larger
window). Shrinking by dragging works at any time.

## Building

```
# Windows 10/11 with Visual Studio 2022 (v143 toolset)
cmake --preset msvc-x64 && cmake --build --preset msvc-x64     -> build\x64\sevenmon.exe
cmake --preset msvc-x86 && cmake --build --preset msvc-x86     -> build\x86\sevenmon.exe

# Linux with mingw-w64
cmake --preset mingw-x64 && cmake --build build/x64              -> build/x64/sevenmon.exe
cmake --preset mingw-x86 && cmake --build build/x86              -> build/x86/sevenmon.exe

python tools/check_imports.py build/x64/sevenmon.exe build/x86/sevenmon.exe
ctest --test-dir build/x64 --output-on-failure
```

Full instructions, toolchain notes and release packaging: [docs/building.md](docs/building.md).

## Documentation

* [docs/commands.md](docs/commands.md) - command and option reference, exit codes
* [docs/json-schema.md](docs/json-schema.md) - JSON/NDJSON/CSV formats
* [docs/hardware-support.md](docs/hardware-support.md) - data sources, permissions, sensor limitations
* [docs/building.md](docs/building.md) - toolchains, presets, packaging
* [docs/windows7-testing.md](docs/windows7-testing.md) - what has been tested where, and the Windows 7 test plan

## Status and honesty notes

* Cross-built with mingw-w64 (x86 and x64) and statically checked for Windows 7
  imports; the x64 test suite and every command were exercised under Wine on Linux. The
  MSVC build is defined in the GitHub Actions workflow but was not compiled in the
  development environment. **Wine is not Windows 7**: see
  [docs/windows7-testing.md](docs/windows7-testing.md) for which behaviors still need to
  be confirmed on a real Windows 7 SP1 machine or VM.
* GPU utilization/temperature exist only through NVML (NVIDIA) or ADL Overdrive 5
  (AMD). Intel integrated graphics and other vendors get detection, driver version and
  VRAM size only.

## License

MIT - see [LICENSE](LICENSE).
