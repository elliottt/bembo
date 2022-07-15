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

    virtual void line() = 0;
    virtual void write(std::string_view sv) = 0;
};

class Doc final {
private:
    friend class Fits;
    friend class DocRenderer;

    // Shared refcount for when the tag of `data` doesn't indicate an inlined case.
    std::atomic<int> *refs;

    // Optional pointer + tag in the following format:
    // 63                                    0
    // +--------------------+----------------+
    // | 48 bits of pointer | 16 bits of tag |
    // +--------------------+----------------+
    uint64_t value;

    // TODO: char variant that puts the character in the pointer bits
    enum class Tag : uint16_t {
        Nil = 0,
        Line = 1,
        Text = 2,
        Concat = 3,
        Choice = 4,
    };

    Tag tag() const;
    void *data() const;

    template <typename T> T &cast();
    template <typename T> const T &cast() const;

    void increment();
    bool decrement();
    void cleanup();

    Doc(Tag tag);
    Doc(std::atomic<int> *refs, Tag tag, void *ptr);
    static Doc choice(Doc left, Doc right);

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
    Doc operator+(Doc other) const;
    Doc &operator+=(Doc other);

    static Doc group(Doc other);

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
