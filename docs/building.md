# Building SevenMonitor

SevenMonitor is plain C++17 + Win32 with no third-party libraries. Two toolchains are
supported: Visual Studio 2022 (Windows host) and mingw-w64 (Linux host, cross build).
Both produce statically linked console executables that target Windows 7 SP1:

* `_WIN32_WINNT=0x0601`, `WINVER=0x0601`, `NTDDI_VERSION=0x06010000`
* PE subsystem version 6.01 (`/SUBSYSTEM:CONSOLE,6.01` or `--major/minor-subsystem-version`)
* static C/C++ runtime (`/MT`, or `-static -static-libgcc -static-libstdc++`)
* embedded manifest: `asInvoker`, `supportedOS` for Windows 7, 8, 8.1 and 10/11
* APIs newer than Windows 7 are never imported; optional APIs (DXGI, NVML, ADL,
  `NtQuerySystemInformation`, `RtlGetVersion`) are resolved with `GetProcAddress`.

Release paths: `build/x86/sevenmon.exe` and `build/x64/sevenmon.exe`.

## Visual Studio 2022 (Windows 10/11 host)

Requirements: Visual Studio 2022 with "Desktop development with C++" (MSVC v143 and a
Windows 10/11 SDK), CMake >= 3.21 (bundled with VS), Python 3 for the import check.

```
cmake --preset msvc-x64
cmake --build --preset msvc-x64
ctest --test-dir build\x64 -C Release --output-on-failure

cmake --preset msvc-x86
cmake --build --preset msvc-x86
ctest --test-dir build\x86 -C Release --output-on-failure

python tools\check_imports.py build\x64\sevenmon.exe build\x86\sevenmon.exe
```

Toolset notes:

* The MSVC v143 toolset and its static CRT support Windows 7 SP1 as a target. Because
  `/MT` is used, the Visual C++ Redistributable is not needed on the target machine.
* The Windows 10/11 SDK headers are fine for Windows 7 targets as long as
  `_WIN32_WINNT=0x0601` is defined (CMake does this); the import check verifies the result.
* Do not use the Visual Studio 2015-2019 "v14x_xp" toolsets - they are not needed and the
  project relies on Vista+ APIs anyway.
* If a future MSVC toolset drops Windows 7 support, `tools/check_imports.py` is the
  first line of defense; the definitive test is running on Windows 7 (see
  windows7-testing.md).

## mingw-w64 (Linux host)

Debian/Ubuntu packages: `g++-mingw-w64-x86-64-win32 g++-mingw-w64-i686-win32 cmake
ninja-build` (the `-win32` thread-model compilers avoid a winpthreads dependency; the
toolchain files fall back to the plain `x86_64-w64-mingw32-g++` names).

```
cmake --preset mingw-x64 && cmake --build build/x64
cmake --preset mingw-x86 && cmake --build build/x86
python3 tools/check_imports.py build/x64/sevenmon.exe build/x86/sevenmon.exe
```

mingw-w64 links against `msvcrt.dll`, which every Windows 7 installation has.

### Running the tests under Wine

If `wine` is installed, the x64 toolchain file sets it as the cross-compiling emulator,
so `ctest` runs the x64 test binary through Wine:

```
WINEDEBUG=-all LANG=C.UTF-8 ctest --test-dir build/x64 --output-on-failure
```

`LANG=C.UTF-8` matters for tests that pass non-ASCII paths on the command line (Wine
converts arguments using the Unix locale). A 64-bit-only Wine cannot run the x86 build.
Wine is useful for catching crashes and logic errors; it is **not** evidence of Windows 7
compatibility (it implements several APIs differently or not at all, e.g.
`IOCTL_DISK_PERFORMANCE`, page-file information, handle counts).

## Tests

`sevenmon_tests.exe` contains:

* **unit** tests (no system access): utilization/throughput math including counter
  wrap and reset, formatting of missing sensors, CLI parsing and validation, JSON and
  CSV escaping/flattening, SMBIOS and SMART parsers with malformed input, dashboard
  layouts for every view at sizes from 40x10 to 250x100 in color/monochrome and
  Unicode/ASCII, console resize behavior of the process selection, keyboard flows
  (filter, sort, kill confirmation).
* **live** tests (read the real machine): each collector, a full snapshot -> JSON/CSV,
  and a stability test running 400 sample+render iterations with the background tier
  at 100 ms, asserting no growth of handles (> 16) or private memory (> 4 MiB).

```
sevenmon_tests.exe                     all tests
sevenmon_tests.exe --exclude-tag live  unit tests only
sevenmon_tests.exe --tag live          live tests only
sevenmon_tests.exe layout              tests whose name contains "layout"
```

`ctest` additionally runs `sevenmon version`, `sevenmon summary --json` and checks that an
invalid option fails.

## Release packaging

Build both Release configurations, then:

```
python3 tools/package.py
```

This creates `dist/sevenmon-<version>-win-x86.zip`, `dist/sevenmon-<version>-win-x64.zip`
(each with `sevenmon.exe`, README, LICENSE and docs) and `dist/SHA256SUMS`. The GitHub
Actions workflow (`.github/workflows/build.yml`) builds with MSVC (x86, x64; runs the
tests natively on Windows Server) and mingw-w64 (x86, x64; packages the zips), and runs
the import check on every binary.

Recommended release checklist:

1. Bump `project(SevenMonitor VERSION ...)` in `CMakeLists.txt` and the version strings
   in `res/sevenmon.rc` / `res/sevenmon.manifest`.
2. CI green; `tools/check_imports.py` OK for both architectures.
3. Run the Windows 7 SP1 checklist in `docs/windows7-testing.md` for both builds and
   record the results.
4. `python3 tools/package.py`, publish the zips with `SHA256SUMS`.
5. Optionally Authenticode-sign `sevenmon.exe` with a SHA-256 certificate (Windows 7 SP1
   verifies SHA-2 signatures once KB3033929 or KB4474419 is installed).
