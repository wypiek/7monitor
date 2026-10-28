# Windows 7 SP1 compatibility: tested vs. not yet tested

A successful build is not proof that SevenMonitor runs on Windows 7. This page records
exactly what has been verified, where, and what still requires a real Windows 7 SP1
machine or virtual machine.

## Verified in the development environment (Linux, mingw-w64 13, Wine 9.0)

| Item | Result |
|------|--------|
| x64 and x86 Release builds (mingw-w64 GCC 13, `-win32` thread model, static) | build without warnings (`-Wall -Wextra`) |
| PE subsystem / OS version fields | 6.1 for both binaries |
| Import table vs. Windows 7 SP1 baseline (`tools/check_imports.py`) | OK for `sevenmon.exe` and `sevenmon_tests.exe`, x86 and x64: only KERNEL32, USER32, ADVAPI32, OLE32, OLEAUT32, IPHLPAPI, WS2_32, PSAPI, POWRPROF and msvcrt are imported; no API-set DLLs; no deny-listed functions. Every imported function was additionally reviewed by hand against the Windows 7 SDK documentation. |
| Unit tests (x64, under Wine) | 46 unit tests pass |
| Live tests (x64, under Wine) | 7 live tests pass, including the 400-iteration stability test (private memory 9.5 MiB, growth < 0.1 MiB) |
| `ctest` (x64, under Wine) | 5/5 pass |
| Every command run under Wine (x64) | `summary`, `cpu`, `memory`, `gpu`, `disk`, `network`, `battery`, `processes`, `process`, `sensors`, `hardware`, `doctor`, `export` (CSV/JSON, append, directory output, non-ASCII path), `watch` streaming (NDJSON, text), error paths (bad option -> 2, unknown PID -> 3, invalid output path -> 4) |
| Interactive dashboard under Wine's console in a pseudo-terminal | starts, renders the overview at 120x40 with box-drawing characters, exits after `--count 3` with exit code 0 |

What Wine could **not** exercise (its implementation differs from Windows): disk
throughput (`IOCTL_DISK_PERFORMANCE` unsupported), SSD detection, page-file details,
handle counts, private bytes, DXGI (Wine's adapter fell back to `EnumDisplayDevices`),
SMART, ACPI thermal zones, battery, NVML, ADL, and real `conhost.exe` behavior (font
detection, raster fonts, window resizing, Ctrl+C delivery, screen-buffer restoration).

## Not yet verified (requires Windows 7 SP1)

| Item | Status |
|------|--------|
| Process starts on a clean Windows 7 SP1 (x86 and x64) without missing-import errors | **needs Win7** |
| MSVC v143 build (x86/x64) compiles and passes tests | defined in CI (`windows-2022`), not compiled in the dev environment |
| Disk throughput and active % via `IOCTL_DISK_PERFORMANCE` as a standard user | **needs Win7** |
| Seek-penalty SSD detection behavior with Windows 7 `msahci` / vendor drivers | **needs Win7** |
| DXGI 1.1 adapter enumeration, driver version matching | **needs Win7** |
| NVML on a Windows 7 NVIDIA driver (e.g. 47x series), ADL on a Radeon Crimson driver | **needs Win7 + hardware** |
| SMART via WMI (elevated) on SATA disks | **needs Win7** |
| Battery health WMI classes on a laptop | **needs Win7 laptop** |
| Dashboard in the original `cmd.exe` window: raster font (ASCII fallback), Lucida Console/Consolas (box drawing), resize via Properties, drag-to-shrink, Ctrl+C restores the original buffer, Q restores it, closing the window | **needs Win7** |
| Idle CPU usage and memory of `sevenmon watch` on Windows 7 | **needs Win7** (to be recorded below) |
| 24-hour `sevenmon watch` / `export --duration 86400` without handle or memory growth | **needs Win7** |
| Standard user (non-admin, UAC on): every command works, SMART/ACPI show explanations | **needs Win7** |
| Windows 8.1 / 10 / 11 smoke test | MSVC CI runs on Windows Server 2022 (Windows 10 kernel family) once the workflow runs |

## Windows 7 SP1 test procedure

Use a Windows 7 SP1 VM (x64; repeat the core steps with x86 Windows 7 for the x86 build).
No updates beyond SP1 are required for SevenMonitor itself.

1. Copy `sevenmon.exe` (x64 and x86) to e.g. `C:\Tools\Seven Monitor\` (a path with a
   space). Open the *original* Command Prompt (not PowerShell), standard user.
2. `sevenmon version` -> `sevenmon 1.0.0`; `echo %ERRORLEVEL%` -> 0.
3. `sevenmon doctor` -> record the table. Expect `Service Pack 1 ok`, `Security support
   warning`, CPU/memory/process/network checks `ok`, disk throughput `ok`.
4. `sevenmon` -> compare RAM with Task Manager (Performance tab); CPU % should track Task
   Manager within a few percent while a load is running (e.g. a busy loop in another window).
5. `sevenmon cpu`, `memory`, `gpu --verbose`, `disk --verbose`, `network --verbose`,
   `battery`, `processes --top 10`, `process <PID of explorer.exe>`, `sensors`,
   `hardware` -> no crash; values plausible; unavailable values say N/A with a reason.
6. Copy a large file while running `sevenmon disk --interval 2000` -> read/write
   throughput non-zero for the source/target disks.
7. Download something while running `sevenmon network --interval 2000` -> throughput
   matches Task Manager's Networking tab roughly. Disable and re-enable the adapter
   during `sevenmon watch --json` -> no crash, "counter reset"/"first sample" handled.
8. JSON/CSV: `sevenmon summary --json > s.json`, `sevenmon processes --csv > p.csv`;
   open in a JSON validator / Excel. `sevenmon export --csv --output "C:\Tools\Seven
   Monitor\Müller stats.csv" --count 5` (type the ü) -> file created with 5 rows; repeat
   with `--append` -> 10 rows, one header.
9. Dashboard, default raster font: `sevenmon watch` -> ASCII borders and bars, no
   garbage; press P, Down x5, Enter, Esc, S, `/` + text + Enter, C, M, G, D, N, H, Q.
   The previous console contents reappear after Q.
10. Switch the console font to Lucida Console -> box-drawing borders. Use Properties >
    Layout to resize to 160x50 and to 60x20 -> layout adapts; drag the window smaller ->
    adapts, no scrollbars.
11. Ctrl+C inside the dashboard -> exits cleanly, cursor visible, prompt works.
12. Start Notepad, select it in the dashboard, press K then N (nothing happens), K then Y
    (Notepad closes). Try K on `csrss.exe` -> "Access is denied", no crash.
13. Leave `sevenmon watch` running 1 hour (ideally 24 h); record CPU % and memory of
    `sevenmon.exe` in Task Manager at start and end.
14. Run an elevated prompt: `sevenmon disk` / `sevenmon sensors` -> SMART and ACPI rows
    now show values or a precise reason.
15. Repeat 2-5 and 9 with the x86 build on Windows 7 x64 (WOW64) and on Windows 7 x86.

Record results here:

| Date | Windows build | Arch | Build tested | Steps passed | Idle CPU / memory | Notes |
|------|---------------|------|--------------|--------------|-------------------|-------|
| - | - | - | - | not yet run | - | - |
