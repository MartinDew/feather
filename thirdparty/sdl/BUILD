# load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

cmake(
    name = "sdl",
    generate_args = ["-G Ninja"],
    lib_source = "@sdl//:src",
    # out_shared_libs = ["libsdl.so"],
    # out_static_libs = ["libsdl.lib"],
    visibility = ["//visibility:public"],
)
