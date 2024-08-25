
cc_test(
    name = "tests",
    srcs = ["tests.cc"],
    copts = ["-std=c++20"],
    deps = [
        "//bembo",
        "@doctest//doctest",
        "@doctest//doctest:main",
    ],
    visibility = ["//:__pkg__"],
    linkstatic = True,
)
