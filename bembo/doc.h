#ifndef PPRINT_DOC_H
#define PPRINT_DOC_H

#include <atomic>
#include <cstdint>
#include <string>

namespace bembo {

class Doc final {
private:
    // Shared refcount for when the tag of `data` doesn't indicate an inlined case.
    std::atomic<int> *refs;

    // Optional pointer + tag in the following format:
    // 63                                    0
    // +--------------------+----------------+
    // | 48 bits of pointer | 16 bits of tag |
    // +--------------------+----------------+
    uint64_t value;

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

public:
    ~Doc();

    Doc(const Doc &other);
    Doc &operator=(const Doc &other);

    Doc(Doc &&other);
    Doc &operator=(Doc &&other);

    Doc();
    static Doc nil();

    static Doc s(std::string str);
    static Doc sv(std::string_view str);

    Doc(Doc left, Doc right);
    Doc operator+(Doc other) const;

    // Render the document out assuming a line length of `cols`.
    template <typename T>
    void render(T &target, int cols) const;

    // Render to a string.
    std::string pretty(int cols) const;
};

} // namespace bembo

#endif
