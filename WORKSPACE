load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "doctest",
    urls = ["https://github.com/doctest/doctest/archive/refs/tags/v2.4.9.tar.gz"],
    sha256 = "19b2df757f2f3703a5e63cee553d85596875f06d91a3333acd80a969ef210856",
    strip_prefix = "doctest-2.4.9",
)

http_archive(
    name = "com_grail_bazel_compdb",
    urls = ["https://github.com/grailbio/bazel-compilation-database/archive/be2ea876e64d9047ead799281f594d7b924c1c09.zip"],
    sha256 = "b926ed2a9bf6a57ddf19287784a1a93dce87228c1d311940ffb8716131fa9dd9",
    strip_prefix = "bazel-compilation-database-be2ea876e64d9047ead799281f594d7b924c1c09",
)

load("@com_grail_bazel_compdb//:deps.bzl", "bazel_compdb_deps")
bazel_compdb_deps()
