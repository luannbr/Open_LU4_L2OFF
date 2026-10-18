# NASC compiles the NPC AI sources in script/ai/src into ai.obj for L2NPC.
# The output always uses the win64 layout of the original NASC64_2015.exe.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "NASC needs a 64-bit host because the ai.obj pointer size is 8")
endif()

add_library(nasc_core STATIC
    src/AICompiler.cpp
    src/AILexer.cpp
    src/AIParser.cpp
    src/AISymbolDatabase.cpp
    src/CLI.cpp
    src/CodeGenerator.cpp
    src/CompileInventory.cpp
    src/Encoding.cpp
    src/HelpGenerator.cpp
    src/ParserTables.cpp
    src/Platform.cpp
    src/PrecompiledHeaderDB.cpp
    src/ScriptMetadata.cpp
    src/ScriptTypes.cpp
    src/SemanticActions.cpp
    src/Target.cpp
    src/Version.cpp)
target_include_directories(nasc_core PUBLIC src)
target_compile_features(nasc_core PUBLIC cxx_std_20)

add_executable(NASC src/NASC.cpp)
target_link_libraries(NASC PRIVATE nasc_core)
# The recursive parser overflows the default Windows stack (1 MB, MinGW 2 MB)
# on npc_class1.ai. Reserve 256 MB; only the pages in use are committed.
if(MSVC)
    target_link_options(NASC PRIVATE /STACK:268435456)
elseif(MINGW)
    target_link_options(NASC PRIVATE -Wl,--stack,268435456)
endif()
if(MINGW)
    # NASC.cpp uses wmain on Windows to read UTF-16 arguments. Static runtime
    # libraries keep the tool usable without the MinGW DLLs.
    target_link_options(NASC PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()
l2_configure_product_runtime(NASC NASC NO_POSTGRESQL_DLLS)
l2_target_warnings(nasc_core NASC)

install(TARGETS NASC RUNTIME DESTINATION NASC)
