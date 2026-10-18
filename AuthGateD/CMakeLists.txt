find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# Only recovered production owners are listed here. Additional original
# AuthGateD units enter this target as their EXE/PDB bodies are closed.
add_library(AuthGateDRecovered STATIC
    src/AuthPacket.cpp
    src/AuthPacketPool.cpp
    src/AuthSocket.cpp
    src/AuthGateConfig.cpp
    src/AuthGateSvcMgr.cpp
    src/BadUser.cpp
    src/Blowfish.cpp
    src/BlowfishWrapper.cpp
    src/ClientPacket.cpp
    src/ClientPacketPool.cpp
    src/ClientSocket.cpp
    src/ClientSocketContainer.cpp
    src/Config.cpp
    src/ExceptionReport.cpp
    src/FileLog.cpp
    src/GameGuard.cpp
    src/GlobalFunc.cpp
    src/IPList.cpp
    src/IOBuffer.cpp
    src/IOBufferPool.cpp
    src/IOObject.cpp
    src/IOServer.cpp
    src/IOSocket.cpp
    src/Lock.cpp
    src/LogSocket.cpp
    src/Lu4Login.cpp
    src/MailReport.cpp
    src/MonWnd.cpp
    src/OutputWnd.cpp
    src/RSAKeyPool.cpp
    src/ServiceMgr.cpp
    src/Platform/IOCompletionDispatch.cpp
    src/Platform/GameGuardProvider.cpp
    src/Platform/IOServerTransport.cpp
    src/Platform/IOSocketTransport.cpp
    src/Platform/MailReportSocket.cpp
    src/Platform/ProcessRuntime.cpp
    src/Platform/ServiceThreadRuntime.cpp
    src/Platform/ThreadPoolRuntime.cpp
    src/Platform/WindowRuntime.cpp
    src/Processor.cpp
    src/StateMonMgr.cpp
    src/Thread.cpp
    src/ThreadPool.cpp
    src/TryList.cpp
    src/StatusBarWnd.cpp
    src/WindowMgr.cpp)
target_sources(AuthGateDRecovered PRIVATE src/Base64.cpp)
add_library(AuthGateD::Recovered ALIAS AuthGateDRecovered)

target_include_directories(AuthGateDRecovered PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(AuthGateDRecovered
    PUBLIC Shared::Runtime Threads::Threads
    PRIVATE OpenSSL::Crypto)

# The original GameGuard provider is a 32-bit PE DLL.  On Windows it is
# loaded by the native backend.  Other hosts can execute the same exported
# machine code through Unicorn, preserving the shipped provider meaning.
set(AUTHGATED_UNICORN_ROOT "" CACHE PATH
    "Optional Unicorn installation root for the GameGuard PE backend")
set(_authgated_unicorn_hints)
if(AUTHGATED_UNICORN_ROOT)
    list(APPEND _authgated_unicorn_hints
        "${AUTHGATED_UNICORN_ROOT}")
endif()
find_path(AUTHGATED_UNICORN_INCLUDE_DIR
    NAMES unicorn/unicorn.h
    HINTS ${_authgated_unicorn_hints}
    PATH_SUFFIXES include)
find_library(AUTHGATED_UNICORN_LIBRARY
    NAMES unicorn unicorn.2
    HINTS ${_authgated_unicorn_hints}
    PATH_SUFFIXES lib)
if(AUTHGATED_UNICORN_INCLUDE_DIR AND AUTHGATED_UNICORN_LIBRARY)
    target_compile_definitions(AuthGateDRecovered PRIVATE
        AUTHGATED_HAS_UNICORN=1)
    target_include_directories(AuthGateDRecovered PRIVATE
        "${AUTHGATED_UNICORN_INCLUDE_DIR}")
    target_link_libraries(AuthGateDRecovered PRIVATE
        "${AUTHGATED_UNICORN_LIBRARY}")
endif()
unset(_authgated_unicorn_hints)

if(WIN32)
    target_link_libraries(AuthGateDRecovered PRIVATE ws2_32 dbghelp)
endif()

l2_target_warnings(AuthGateDRecovered)

set_target_properties(AuthGateDRecovered PROPERTIES OUTPUT_NAME AuthGateDRecovered)

add_executable(AuthGateD src/AuthGateD.cpp)
target_link_libraries(AuthGateD PRIVATE AuthGateD::Recovered)
if(WIN32)
    target_link_libraries(AuthGateD PRIVATE shell32)
endif()

l2_target_warnings(AuthGateD)
l2_configure_product_runtime(AuthGateD AuthGateD)
l2_stage_runtime_file(AuthGateD etc/config.txt etc/config.txt)
l2_stage_runtime_file(AuthGateD csauth2.cfg csauth2.cfg)

install(TARGETS AuthGateD RUNTIME DESTINATION AuthGateD)
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/etc/config.txt"
    DESTINATION AuthGateD/etc)
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/csauth2.cfg"
    DESTINATION AuthGateD)
