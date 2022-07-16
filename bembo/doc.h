#ifndef BEMBO_DOC_H
#define BEMBO_DOC_H

#include <atomic>
#include <cstdint>
#include <numeric>
#include <ostream>
#include <string>

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

    // Construct an empty doc.
    Doc();

    // Construct an empty doc.
    static Doc nil();

    // A newline.
    static Doc line();

    // A soft newline, following these rules: if there's enough space behave like a space, otherwise behavie like a
    // newline.
    static Doc softline();

    // A single character.
    static Doc c(char c);

    // A string.
    static Doc s(std::string str);

    // A string, populated from a `std::string_view`.
    static Doc sv(std::string_view str);

    // Concatenate two documents together.
    Doc(Doc left, Doc right);

    // Concatenate this document with another.
    Doc operator+(Doc other) const;

    // Append another document to this one.
    Doc &append(Doc other);

    // Append another document to this one.
    Doc &operator+=(Doc other);

    // Concatenate another doc with this one, with a space between.
    Doc operator<<(Doc other) const;

    // Adjust the indentation level in `other` by `indent`.
    static Doc nest(int indent, Doc other);

    // If possible, emit all of `other` on a single line, treating newlines as spaces instead.
    static Doc group(Doc other);

    // True if this doc is empty.
    bool is_nil() const {
        return this->tag() == Tag::Nil;
    }

    // Render the document out assuming a line length of `cols`.
    void render(Writer &target, int cols) const;

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
