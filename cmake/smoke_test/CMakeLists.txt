# Builds phaser as a parent project does and generates libraries from the test
# protos with each frontend.  CI runs this to keep the CMake build in step
# with Bazel.

cmake_minimum_required(VERSION 3.15)
project(phaser_cmake_smoke_test LANGUAGES CXX)

get_filename_component(PHASER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${PHASER_ROOT}" phaser)

phaser_add_library(test_phaser
    PROTOS phaser/testdata/Test.proto
    IMPORTS phaser/testdata/Foo.proto google/protobuf/any.proto
    IMPORT_DIRS "${PHASER_ROOT}" "${protobuf_SOURCE_DIR}/src"
    ADD_NAMESPACE smoke
)

phaser_add_library(test_phaser_c
    PROTOS phaser/testdata/Test.proto
    IMPORTS phaser/testdata/Foo.proto google/protobuf/any.proto
    IMPORT_DIRS "${PHASER_ROOT}" "${protobuf_SOURCE_DIR}/src"
    ADD_NAMESPACE smoke_c
    RUST
)

add_executable(smoke_test smoke_test.cc)
target_link_libraries(smoke_test PRIVATE test_phaser test_phaser_c)

find_program(RUSTC_EXECUTABLE rustc)
if(RUSTC_EXECUTABLE)
    set(rust_lib "${CMAKE_CURRENT_BINARY_DIR}/rust/lib.rs")
    file(GENERATE OUTPUT "${rust_lib}" CONTENT
        "#![allow(non_camel_case_types, non_snake_case, non_upper_case_globals, dead_code)]\ninclude!(\"$<TARGET_PROPERTY:test_phaser_c,PHASER_RUST_SOURCE>\");\n")
    add_custom_target(smoke_test_rust ALL
        COMMAND "${RUSTC_EXECUTABLE}" --edition 2021 --crate-type rlib
                --crate-name test_phaser_c
                --out-dir "${CMAKE_CURRENT_BINARY_DIR}/rust" "${rust_lib}"
        COMMENT "Compiling the Rust bindings for test_phaser_c"
        VERBATIM
    )
    add_dependencies(smoke_test_rust test_phaser_c)
else()
    message(STATUS "rustc not found: not compiling the Rust bindings")
endif()

enable_testing()
add_test(NAME smoke_test COMMAND smoke_test)
