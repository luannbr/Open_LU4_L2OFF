# Compile the same sources with each product's macro and Shared headers.
# Put the objects in Shared::<Product> to avoid a circular library link.
add_library(L2Core INTERFACE)
target_compile_features(L2Core INTERFACE cxx_std_20)
add_library(L2Core::Headers ALIAS L2Core)
target_compile_definitions(
  L2Core
  INTERFACE
    L2_ENABLE_GUARD_PROFILING=$<BOOL:${L2_ENABLE_GUARD_PROFILING}>)
target_include_directories(L2Core INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(L2CORE_RECOVERED_SOURCES
  src/AES.cpp
  src/BaseConverter.cpp
  src/FileUtils.cpp
  src/Guard.cpp
  src/GuardProfile.cpp
  src/MemoryMappedFile.cpp
  src/MiniDump.cpp
  src/utils/dbg/Assert.cpp
)

target_sources(L2Core INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/inc/AES.h>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/inc/BaseConverter.h>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/inc/FileUtils.h>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/inc/Guard.h>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/inc/GuardProfile.h>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/inc/MemoryMappedFile.h>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/inc/MiniDump.h>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/inc/XStd.h>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/utils/dbg/Assert.h>"
)

function(add_l2core_product target_name alias_name product_macro)
  # Materialize only when a selected product consumes these objects.
  add_library(${target_name} OBJECT EXCLUDE_FROM_ALL ${L2CORE_RECOVERED_SOURCES})
  add_library(L2Core::${alias_name} ALIAS ${target_name})
  target_compile_definitions(${target_name} PRIVATE ${product_macro}=1)
  target_include_directories(${target_name} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../Shared/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Shared/src/platform")
  target_link_libraries(${target_name} PUBLIC L2Core::Headers)
endfunction()

add_l2core_product(L2CoreL2NPC L2NPC L2_BUILD_L2NPC)
add_l2core_product(L2CoreCacheD CacheD L2_BUILD_CACHED)
add_l2core_product(L2CoreL2Server L2Server L2_BUILD_L2SERVER)
