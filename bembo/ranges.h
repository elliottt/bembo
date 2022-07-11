#ifndef BEMBO_RANGES_H
#define BEMBO_RANGES_H

#include <numeric>
#include <ranges>

#include "doc.h"

namespace bembo {

template <typename Range> Doc join(Range &&rng) {
    namespace ranges = std::ranges;
    return std::accumulate(ranges::cbegin(rng), ranges::cend(rng), Doc::nil());
}

template <typename Range> Doc sep(Doc sep, Range &&rng) {
    namespace ranges = std::ranges;

    Doc res;

    if (ranges::empty(rng)) {
        return res;
    }

    auto it = ranges::begin(rng);
    while (true) {
        auto item = *it;
        ++it;
        if (it == ranges::end(rng)) {
            res += Doc::group(item);
            break;
        }

        res += Doc::group(item + sep);
    }

    return res;
}

} // namespace bembo

#endif
