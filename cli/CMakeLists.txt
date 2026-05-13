# cli/CMakeLists.txt
add_executable(betterconn src/main.cpp)
target_compile_definitions(betterconn PRIVATE BETTERCONN_VERSION="${BETTERCONN_VERSION}")
target_link_libraries(betterconn PRIVATE betterconn_core compiler_options)
install(TARGETS betterconn DESTINATION bin)