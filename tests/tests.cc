#include "doctest/doctest.h"
#include <string>
#include <vector>

#include "bembo/doc.h"
#include "bembo/ranges.h"

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

namespace bembo {

void check_pretty(std::string_view expected, const Doc doc, int cols = 80) {
    CHECK_EQ(expected, doc.pretty(cols));
}

TEST_CASE("basic") {

    check_pretty("", Doc{});
    check_pretty("", Doc::nil());

    check_pretty("hello, world", Doc::s("hello, world"));

    auto hello = Doc::sv("hello, world");
    check_pretty("hello, world", hello + Doc::nil());
    check_pretty("hello, world", Doc::nil() + hello);
}

TEST_CASE("line") {
    auto x = Doc::sv("x");
    check_pretty("x\nx", (x + Doc::line()) + x);
}

TEST_CASE("join") {
    std::vector<Doc> docs{Doc::sv("a"), Doc::sv("b"), Doc::sv("c")};

    check_pretty("abc", bembo::join(docs.begin(), docs.end()));
    check_pretty("abc", bembo::join(docs));
}

TEST_CASE("sep") {
    std::vector<Doc> docs{Doc::sv("a"), Doc::sv("b"), Doc::sv("c")};

    check_pretty("a, b, c", bembo::sep(Doc::sv(", "), docs.begin(), docs.end()));
    check_pretty("a, b, c", bembo::sep(Doc::sv(", "), docs));
}

} // namespace bembo
