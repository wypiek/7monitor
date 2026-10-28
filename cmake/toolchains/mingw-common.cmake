# Shared logic for the mingw-w64 cross toolchains. Expects SEVENMON_MINGW_TRIPLE.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 6.1)

# Prefer the "-win32" thread-model compilers (Debian/Ubuntu naming): they do not pull in
# winpthreads. Fall back to the plain triple name used by most other distributions.
find_program(_sm_cxx NAMES ${SEVENMON_MINGW_TRIPLE}-g++-win32 ${SEVENMON_MINGW_TRIPLE}-g++ REQUIRED)
find_program(_sm_cc  NAMES ${SEVENMON_MINGW_TRIPLE}-gcc-win32 ${SEVENMON_MINGW_TRIPLE}-gcc REQUIRED)
find_program(_sm_rc  NAMES ${SEVENMON_MINGW_TRIPLE}-windres windres REQUIRED)
set(CMAKE_CXX_COMPILER ${_sm_cxx})
set(CMAKE_C_COMPILER ${_sm_cc})
set(CMAKE_RC_COMPILER ${_sm_rc})

set(CMAKE_FIND_ROOT_PATH /usr/${SEVENMON_MINGW_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
