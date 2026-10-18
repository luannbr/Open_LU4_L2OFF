include_guard(GLOBAL)

# Keep each built executable and its runtime files in one product-owned
# directory. Single-config generators use build/<preset>/<Product>. Visual
# Studio and other multi-config generators use build/<preset>/<Config>/<Product>
# so configurations can never overwrite one another. NO_POSTGRESQL_DLLS skips
# the PostgreSQL client DLLs for a tool that does not use libpq.
function(l2_configure_product_runtime target product)
    cmake_parse_arguments(RUNTIME "NO_POSTGRESQL_DLLS" "" "" ${ARGN})
    if(NOT TARGET "${target}")
        message(FATAL_ERROR
            "l2_configure_product_runtime: unknown target '${target}'")
    endif()

    if(CMAKE_CONFIGURATION_TYPES)
        foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
            string(TOUPPER "${configuration}" configuration_upper)
            set_target_properties(
                "${target}"
                PROPERTIES
                    "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
                    "${CMAKE_BINARY_DIR}/${configuration}/${product}")
        endforeach()
        set(runtime_directory "${CMAKE_BINARY_DIR}/$<CONFIG>/${product}")
    else()
        set(runtime_directory "${CMAKE_BINARY_DIR}/${product}")
        set_target_properties(
            "${target}"
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${runtime_directory}")
    endif()

    set_property(
        TARGET "${target}"
        PROPERTY L2_RUNTIME_PRODUCT "${product}")
    set_property(
        TARGET "${target}"
        PROPERTY L2_RUNTIME_DIRECTORY "${runtime_directory}")
    if(NOT RUNTIME_NO_POSTGRESQL_DLLS)
        l2_stage_postgresql_client_dlls("${target}")
    endif()
endfunction()

# Windows loads a DLL from the executable directory before PATH. Other tools
# on PATH (MinGW, Git) ship DLLs with the same names, for example zlib1.dll
# and libiconv-2.dll, built for another C runtime. Copy the client DLLs of the
# PostgreSQL installation that provides libpq (libpq, libxml2, OpenSSL and
# their dependencies) beside each product.
function(l2_stage_postgresql_client_dlls target)
    if(NOT WIN32 OR NOT PQ_LIB)
        return()
    endif()
    get_filename_component(library_directory "${PQ_LIB}" DIRECTORY)
    get_filename_component(dll_directory "${library_directory}/../bin" ABSOLUTE)
    file(GLOB dlls
        "${dll_directory}/libpq.dll"
        "${dll_directory}/libxml2.dll"
        "${dll_directory}/zlib1.dll"
        "${dll_directory}/libintl-*.dll"
        "${dll_directory}/libiconv-*.dll"
        "${dll_directory}/libwinpthread-*.dll"
        "${dll_directory}/libssl-*.dll"
        "${dll_directory}/libcrypto-*.dll")
    foreach(dll IN LISTS dlls)
        get_filename_component(name "${dll}" NAME)
        l2_stage_runtime_file("${target}" "${dll}" "${name}")
    endforeach()
endfunction()

# Stage a default runtime file at configure time. configure_file records the
# source as a CMake dependency, so editing a default config triggers a reload
# and updates every configured runtime layout without requiring a relink.
function(l2_stage_runtime_file target source destination)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR
            "l2_stage_runtime_file: unknown target '${target}'")
    endif()

    get_target_property(product "${target}" L2_RUNTIME_PRODUCT)
    if(NOT product)
        message(FATAL_ERROR
            "l2_stage_runtime_file: '${target}' has no product runtime")
    endif()

    get_filename_component(
        source_absolute "${source}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${source_absolute}")
        message(FATAL_ERROR
            "runtime file for '${target}' does not exist: ${source_absolute}")
    endif()

    if(CMAKE_CONFIGURATION_TYPES)
        foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
            configure_file(
                "${source_absolute}"
                "${CMAKE_BINARY_DIR}/${configuration}/${product}/${destination}"
                COPYONLY)
        endforeach()
    else()
        configure_file(
            "${source_absolute}"
            "${CMAKE_BINARY_DIR}/${product}/${destination}"
            COPYONLY)
    endif()
endfunction()
