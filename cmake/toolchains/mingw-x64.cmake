set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(SEVENMON_MINGW_TRIPLE x86_64-w64-mingw32)
include(${CMAKE_CURRENT_LIST_DIR}/mingw-common.cmake)
# Lets ctest run the x64 test binaries through Wine on a Linux host (if installed).
find_program(_sm_wine NAMES wine wine64)
if(_sm_wine)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${_sm_wine})
endif()
