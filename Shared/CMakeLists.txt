# Compile this one Shared source tree with each product's macros.
# Shared provides headers and dependencies. SharedL2NPC, SharedCacheD and
# SharedL2Server each compile their own product objects.

# ---- libpq (PostgreSQL client) ---------------------------------------------
if(NOT DEFINED PG_PREFIX)
  if(DEFINED ENV{PG_PREFIX})
    set(PG_PREFIX $ENV{PG_PREFIX})
  elseif(APPLE AND NOT CMAKE_CROSSCOMPILING)
    set(PG_PREFIX "/Library/PostgreSQL/18")
  endif()
endif()

find_path(
  PQ_INCLUDE_DIR
  libpq-fe.h
  HINTS
    "${PG_PREFIX}/include"
    /usr/include
    /usr/local/include
    /opt/homebrew/include
  PATH_SUFFIXES postgresql)
find_library(PQ_LIB pq HINTS "${PG_PREFIX}/lib" /usr/lib /usr/local/lib /opt/homebrew/lib)

if((L2_BUILD_L2NPC OR L2_BUILD_CACHED OR L2_BUILD_L2SERVER) AND
   (NOT PQ_INCLUDE_DIR OR NOT PQ_LIB))
  message(FATAL_ERROR
    "L2NPC/CacheD/L2Server require libpq for the target platform (${CMAKE_SYSTEM_NAME}); set PG_PREFIX to that PostgreSQL installation")
endif()

