# Protocol contains numeric declarations in headers.
# Expose those headers without creating a binary library.
add_library(L2Protocol INTERFACE)
add_library(Protocol::Headers ALIAS L2Protocol)
target_compile_features(L2Protocol INTERFACE cxx_std_20)
target_include_directories(
    L2Protocol
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
