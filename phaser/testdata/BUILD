load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//phaser:phaser_library.bzl", "phaser_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "foo_proto",
    srcs = [
        "Foo.proto",
    ],
)

proto_library(
    name = "test_proto",
    srcs = [
        "Test.proto",
    ],
    deps = [
        ":foo_proto",
        "@com_google_protobuf//:any_proto",
    ],
)

cc_proto_library(
    name = "test_cc_proto",
    deps = [":test_proto"],
)

proto_library(
    name = "test_message_proto",
    srcs = [
        "TestMessage.proto",
    ],
    deps = [
        "@com_google_protobuf//:any_proto",
    ],
)

cc_proto_library(
    name = "test_message_cc_proto",
    deps = [":test_message_proto"],
)

phaser_library(
    name = "test_message_phaser",
    add_namespace = "phaser",
    runtime = "//phaser/runtime:phaser_runtime",
    deps = [":test_message_proto"],
)

phaser_library(
    name = "test_phaser",
    add_namespace = "phaser",
    runtime = "//phaser/runtime:phaser_runtime",
    deps = [":test_proto"],
)

# Same as foo_proto but with the optional `std::any active_message` field
# enabled, used to exercise phaser_library(enable_active_message = True).
phaser_library(
    name = "foo_active_message_phaser",
    add_namespace = "phaser",
    enable_active_message = True,
    runtime = "//phaser/runtime:phaser_runtime",
    deps = [":foo_proto"],
)

proto_library(
    name = "ros_compile_proto",
    srcs = ["RosCompile.proto"],
    deps = ["//phaser:options_proto"],
)

cc_proto_library(
    name = "ros_compile_cc_proto",
    deps = [":ros_compile_proto"],
)

phaser_library(
    name = "ros_compile_phaser",
    add_namespace = "phaser",
    frontend = "ros",
    runtime = "//phaser/runtime:phaser_runtime",
    deps = [":ros_compile_proto"],
)

proto_library(
    name = "ros_metadata_proto",
    srcs = ["RosMetadata.proto"],
    deps = [
        "//phaser:options_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

phaser_library(
    name = "ros_metadata_protobuf_phaser",
    add_namespace = "protobuf_api",
    direct_header_symlinks = False,
    ros_metadata = True,
    runtime = "//phaser/runtime:phaser_runtime",
    deps = [":ros_metadata_proto"],
)

phaser_library(
    name = "ros_metadata_ros_phaser",
    add_namespace = "ros_api",
    cc_deps = [":ros1_shim"],
    direct_header_symlinks = False,
    frontend = "ros",
    ros_metadata = True,
    runtime = "//phaser/runtime:phaser_runtime",
    deps = [":ros_metadata_proto"],
)

cc_library(
    name = "ros1_shim",
    hdrs = glob(["ros_shim/**/*.h"]),
    strip_include_prefix = "ros_shim",
)

proto_library(
    name = "ros_header_proto",
    srcs = ["RosHeader.proto"],
    deps = ["@com_google_protobuf//:timestamp_proto"],
)

proto_library(
    name = "ros_intrinsics_proto",
    srcs = ["RosIntrinsics.proto"],
    deps = [
        ":ros_header_proto",
        "//phaser:options_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "ros_intrinsics_cc_proto",
    deps = [":ros_intrinsics_proto"],
)

phaser_library(
    name = "ros_intrinsics_phaser",
    add_namespace = "phaser",
    cc_deps = [":ros1_shim"],
    frontend = "ros",
    runtime = "//phaser/runtime:phaser_runtime",
    deps = [":ros_intrinsics_proto"],
)

proto_library(
    name = "ros_intrinsics_protobuf_frontend_proto",
    srcs = ["RosIntrinsicsProtobufFrontend.proto"],
    deps = [
        ":ros_header_proto",
        "//phaser:options_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

phaser_library(
    name = "ros_intrinsics_protobuf_phaser",
    add_namespace = "protobuf_phaser",
    frontend = "protobuf",
    runtime = "//phaser/runtime:phaser_runtime",
    deps = [":ros_intrinsics_protobuf_frontend_proto"],
)

cc_binary(
    name = "ros_intrinsics_phaser_wire_tool",
    srcs = ["ros_intrinsics_phaser_wire_tool.cc"],
    deps = [":ros_intrinsics_phaser"],
)

cc_binary(
    name = "ros_intrinsics_protobuf_wire_tool",
    srcs = ["ros_intrinsics_protobuf_wire_tool.cc"],
    deps = [":ros_intrinsics_cc_proto"],
)

sh_test(
    name = "ros_intrinsics_wire_compatibility_test",
    srcs = ["ros_intrinsics_wire_compatibility_test.sh"],
    args = [
        "$(rootpath :ros_intrinsics_phaser_wire_tool)",
        "$(rootpath :ros_intrinsics_protobuf_wire_tool)",
    ],
    data = [
        ":ros_intrinsics_phaser_wire_tool",
        ":ros_intrinsics_protobuf_wire_tool",
        "//phaser:valgrind.supp",
    ],
)

sh_test(
    name = "invalid_array_size_test",
    srcs = ["invalid_array_size_test.sh"],
    args = [
        "$(rootpath @com_google_protobuf//:protoc)",
        "$(rootpath //phaser/compiler:phaser)",
        "$(rootpath InvalidArraySize.proto)",
    ],
    data = [
        "InvalidArraySize.proto",
        "//phaser:valgrind.supp",
        "//phaser:options_proto",
        "//phaser/compiler:phaser",
        "@com_google_protobuf//:protoc",
        "@com_google_protobuf//:well_known_protos",
    ],
)

sh_test(
    name = "invalid_ros_intrinsic_test",
    srcs = ["invalid_ros_intrinsic_test.sh"],
    args = [
        "$(rootpath @com_google_protobuf//:protoc)",
        "$(rootpath //phaser/compiler:phaser)",
        "$(rootpath InvalidRosIntrinsic.proto)",
    ],
    data = [
        "InvalidRosIntrinsic.proto",
        "//phaser:valgrind.supp",
        "//phaser/compiler:phaser",
        "@com_google_protobuf//:protoc",
        "@com_google_protobuf//:well_known_protos",
    ],
)

proto_library(
    name = "coverage_proto",
    srcs = ["coverage.proto"],
    deps = [
        ":foo_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

cc_proto_library(
    name = "coverage_cc_proto",
    deps = [":coverage_proto"],
)

phaser_library(
    name = "coverage_phaser",
    add_namespace = "phaser",
    runtime = "//phaser/runtime:phaser_runtime",
    deps = [":coverage_proto"],
)

proto_library(
    name = "vision_proto",
    srcs = ["vision.proto"],
)

cc_proto_library(
    name = "vision_cc_proto",
    deps = [":vision_proto"],
)

phaser_library(
    name = "vision_phaser",
    add_namespace = "phaser",
    runtime = "//phaser/runtime:phaser_runtime",
    deps = [":vision_proto"],
)
