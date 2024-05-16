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

