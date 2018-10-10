add_library(nosx STATIC
    nosx.cpp)
target_include_directories(nosx PUBLIC
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
