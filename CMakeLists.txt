cmake_minimum_required(VERSION 3.16)
project(seum CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(SEUM_SOURCES
    src/utf8.cc
    src/token.cc
    src/error_reporter.cc
    src/lexer.cc
    src/ast.cc
    src/parser.cc
    src/value.cc
    src/environment.cc
    src/interpreter.cc
    src/ir.cc
    src/bytecode.cc
    src/vm.cc
    src/dameum.cc
    src/jamo_huffman.cc
    src/modules/builtin.cc
    src/modules/time.cc
    src/modules/native_bridge.cc
)

add_library(seum_core STATIC ${SEUM_SOURCES})
target_include_directories(seum_core PUBLIC src)

add_executable(seum src/main.cc)
target_link_libraries(seum PRIVATE seum_core)

option(SEUM_BUILD_TESTS "Build Seum unit tests" ON)
if(SEUM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
