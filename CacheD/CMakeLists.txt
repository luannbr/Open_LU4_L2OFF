if(WIN32)
    enable_language(RC)
endif()

set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Shared")
set(CACHED_RECOVERED_SOURCES
    src/AddData.cpp
    src/AdminUtil.cpp
    src/AdminServer.cpp
    src/AdminSocket.cpp
    src/Alliance.cpp
    src/AllianceDb.cpp
    src/CAccount.cpp
    src/CAnnounce.cpp
    src/BattleCampDb.cpp
    src/BossRecordDB.cpp
    src/Castle.cpp
    src/CastleDb.cpp
    src/CacheSocket.cpp
    src/CursedWeaponDB.cpp
    src/CDBJobDeleteQuest.cpp
    src/EventDataHelper.cpp
    src/FileServerManager.cpp
    src/FishingEventDB.cpp
    src/Friends.cpp
    src/FriendsDb.cpp
    src/HtmlCommandHandler.cpp
    src/IOCPMessageCached.cpp
    src/Item.cpp
    src/ItemAssembler.cpp
    src/ManorDb.cpp
    src/MiniGameAgitDB.cpp
    src/NewCharDB.cpp
    src/NewbieDb.cpp
    src/NpcDbSocket.cpp
    src/NpcServer.cpp
    src/Pet.cpp
    src/Pledge.cpp
    src/PledgeAnnounceDBHandlerImpl.cpp
    src/PledgeBBS.cpp
    src/PledgeCrestDb.cpp
    src/PledgeContributionInCache.cpp
    src/PledgeItemInCache.cpp
    src/PledgeMasteryInCache.cpp
    src/PledgeDb.cpp
    src/ReservedAnnounce.cpp
    src/SevenSignQuestDB.cpp
    src/ServerPacketEx.cpp
    src/Server.cpp
    src/TimeAttackDB.cpp
    src/User.cpp
    src/UserDb.cpp
    src/UserMacro.cpp
    src/WareHouse.cpp
    src/BRWareHouse.cpp
    src/CReadItemData.cpp
    src/CReadFile.cpp
    src/Cached.cpp
    src/ConsoleCommands.cpp
    src/CachedStatusWnd.cpp
    src/Default.cpp
    src/DatabaseStartup.cpp
    src/BaseAttributeDiagnostics.cpp
    src/Portable/ProcessRuntime.cpp
    src/Platform/PostgreSQLDeployment.cpp
    src/Platform/CacheDProcessLifecycle.cpp
    src/Platform/ProcessInstanceLock.cpp)

# Use the same CacheD objects in the application and runtime checks.
add_library(CacheDRecoveredObjects OBJECT
    ${CACHED_RECOVERED_SOURCES})
target_include_directories(CacheDRecoveredObjects PRIVATE
    src
    src/Portable
    src/Platform)
target_link_libraries(CacheDRecoveredObjects PRIVATE Shared::CacheD)

add_executable(CacheD
    src/Platform/Main.cpp
    $<TARGET_OBJECTS:CacheDRecoveredObjects>)
l2_configure_product_runtime(CacheD CacheD)
l2_stage_runtime_file(CacheD etc/l2cache.ini l2cache.ini)
l2_stage_runtime_file(CacheD etc/perfmon.ini perfmon.ini)
l2_stage_runtime_file(CacheD etc/exception.ini exception.ini)
l2_stage_runtime_file(CacheD etc/secure.ini secure.ini)
l2_stage_runtime_file(
    CacheD etc/cached-postgresql.ini.example etc/cached-postgresql.ini.example)

if(WIN32)
    if(MSVC)
        # The script is in windres syntax. Microsoft rc.exe rejects its raw
        # icon (3), icon group (14) and manifest (24) entries, but CacheD loads
        # the menu, dialogs, strings and accelerators at startup. Drop only the
        # three unsupported kinds, so the executable has no custom icon.
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/CachedResources.rc" CACHED_RC_TEXT)
        string(REGEX REPLACE
            "\n[0-9]+ (3|14) /\\*[^\n]*\\*/\nBEGIN\n([ \t][^\n]*\n)*END\n" "\n"
            CACHED_RC_TEXT "${CACHED_RC_TEXT}")
        string(REGEX REPLACE "\n1 24 /\\*.*$" "\n" CACHED_RC_TEXT "${CACHED_RC_TEXT}")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CachedResources.msvc.rc" "${CACHED_RC_TEXT}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/CachedResources.rc")
        target_sources(CacheD PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/CachedResources.msvc.rc")
    else()
        target_sources(CacheD PRIVATE
            src/CachedResources.rc)
    endif()
endif()

target_include_directories(CacheD PRIVATE
    src
    src/Portable
    src/Platform)
target_link_libraries(CacheD PRIVATE Shared::CacheD)

if(WIN32)
    target_link_libraries(CacheD PRIVATE iphlpapi user32 ws2_32)
endif()

l2_target_warnings(CacheDRecoveredObjects CacheD)

# Same layout as the staged runtime directory. The default
# MigrationsDirectory "../sql" is relative to etc/cached-postgresql.ini.
install(TARGETS CacheD RUNTIME DESTINATION CacheD)
install(
    FILES
        etc/l2cache.ini
        etc/perfmon.ini
        etc/exception.ini
        etc/secure.ini
    DESTINATION CacheD)
install(FILES etc/cached-postgresql.ini.example DESTINATION CacheD/etc)
install(
    DIRECTORY sql/
    DESTINATION CacheD/sql
    FILES_MATCHING
        PATTERN "*.sql"
        PATTERN "*.md"
        PATTERN "migrations.order")
