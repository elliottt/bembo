#include <cstdint>
#include <cstring>
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
    bool flattening;
};

struct Nest final {
    Doc doc;
    int indent;
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

bool Doc::boxed() const {
    return this->value & 0x1;
}

void Doc::increment() {
    if (this->boxed()) {
        this->refs->fetch_add(1);
    }
}

bool Doc::decrement() {
    return this->refs->fetch_sub(1) <= 1;
}

void Doc::cleanup() {
    switch (this->tag()) {
    case Tag::Nil:
    case Tag::Line:
    case Tag::ShortText:
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

    case Tag::Nest:
        if (this->decrement()) {
            delete this->refs;
            delete static_cast<Nest *>(this->data());
        }
        return;
    }
}

Doc::~Doc() {
    this->cleanup();
}

Doc::Doc(const Doc &other) : refs{other.refs}, value{other.value} {
    if (this->boxed()) {
        this->increment();
    }
}

Doc &Doc::operator=(const Doc &other) {
    if (this == &other) {
        return *this;
    }

    if (this->boxed()) {
        this->cleanup();
    }

    this->refs = other.refs;
    this->value = other.value;

    if (this->boxed()) {
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

    if (this->boxed()) {
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

Doc Doc::choice(bool flattening, Doc left, Doc right) {
    return Doc{new std::atomic<int>(0), Tag::Choice, new Choice{std::move(left), std::move(right), flattening}};
}

// Construct a short text node. This assumes that the string is 8 chars or less.
Doc Doc::short_text(std::string_view text) {
    Doc res{Tag::ShortText};

    auto size = std::min(text.size(), 8ul);

    res.value |= static_cast<uint64_t>(size << 8);

    memcpy(res.short_text_data, text.data(), size);

    return res;
}

std::string_view Doc::get_short_text() const {
    auto size = (this->value >> 8) & 0xff;
    return std::string_view{this->short_text_data, size};
}

Doc::Doc() : Doc(Tag::Nil) {}

Doc Doc::nil() {
    return Doc{};
}

Doc Doc::line() {
    return Doc{Tag::Line};
}

Doc Doc::softline() {
    return Doc::choice(false, Doc::c(' '), Doc::line());
}

Doc Doc::c(char c) {
    return Doc::short_text(std::string_view{&c, 1});
}

Doc Doc::s(std::string str) {
    return Doc{new std::atomic<int>(0), Tag::Text, new Text{std::move(str)}};
}

Doc Doc::sv(std::string_view str) {
    if (str.size() <= 8) {
        return Doc::short_text(str);
    } else {
        return Doc{new std::atomic<int>(0), Tag::Text, new Text{std::string(str)}};
    }
}

Doc::Doc(Doc left, Doc right)
    : Doc{new std::atomic<int>(0), Tag::Concat, new Concat{std::move(left), std::move(right)}} {}

Doc Doc::append(Doc other) const {
    if (this->is_nil()) {
        return other;
    }

    if (other.is_nil()) {
        return *this;
    }

    return Doc{*this, std::move(other)};
}

Doc Doc::operator+(Doc other) const {
    return this->append(other);
}

Doc &Doc::operator+=(Doc other) {
    *this = Doc{std::move(*this), std::move(other)};
    return *this;
}

Doc Doc::operator<<(Doc other) const {
    return this->append(Doc::c(' ') + other);
}

Doc Doc::group(Doc doc) {
    // This looks odd but the behavior of choice is to always flatten the left branch. The result is equivalent to:
    // > flatten doc | doc
    return Doc::choice(true, doc, doc);
}

Doc Doc::nest(int indent, Doc doc) {
    return Doc{new std::atomic<int>(0), Tag::Nest, new Nest{std::move(doc), indent}};
}

namespace {

struct Node {
    const Doc *doc;
    int indent;
    bool flattening;

    Node(const Doc *doc, int indent, bool flattening) : doc{doc}, indent{indent}, flattening{flattening} {}
};
} // namespace

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

    // Check to see if a Doc will fit in he space remaining on a line.
    // NOTE: this check completely ignores indentation, as newlines terminate the check.
    bool check(const Doc *doc, bool flattening) {
        this->work.emplace_back(doc, 0, flattening);

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

                case Doc::Tag::ShortText: {
                    auto text = node.doc->get_short_text();
                    this->col += text.size();

                    if (this->col > this->width) {
                        return false;
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
                    this->work.emplace_back(&cat.right, 0, node.flattening);
                    this->work.emplace_back(&cat.left, 0, node.flattening);
                    break;
                }

                case Doc::Tag::Choice: {
                    auto &choice = node.doc->cast<Choice>();
                    // TODO: figure out how to avoid allocating a whole extra `Fits` here
                    Fits nested{*this};
                    if (nested.check(&choice.left, choice.flattening)) {
                        this->work.emplace_back(&choice.left, 0, true);
                    } else {
                        this->work.emplace_back(&choice.right, 0, node.flattening);
                    }
                    break;
                }

                case Doc::Tag::Nest: {
                    auto &nest = node.doc->cast<Nest>();
                    this->work.emplace_back(&nest.doc, 0, node.flattening);
                    break;
                }
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
    this->work.emplace_back(doc, 0, false);

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
                this->out.line(node.indent);
            }

            break;
        }

        case Doc::Tag::ShortText: {
            auto text = node.doc->get_short_text();
            this->col += text.size();
            this->out.write(text);
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
            this->work.emplace_back(&cat.right, node.indent, node.flattening);
            this->work.emplace_back(&cat.left, node.indent, node.flattening);
            break;
        }

        case Doc::Tag::Choice: {
            auto &choice = node.doc->cast<Choice>();
            if (node.flattening) {
                this->work.emplace_back(&choice.left, node.indent, true);
            } else {
                Fits fit{this->width, this->col, this->work.rbegin(), this->work.rend()};
                if (fit.check(&choice.left, choice.flattening)) {
                    this->work.emplace_back(&choice.left, node.indent, choice.flattening);
                } else {
                    this->work.emplace_back(&choice.right, node.indent, false);
                }
            }
            break;
        }

        case Doc::Tag::Nest: {
            auto &nest = node.doc->cast<Nest>();
            this->work.emplace_back(&nest.doc, node.indent + nest.indent, node.flattening);
            break;
        }
        }
    }
}

namespace {

// A simple renderer for collecting the output in a buffer.
struct StringRenderer final : public Renderer {
    std::string buffer;

    void line(int indent) override {
        this->buffer.push_back('\n');
        this->buffer.append(indent, ' ');
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
