# CMakeLists.txt
# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" BETTERCONN_VERSION LIMIT_COUNT 1)

project(betterconn VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CompilerOptions.cmake)

add_subdirectory(core)
add_subdirectory(cli)