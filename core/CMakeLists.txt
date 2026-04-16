# core/CMakeLists.txt
add_library(betterconn_core
    src/storage.cpp
    src/optimizer.cpp
    src/status.cpp
    src/cleaner.cpp
)

target_include_directories(betterconn_core PUBLIC include)
target_link_libraries(betterconn_core PRIVATE compiler_options)