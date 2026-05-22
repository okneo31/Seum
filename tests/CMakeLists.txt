# Catch2 v3.5.4 amalgamated (vendored). 출처: third_party/catch2/
# FetchContent 폐지 — 인터넷·git 의존 0.
add_library(catch2_amalgamated STATIC
    ${CMAKE_SOURCE_DIR}/third_party/catch2/catch_amalgamated.cpp
)
target_include_directories(catch2_amalgamated PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party
)
target_compile_features(catch2_amalgamated PUBLIC cxx_std_17)

set(SEUM_TESTS
    test_utf8.cc
    test_lexer.cc
    test_parser.cc
    test_interpreter.cc
    test_time.cc
    test_integration.cc
    test_ir.cc
    test_bytecode.cc
    test_vm.cc
    test_dameum.cc
    test_jamo.cc
)

add_executable(seum_tests ${SEUM_TESTS})
target_link_libraries(seum_tests PRIVATE seum_core catch2_amalgamated)
target_include_directories(seum_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

include(CTest)
add_test(NAME seum_all_tests COMMAND seum_tests)
