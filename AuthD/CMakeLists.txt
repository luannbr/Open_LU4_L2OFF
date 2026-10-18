find_package(Threads REQUIRED)

option(
    AUTHD_USE_POSTGRESQL
    "Use the portable PostgreSQL transport instead of the legacy ODBC baseline"
    ON)

if(AUTHD_USE_POSTGRESQL)
    if(NOT TARGET Shared::PostgreSQL)
        message(FATAL_ERROR
            "AUTHD_USE_POSTGRESQL requires libpq headers and library; set PG_PREFIX")
    endif()
    set(AUTHD_DATABASE_TRANSPORT_SOURCE src/Platform/PostgreSqlOdbc.cpp)
else()
    set(AUTHD_DATABASE_TRANSPORT_SOURCE src/Platform/Odbc.cpp)
endif()

# This target contains the original game-platform-auth_l2a1authd compilation
# units whose bodies have been checked against the selected EXE/PDB, plus
# named portable platform/profile boundaries. The real
# Src/L2AuthD.cpp process owner is kept in the executable target below.
add_library(AuthDRecovered STATIC
    src/Platform/CriticalSection.cpp
    src/Platform/CurrentL2ServerProfile.cpp
    src/Platform/Guid.cpp
    src/Platform/IOSocketTransport.cpp
    ${AUTHD_DATABASE_TRANSPORT_SOURCE}
    src/authlib/CDes.cpp
    src/authlib/Generator.cpp
    src/authlib/MD5.cpp
    src/Account.cpp
    src/AccountContainer.cpp
    src/AccountDB.cpp
    src/BuildN.cpp
    src/ClientSession.cpp
    src/Lu4Validator.cpp
    src/Config.cpp
    src/ConsoleCommands.cpp
    src/ConnectedIPList.cpp
    src/CSocketInt.cpp
    src/CSocketMI.cpp
    src/CSocketServer.cpp
    src/DBConn.cpp
    src/DES.cpp
    src/Exception.cpp
    src/GPProxyServerMgr.cpp
    src/GPProxySocket.cpp
    src/GradeServerMgr.cpp
    src/GradeSocket.cpp
    src/IFatigue.cpp
    src/IOBuffer.cpp
    src/IOObject.cpp
    src/IOServer.cpp
    src/IOServerEx.cpp
    src/IOSocket.cpp
    src/IovationServerMgr.cpp
    src/IovationSocket.cpp
    src/IPAccessLimit.cpp
    src/IPSessionDB.cpp
    src/IPListMonitor.cpp
    src/Job.cpp
    src/Lock.cpp
    src/Log.cpp
    src/LogSocket.cpp
    src/MailReport.cpp
    src/Memory.cpp
    src/PacketServerEx.cpp
    src/PAServerMgr.cpp
    src/PASocket.cpp
    src/PBServerMgr.cpp
    src/PBSSocket.cpp
    src/Parser.cpp
    src/Reporter.cpp
    src/SendMail.cpp
    src/ServerList.cpp
    src/ServiceMgr.cpp
    src/SocketServerEx.cpp
    src/Thread.cpp
    src/TokenServerMgr.cpp
    src/TokenSocket.cpp
    src/Types.cpp
    src/Util.cpp
    src/WhiteListIPMgr.cpp
    src/WSMServerMgr.cpp
    src/WSMSocket.cpp
    src/ZoneFatigue.cpp)
add_library(AuthD::Recovered ALIAS AuthDRecovered)

target_include_directories(AuthDRecovered PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(AuthDRecovered PUBLIC Shared::Runtime Threads::Threads)
if(AUTHD_USE_POSTGRESQL)
    target_link_libraries(AuthDRecovered PRIVATE Shared::PostgreSQL Shared::PostgreSQLMigrations)
    target_compile_definitions(AuthDRecovered PRIVATE AUTHD_USE_POSTGRESQL=1)
endif()
if(CMAKE_DL_LIBS)
    target_link_libraries(AuthDRecovered PRIVATE ${CMAKE_DL_LIBS})
endif()

if(WIN32)
    target_link_libraries(AuthDRecovered PRIVATE advapi32 dbghelp user32 gdi32 version ws2_32 mswsock)
    if(NOT AUTHD_USE_POSTGRESQL)
        target_link_libraries(AuthDRecovered PRIVATE odbc32)
    endif()
endif()

l2_target_warnings(AuthDRecovered)

set_target_properties(AuthDRecovered PROPERTIES OUTPUT_NAME AuthDRecovered)

add_executable(AuthD src/L2AuthD.cpp)
add_executable(AuthD::Server ALIAS AuthD)
target_link_libraries(AuthD PRIVATE AuthDRecovered)

l2_target_warnings(AuthD)

l2_configure_product_runtime(AuthD AuthD)
l2_stage_runtime_file(AuthD etc/config.txt etc/config.txt)
l2_stage_runtime_file(AuthD etc/fcmsdk.ini etc/fcmsdk.ini)
l2_stage_runtime_file(AuthD etc/serverlist etc/serverlist)

# InitializeConfig resolves etc/config.txt relative to the executable exactly
# as the selected process does. The portable default keeps every outbound
# platform and SMTP peer disabled until a deployer opts in.
# The default migrationsDirectory "../sql" is relative to etc/config.txt.
install(TARGETS AuthD RUNTIME DESTINATION AuthD)
install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/etc/config.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/etc/fcmsdk.ini"
        "${CMAKE_CURRENT_SOURCE_DIR}/etc/serverlist"
    DESTINATION AuthD/etc)
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/sql/"
    DESTINATION AuthD/sql
    FILES_MATCHING PATTERN "*.sql" PATTERN "*.md")

