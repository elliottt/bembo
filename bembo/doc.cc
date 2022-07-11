#include <cstdint>
#include <vector>

#include "doc.h"

using namespace std::literals::string_view_literals;

namespace bembo {

namespace {

constexpr int TAG_MASK_BITS = 16;
constexpr uint64_t TAG_MASK = (1 << TAG_MASK_BITS) - 1;

uint64_t make_tagged(uint16_t tag, void *ptr) {
    return (reinterpret_cast<uint64_t>(ptr) << TAG_MASK_BITS) | static_cast<uint64_t>(tag);
}

struct Text final {
    std::string text;
};

struct Concat final {
    Doc left;
    Doc right;
};

// NOTE: the left side is always flattened implicitly, so lines will be interpreted as a single space.
struct Choice final {
    Doc left;
    Doc right;
};

} // namespace

Doc::Tag Doc::tag() const {
    return static_cast<Tag>(this->value & TAG_MASK);
}

void *Doc::data() const {
    return reinterpret_cast<void *>(this->value >> TAG_MASK_BITS);
}

template <typename T> T &Doc::cast() {
    return *reinterpret_cast<T *>(this->data());
}

template <typename T> const T &Doc::cast() const {
    return *reinterpret_cast<T *>(this->data());
}

void Doc::increment() {
    this->refs->fetch_add(1);
}

bool Doc::decrement() {
    return this->refs->fetch_sub(1) <= 1;
}

void Doc::cleanup() {
    switch (this->tag()) {
    case Tag::Nil:
    case Tag::Line:
        return;

    case Tag::Text:
        if (this->decrement()) {
            delete this->refs;
            delete static_cast<Text *>(this->data());
        }
        return;

    case Tag::Concat:
        if (this->decrement()) {
            delete this->refs;
            delete static_cast<Concat *>(this->data());
        }
        return;

    case Tag::Choice:
        if (this->decrement()) {
            delete this->refs;
            delete static_cast<Choice *>(this->data());
        }
        return;
    }
}

Doc::~Doc() {
    this->cleanup();
}

Doc::Doc(const Doc &other) : refs{other.refs}, value{other.value} {
    if (this->refs != nullptr) {
        this->increment();
    }
}

Doc &Doc::operator=(const Doc &other) {
    if (this == &other) {
        return *this;
    }

    if (this->refs != nullptr) {
        this->cleanup();
    }

    this->refs = other.refs;
    this->value = other.value;

    if (this->refs) {
        this->increment();
    }

    return *this;
}

Doc::Doc(Doc &&other) : refs{other.refs}, value{other.value} {
    // the tag guides the destructor, so skip clearing out the refs field by instead turning this into a Nil.
    other.value = 0;
}

Doc &Doc::operator=(Doc &&other) {
    if (this == &other) {
        return *this;
    }

    if (this->refs != nullptr) {
        this->cleanup();
    }

    this->refs = other.refs;
    this->value = other.value;

    other.value = 0;

    return *this;
}

// Initialization of a variant that doesn't live in the heap.
Doc::Doc(Tag tag) : refs{nullptr}, value{static_cast<uint64_t>(tag)} {}

// Initialization of a variant that lives in the heap.
Doc::Doc(std::atomic<int> *refs, Tag tag, void *ptr) : refs{refs}, value{make_tagged(static_cast<uint16_t>(tag), ptr)} {
    this->increment();
}

Doc::Doc() : Doc(Tag::Nil) {}

Doc Doc::nil() {
    return Doc{};
}

Doc Doc::line() {
    return Doc{Tag::Line};
}

Doc Doc::s(std::string str) {
    return Doc{new std::atomic<int>(0), Tag::Text, new Text{std::move(str)}};
}

Doc Doc::sv(std::string_view str) {
    return Doc{new std::atomic<int>(0), Tag::Text, new Text{std::string(str)}};
}

Doc::Doc(Doc left, Doc right)
    : Doc{new std::atomic<int>(0), Tag::Concat, new Concat{std::move(left), std::move(right)}} {}

Doc Doc::operator+(Doc other) const {
    return Doc{*this, std::move(other)};
}

Doc &Doc::operator+=(Doc other) {
    *this = Doc{std::move(*this), std::move(other)};
    return *this;
}

Doc Doc::group(Doc doc) {
    // TODO: flatten doc | doc
    return doc;
}

template <typename T> void Doc::render(T &out, int cols) const {
    std::string buffer;

    std::vector<const Doc *> work{this};

    while (!work.empty()) {
        auto *node = work.back();
        work.pop_back();

        switch (node->tag()) {
        case Tag::Nil:
            break;

        case Tag::Line:
            out.line();
            break;

        case Tag::Text:
            out.write(node->cast<Text>().text);
            break;

        case Tag::Concat: {
            auto &cat = node->cast<Concat>();
            work.push_back(&cat.right);
            work.push_back(&cat.left);
            break;
        }

        case Tag::Choice:
            break;
        }
    }
}

namespace {

// A simple renderer for collecting the output in a buffer.
struct StringRenderer {
    std::string buffer;

    void line() {
        this->buffer.push_back('\n');
    }

    void write(std::string_view sv) {
        this->buffer.append(sv);
    }
};

} // namespace

std::string Doc::pretty(int cols) const {
    StringRenderer out;
    this->render(out, cols);
    return out.buffer;
}

} // namespace bembo
