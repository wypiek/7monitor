#!/usr/bin/env python3
"""Static Windows 7 compatibility check for PE executables.

Parses the PE import directory directly (no objdump/dumpbin needed, works on Linux and
Windows) and fails when an executable

  * imports from a DLL that is not on the Windows 7 SP1 allow-list,
  * imports from an API-set DLL (api-ms-win-*, ext-ms-*), which Windows 7 resolves only
    partially and only with later updates,
  * imports a function known to be missing from Windows 7 SP1 (deny-list below), or
  * declares a subsystem / OS version higher than 6.1.

This is a *static* check. It catches the common ways a build silently loses Windows 7
support, but it is not a substitute for running the binary on Windows 7 SP1. Functions
that exist only on newer systems must be resolved at run time with GetProcAddress; those
calls do not appear in the import table and are therefore allowed.

Usage: check_imports.py [--list] file.exe [file.exe ...]
"""
import struct
import sys

# DLLs present on a clean Windows 7 SP1 installation.
ALLOWED_DLLS = {
    "kernel32.dll", "user32.dll", "advapi32.dll", "gdi32.dll", "shell32.dll", "ole32.dll",
    "oleaut32.dll", "iphlpapi.dll", "ws2_32.dll", "psapi.dll", "powrprof.dll", "msvcrt.dll",
    "ntdll.dll", "setupapi.dll", "version.dll", "shlwapi.dll", "wbemuuid.dll", "pdh.dll",
    "dbghelp.dll", "userenv.dll", "secur32.dll", "crypt32.dll", "bcrypt.dll", "winmm.dll",
}

# Functions introduced after Windows 7 SP1 (or only via optional updates such as
# KB2533623) that toolchains are known to pull in.
DENIED_FUNCTIONS = {
    "GetSystemTimePreciseAsFileTime", "CreateFile2", "CopyFile2", "GetCurrentThreadStackLimits",
    "SetThreadDescription", "GetThreadDescription", "InitializeSynchronizationBarrier",
    "EnterSynchronizationBarrier", "DeleteSynchronizationBarrier", "WaitOnAddress",
    "WakeByAddressSingle", "WakeByAddressAll", "GetOverlappedResultEx", "CreateEventExW_",
    "SetDefaultDllDirectories", "AddDllDirectory", "RemoveDllDirectory",  # KB2533623 only
    "GetFirmwareType", "PowerSettingRegisterNotification", "GetProcessMitigationPolicy",
    "SetProcessMitigationPolicy", "PrefetchVirtualMemory", "DiscardVirtualMemory",
    "OfferVirtualMemory", "ReclaimVirtualMemory", "VirtualAlloc2", "MapViewOfFile3",
    "CreateFileMapping2", "GetPackageFamilyName", "GetCurrentPackageId", "GetCurrentPackageFullName",
    "GetDpiForWindow", "GetDpiForSystem", "SetThreadDpiAwarenessContext", "SetProcessDpiAwarenessContext",
    "CreatePseudoConsole", "ResizePseudoConsole", "ClosePseudoConsole", "SetCoalescableTimer",
    "GetSystemCpuSetInformation", "GetLogicalProcessorInformationEx_", "QueryInterruptTimePrecise",
    "QueryUnbiasedInterruptTimePrecise", "GetTempPath2W", "GetTempPath2A", "SetFileInformationByHandle_",
    "GetIntegratedDisplaySize", "GetMachineTypeAttributes", "IsWow64Process2", "GetProcessInformation",
    "SetProcessInformation", "GetSystemFirmwareTable_", "GetOsSafeBootMode",
    "CreateDXGIFactory2", "DXGIGetDebugInterface1", "D3D11On12CreateDevice", "PathCchCombine",
    "PathCchCanonicalize", "PathCchAppend", "PathAllocCombine", "RoInitialize", "WindowsCreateString",
    "_initialize_onexit_table", "_register_onexit_function", "_crt_atexit",  # UCRT-only CRT entry points
}


def rva_to_offset(sections, rva):
    for va, vsize, raw_ptr, raw_size in sections:
        if va <= rva < va + max(vsize, raw_size):
            return rva - va + raw_ptr
    return None


