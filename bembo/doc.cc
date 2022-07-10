#include <cstdint>

#include "doc.h"

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

} // namespace

Doc::Tag Doc::tag() const {
    return static_cast<Tag>(this->value & TAG_MASK);
}

void *Doc::data() const {
    return reinterpret_cast<void *>(this->value >> TAG_MASK_BITS);
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
    (*this->refs)++;
}

Doc::Doc() : Doc(Tag::Nil) {}

Doc Doc::nil() {
    return Doc{};
}

Doc::Doc(std::string str) : Doc{new std::atomic<int>(0), Tag::Text, new Text{std::move(str)}} {}

Doc Doc::text(std::string str) {
    return Doc{str};
}

Doc::Doc(Doc left, Doc right)
    : Doc{new std::atomic<int>(0), Tag::Concat, new Concat{std::move(left), std::move(right)}} {}

Doc Doc::operator+(Doc other) const {
    return Doc{*this, std::move(other)};
}

} // namespace bembo
