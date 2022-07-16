#ifndef BEMBO_DOC_H
#define BEMBO_DOC_H

#include <atomic>
#include <cstdint>
#include <numeric>
#include <string>

namespace bembo {

class Renderer {
public:
    virtual ~Renderer() = default;

    // Emit a newline, and indent by `indent` spaces.
    virtual void line(int indent) = 0;

    // Emit the string.
    virtual void write(std::string_view sv) = 0;
};

class Doc final {
private:
    friend class Fits;
    friend class DocRenderer;

    // Shared refcount for when the tag of `data` doesn't indicate an inlined case.
    union {
        char short_text_data[8];
        std::atomic<int> *refs;
    };

    // Optional pointer + tag in the following format:
    // 63                                    0
    // +--------------------+------------------------+---------------+
    // | 48 bits of pointer | 8 bits of short length | 8 bits of tag |
    // +--------------------+------------------------+---------------+
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

    template <typename T> T &cast();
    template <typename T> const T &cast() const;

    bool boxed() const;
    void increment();
    bool decrement();
    void cleanup();

    Doc(Tag tag);
    Doc(std::atomic<int> *refs, Tag tag, void *ptr);
    static Doc choice(bool flattening, Doc left, Doc right);

    static Doc short_text(std::string_view text);

    std::string_view get_short_text() const;

public:
    ~Doc();

    Doc(const Doc &other);
    Doc &operator=(const Doc &other);

    Doc(Doc &&other);
    Doc &operator=(Doc &&other);

    Doc();
    static Doc nil();
    static Doc line();
    static Doc softline();

    static Doc c(char c);
    static Doc s(std::string str);
    static Doc sv(std::string_view str);

    Doc(Doc left, Doc right);
    Doc append(Doc other) const;
    Doc operator+(Doc other) const;
    Doc &operator+=(Doc other);

    // Append with a space between.
    Doc operator<<(Doc other) const;

    static Doc nest(int level, Doc other);
    static Doc group(Doc other);

    bool is_nil() const {
        return this->tag() == Tag::Nil;
    }

    // Render the document out assuming a line length of `cols`.
    void render(Renderer &target, int cols) const;

    // Render to a string.
    std::string pretty(int cols) const;
};

template <typename It, typename Sentinel> Doc join(It &&begin, Sentinel &&end) {
    return std::accumulate(std::forward<It>(begin), std::forward<Sentinel>(end), Doc::nil());
}

template <typename It, typename Sentinel> Doc sep(Doc sep, It &&begin, Sentinel &&end) {
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

        res += Doc::group(chunk + sep);
    }

    return res;
}

} // namespace bembo

#endif
