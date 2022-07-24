#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "doc.h"

using namespace std::literals::string_view_literals;

namespace bembo {

namespace {

constexpr uint64_t TAG_MASK_BITS = 8;
constexpr uint64_t TAG_MASK = (1 << TAG_MASK_BITS) - 1;

constexpr uint64_t SIZE_MASK_BITS = 7;
constexpr uint64_t SIZE_MASK = ((1 << SIZE_MASK_BITS) - 1) << TAG_MASK_BITS;

constexpr uint64_t FLATTENED_MASK_BITS = 1;
constexpr uint64_t FLATTENED_MASK = ((1 << FLATTENED_MASK_BITS) - 1) << (TAG_MASK_BITS + SIZE_MASK_BITS);

constexpr int METADATA_BITS = TAG_MASK_BITS + SIZE_MASK_BITS + FLATTENED_MASK_BITS;

// The format of the Doc field is as follows:
//
// 0        8         16                                                    64
// +------------------------------------------------------------------------+
// |                 shared refcount, or inlined string data                |
// +--------+-------+-+-----------------------------------------------------+
// |  tag   |  size |f|         48 bits of pointer to heap object           |
// +--------+-------+-+-----------------------------------------------------+
//
// tag:       The `Tag` value for this doc, with odd tags indicating that the object is heap allocated.
// size:      The size of the inlined string case, invalid for all other tags.
// f:         Whether or not this doc has had `flatten` applied to it.
// pointer:   A pointer to the heap object whose shape is determined by `tag`.
//
// refcount/string data: either a pointer to an atomic refcount shared by heap allocated objects, or up to eight bytes
// of inlined string data.

uint64_t make_tagged(uint16_t tag, void *ptr) {
    return (reinterpret_cast<uint64_t>(ptr) << METADATA_BITS) | static_cast<uint64_t>(tag);
}

using Concat = std::vector<Doc>;
using Text = std::string;

// NOTE: the left side is always flattened implicitly, so lines will be interpreted as a single space.
struct Choice final {
    Doc left;
    Doc right;
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
    return reinterpret_cast<void *>(this->value >> METADATA_BITS);
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
    assert(this->boxed());
    this->refs->fetch_add(1);
}

bool Doc::decrement() {
    assert(this->boxed());
    auto count = this->refs->fetch_sub(1);
    return count == 1;
}

bool Doc::is_unique() const {
    assert(this->boxed());
    return this->refs->load() == 1;
}

bool Doc::is_flattened() const {
    return (this->value & FLATTENED_MASK) != 0;
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
Doc::Doc(Tag tag, void *ptr) : refs{new std::atomic<int>(1)}, value{make_tagged(static_cast<uint16_t>(tag), ptr)} {
    assert(this->boxed());
}

Doc Doc::choice(Doc left, Doc right) {
    return Doc{Tag::Choice, new Choice{std::move(left), std::move(right)}};
}

// Construct a short text node. This assumes that the string is 8 chars or less.
Doc Doc::short_text(std::string_view text) {
    Doc res(Tag::ShortText);

    auto size = text.size();
    assert(text.size() <= 8);

    res.value |= static_cast<uint64_t>(size << TAG_MASK_BITS) & SIZE_MASK;
    std::copy_n(text.begin(), size, res.short_text_data.begin());

    return res;
}

std::string_view Doc::get_short_text() const {
    auto size = (this->value & SIZE_MASK) >> TAG_MASK_BITS;
    return std::string_view{this->short_text_data.data(), size};
}

bool Doc::init_short_str(std::string_view str) {
    auto size = str.size();
    if (str.size() > 8) {
        return false;
    }

    this->value = static_cast<uint64_t>(Tag::ShortText) | static_cast<uint64_t>(size << TAG_MASK_BITS);
    std::copy_n(str.begin(), size, this->short_text_data.begin());

    return true;
}

void Doc::init_string(std::string str) {
    this->refs = new std::atomic<int>(1);
    this->value = make_tagged(static_cast<uint16_t>(Tag::Text), new Text{std::move(str)});
}

Doc::Doc() : Doc{Tag::Nil} {}

Doc::Doc(const char *str) : Doc{} {
    auto view = std::string_view(str, strlen(str));
    if (view.empty()) {
        return;
    }

    if (!this->init_short_str(view)) {
        this->init_string(std::string(view));
    }
}

Doc::Doc(std::string_view str) : Doc{} {
    if (str.empty()) {
        return;
    }

    if (!this->init_short_str(str)) {
        this->init_string(std::string(str));
    }
}

Doc Doc::nil() {
    return Doc{};
}

Doc Doc::line() {
    return Doc{Tag::Line};
}

Doc Doc::softline() {
    return Doc::choice(Doc::c(' '), Doc::line());
}

Doc Doc::softbreak() {
    return Doc::choice(Doc::nil(), Doc::line());
}

Doc Doc::c(char c) {
    return Doc::short_text(std::string_view{&c, 1});
}

Doc Doc::s(std::string str) {
    Doc res{};

    if (str.empty()) {
        return res;
    }

    if (!res.init_short_str(str)) {
        res.init_string(std::move(str));
    }

    return res;
}

Doc Doc::s(const char *str) {
    if (str == nullptr) {
        return Doc::nil();
    }

    auto len = strlen(str);
    if (len <= 8) {
        return Doc::short_text(std::string_view{str, len});
    }

    return Doc{Tag::Text, new Text{str, len}};
}

Doc Doc::sv(std::string_view str) {
    if (str.empty()) {
        return Doc::nil();
    }

    if (str.size() <= 8) {
        return Doc::short_text(str);
    }

    return Doc{Tag::Text, new Text{str}};
}

Doc Doc::operator+(Doc other) const {
    return Doc::concat(*this, std::move(other));
}

Doc &Doc::append(Doc other) {
    if (other.is_nil()) {
        return *this;
    }

    switch (this->tag()) {
    case Tag::Nil:
        *this = std::move(other);
        break;

    case Tag::Concat:
        if (this->is_unique()) {
            this->cast<Concat>().emplace_back(std::move(other));
            break;
        }

        [[fallthrough]];
    default:
        *this = Doc::concat(*this, std::move(other));
        break;
    }

    return *this;
}

Doc &Doc::operator+=(Doc other) {
    return this->append(std::move(other));
}

Doc Doc::operator<<(Doc other) const {
    return *this + Doc::c(' ') + std::move(other);
}

Doc &Doc::operator<<=(Doc other) {
    this->append(Doc::c(' '));
    this->append(std::move(other));
    return *this;
}

Doc Doc::operator/(Doc other) const {
    return Doc::concat(*this, Doc::line(), std::move(other));
}

Doc &Doc::operator/=(Doc other) {
    *this = Doc::concat(*this, Doc::line(), std::move(other));
    return *this;
}

Doc Doc::group(Doc doc) {
    auto flattened = Doc::flatten(doc);
    return Doc::choice(std::move(flattened), std::move(doc));
}

Doc &Doc::flatten() {
    this->value |= FLATTENED_MASK;
    return *this;
}

Doc Doc::flatten(Doc doc) {
    Doc copy = doc;
    copy.flatten();
    return copy;
}

Doc Doc::nest(int indent, Doc doc) {
    return Doc{Tag::Nest, new Nest{std::move(doc), indent}};
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
        this->work.emplace_back(doc, 0, flattening || doc->is_flattened());

        auto push = [this](Node &parent, const Doc *doc) {
            this->work.emplace_back(doc, 0, parent.flattening || doc->is_flattened());
        };

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
                    auto &text = node.doc->cast<Text>();
                    this->col += text.size();

                    if (this->col > this->width) {
                        return false;
                    }

                    break;
                }

                case Doc::Tag::Concat: {
                    auto &cat = node.doc->cast<Concat>();
                    for (auto it = cat.rbegin(); it != cat.rend(); ++it) {
                        push(node, &*it);
                    }
                    break;
                }

                case Doc::Tag::Choice: {
                    auto &choice = node.doc->cast<Choice>();
                    // TODO: figure out how to avoid allocating a whole extra `Fits` here
                    Fits nested{*this};
                    if (nested.check(&choice.left, node.flattening || choice.left.is_flattened())) {
                        push(node, &choice.left);
                    } else {
                        push(node, &choice.right);
                    }
                    break;
                }

                case Doc::Tag::Nest: {
                    auto &nest = node.doc->cast<Nest>();
                    push(node, &nest.doc);
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
    Writer &out;

    int col = 0;

    DocRenderer(const int width, Writer &out) : width{width}, out{out} {}

    std::vector<Node> work{};

    void render(const Doc *doc);
};

void DocRenderer::render(const Doc *doc) {
    this->work.clear();
    this->work.emplace_back(doc, 0, doc->is_flattened());

    auto push = [this](Node &parent, const Doc *doc) -> Node & {
        return this->work.emplace_back(doc, parent.indent, parent.flattening || doc->is_flattened());
    };

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
            auto &text = node.doc->cast<Text>();
            this->col += text.size();
            this->out.write(text);
            break;
        }

        case Doc::Tag::Concat: {
            auto &cat = node.doc->cast<Concat>();
            for (auto it = cat.rbegin(); it != cat.rend(); ++it) {
                push(node, &*it);
            }
            break;
        }

        case Doc::Tag::Choice: {
            auto &choice = node.doc->cast<Choice>();
            if (node.flattening) {
                this->work.emplace_back(&choice.left, node.indent, true);
            } else {
                Fits fit{this->width, this->col, this->work.rbegin(), this->work.rend()};
                if (fit.check(&choice.left, node.flattening)) {
                    push(node, &choice.left);
                } else {
                    push(node, &choice.right);
                }
            }
            break;
        }

        case Doc::Tag::Nest: {
            auto &nest = node.doc->cast<Nest>();
            auto &next = push(node, &nest.doc);
            next.indent += nest.indent;
            break;
        }
        }
    }
}

