load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
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
