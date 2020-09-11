package(default_visibility = ["//visibility:public"])
load("@io_bazel_rules_go//go:def.bzl", "go_library", "go_test")
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "libv8",
    srcs = select({
        "@io_bazel_rules_go//go/platform:darwin": glob(["libv8/darwin/lib/*.a"]),
        "@io_bazel_rules_go//go/platform:linux": glob(["libv8/linux/lib/*.a"]),
        "//conditions:default": [],
    }),
    hdrs = select({
       "@io_bazel_rules_go//go/platform:darwin": glob(["libv8/darwin/include/*.h", "libv8/darwin/include/libplatform/*.h"]),
       "@io_bazel_rules_go//go/platform:linux": glob(["libv8/linux/include/*.h", "libv8/linux/include/libplatform/*.h"]),
       "//conditions:default": [],
   }),
)

go_library(
    name = "go_default_library",
    srcs = [
        "v8bridge.cc",
        "v8bridge.h",
        "v8_darwin.go",
        "v8_linux.go",
    ] + select({
        "@io_bazel_rules_go//go/platform:darwin": glob(["libv8/darwin/include/*.h", "libv8/darwin/include/libplatform/*.h"]),
        "@io_bazel_rules_go//go/platform:linux": glob(["libv8/linux/include/*.h", "libv8/linux/include/libplatform/*.h"]),
        "//conditions:default": [],
    }),
    cdeps = [":libv8"],
    cgo = True,
    importpath = "github.com/packing/v8go",
)

go_test(
    name = "go_default_test",
    srcs = [
        "v8_test.go",
    ],
    embed = [":go_default_library"],
)