StreamWriter::StreamWriter(std::ostream &out) : out{out} {}

void StreamWriter::line(int indent) {
    this->out << std::endl;

    for (int i = 0; i < indent; i++) {
        this->out.put(' ');
    }
}

void StreamWriter::write(std::string_view sv) {
    this->out << sv;
}

StringWriter::StringWriter() : buffer{} {}

void StringWriter::line(int indent) {
    this->buffer.push_back('\n');
    this->buffer.append(indent, ' ');
}

void StringWriter::write(std::string_view sv) {
    this->buffer.append(sv);
}

void Doc::render(Writer &out, int cols) const {
    DocRenderer r{cols, out};
    r.render(this);
}

std::string Doc::pretty(int cols) const {
    StringWriter out;
    this->render(out, cols);
    return out.buffer;
}

Doc Doc::angles(Doc doc) {
    return Doc::concat(Doc::c('<'), std::move(doc), Doc::c('>'));
}

Doc Doc::braces(Doc doc) {
    return Doc::concat(Doc::c('{'), std::move(doc), Doc::c('}'));
}

Doc Doc::brackets(Doc doc) {
    return Doc::concat(Doc::c('['), std::move(doc), Doc::c(']'));
}

Doc Doc::quotes(Doc doc) {
    return Doc::concat(Doc::c('\''), std::move(doc), Doc::c('\''));
}

Doc Doc::dquotes(Doc doc) {
    return Doc::concat(Doc::c('"'), std::move(doc), Doc::c('"'));
}

Doc Doc::parens(Doc doc) {
    return Doc::concat(Doc::c('('), std::move(doc), Doc::c(')'));
}

} // namespace bembo
