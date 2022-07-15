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

Doc Doc::choice(Doc left, Doc right) {
    return Doc{new std::atomic<int>(0), Tag::Choice, new Choice{std::move(left), std::move(right)}};
}

Doc::Doc() : Doc(Tag::Nil) {}

Doc Doc::nil() {
    return Doc{};
}

Doc Doc::line() {
    return Doc{Tag::Line};
}

Doc Doc::softline() {
    return Doc::choice(Doc::c(' '), Doc::line());
}

Doc Doc::c(char c) {
    return Doc{new std::atomic<int>(0), Tag::Text, new Text{std::string(1, c)}};
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
    // This looks odd but the behavior of choice is to always flatten the left branch. The result is equivalent to:
    // > flatten doc | doc
    return Doc::choice(doc, doc);
}

namespace {

struct Node {
    const Doc *doc;
    bool flattening;

    Node(const Doc *doc, bool flattening) : doc{doc}, flattening{flattening} {}
};
}

class Fits final {
public:
    using Iterator = std::vector<Node>::const_reverse_iterator;

    const int width;
    int col;

    // Iterators from the DocRenderer to consume doc parts that trail a choice
    Iterator it;
    Iterator end;

    std::vector<Node> work;

    Fits(const int width, int col, Iterator it, Iterator end) : width{width}, col{col}, it{it}, end{end} {}

    bool advance() {
        if (this->it == this->end) {
            return false;
        }

        this->work.emplace_back(*this->it);
        ++this->it;

        return true;
    }

    bool check(const Doc *doc) {
        this->work.emplace_back(doc, true);

        do {
            while (!this->work.empty()) {
                auto node = this->work.back();
                this->work.pop_back();

                switch (node.doc->tag()) {
                case Doc::Tag::Nil:
                    break;

                case Doc::Tag::Line: {
                    if (node.flattening) {
                        this->col += 1;
                    } else {
                        return true;
                    }
                    break;
                }

                case Doc::Tag::Text: {
                    auto &text = node.doc->cast<Text>().text;
                    this->col += text.size();

                    if (this->col > this->width) {
                        return false;
                    }

                    break;
                }

                case Doc::Tag::Concat: {
                    auto &cat = node.doc->cast<Concat>();
                    this->work.emplace_back(&cat.right, node.flattening);
                    this->work.emplace_back(&cat.left, node.flattening);
                    break;
                }

                case Doc::Tag::Choice:
                    auto &choice = node.doc->cast<Choice>();
                    // TODO: figure out how to avoid allocating a whole extra `Fits` here
                    Fits nested{*this};
                    if (nested.check(&choice.left)) {
                        this->work.emplace_back(&choice.left, true);
                    } else {
                        this->work.emplace_back(&choice.right, node.flattening);
                    }
                    break;
                }
            }
        } while (this->advance());

        return true;
    }
};

class DocRenderer final {
public:
    const int width;
    Renderer &out;

    int col = 0;

    DocRenderer(const int width, Renderer &out) : width{width}, out{out} {}

    std::vector<Node> work{};

    void render(const Doc *doc);
};

void DocRenderer::render(const Doc *doc) {
    this->work.clear();
    this->work.emplace_back(doc, false);

    while (!this->work.empty()) {
        auto node = this->work.back();
        this->work.pop_back();

        switch (node.doc->tag()) {
        case Doc::Tag::Nil:
            break;

        case Doc::Tag::Line: {
            if (node.flattening) {
                this->col += 1;
                this->out.write(" ");
            } else {
                this->col = 0;
                this->out.line();
            }

            break;
        }

        case Doc::Tag::Text: {
            auto &text = node.doc->cast<Text>().text;
            this->col += text.size();
            this->out.write(text);
            break;
        }

        case Doc::Tag::Concat: {
            auto &cat = node.doc->cast<Concat>();
            this->work.emplace_back(&cat.right, node.flattening);
            this->work.emplace_back(&cat.left, node.flattening);
            break;
        }

        case Doc::Tag::Choice: {
            auto &choice = node.doc->cast<Choice>();

            Fits fit{this->width, this->col, this->work.rbegin(), this->work.rend()};
            if (fit.check(&choice.left)) {
                this->work.emplace_back(&choice.left, true);
            } else {
                this->work.emplace_back(&choice.right, node.flattening);
            }
            break;
        }
        }
    }
}

namespace {

// A simple renderer for collecting the output in a buffer.
struct StringRenderer final : public Renderer {
    std::string buffer;

    void line() override {
        this->buffer.push_back('\n');
    }

    void write(std::string_view sv) override {
        this->buffer.append(sv);
    }
};

} // namespace

void Doc::render(Renderer &out, int cols) const {
    DocRenderer r{cols, out};
    r.render(this);
}

std::string Doc::pretty(int cols) const {
    StringRenderer out;
    this->render(out, cols);
    return out.buffer;
}

} // namespace bembo
