set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(_l2_mingw_cxx NAMES x86_64-w64-mingw32-g++ REQUIRED)
find_program(_l2_mingw_rc NAMES x86_64-w64-mingw32-windres REQUIRED)
set(CMAKE_CXX_COMPILER "${_l2_mingw_cxx}")
set(CMAKE_RC_COMPILER "${_l2_mingw_rc}")

# ONLY has no effect when the find root is empty: on macOS that selected
# /Library/PostgreSQL's Mach-O libpq for Windows targets.
if(NOT CMAKE_FIND_ROOT_PATH)
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -print-sysroot
        OUTPUT_VARIABLE _l2_mingw_sysroot
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    if(NOT IS_DIRECTORY "${_l2_mingw_sysroot}")
        message(FATAL_ERROR "Set CMAKE_FIND_ROOT_PATH to the MinGW target sysroot")
    endif()
    set(CMAKE_FIND_ROOT_PATH "${_l2_mingw_sysroot}")
endif()
if(PG_PREFIX)
    list(APPEND CMAKE_FIND_ROOT_PATH "${PG_PREFIX}")
elseif(DEFINED ENV{PG_PREFIX})
    list(APPEND CMAKE_FIND_ROOT_PATH "$ENV{PG_PREFIX}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
