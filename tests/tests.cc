#include "doctest/doctest.h"
#include <array>
#include <sstream>
#include <string>

#include "bembo/doc.h"

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
    check_pretty("x\nx", x + Doc::line() + x);
}

TEST_CASE("join") {
    std::array<Doc, 3> docs{Doc::sv("a"), Doc::sv("b"), Doc::sv("c")};

    check_pretty("abc", bembo::join(docs.begin(), docs.end()));
    check_pretty("abc", bembo::join(docs));
}

TEST_CASE("sep") {
    std::array<Doc, 3> docs{Doc::sv("a"), Doc::sv("b"), Doc::sv("c")};

    check_pretty("a, b, c", bembo::sep(Doc::sv(", "), docs.begin(), docs.end()));
    check_pretty("a, b, c", bembo::sep(Doc::sv(", "), docs));
}

TEST_CASE("nest") {
    auto hello = Doc::sv("hello");
    auto world = Doc::sv("world");

    check_pretty("hello\n  world", hello + Doc::nest(2, Doc::line() + world));
}

TEST_CASE("softline") {
    auto d = Doc::sv("hello");

    check_pretty("hello hello", d + Doc::softline() + d);
    check_pretty("hello\nhello", d + Doc::softline() + d, 5);

    std::array<Doc, 3> docs{Doc::sv("a"), Doc::sv("b"), Doc::sv("c")};
    check_pretty("a b c", bembo::sep(Doc::softline(), docs));
    check_pretty("a\nb\nc", bembo::sep(Doc::softline(), docs), 1);
    check_pretty("a\nb\nc", bembo::sep(Doc::softline(), docs), 2);
    check_pretty("a b\nc", bembo::sep(Doc::softline(), docs), 3);
}

TEST_CASE("stream output") {
    std::array<Doc, 3> docs{Doc::sv("a"), Doc::sv("b"), Doc::sv("c")};

    {
        std::stringstream out;
        StreamWriter res{out};
        bembo::join(docs).render(res, 80);
        CHECK_EQ("abc", out.str());
    }

    {
        std::stringstream out;
        StreamWriter res{out};
        bembo::sep(Doc::sv(", "), docs).render(res, 80);
        CHECK_EQ("a, b, c", out.str());
    }

    {
        std::stringstream out;
        StreamWriter res{out};
        bembo::sep(Doc::c(',') + Doc::softline(), docs).render(res, 3);
        CHECK_EQ("a,\nb,\nc", out.str());
    }

    {
        std::stringstream out;
        StreamWriter res{out};
        bembo::sep(Doc::c(',') + Doc::softline(), docs).render(res, 5);
        CHECK_EQ("a, b,\nc", out.str());
    }
}

Doc tag(std::string_view name, Doc body = Doc::nil()) {
    if (body.is_nil()) {
        return Doc::angles(Doc::sv(name) << Doc::c('/'));
    } else {
        auto tag = Doc::sv(name);
        return Doc::concat(
            Doc::angles(tag),
            Doc::group(Doc::concat(Doc::nest(2, Doc::softbreak() + body), Doc::softbreak())),
            Doc::angles(Doc::c('/') + tag));
    }
}

TEST_CASE("xml") {
    check_pretty("<br />", tag("br"));

    auto ab = tag("a", tag("b"));
    check_pretty("<a><b /></a>", ab);
    check_pretty("<a>\n  <b />\n</a>", ab, 6);
    check_pretty("<a>\n  <b>\n    <c />\n  </b>\n</a>", tag("a", tag("b", tag("c"))), 2);
}

TEST_CASE("concat") {
    check_pretty("ab", Doc::concat(Doc::sv("a"), Doc::sv("b")));
    check_pretty("ab", Doc::concat(Doc::sv("a"), "b"));
    check_pretty("ab", Doc::concat("a", "b"));

    check_pretty("abcd", Doc::concat("a", Doc::concat("b", "c"), "d"));
}

TEST_CASE("vcat") {
    check_pretty("a\nb", Doc::vcat("a", "b"));
}

TEST_CASE("append") {
    std::array<Doc, 3> docs{Doc::sv("a"), Doc::sv("b"), Doc::sv("c")};
    {
        Doc res = Doc::nil();
        res.append(docs.begin(), docs.end());
        check_pretty("abc", res);
    }

    {
        Doc res = Doc::nil();
        res.append(docs);
        check_pretty("abc", res);
    }

    {
        Doc res = Doc::concat("a", "b");
        res.append(Doc::concat("c", "d"));
        check_pretty("abcd", std::move(res));
    }

}

TEST_CASE("flatten") {
    auto d = Doc::concat(Doc::sv("a"), Doc::line(), Doc::sv("b"), Doc::line(), Doc::sv("c"));

    check_pretty("a\nb\nc", d);
    check_pretty("a b c", Doc::flatten(d));
}

TEST_CASE("strings") {
    check_pretty("hi", "hi");
    check_pretty("hi", "hi"sv);
}

TEST_CASE("moving") {
    Doc foo = "hi";
    foo = "there";
    check_pretty("there", std::move(foo));
}

TEST_CASE("copying") {
    Doc foo{"hi"};
    Doc bar = foo;
    check_pretty("hi", foo);
}

} // namespace bembo
