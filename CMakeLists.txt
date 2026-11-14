# CMakeLists.txt for the phaser project
#
# Builds the phaser runtime library, the protoc-gen-phaser protoc plugin, and
# provides phaser_add_library() to generate a C++ library from .proto files.
#
# A parent project that already provides Abseil (absl::*), cpp_toolbelt
# (toolbelt) and protobuf (protobuf::libprotoc, protobuf::protoc) targets is
# used as is.  Missing dependencies are fetched with FetchContent.
#
# When cross-compiling, the plugin must run on the build host, so it is not
# built.  Build it for the host separately and pass its path in
# PHASER_PLUGIN_EXECUTABLE.

cmake_minimum_required(VERSION 3.15)
project(phaser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CROSSCOMPILING)
    set(PHASER_BUILD_COMPILER_DEFAULT OFF)
else()
    set(PHASER_BUILD_COMPILER_DEFAULT ON)
endif()
option(PHASER_BUILD_COMPILER "Build the protoc-gen-phaser plugin"
       ${PHASER_BUILD_COMPILER_DEFAULT})
set(PHASER_PLUGIN_EXECUTABLE "" CACHE FILEPATH
    "Host protoc-gen-phaser used when the plugin is not built here")
option(PHASER_TRUST_SERIALIZED_SIZE
       "Serialize messages without checking for room, trusting the caller"
       OFF)

include(FetchContent)

if(NOT TARGET absl::base)
    set(ABSL_PROPAGATE_CXX_STD ON)
    # protobuf's install rules export targets that link Abseil.
    set(ABSL_ENABLE_INSTALL ON)
    FetchContent_Declare(
        abseil
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG 20250814.1
    )
    FetchContent_MakeAvailable(abseil)
endif()

if(NOT TARGET toolbelt)
    # Matches the cpp_toolbelt git_override in MODULE.bazel.
    FetchContent_Declare(
        cpp_toolbelt
        GIT_REPOSITORY https://github.com/dallison/cpp_toolbelt.git
        GIT_TAG 86b45cdd3da46a97a4476be2f2a61909677284a2
    )
    FetchContent_MakeAvailable(cpp_toolbelt)
endif()

# phaser_bank_protobuf links libprotobuf, so a cross-compile needs it too.
if(NOT TARGET protobuf::libprotobuf OR
   (PHASER_BUILD_COMPILER AND NOT TARGET protobuf::libprotoc))
    set(protobuf_BUILD_TESTS OFF CACHE INTERNAL "")
    FetchContent_Declare(
        protobuf
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
        GIT_TAG v34.1
    )
    FetchContent_MakeAvailable(protobuf)
endif()

