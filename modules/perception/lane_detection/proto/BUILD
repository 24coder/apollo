## Auto generated by `proto_build_generator.py`
load("//tools:apollo_package.bzl", "apollo_package")
load("//tools/proto:proto.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "denseline_proto",
    srcs = ["denseline.proto"],
)

proto_library(
    name = "lane_perception_component_proto",
    srcs = ["lane_perception_component.proto"],
    deps = [
        "//modules/perception/common/proto:plugin_param_proto",
    ],
)

proto_library(
    name = "darkSCNN_postprocessor_proto",
    srcs = ["darkSCNN_postprocessor.proto"],
)

proto_library(
    name = "perception_proto",
    srcs = ["perception.proto"],
    deps = [
        "//modules/perception/common/proto:plugin_param_proto",
    ],
)

proto_library(
    name = "darkSCNN_proto",
    srcs = ["darkSCNN.proto"],
)

proto_library(
    name = "denseline_postprocessor_proto",
    srcs = ["denseline_postprocessor.proto"],
)

apollo_package()