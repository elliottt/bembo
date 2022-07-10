#include "doctest/doctest.h"
#include <string>

#include "bembo/doc.h"

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

namespace bembo {

TEST_CASE("basic") {

    CHECK_EQ("", Doc{}.pretty(80));
    CHECK_EQ("", Doc::nil().pretty(80));

    CHECK_EQ("hello, world", Doc::s("hello, world").pretty(80));

    auto hello = Doc::sv("hello, world");
    CHECK_EQ("hello, world", (hello + Doc::nil()).pretty(80));
    CHECK_EQ("hello, world", (Doc::nil() + hello).pretty(80));
}

} // namespace bembo
