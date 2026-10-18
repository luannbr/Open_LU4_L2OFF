include_guard(GLOBAL)
include(CheckCXXCompilerFlag)

# Many final classes keep the virtual methods of the original class, so their
# vtables keep the original shape. Clang 21 reports each such method as an
# unnecessary virtual specifier. The option must follow -Wall and -Wextra,
# which enable it again. CMake removes repeated options, so the per-target copy
# uses the SHELL: form.
set(L2_WARNING_EXCEPTIONS)
if(NOT MSVC)
    check_cxx_compiler_flag(-Wunnecessary-virtual-specifier L2_HAS_UNNECESSARY_VIRTUAL_WARNING)
    if(L2_HAS_UNNECESSARY_VIRTUAL_WARNING)
        list(APPEND L2_WARNING_EXCEPTIONS "SHELL:-Wno-unnecessary-virtual-specifier")
        add_compile_options(-Wno-unnecessary-virtual-specifier)
    endif()
endif()

# l2_target_warnings(<target>...)
# Enables the project warning set for each target. MSVC uses /W4 with the
# conforming /permissive- mode. Clang and GCC use -Wall -Wextra -Wpedantic.
function(l2_target_warnings)
    foreach(target IN LISTS ARGN)
        if(NOT TARGET "${target}")
            message(FATAL_ERROR "l2_target_warnings: unknown target '${target}'")
        endif()
        if(MSVC)
            target_compile_options("${target}" PRIVATE /W4 /permissive-)
        else()
            target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic ${L2_WARNING_EXCEPTIONS})
        endif()
    endforeach()
endfunction()
