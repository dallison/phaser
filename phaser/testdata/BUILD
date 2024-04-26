package(default_visibility = ["//visibility:public"])

proto_library(
    name = "test_proto",
    srcs = [
        "Foo.proto",
        "Test.proto",
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
