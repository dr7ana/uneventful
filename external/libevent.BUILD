load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

filegroup(
    name = "all_srcs",
    srcs = glob(
        ["**"], 
        exclude = [
            "sample/**", 
            "test/**", 
            "test-export/**"
        ]
    ),
    visibility = ["//visibility:public"],
)

cmake(
    name = "event",
    cache_entries = {
        "EVENT__LIBRARY_TYPE": "STATIC",
        "EVENT__DISABLE_TESTS": "ON",
        "EVENT__DISABLE_OPENSSL": "ON",
        "EVENT__DISABLE_MBEDTLS": "ON",
        "EVENT__DISABLE_BENCHMARK": "ON",
        "EVENT__DISABLE_SAMPLES": "ON",
    },
    lib_source = ":all_srcs",
    out_static_libs = select({
        ":debug": [
            "libevent_cored.a",
            "libevent_extrad.a",
            "libevent_pthreadsd.a"
        ],
        "//conditions:default": [
            "libevent_core.a",
            "libevent_extra.a",
            "libevent_pthreads.a"
        ],
    }),
    visibility = ["//visibility:public"],
    deps = [
        "@openssl",
    ],
)

config_setting(
    name = "debug",
    values = {
        "compilation_mode": "dbg",
    },
)
