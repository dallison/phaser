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
)

cc_proto_library(
    name = "test_message_cc_proto",
    deps = [":test_message_proto"],
)

phaser_library(
    name = "test_message_phaser",
    deps = [":test_message_proto"],
    runtime = "//phaser/runtime:phaser_runtime",
    add_namespace = "phaser",
)

phaser_library(
    name = "test_phaser",
    deps = [":test_proto"],
    runtime = "//phaser/runtime:phaser_runtime",
    add_namespace = "phaser",
)