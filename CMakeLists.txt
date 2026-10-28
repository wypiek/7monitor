cmake_minimum_required(VERSION 3.16)

project(SevenMonitor VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "SevenMonitor is a Windows program. On Linux/macOS configure with "
                      "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x64.cmake (or mingw-x86.cmake).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SEVENMON_BUILD_TESTS "Build the sevenmon_tests executable" ON)

# Static CRT for MSVC so the executable needs no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(SEVENMON_CORE_SOURCES
  src/util/strings.cpp
  src/util/format.cpp
  src/util/json.cpp
  src/util/csv.cpp
  src/platform/win.cpp
  src/platform/console.cpp
  src/platform/wmi.cpp
  src/platform/interrupt.cpp
  src/collectors/cpu.cpp
  src/collectors/memory.cpp
  src/collectors/gpu.cpp
  src/collectors/gpu_nvml.cpp
  src/collectors/gpu_adl.cpp
  src/collectors/disk.cpp
  src/collectors/smart.cpp
  src/collectors/network.cpp
  src/collectors/battery.cpp
  src/collectors/system.cpp
  src/collectors/smbios.cpp
  src/collectors/process.cpp
  src/collectors/sensors.cpp
  src/sampler/sampler.cpp
  src/output/table.cpp
  src/output/report_json.cpp
  src/output/report_text.cpp
  src/tui/canvas.cpp
  src/tui/layout.cpp
  src/tui/ui_state.cpp
  src/tui/console_tui.cpp
  src/app/cli.cpp
  src/app/commands.cpp
  src/app/doctor.cpp
)

add_library(sevenmon_core STATIC ${SEVENMON_CORE_SOURCES})
target_include_directories(sevenmon_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(sevenmon_core PUBLIC
  _WIN32_WINNT=0x0601
  WINVER=0x0601
  NTDDI_VERSION=0x06010000
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  PSAPI_VERSION=1
  SEVENMON_VERSION="${PROJECT_VERSION}")
target_link_libraries(sevenmon_core PUBLIC
  iphlpapi ws2_32 psapi powrprof ole32 oleaut32 wbemuuid advapi32 user32 uuid)

if(MSVC)
  target_compile_options(sevenmon_core PUBLIC /W4 /utf-8 /permissive- /EHsc /Zc:__cplusplus
    $<$<CONFIG:Release>:/O1 /Gy /Gw>)
  target_compile_definitions(sevenmon_core PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(sevenmon_core PUBLIC -Wall -Wextra -Wno-cast-function-type -municode
    $<$<CONFIG:Release>:-Os -ffunction-sections -fdata-sections>)
endif()

function(sevenmon_configure_exe target)
  # Keep binaries directly in the build directory (build/x64/sevenmon.exe) even for
  # multi-config generators; "$<0:>" suppresses the per-configuration subdirectory.
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}$<0:>")
  if(MSVC)
    target_link_options(${target} PRIVATE /SUBSYSTEM:CONSOLE,6.01 /MANIFEST:NO
      $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
  else()
    target_link_options(${target} PRIVATE
      -municode -static -static-libgcc -static-libstdc++
      -Wl,--major-subsystem-version,6 -Wl,--minor-subsystem-version,1
      -Wl,--major-os-version,6 -Wl,--minor-os-version,1
      -Wl,--dynamicbase -Wl,--nxcompat
      $<$<CONFIG:Release>:-s -Wl,--gc-sections>)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      target_link_options(${target} PRIVATE -Wl,--high-entropy-va)
    endif()
  endif()
endfunction()

add_executable(sevenmon src/main.cpp res/sevenmon.rc)
target_link_libraries(sevenmon PRIVATE sevenmon_core)
sevenmon_configure_exe(sevenmon)

if(SEVENMON_BUILD_TESTS)
  enable_testing()
  add_executable(sevenmon_tests
    tests/test_main.cpp
    tests/test_rates.cpp
    tests/test_format.cpp
    tests/test_cli.cpp
    tests/test_json_csv.cpp
    tests/test_smbios_smart.cpp
    tests/test_layout.cpp
    tests/test_collectors.cpp
    tests/test_stability.cpp)
  target_link_libraries(sevenmon_tests PRIVATE sevenmon_core)
  sevenmon_configure_exe(sevenmon_tests)
  add_test(NAME unit COMMAND sevenmon_tests --exclude-tag live)
  add_test(NAME live COMMAND sevenmon_tests --tag live)
  add_test(NAME cli_version COMMAND sevenmon version)
  add_test(NAME cli_summary_json COMMAND sevenmon summary --json --interval 200)
  add_test(NAME cli_usage_error COMMAND sevenmon --interval nope)
  set_tests_properties(cli_usage_error PROPERTIES WILL_FAIL TRUE)
endif()

install(TARGETS sevenmon RUNTIME DESTINATION .)
install(FILES README.md LICENSE DESTINATION .)
install(DIRECTORY docs/ DESTINATION docs)
