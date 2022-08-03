#ifndef BEMBO_DOC_H
#define BEMBO_DOC_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <numeric>
#include <ostream>
#include <string>
#include <vector>

namespace bembo {

class Writer {
public:
    virtual ~Writer() = default;

    // Emit a newline, and indent by `indent` spaces.
    virtual void line(int indent) = 0;

    // Emit the string.
    virtual void write(std::string_view sv) = 0;
};

class StreamWriter final : public Writer {
private:
    std::ostream &out;

public:
    StreamWriter(std::ostream &out);

    void line(int indent) override;
    void write(std::string_view sv) override;
};

class StringWriter final : public Writer {
public:
    std::string buffer;

    StringWriter();

    void line(int indent) override;
    void write(std::string_view sv) override;
};

class Doc final {
private:
    friend class Fits;
    friend class DocRenderer;

    // Shared refcount for when the tag of `data` doesn't indicate an inlined case.
    union {
        std::array<char, 8> short_text_data;
        std::atomic<int> *refs;
    };

    uint64_t value;

    enum class Tag : uint8_t {
        Nil = 0x0,
        Line = 0x2,
        ShortText = 0x4,

        // nodes that count refs are odd
        Text = 0x1,
        Concat = 0x3,
        Choice = 0x5,
        Nest = 0x7,
    };

    Tag tag() const;
    void *data() const;

    template <typename T> T &cast() {
        return *reinterpret_cast<T *>(this->data());
    }

    template <typename T> const T &cast() const {
        return *reinterpret_cast<T *>(this->data());
    }

    bool boxed() const;
    void increment();
    bool decrement();
    bool is_unique() const;
    void cleanup();

    bool is_flattened() const;

    Doc(Tag tag);
    Doc(Tag tag, void *ptr);
    static Doc choice(Doc left, Doc right);

    static Doc short_text(std::string_view text);

    std::string_view get_short_text() const;

    bool init_short_str(std::string_view str);
    void init_string(std::string str);

public:
    ~Doc();

    Doc(const Doc &other);
    Doc &operator=(const Doc &other);

    Doc(Doc &&other);
    Doc &operator=(Doc &&other);

    // Construct an empty doc.
    Doc();

    // Construct a Doc from a string constant.
    Doc(const char *str);

    // Construct a Doc from a string constant.
    Doc(std::string_view str);

    // Construct an empty doc.
    static Doc nil();

    // A newline.
    static Doc line();

    // A soft newline, following these rules: if there's enough space behave like a space, otherwise behave like a
    // newline.
    static Doc softline();

    // A soft break, following these rules: if there's enough space behave like nil, otherwise behave like a newline.
    static Doc softbreak();

    // A single character.
    static Doc c(char c);

    // A string.
    static Doc s(std::string str);

    // A string.
    static Doc s(const char *str);

    // A string, populated from a `std::string_view`.
    static Doc sv(std::string_view str);

    // Concatenate this document with another.
    Doc operator+(Doc other) const;

    // Append another document to this one.
    Doc &append(Doc other);

    // Append the contents of the range to this Doc by copying its elements.
    template <typename InputIt, typename Sentinel> Doc &append(InputIt &&begin, Sentinel &&end) {
        if (this->tag() != Tag::Concat) {
            *this = Doc{Tag::Concat, new std::vector<Doc>()};
        }

        auto &vec = this->cast<std::vector<Doc>>();
        vec.reserve(vec.size() + std::distance(begin, end));
        std::copy(begin, end, std::back_inserter(vec));

        return *this;
    }

    // Append the contents of the range to this Doc by copying its elements.
    template <typename Rng> Doc &append(Rng &&rng) {
        if (this->tag() != Tag::Concat) {
            *this = Doc{Tag::Concat, new std::vector<Doc>()};
        }

        auto begin = rng.begin();
        auto end = rng.end();

        auto &vec = this->cast<std::vector<Doc>>();
        vec.reserve(vec.size() + std::distance(begin, end));
        std::copy(begin, end, std::back_inserter(vec));

        return *this;
    }

    // Append another document to this one.
    Doc &operator+=(Doc other);

    // Concatenate another doc with this one, with a space between.
    Doc operator<<(Doc other) const;

    // Append another document to this one with a space between.
    Doc &operator<<=(Doc other);

    // Concatenate two Docs with a line between them.
    Doc operator/(Doc other) const;

    // Append a Doc after a newline.
    Doc &operator/=(Doc other);

    // Adjust the indentation level in `other` by `indent`.
    static Doc nest(int indent, Doc other);

    // If possible, emit all of `other` on a single line, treating newlines as spaces instead.
    static Doc group(Doc other);

    Doc &flatten();
    static Doc flatten(Doc other);

    // True if this doc is empty.
    bool is_nil() const {
        return this->tag() == Tag::Nil;
    }

    // Render the document out assuming a line length of `cols`.
    void render(Writer &target, int cols) const;

    // Render to a string.
    std::string pretty(int cols) const;

private:
    template <typename... Docs> static void concat_impl(std::vector<Doc> &acc, Doc arg, Docs &&...rest) {
        acc.emplace_back(std::move(arg));
        if constexpr (sizeof...(Docs) > 0) {
            concat_impl(acc, std::forward<Docs>(rest)...);
        }
    }

    template <typename... Docs> static void vcat_impl(std::vector<Doc> &acc, Doc arg, Docs &&...rest) {
        acc.emplace_back(std::move(arg));
        if constexpr (sizeof...(Docs) > 0) {
            acc.emplace_back(Doc::line());
            vcat_impl(acc, std::forward<Docs>(rest)...);
        }
    }

public:
    template <typename... Docs> static Doc concat(Docs &&...rest) {
        Doc res{Tag::Concat, new std::vector<Doc>()};
        auto &acc = res.cast<std::vector<Doc>>();
        acc.reserve(sizeof...(Docs));
        concat_impl(acc, std::forward<Docs>(rest)...);
        return res;
    }

    template <typename... Docs> static Doc vcat(Docs &&...rest) {
        Doc res{Tag::Concat, new std::vector<Doc>()};
        auto &acc = res.cast<std::vector<Doc>>();
        acc.reserve(sizeof...(Docs) + sizeof...(Docs) - 1);
        vcat_impl(acc, std::forward<Docs>(rest)...);
        return res;
    }

    static Doc angles(Doc doc);
    static Doc braces(Doc doc);
    static Doc brackets(Doc doc);
    static Doc quotes(Doc doc);
    static Doc dquotes(Doc doc);
    static Doc parens(Doc doc);
};

template <typename It, typename Sentinel> Doc join(It &&begin, Sentinel &&end) {
    return std::accumulate(std::forward<It>(begin), std::forward<Sentinel>(end), Doc::nil());
}

template <typename Range> Doc join(Range &&rng) {
    return std::accumulate(rng.begin(), rng.end(), Doc::nil());
}

template <typename It, typename Sentinel> Doc sep(Doc d, It &&begin, Sentinel &&end) {
    Doc res;

    if (begin == end) {
        return res;
    }

    while (true) {
        Doc chunk = *begin;
        ++begin;

        if (begin == end) {
            res += chunk;
            break;
        }

        res += Doc::group(chunk + d);
    }

    return res;
}

template <typename Range> Doc sep(Doc d, Range &&rng) {
    return sep(std::move(d), rng.begin(), rng.end());
}

} // namespace bembo

#endif
