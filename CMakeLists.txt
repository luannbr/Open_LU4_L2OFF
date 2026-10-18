cmake_minimum_required(VERSION 3.21)
project(Lineage2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(L2_MEMORY_BACKEND "system" CACHE STRING "Memory allocation backend: system or legacy")
set_property(CACHE L2_MEMORY_BACKEND PROPERTY STRINGS system legacy)
if(L2_MEMORY_BACKEND STREQUAL "system")
    add_compile_definitions(L2_MEMORY_BACKEND_LEGACY=0)
elseif(L2_MEMORY_BACKEND STREQUAL "legacy")
    add_compile_definitions(L2_MEMORY_BACKEND_LEGACY=1)
else()
    message(FATAL_ERROR "L2_MEMORY_BACKEND must be system or legacy")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(L2RuntimeFiles)
include(L2Warnings)

# Shared-record copying and field registration use offsetof on polymorphic
# records. Clang supports this extension but otherwise warns for non-standard-
# layout types. These are operational offsets, not layout auditors.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    add_compile_options(-Wno-invalid-offsetof)
endif()

if(WIN32)
    # Windows.h min/max macros break std::min and std::numeric_limits<T>::max.
    add_compile_definitions(NOMINMAX)
endif()

if(MSVC)
    # Variadic selector macros such as L2_RELEASE need the conforming
    # preprocessor. The traditional one passes __VA_ARGS__ as one argument.
    add_compile_options(/Zc:preprocessor)
    # Clang and GCC accept a string literal for a non-const char pointer
    # parameter with a warning. Keep that behavior for the shared code.
    add_compile_options(/Zc:strictStrings-)
    # Allow automatic inlining in optimized builds with debug symbols.
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:/Ob2>")
    # The sources are UTF-8 without a BOM. Without this option MSVC reads
    # them in the Windows code page and changes non-ASCII string literals.
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>")

    # MSBuild hands one cl.exe the whole source list of a project, and without
    # /MP that cl.exe compiles the files one after another. A single process
    # can hold 2 to 3 GB on the large L2Server sources, so keep the default
    # small. Ninja and Make already run one compiler per job and get no /MP.
    set(L2_MSVC_COMPILE_JOBS "3" CACHE STRING
        "Parallel cl.exe processes per project with a Visual Studio generator (0 turns /MP off)")
    if(NOT L2_MSVC_COMPILE_JOBS MATCHES "^[0-9]+$")
        message(FATAL_ERROR "L2_MSVC_COMPILE_JOBS must be a non-negative integer")
    endif()
    if(CMAKE_GENERATOR MATCHES "Visual Studio" AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
       AND NOT L2_MSVC_COMPILE_JOBS EQUAL 0)
        add_compile_options(/MP${L2_MSVC_COMPILE_JOBS})
    endif()
endif()

option(L2_BUILD_L2NPC "Build the recovered L2NPC executable" ON)
option(L2_BUILD_CACHED "Build the recovered CacheD executable" ON)
option(L2_BUILD_L2SERVER "Build the recovered L2Server executable" ON)
option(L2_BUILD_AUTHD "Build the recovered AuthD project" ON)
option(L2_BUILD_AUTHGATED "Build the recovered AuthGateD gateway" ON)
option(L2_BUILD_NASC "Build the NASC compiler for NPC AI scripts" ON)
option(
    L2_ENABLE_GUARD_PROFILING
    "Instrument L2_GUARD_FUNCTION scopes with the optional L2Core profiler"
    OFF)
option(
    L2_ENABLE_STARTUP_DIAGNOSTICS
    "Collect the high-frequency geodata startup probes in L2Server (slow startup)"
    OFF)
option(
    L2_ENABLE_ASAN_UBSAN
    "Build native targets with AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF)
option(
    L2_ENABLE_TSAN
    "Build native targets with ThreadSanitizer"
    OFF)
option(
    L2_ENABLE_IPO
    "Enable IPO/LTO in Release and RelWithDebInfo builds"
    OFF)

if(L2_ENABLE_ASAN_UBSAN AND L2_ENABLE_TSAN)
    message(FATAL_ERROR
        "L2_ENABLE_ASAN_UBSAN and L2_ENABLE_TSAN are mutually exclusive")
endif()

if(L2_ENABLE_ASAN_UBSAN OR L2_ENABLE_TSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR
            "Portable sanitizer presets require a Clang or GCC-compatible compiler")
    endif()
    if(L2_ENABLE_ASAN_UBSAN)
        set(_l2_sanitizers "address,undefined")
    else()
        set(_l2_sanitizers "thread")
    endif()
    add_compile_options(
        "-fsanitize=${_l2_sanitizers}"
        -fno-omit-frame-pointer)
    add_link_options("-fsanitize=${_l2_sanitizers}")
    if(L2_ENABLE_ASAN_UBSAN AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # The GCC vptr check needs the typeinfo of each checked class.
        # L2Server does not define the key functions of the shared memory
        # records (CSharedObject and its subclasses), so their typeinfo
        # does not exist and L2Server does not link. These records live in
        # shared memory, so a vptr check on them is not useful anyway.
        add_compile_options(-fno-sanitize=vptr)
    endif()
    unset(_l2_sanitizers)
endif()

if(L2_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _l2_ipo_supported OUTPUT _l2_ipo_error LANGUAGES CXX)
    if(NOT _l2_ipo_supported)
        message(FATAL_ERROR "IPO/LTO check failed:\n${_l2_ipo_error}")
    endif()
    unset(_l2_ipo_supported)
    unset(_l2_ipo_error)
endif()
# Set these before creating libraries, object libraries and executables.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ${L2_ENABLE_IPO})
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ${L2_ENABLE_IPO})

# The Windows PostgreSQL installer puts libpq, OpenSSL and libxml2 in one
# prefix. Without PG_PREFIX, use the newest installation in Program Files.
# Products search that prefix for all three libraries.
if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
    if(NOT DEFINED PG_PREFIX AND NOT DEFINED ENV{PG_PREFIX})
        file(TO_CMAKE_PATH "$ENV{ProgramFiles}" _l2_program_files)
        file(GLOB _l2_pg_headers "${_l2_program_files}/PostgreSQL/*/include/libpq-fe.h")
        if(_l2_pg_headers)
            list(SORT _l2_pg_headers COMPARE NATURAL ORDER DESCENDING)
            list(GET _l2_pg_headers 0 _l2_pg_header)
            cmake_path(GET _l2_pg_header PARENT_PATH _l2_pg_include)
            cmake_path(GET _l2_pg_include PARENT_PATH PG_PREFIX)
            message(STATUS "Using PostgreSQL installation ${PG_PREFIX}")
        endif()
        unset(_l2_program_files)
        unset(_l2_pg_headers)
        unset(_l2_pg_header)
        unset(_l2_pg_include)
    endif()
    if(DEFINED PG_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH "${PG_PREFIX}")
    elseif(DEFINED ENV{PG_PREFIX})
        file(TO_CMAKE_PATH "$ENV{PG_PREFIX}" _l2_pg_prefix)
        list(APPEND CMAKE_PREFIX_PATH "${_l2_pg_prefix}")
        unset(_l2_pg_prefix)
    endif()
endif()

# L2Core is a separate source owner from Shared.
add_subdirectory(L2Core)

# Protocol owns common numeric declarations in headers.
add_subdirectory(Protocol)

# Keep one Shared source tree and compile it with each product's macros.
add_subdirectory(Shared)

if(L2_BUILD_L2NPC)
    add_subdirectory(L2NPC)
endif()

if(L2_BUILD_CACHED)
    add_subdirectory(CacheD)
endif()

if(L2_BUILD_AUTHD)
    add_subdirectory(AuthD)
endif()

if(L2_BUILD_AUTHGATED)
    add_subdirectory(AuthGateD)
endif()

if(L2_BUILD_L2SERVER)
    add_subdirectory(L2Server)
endif()

if(L2_BUILD_NASC)
    add_subdirectory(NASC)
endif()

# CLion uses FOLDER for generated run configurations. Keep only the five
# applications at the top level. Folder names are flat: CLion
# displays "Tools/Product" as one literal folder, not a nested hierarchy.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
function(l2_group_ide_targets directory)
    get_property(directory_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(candidate IN LISTS directory_targets)
        get_target_property(candidate_type "${candidate}" TYPE)
        if(candidate MATCHES "^(AuthD|AuthGateD|CacheD|L2NPC|L2Server)$")
            # An empty FOLDER becomes an unnamed folder in CLion.
            set_property(TARGET "${candidate}" PROPERTY FOLDER)
            continue()
        elseif(candidate_type MATCHES "_LIBRARY$")
            set(folder "Build internals")
        elseif(candidate_type STREQUAL "EXECUTABLE")
            set(folder "Tools")
        else()
            set(folder "Maintenance")
        endif()
        set_target_properties("${candidate}" PROPERTIES FOLDER "${folder}")
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        l2_group_ide_targets("${subdirectory}")
    endforeach()
endfunction()
l2_group_ide_targets("${CMAKE_CURRENT_SOURCE_DIR}")