# One dependency owner for portable PostgreSQL transports. Product database
# classes stay in their own source trees, but the products must not each find
# or spell libpq differently.
# Keep PostgreSQL headers off the general Shared include path: some vendor
# distributions colocate an incompatible iconv.h beside libpq-fe.h.
if(PQ_INCLUDE_DIR AND PQ_LIB)
  add_library(SharedPostgreSQL INTERFACE)
  add_library(Shared::PostgreSQL ALIAS SharedPostgreSQL)
  target_include_directories(SharedPostgreSQL SYSTEM INTERFACE
    "${PQ_INCLUDE_DIR}")
  target_link_libraries(SharedPostgreSQL INTERFACE "${PQ_LIB}")
  add_library(SharedPostgreSQLMigrations STATIC EXCLUDE_FROM_ALL
    src/Runtime/PostgreSQLMigrations.cpp
  )
  add_library(Shared::PostgreSQLMigrations ALIAS SharedPostgreSQLMigrations)
  target_include_directories(SharedPostgreSQLMigrations PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
  target_link_libraries(SharedPostgreSQLMigrations PRIVATE Shared::PostgreSQL)
endif()

find_package(Threads REQUIRED)
if(NOT WIN32)
  find_package(Iconv REQUIRED)
endif()

foreach(_l2core_target
    L2CoreL2NPC L2CoreCacheD L2CoreL2Server)
  if(NOT TARGET ${_l2core_target})
    message(FATAL_ERROR
      "L2Core did not publish required target ${_l2core_target}")
  endif()
endforeach()

# Macro-neutral portable mechanisms shared by every sibling product, including
# standalone products whose original source did not come from Server/Main/Shared.
# Product classes and source ownership do not live here.  Macro-neutral
# transport and host-encoding replacements keep one physical implementation
# in this main target instead of being recompiled privately per product.
add_library(Runtime STATIC
  src/Runtime/AcceptedSocketTransport.cpp
  src/Runtime/ConsoleInput.cpp
  src/Runtime/ConsoleSignals.cpp
  src/Runtime/GeoDataMapping.cpp
  src/Runtime/FileTimeTimer.cpp
  src/Runtime/Guid.cpp
  src/Runtime/LegacyCrtRandom.cpp
  src/Runtime/NativeListenerTransport.cpp
  src/Runtime/NativeSocketError.cpp
  src/Runtime/NativeTcpSocket.cpp
  src/Runtime/OwnedThread.cpp
  src/Runtime/ProcessServices.cpp
  src/Runtime/SerialKey.cpp
  src/Runtime/SocketTransport.cpp
  src/Runtime/SystemMailTransport.cpp
  src/Runtime/TimerQueue.cpp
  src/Runtime/Utf16Format.cpp
  src/Runtime/WindowsAnsi.cpp
  src/TextRuntime.cpp
)
target_include_directories(Runtime PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(Runtime PUBLIC Threads::Threads)
if(WIN32)
  # dbghelp serves crash reports and pdh the performance counters.
  target_link_libraries(Runtime PUBLIC iphlpapi ws2_32 dbghelp pdh)
endif()
if(NOT WIN32)
  target_link_libraries(Runtime PUBLIC Iconv::Iconv)
endif()

add_library(Shared::Runtime ALIAS Runtime)
target_compile_features(Runtime PUBLIC cxx_std_20)

# Provide headers and dependencies only. Compile source files with a product
# macro so IOC.h and the product-specific methods select the right version.
add_library(Shared INTERFACE)
target_include_directories(Shared INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/net"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/proto"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/include"
)
target_link_libraries(
  Shared INTERFACE
  Shared::Runtime L2Core::Headers Protocol::Headers Threads::Threads)
if(WIN32)
  target_link_libraries(Shared INTERFACE advapi32 ws2_32)
else()
  target_link_libraries(Shared INTERFACE Iconv::Iconv)
endif()
if(PQ_LIB)
  target_link_libraries(Shared INTERFACE "${PQ_LIB}")
endif()

# Bison and re2c generate the shared script parsers and scanners once. Each
# product library compiles the generated sources with its own macros. AuthD
# and AuthGateD use no grammar, so an auth-only build needs neither tool.
set(SHARED_GRAMMAR_SOURCES)
set(SHARED_L2SERVER_GRAMMAR_SOURCES)
if(L2_BUILD_L2NPC OR L2_BUILD_CACHED OR L2_BUILD_L2SERVER)
  include(L2Grammar)
  l2_grammar_sources(
    SHARED_GRAMMAR_SOURCES
    src/ParserCategory.y
    src/LexerCategory.re)
endif()
if(L2_BUILD_L2SERVER)
  l2_grammar_sources(
    SHARED_L2SERVER_GRAMMAR_SOURCES
    src/ParserSpawn.y
    src/LexerSpawn.re)
endif()
# Generate them before any product library compiles, so parallel product
# builds do not run the same generator rule at once.
add_custom_target(
  SharedGrammarSources
  DEPENDS ${SHARED_GRAMMAR_SOURCES} ${SHARED_L2SERVER_GRAMMAR_SOURCES})

# These sources were already shared by the original products.  The
# list is reused as CMake data only: every entry is compiled once per final
# product, with that product's macro and header layout.
set(SHARED_PRODUCT_BASE_SOURCES
  ${SHARED_GRAMMAR_SOURCES}
  src/Runtime/GrammarRuntime.cpp
  src/platform/Runtime.cpp
  src/Runtime/FixedSharedMemoryTransport.cpp
  src/Runtime/AcceptedServerSocketTransport.cpp
  src/Runtime/IOThreadRuntime.cpp
  src/Runtime/WaitThreadRuntime.cpp
  src/Runtime/L2ServerClient.cpp
  src/Runtime/PerformanceCounter.cpp
  src/Runtime/SharedMemoryRegion.cpp
  src/Runtime/ThreadQuiescence.cpp
  src/net/Config.cpp
  src/net/Serialize.cpp
  src/net/Protocol.cpp
  src/BaseAttribute.cpp
  src/BaseStructure.cpp
  src/CategoryDataContent.cpp
  src/CategoryDataDB.cpp
  src/CriticalSectionStorage.cpp
  src/DateTime.cpp
  src/Obj.cpp
  src/RWLock.cpp
  src/DBConn.cpp
  src/ExecutionSerializerDiagnostics.cpp
  src/ExecutionSerializerFast.cpp
  src/GIP.cpp
  src/GIPSocket.cpp
  src/IOCSocketCore.cpp
  src/IOCPMessage.cpp
  src/LogFormatter.cpp
  src/MemorySub.cpp
  src/MwsrQueue.cpp
  src/ScriptAccessible.cpp
  src/StringTokenizer.cpp
  src/TemplateHelper.cpp
  src/TemplateHelperSingleton.cpp
  src/ThreadSafeNullPointer.cpp
  src/Utils.cpp
  src/UtilsRandom.cpp
  src/UtilsString.cpp
  src/Vitality.cpp
  src/FHtml.cpp
  src/IOCPacket.cpp
  src/IOCs.cpp
  src/ManorData.cpp
  src/PledgeAnnounce.cpp
  src/PledgeAnnounceRegistry.cpp
  src/Protocol/VariableBitSet.cpp
  src/PledgeCrest.cpp
  src/PledgeType.cpp
  src/PrecompiledHeaderDB.cpp
  src/ResidenceData.cpp
  src/ShareDefine.cpp
  src/SharedRWLock.cpp
  src/Systems.cpp
  src/YieldLock.cpp
)

set(SHARED_L2NPC_SOURCES
  src/Runtime/SharedFactoryMapping.cpp
  src/AtomicJob.cpp
  src/EtcMani.cpp
  src/Exception.cpp
  src/GeoData.cpp
  src/InventoryCommon.cpp
  src/IOC.cpp
  src/ItemMani.cpp
  src/LocalizationSystem.cpp
  src/Log.cpp
  src/LogCli.cpp
  src/LogFormatterNpc.cpp
  src/LogSocket.cpp
  src/PerfMon.cpp
  src/Runtime/GeoDataSource.cpp
  src/Runtime/IOThreadRuntimeWorkers.cpp
  src/SharedFactory.cpp
  src/ShareInfo.cpp
  src/Socket.cpp
  src/StatusWnd.cpp
  src/ThreadSafeLibrary.cpp
  src/ThreadManager.cpp
  src/TimeAnalysis.cpp
  src/SystemMail.cpp
  src/Territory.cpp
  src/TerritoryCore.cpp
  src/TerritoryInfo.cpp
  src/WorldCommon.cpp
)

set(SHARED_CACHED_SOURCES
  src/AtomicJob.cpp
  src/DXPacket.cpp
  src/EtcMani.cpp
  src/Exception.cpp
  src/IOC.cpp
  src/Log.cpp
  src/LogCli.cpp
  src/LogFormatterCacheD.cpp
  src/LogSocket.cpp
  src/MailListParameter.cpp
  src/PerfMon.cpp
  src/Runtime/IOThreadRuntimeWorkers.cpp
  src/PledgeRanking.cpp
  src/QueryPacket.cpp
  src/QsCheck.cpp
  src/SystemMail.cpp
  src/StatusWnd.cpp
  src/ThreadManager.cpp
  src/WatchList.cpp
)

set(SHARED_L2SERVER_SOURCES
  ${SHARED_L2SERVER_GRAMMAR_SOURCES}
  src/Runtime/SharedFactoryMapping.cpp
  src/AtomicJob.cpp
  src/BaseStructureL2Server.cpp
  src/DXPacket.cpp
  src/EtcManiL2Server.cpp
  src/Exception.cpp
  src/GeoData.cpp
  src/SharedFactory.cpp
  src/ItemMani.cpp
  src/IOCL2Server.cpp
  src/Log.cpp
  src/LogCli.cpp
  src/LogSocket.cpp
  src/PerfMon.cpp
  src/Runtime/IOThreadRuntimeWorkers.cpp
  src/LocalizationSystem.cpp
  src/MailConstant.cpp
  src/MailListParameter.cpp
  src/MD5.cpp
  src/Runtime/GeoDataSource.cpp
  src/PledgeRanking.cpp
  src/QueryPacket.cpp
  src/RelaySocket.cpp
  src/ShareInfoL2Server.cpp
  src/Socket.cpp
  src/SystemMail.cpp
  src/Territory.cpp
  src/TerritoryCore.cpp
  src/TerritoryInfo.cpp
  src/WorldCommon.cpp
  src/StatusWnd.cpp
  src/ThreadSafeLibrary.cpp
  src/ThreadManager.cpp
  src/TimeAnalysis.cpp
  src/WatchList.cpp
)

# The current L2Server project and _cinit table place the three
# PledgeRanking.cpp statics immediately before the PledgeType.cpp global.
# Inject the product-only owner at that exact point without compiling it into
# L2NPC, where the source is absent.
function(shared_sources_with_pledge_ranking output_name)
  set(_sources)
  foreach(_source IN LISTS SHARED_PRODUCT_BASE_SOURCES)
    if(_source STREQUAL "src/PledgeType.cpp")
      list(APPEND _sources src/PledgeRanking.cpp)
    endif()
    list(APPEND _sources "${_source}")
  endforeach()
  set(${output_name} ${_sources} PARENT_SCOPE)
endfunction()

shared_sources_with_pledge_ranking(SHARED_CACHED_BASE_SOURCES)
shared_sources_with_pledge_ranking(SHARED_L2SERVER_BASE_SOURCES)

# libpq is an implementation detail of DBConn.cpp.  PostgreSQL's macOS
# distribution also ships a GNU iconv.h in this directory, so exposing it to
# unrelated units changes which iconv ABI they compile against.
if(PQ_INCLUDE_DIR)
  set_property(
    SOURCE src/DBConn.cpp
    APPEND PROPERTY INCLUDE_DIRECTORIES "${PQ_INCLUDE_DIR}")
endif()

function(configure_shared_product target_name product_macro)
  target_compile_definitions(${target_name}
    PUBLIC ${product_macro}=1
    PRIVATE L2NPC_RECOVERED_IOC_ASSEMBLE=1)
  target_link_libraries(${target_name} PUBLIC Shared)
  add_dependencies(${target_name} SharedGrammarSources)
  if(TARGET shared_ownership_audit)
    add_dependencies(${target_name} shared_ownership_audit)
  endif()
endfunction()

add_library(SharedL2NPC STATIC EXCLUDE_FROM_ALL
  $<TARGET_OBJECTS:L2CoreL2NPC>
  ${SHARED_PRODUCT_BASE_SOURCES}
  ${SHARED_L2NPC_SOURCES})
add_library(Shared::L2NPC ALIAS SharedL2NPC)
configure_shared_product(SharedL2NPC L2_BUILD_L2NPC)
target_include_directories(SharedL2NPC PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../L2NPC"
  "${CMAKE_CURRENT_SOURCE_DIR}/../L2NPC/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/../L2NPC/src/Portable")
if(WIN32)
  target_link_libraries(SharedL2NPC PUBLIC pdh psapi)
endif()
set_target_properties(SharedL2NPC PROPERTIES OUTPUT_NAME SharedL2NPC)

add_library(SharedCacheD STATIC EXCLUDE_FROM_ALL
  $<TARGET_OBJECTS:L2CoreCacheD>
  ${SHARED_CACHED_BASE_SOURCES}
  ${SHARED_CACHED_SOURCES})
add_library(Shared::CacheD ALIAS SharedCacheD)
configure_shared_product(SharedCacheD L2_BUILD_CACHED)
if(TARGET SharedPostgreSQLMigrations)
  target_link_libraries(SharedCacheD PUBLIC Shared::PostgreSQLMigrations)
endif()
target_include_directories(SharedCacheD PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../CacheD/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/../CacheD/src/Portable"
)

if(WIN32)
  target_link_libraries(SharedCacheD PUBLIC dbghelp pdh psapi)
endif()
set_target_properties(SharedCacheD PROPERTIES OUTPUT_NAME SharedCacheD)

add_library(SharedL2Server STATIC EXCLUDE_FROM_ALL
  $<TARGET_OBJECTS:L2CoreL2Server>
  ${SHARED_L2SERVER_BASE_SOURCES}
  ${SHARED_L2SERVER_SOURCES})
add_library(Shared::L2Server ALIAS SharedL2Server)
configure_shared_product(SharedL2Server L2_BUILD_L2SERVER)
if(L2_ENABLE_STARTUP_DIAGNOSTICS)
  # PUBLIC, because L2Server.cpp prints the counters that GeoData.cpp collects.
  target_compile_definitions(SharedL2Server PUBLIC
    L2_STARTUP_DIAGNOSTICS=1)
endif()
target_include_directories(SharedL2Server PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../L2Server/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/../L2Server/src/Portable"
)

set_target_properties(SharedL2Server PROPERTIES OUTPUT_NAME SharedL2Server)
