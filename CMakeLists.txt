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

if(PHASER_BUILD_COMPILER AND NOT TARGET protobuf::libprotoc)
    set(protobuf_BUILD_TESTS OFF CACHE INTERNAL "")
    FetchContent_Declare(
        protobuf
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
        GIT_TAG v34.1
    )
    FetchContent_MakeAvailable(protobuf)
endif()

# --- Runtime library ---
add_library(phaser_runtime STATIC
    phaser/runtime/md5.cc
    phaser/runtime/message.cc
    phaser/runtime/phaser_bank.cc
)
add_library(phaser::runtime ALIAS phaser_runtime)
target_include_directories(phaser_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(phaser_runtime PUBLIC
    absl::flat_hash_map
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

    add_executable(protoc-gen-phaser
        phaser/compiler/enum_gen.cc
        phaser/compiler/gen.cc
        phaser/compiler/main.cc
        phaser/compiler/message_gen.cc
        ${PHASER_OPTIONS_SRC}
    )
    target_include_directories(protoc-gen-phaser PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
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
#     [FRONTEND protobuf|ros]
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
# PROTOC defaults to the protobuf::protoc target.  PLUGIN defaults to the
# protoc-gen-phaser target, or PHASER_PLUGIN_EXECUTABLE when that is set.
function(phaser_add_library name)
    cmake_parse_arguments(ARG ""
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
        set(ARG_FRONTEND protobuf)
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

    set(inputs)
    set(outputs)
    set(short_headers)
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
        list(APPEND outputs "${gen_dir}/${base}.phaser.cc" "${gen_dir}/${base}.phaser.h")
        if(proto IN_LIST ARG_PROTOS)
            set(short_header "${out_dir}/${base}.phaser.h")
            file(GENERATE OUTPUT "${short_header}"
                CONTENT "#pragma once\n#include \"${gen_include}/${base}.phaser.h\"\n")
            list(APPEND short_headers "${short_header}")
        endif()
    endforeach()

    # The protos are named by import path.  An absolute path could map to a
    # second name when one import directory contains another.
    add_custom_command(
        OUTPUT ${outputs}
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

    add_library(${name} STATIC ${outputs} ${short_headers})
    target_include_directories(${name} PUBLIC "${out_dir}")
    target_link_libraries(${name} PUBLIC phaser_runtime)
    if(ARG_COMPILE_OPTIONS)
        target_compile_options(${name} PRIVATE ${ARG_COMPILE_OPTIONS})
    endif()
endfunction()
