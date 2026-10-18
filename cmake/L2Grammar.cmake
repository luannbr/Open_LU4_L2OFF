# Parser and scanner generation for the game data grammars.
#
# Each grammar is a bison file (<Name>.y) and each scanner is a re2c file
# (<Name>.re). The generated C++ sources go to <binary dir>/grammar and are
# compiled like ordinary sources of the product that owns them.

include_guard(GLOBAL)

# Homebrew installs a current bison as a keg-only package, because macOS
# ships bison 2.3 in /usr/bin.
# A tree configured before that install keeps the old path in the cache, and
# find_program does not search again. Drop a cached bison that is too old.
if(BISON_EXECUTABLE)
    execute_process(
        COMMAND "${BISON_EXECUTABLE}" --version
        OUTPUT_VARIABLE _l2_bison_version_output
        ERROR_QUIET)
    string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" _l2_bison_version "${_l2_bison_version_output}")
    if(NOT _l2_bison_version OR _l2_bison_version VERSION_LESS 3.5)
        unset(BISON_EXECUTABLE CACHE)
    endif()
    unset(_l2_bison_version_output)
    unset(_l2_bison_version)
endif()
find_program(
    BISON_EXECUTABLE
    NAMES bison win_bison win-bison
    HINTS
        /opt/homebrew/opt/bison/bin
        /usr/local/opt/bison/bin
    DOC "GNU Bison 3.5 or later")
find_package(BISON 3.5 REQUIRED)

find_program(RE2C_EXECUTABLE NAMES re2c DOC "re2c 3.0 or later")
if(NOT RE2C_EXECUTABLE)
    message(FATAL_ERROR "re2c 3.0 or later is required to build the game data scanners")
endif()
execute_process(
    COMMAND "${RE2C_EXECUTABLE}" --version
    OUTPUT_VARIABLE _l2_re2c_version_output
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" L2_RE2C_VERSION "${_l2_re2c_version_output}")
if(L2_RE2C_VERSION VERSION_LESS 3.0)
    message(FATAL_ERROR "re2c 3.0 or later is required, found '${_l2_re2c_version_output}'")
endif()
unset(_l2_re2c_version_output)

# l2_bison_grammar(<out_var> <grammar.y>)
# Generates <Name>.cpp and <Name>Tokens.h. <out_var> receives both paths.
function(l2_bison_grammar out_var grammar)
    get_filename_component(_name "${grammar}" NAME_WE)
    get_filename_component(_source "${grammar}" ABSOLUTE)
    set(_dir "${CMAKE_CURRENT_BINARY_DIR}/grammar")
    set(_cpp "${_dir}/${_name}.cpp")
    set(_header "${_dir}/${_name}Tokens.h")
    add_custom_command(
        OUTPUT "${_cpp}" "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_dir}"
        COMMAND "${BISON_EXECUTABLE}" -Wall "--defines=${_header}" -o "${_cpp}" "${_source}"
        DEPENDS "${_source}"
        COMMENT "Generating parser ${_name} with bison"
        VERBATIM)
    set(${out_var} "${_cpp}" "${_header}" PARENT_SCOPE)
endfunction()

# l2_re2c_scanner(<out_var> <scanner.re>)
# Generates <Name>.cpp. <out_var> receives its path.
function(l2_re2c_scanner out_var scanner)
    set(_runtime "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Shared/src/Runtime")
    get_filename_component(_name "${scanner}" NAME_WE)
    get_filename_component(_source "${scanner}" ABSOLUTE)
    set(_dir "${CMAKE_CURRENT_BINARY_DIR}/grammar")
    set(_cpp "${_dir}/${_name}.cpp")
    add_custom_command(
        OUTPUT "${_cpp}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_dir}"
        COMMAND "${RE2C_EXECUTABLE}" -W -c --ucs2 --input custom
                -I "${_runtime}" -o "${_cpp}" "${_source}"
        DEPENDS "${_source}" "${_runtime}/UTF16Scanner.re"
        COMMENT "Generating scanner ${_name} with re2c"
        VERBATIM)
    set(${out_var} "${_cpp}" PARENT_SCOPE)
endfunction()

# l2_grammar_sources(<out_var> <file>...)
# Runs bison for every .y file and re2c for every .re file.
function(l2_grammar_sources out_var)
    set(_all)
    foreach(_file IN LISTS ARGN)
        if(_file MATCHES "\\.y$")
            l2_bison_grammar(_generated "${_file}")
        elseif(_file MATCHES "\\.re$")
            l2_re2c_scanner(_generated "${_file}")
        else()
            message(FATAL_ERROR "l2_grammar_sources: unsupported file '${_file}'")
        endif()
        list(APPEND _all ${_generated})
    endforeach()
    set(${out_var} ${_all} PARENT_SCOPE)
endfunction()
