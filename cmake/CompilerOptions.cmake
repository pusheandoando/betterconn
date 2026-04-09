# cmake/CompilerOptions.cmake
add_library(compiler_options INTERFACE)
target_compile_options(compiler_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Release>:-O2>
)