def read_cstr(data, off):
    end = data.index(b"\0", off)
    return data[off:end].decode("ascii", "replace")


def parse(path):
    data = open(path, "rb").read()
    if data[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    machine, nsections, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    pe32plus = magic == 0x20B
    os_major, os_minor = struct.unpack_from("<HH", data, opt + 40)
    sub_major, sub_minor = struct.unpack_from("<HH", data, opt + 48)
    subsystem = struct.unpack_from("<H", data, opt + 68)[0]
    dd = opt + (112 if pe32plus else 96)
    imp_rva, imp_size = struct.unpack_from("<II", data, dd + 8)
    sec_off = opt + opt_size
    sections = []
    for i in range(nsections):
        s = sec_off + i * 40
        vsize, va, raw_size, raw_ptr = struct.unpack_from("<IIII", data, s + 8)
        sections.append((va, vsize, raw_ptr, raw_size))
    imports = {}
    off = rva_to_offset(sections, imp_rva) if imp_rva else None
    while off is not None:
        ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", data, off)
        if name_rva == 0:
            break
        dll = read_cstr(data, rva_to_offset(sections, name_rva))
        funcs = []
        thunk = rva_to_offset(sections, ilt or iat)
        step = 8 if pe32plus else 4
        while thunk is not None:
            val = struct.unpack_from("<Q" if pe32plus else "<I", data, thunk)[0]
            if val == 0:
                break
            ordinal_flag = 1 << (63 if pe32plus else 31)
            if val & ordinal_flag:
                funcs.append("#%d" % (val & 0xFFFF))
            else:
                funcs.append(read_cstr(data, rva_to_offset(sections, val & 0x7FFFFFFF) + 2))
            thunk += step
        imports[dll] = funcs
        off += 20
    arch = {0x14C: "x86", 0x8664: "x64", 0xAA64: "ARM64"}.get(machine, hex(machine))
    return {
        "arch": arch,
        "os": (os_major, os_minor),
        "subsystem_version": (sub_major, sub_minor),
        "subsystem": subsystem,
        "imports": imports,
    }


def check(path, list_all):
    info = parse(path)
    problems = []
    if info["subsystem_version"] > (6, 1):
        problems.append("subsystem version %d.%d is newer than Windows 7 (6.1)" % info["subsystem_version"])
    if info["os"] > (6, 1):
        problems.append("OS version %d.%d is newer than Windows 7 (6.1)" % info["os"])
    total = 0
    for dll, funcs in sorted(info["imports"].items(), key=lambda kv: kv[0].lower()):
        low = dll.lower()
        total += len(funcs)
        if low.startswith("api-ms-win-") or low.startswith("ext-ms-"):
            problems.append("imports API-set DLL %s" % dll)
        elif low not in ALLOWED_DLLS:
            problems.append("imports %s, which is not on the Windows 7 SP1 allow-list" % dll)
        for f in funcs:
            if f in DENIED_FUNCTIONS:
                problems.append("imports %s!%s, which Windows 7 SP1 does not provide" % (dll, f))
    print("%s: %s, subsystem %d.%d, %d DLLs, %d imported functions" % (
        path, info["arch"], info["subsystem_version"][0], info["subsystem_version"][1],
        len(info["imports"]), total))
    for dll, funcs in sorted(info["imports"].items(), key=lambda kv: kv[0].lower()):
        print("  %-16s %3d functions" % (dll, len(funcs)))
        if list_all:
            for f in sorted(funcs):
                print("      " + f)
    for p in problems:
        print("  FAIL: " + p)
    if not problems:
        print("  OK: no imports outside the Windows 7 SP1 baseline")
    return not problems


def main(argv):
    list_all = "--list" in argv
    files = [a for a in argv[1:] if a != "--list"]
    if not files:
        print(__doc__)
        return 2
    ok = True
    for f in files:
        try:
            ok = check(f, list_all) and ok
        except (OSError, ValueError, struct.error) as e:
            print("%s: cannot parse: %s" % (f, e))
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
