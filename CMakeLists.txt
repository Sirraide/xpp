cmake_minimum_required(VERSION 3.21)
project(xpp)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})
set(CMAKE_CXX_COMPILER g++)

file(GLOB SRC src/*.cc src/*.h)

add_executable(xpp ${SRC})
target_link_libraries(xpp PRIVATE utils fmt)
target_compile_options(xpp PRIVATE
        -Wall -Wextra -Wundef -Werror=return-type -Wconversion -Wpedantic
        -Wno-gnu-zero-variadic-macro-arguments -Wno-dollar-in-identifier-extension
        -fcoroutines)
if (${CMAKE_CXX_COMPILER} STREQUAL "clang++")
    target_compile_options(xpp PRIVATE -Xclang -fcolor-diagnostics )
endif ()
if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
    target_compile_options(xpp PRIVATE -O3)
else ()
    target_compile_options(xpp PRIVATE -O0 -ggdb -fsanitize=address)
    target_link_options(xpp PRIVATE -fsanitize=address)
endif ()