set(_PHASER_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

# Bazel gives some plugin headers shorter paths with strip_include_prefix and
# include_prefix.  These forwarding headers provide the same paths.
set(PHASER_FORWARDING_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/phaser_include")
foreach(header
        "arrow/arrow.h=arrow/runtime/arrow.h"
        "arrow/arrow_abi.h=arrow/runtime/arrow_abi.h"
        "avro/avro.h=avro/runtime/avro.h"
        "parquet/parquet.h=parquet/runtime/parquet.h"
        "ros/ros_codegen_helpers.h=ros/gen/ros_codegen_helpers.h")
    string(REPLACE "=" ";" paths "${header}")
    list(GET paths 0 short_path)
    list(GET paths 1 source_path)
    file(GENERATE
        OUTPUT "${PHASER_FORWARDING_INCLUDE_DIR}/phaser/plugins/${short_path}"
        CONTENT "#pragma once\n#include \"phaser/plugins/${source_path}\"\n")
endforeach()

# --- Runtime libraries ---
add_library(phaser_runtime STATIC
    phaser/runtime/md5.cc
    phaser/runtime/message.cc
    phaser/runtime/message_property.cc
    phaser/runtime/payload_buffer.cc
    phaser/runtime/phaser_bank.cc
)
add_library(phaser::runtime ALIAS phaser_runtime)
target_include_directories(phaser_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PHASER_FORWARDING_INCLUDE_DIR}
)
target_link_libraries(phaser_runtime PUBLIC
    absl::core_headers
    absl::flat_hash_map
    absl::flat_hash_set
    absl::function_ref
    absl::no_destructor
    absl::status
    absl::statusor
    absl::strings
    absl::str_format
    absl::span
    toolbelt
)
if(PHASER_TRUST_SERIALIZED_SIZE)
    target_compile_definitions(phaser_runtime PUBLIC
        PHASER_TRUST_SERIALIZED_SIZE)
endif()

# Generated code registers its embedded descriptors here and calls the Arrow,
# Avro and Parquet conversions, so every generated library links these.
add_library(phaser_bank_protobuf STATIC phaser/runtime/phaser_bank_protobuf.cc)
target_link_libraries(phaser_bank_protobuf PUBLIC
    phaser_runtime
    protobuf::libprotobuf
)

add_library(phaser_arrow STATIC phaser/plugins/arrow/runtime/arrow.cc)
target_link_libraries(phaser_arrow PUBLIC phaser_runtime)

add_library(phaser_avro STATIC phaser/plugins/avro/runtime/avro.cc)
target_link_libraries(phaser_avro PUBLIC phaser_arrow)

add_library(phaser_parquet STATIC phaser/plugins/parquet/runtime/parquet.cc)
target_link_libraries(phaser_parquet PUBLIC phaser_arrow)

# The C API under FRONTEND c libraries and the Rust crates built over them.
add_library(phaser_c STATIC phaser/runtime/phaser_c.cc)
target_link_libraries(phaser_c PUBLIC phaser_runtime)

# --- protoc plugin ---
if(PHASER_BUILD_COMPILER)
    if(DEFINED protobuf_SOURCE_DIR)
        set(PHASER_PROTOBUF_IMPORT_DIR "${protobuf_SOURCE_DIR}/src" CACHE PATH
            "Directory containing google/protobuf/*.proto")
    else()
        set(PHASER_PROTOBUF_IMPORT_DIR "" CACHE PATH
            "Directory containing google/protobuf/*.proto")
    endif()
    if(NOT PHASER_PROTOBUF_IMPORT_DIR)
        message(FATAL_ERROR
            "Set PHASER_PROTOBUF_IMPORT_DIR to the directory containing "
            "google/protobuf/descriptor.proto")
    endif()

    set(PHASER_OPTIONS_SRC "${CMAKE_CURRENT_BINARY_DIR}/phaser/options.pb.cc")
    set(PHASER_OPTIONS_HDR "${CMAKE_CURRENT_BINARY_DIR}/phaser/options.pb.h")
    add_custom_command(
        OUTPUT ${PHASER_OPTIONS_SRC} ${PHASER_OPTIONS_HDR}
        COMMAND $<TARGET_FILE:protobuf::protoc>
        ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
             --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
             --proto_path=${PHASER_PROTOBUF_IMPORT_DIR}
             ${CMAKE_CURRENT_SOURCE_DIR}/phaser/options.proto
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/phaser/options.proto protobuf::protoc
        COMMENT "Generating C++ code from phaser/options.proto"
    )

    # The plugins register their generators from static initializers, so
    # their sources are compiled into the executable rather than linked from
    # a static library.
    add_executable(protoc-gen-phaser
        phaser/compiler/enum_gen.cc
        phaser/compiler/gen.cc
        phaser/compiler/generator_plugin.cc
        phaser/compiler/main.cc
        phaser/compiler/message_gen.cc
        phaser/compiler/message_gen_c.cc
        phaser/compiler/message_gen_python.cc
        phaser/plugins/arrow/gen/arrow_message_codegen.cc
        phaser/plugins/arrow/gen/codegen.cc
        phaser/plugins/avro/gen/codegen.cc
        phaser/plugins/parquet/gen/codegen.cc
        phaser/plugins/protobuf/gen/codegen.cc
        phaser/plugins/protobuf/gen/protobuf_wire_codegen.cc
        phaser/plugins/ros/gen/codegen.cc
        phaser/plugins/ros/gen/ros_codegen_helpers.cc
        phaser/plugins/ros/gen/ros_wire_codegen.cc
        ${PHASER_OPTIONS_SRC}
    )
    target_include_directories(protoc-gen-phaser PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${PHASER_FORWARDING_INCLUDE_DIR}
    )
    target_link_libraries(protoc-gen-phaser PRIVATE
        absl::flat_hash_map
        absl::flat_hash_set
        absl::status
        absl::statusor
        absl::strings
        absl::str_format
        protobuf::libprotoc
        protobuf::libprotobuf
    )
endif()

# phaser_add_library(<name>
#     PROTOS <proto>...
#     IMPORT_DIRS <dir>...
#     [IMPORTS <proto>...]
#     [PACKAGE <package>]
#     [ADD_NAMESPACE <namespace>]
#     [FRONTEND protobuf|ros|c]
#     [RUST]
#     [PROTOC <protoc>]
#     [PLUGIN <protoc-gen-phaser>]
#     [COMPILE_OPTIONS <option>...])
#
# Generates phaser C++ code for PROTOS and builds it into the static library
# <name>.  Proto files are named by their import path and found in
# IMPORT_DIRS.  IMPORTS lists the imported .proto files that need phaser code
# too, such as google/protobuf/any.proto.  descriptor.proto and
# phaser/options.proto need none.
#
# As with the Bazel phaser_library rule, the generated files live in
# <package>/<name>/<proto path>.phaser.{h,cc} and each file in PROTOS also gets
# a short header, so "proto/foo.proto" is included as "proto/foo.phaser.h".
# PACKAGE defaults to the directory of the first file in PROTOS.
#
# FRONTEND c also generates the C API in <proto path>.phaser.c.h, with a short
# header as for the C++ one.  RUST generates the Rust bindings over the C API
# and implies FRONTEND c.  The bindings for all the protos are assembled into
# the file named by the library's PHASER_RUST_SOURCE property.  Unlike the
# crate the Bazel rule builds, the file has no crate attributes, so a crate
# root includes it with include!() and allows the lints listed in
# phaser/runtime/rust_crate_attributes.rs.  The Rust crate links <name>.
#
# PROTOC defaults to the protobuf::protoc target.  PLUGIN defaults to the
# protoc-gen-phaser target, or PHASER_PLUGIN_EXECUTABLE when that is set.
function(phaser_add_library name)
    cmake_parse_arguments(ARG "RUST"
        "PACKAGE;ADD_NAMESPACE;FRONTEND;PROTOC;PLUGIN"
        "PROTOS;IMPORTS;IMPORT_DIRS;COMPILE_OPTIONS"
        ${ARGN})
    if(NOT ARG_PROTOS)
        message(FATAL_ERROR "phaser_add_library(${name}) needs PROTOS")
    endif()

    if(NOT DEFINED ARG_PACKAGE)
        list(GET ARG_PROTOS 0 first_proto)
        get_filename_component(ARG_PACKAGE "${first_proto}" DIRECTORY)
    endif()
    if(NOT ARG_FRONTEND)
        if(ARG_RUST)
            set(ARG_FRONTEND c)
        else()
            set(ARG_FRONTEND protobuf)
        endif()
    endif()
    if(NOT ARG_FRONTEND MATCHES "^(protobuf|ros|c)$")
        message(FATAL_ERROR
            "phaser_add_library(${name}): FRONTEND must be protobuf, ros or c")
    endif()
    if(ARG_RUST AND NOT ARG_FRONTEND STREQUAL "c")
        message(FATAL_ERROR "phaser_add_library(${name}): RUST needs FRONTEND c")
    endif()

    set(tool_depends)
    if(NOT ARG_PROTOC)
        if(NOT TARGET protobuf::protoc)
            message(FATAL_ERROR "phaser_add_library(${name}) needs PROTOC")
        endif()
        set(ARG_PROTOC $<TARGET_FILE:protobuf::protoc>)
        list(APPEND tool_depends protobuf::protoc)
    endif()
    if(NOT ARG_PLUGIN)
        if(PHASER_PLUGIN_EXECUTABLE)
            set(ARG_PLUGIN "${PHASER_PLUGIN_EXECUTABLE}")
        elseif(TARGET protoc-gen-phaser)
            set(ARG_PLUGIN $<TARGET_FILE:protoc-gen-phaser>)
            list(APPEND tool_depends protoc-gen-phaser)
        else()
            message(FATAL_ERROR
                "phaser_add_library(${name}) needs a host protoc-gen-phaser: "
                "pass PLUGIN or set PHASER_PLUGIN_EXECUTABLE")
        endif()
    endif()

    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/${name}_phaser")
    file(MAKE_DIRECTORY "${out_dir}")
    if(ARG_PACKAGE)
        set(gen_dir "${out_dir}/${ARG_PACKAGE}/${name}")
        set(gen_include "${ARG_PACKAGE}/${name}")
    else()
        set(gen_dir "${out_dir}/${name}")
        set(gen_include "${name}")
    endif()

    set(proto_paths)
    foreach(dir ${ARG_IMPORT_DIRS})
        list(APPEND proto_paths "--proto_path=${dir}")
    endforeach()

    set(options "package_name=${ARG_PACKAGE},target_name=${name},frontend=${ARG_FRONTEND}")
    if(ARG_ADD_NAMESPACE)
        set(options "add_namespace=${ARG_ADD_NAMESPACE},${options}")
    endif()
    if(ARG_RUST)
        set(options "${options},rust=true")
    endif()

    set(header_suffixes .phaser.h)
    if(ARG_FRONTEND STREQUAL "c")
        list(APPEND header_suffixes .phaser.c.h)
    endif()

    set(inputs)
    set(outputs)
    set(short_headers)
    set(rust_sources)
    foreach(proto ${ARG_PROTOS} ${ARG_IMPORTS})
        set(found)
        foreach(dir ${ARG_IMPORT_DIRS})
            if(NOT found AND EXISTS "${dir}/${proto}")
                set(found "${dir}/${proto}")
            endif()
        endforeach()
        if(NOT found)
            message(FATAL_ERROR
                "phaser_add_library(${name}): ${proto} is not in IMPORT_DIRS")
        endif()
        list(APPEND inputs "${found}")

        string(REGEX REPLACE "\\.proto$" "" base "${proto}")
        list(APPEND outputs "${gen_dir}/${base}.phaser.cc")
        foreach(suffix ${header_suffixes})
            list(APPEND outputs "${gen_dir}/${base}${suffix}")
            if(proto IN_LIST ARG_PROTOS)
                set(short_header "${out_dir}/${base}${suffix}")
                file(GENERATE OUTPUT "${short_header}"
                    CONTENT "#pragma once\n#include \"${gen_include}/${base}${suffix}\"\n")
                list(APPEND short_headers "${short_header}")
            endif()
        endforeach()
        if(ARG_RUST)
            list(APPEND rust_sources "${gen_dir}/${base}.rs")
        endif()
    endforeach()

    # The protos are named by import path.  An absolute path could map to a
    # second name when one import directory contains another.
    add_custom_command(
        OUTPUT ${outputs} ${rust_sources}
        COMMAND ${ARG_PROTOC}
        ARGS --plugin=protoc-gen-phaser=${ARG_PLUGIN}
             --phaser_out=${options}:${out_dir}
             ${proto_paths}
             ${ARG_PROTOS} ${ARG_IMPORTS}
        DEPENDS ${inputs} ${tool_depends}
        WORKING_DIRECTORY "${out_dir}"
        COMMENT "Generating phaser code for ${name}"
        VERBATIM
    )

    set(rust_file)
    if(ARG_RUST)
        set(rust_file "${out_dir}/${name}.rs")
        set(rust_inputs "${_PHASER_DIR}/phaser/runtime/rust_support.rs" ${rust_sources})
        add_custom_command(
            OUTPUT "${rust_file}"
            COMMAND ${CMAKE_COMMAND}
                "-DOUTPUT=${rust_file}"
                "-DINPUTS=${rust_inputs}"
                -P "${_PHASER_DIR}/cmake/concatenate.cmake"
            DEPENDS ${rust_inputs} "${_PHASER_DIR}/cmake/concatenate.cmake"
            COMMENT "Assembling the Rust bindings for ${name}"
            VERBATIM
        )
    endif()

    add_library(${name} STATIC ${outputs} ${short_headers} ${rust_file})
    target_include_directories(${name} PUBLIC "${out_dir}")
    target_link_libraries(${name} PUBLIC
        phaser_runtime
        phaser_bank_protobuf
        phaser_arrow
        phaser_avro
        phaser_parquet
    )
    if(ARG_FRONTEND STREQUAL "c")
        target_link_libraries(${name} PUBLIC phaser_c)
    endif()
    if(ARG_RUST)
        set_target_properties(${name} PROPERTIES PHASER_RUST_SOURCE "${rust_file}")
    endif()
    if(ARG_COMPILE_OPTIONS)
        target_compile_options(${name} PRIVATE ${ARG_COMPILE_OPTIONS})
    endif()
endfunction()
