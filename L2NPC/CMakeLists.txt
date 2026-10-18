find_package(Threads REQUIRED)
include(L2Grammar)

set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Shared")
set(L2NPC_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SHARED_DIR}/.."
    "${SHARED_DIR}/src"
    "${SHARED_DIR}/src/platform"
    "${SHARED_DIR}/src/net"
    "${SHARED_DIR}/src/common"
    "${SHARED_DIR}/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Portable")

# Bison and re2c generate the game data parsers and scanners at build time.
l2_grammar_sources(
    L2NPC_GRAMMAR_SOURCES
    src/ParserFormation.y
    src/LexerFormation.re
    src/ParserFreeway.y
    src/LexerFreeway.re
    src/ParserSuperPointInfo.y
    src/LexerSuperPointInfo.re
    src/ParserSpawn.y
    src/LexerSpawn.re
    src/LoaderParser.y
    src/LoaderLex.re)

# Shipping Shared sources are compiled exactly once by Shared::L2NPC in the
# L2NPC product context. This target contains only L2NPC and process-
# portability translation units. L2Core has its own physical owner and is
# created by Shared::L2NPC with the same product context.
add_library(l2npc_core STATIC
    src/Portable/NPCServerLifecycle.cpp
    src/DbNpc.cpp
    src/DbNpcSocket.cpp
    src/EventManager.cpp
    src/Config.cpp
    src/Default.cpp
    src/LottoGame.cpp
    src/AIManager.cpp
    src/AirShipEventManager.cpp
    src/ParamProperty.cpp
    src/NpcEvent.cpp
    src/NPCEventFactory.cpp
    src/MakerEventFactory.cpp
    src/CScriptEventHelperClass.cpp
    src/MakerScriptEventHelper.cpp
    src/NPCEventHandler.cpp
    src/ReloadableNPCEventHandler.cpp
    src/ScriptTypeInfo.cpp
    src/ScriptBinCode.cpp
    src/AIScriptCompiler.cpp
    src/L2NPCStatusWnd.cpp
    src/BaseSpawnDefine.cpp
    src/AdminRewardSystem.cpp
    src/DelayedCreator.cpp
    src/NpcMaker.cpp
    src/NpcMakerCommon.cpp
    src/NpcMakerEx.cpp
    src/NpcMakerDB.cpp
    src/NpcSpawnDefine.cpp
    src/NPCServer.cpp
    src/RespawnTimer.cpp
    src/SSQNPCServer.cpp
    src/ServerSocket.cpp
    src/SuperPoint.cpp
    src/Portable/DBNPCConnection.cpp
    src/Portable/NPCServerConnection.cpp
    src/Portable/NPCServerConfiguration.cpp
    src/Portable/NPCServerRuntime.cpp
    src/Portable/NPCServerThread.cpp
    src/Portable/ProcessStartupGate.cpp
    src/DesirePQ.cpp
    src/DesireFactory.cpp
    src/BaseAttributeDiagnostics.cpp
    src/Portable/ProcessRuntime.cpp
    src/Creature.cpp
    src/FormationManager.cpp
    src/Freeway.cpp
    src/NPC.cpp
    src/Object.cpp
    src/Portable/CreatureSharedMemory.cpp
    src/Portable/SharedObjectSharedMemory.cpp
    src/Portable/ItemSharedMemory.cpp
    src/Portable/StaticObjectSharedMemory.cpp
    src/HateInfo.cpp
    src/NpcSpawnDefineEx.cpp
    src/ScriptEngine.cpp
    src/User.cpp
    src/GlobalObject.cpp
    src/Desire.cpp
    ${L2NPC_GRAMMAR_SOURCES})

target_include_directories(l2npc_core PUBLIC ${L2NPC_INCLUDE_DIRS})
target_compile_definitions(
    l2npc_core
    PRIVATE L2NPC_RECOVERED_IOC_ASSEMBLE=1)
target_link_libraries(l2npc_core PUBLIC Shared::L2NPC Threads::Threads)
# Shared::L2NPC also uses functions and globals that l2npc_core defines, so
# each static library needs the other. GNU ld (Linux and MinGW) reads each
# library once, in order. With this cycle in the graph, CMake repeats both
# libraries on the link line. Apple ld searches all libraries and warns
# about repeated ones, so it does not get the cycle.
if(NOT APPLE)
    target_link_libraries(SharedL2NPC INTERFACE "$<LINK_ONLY:l2npc_core>")
    # Two copies are not enough for the GeoData test. Its chain goes through
    # SharedFactory back to the shared memory openers in l2npc_core.
    set_target_properties(l2npc_core PROPERTIES LINK_INTERFACE_MULTIPLICITY 3)
endif()

if(WIN32)
    target_link_libraries(l2npc_core PUBLIC ws2_32 dbghelp pdh)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(l2npc_core PUBLIC rt)
endif()

l2_target_warnings(l2npc_core)

# The recovered WinMain remains in L2NPC.cpp as source-level target evidence,
# but the runnable cross-platform product uses one composition root on every
# host.  That root installs the native TCP, completion, lifecycle and shutdown
# bindings required by the recovered NPCServer/CDBNPC owners.  Building the
# old GUI entry point alone on Windows left all of those bindings empty.
add_executable(L2NPC
    src/L2NPC.cpp
    src/Portable/Console.cpp)
target_link_libraries(L2NPC PRIVATE l2npc_core)
l2_configure_product_runtime(L2NPC L2NPC)
l2_stage_runtime_file(L2NPC etc/l2npc.ini l2npc.ini)
l2_stage_runtime_file(L2NPC etc/perfmon.ini perfmon.ini)
l2_stage_runtime_file(L2NPC etc/secure.ini secure.ini)

install(TARGETS L2NPC RUNTIME DESTINATION L2NPC)
install(FILES etc/l2npc.ini etc/perfmon.ini etc/secure.ini DESTINATION L2NPC)

