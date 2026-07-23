#ifndef CTHTML__VALUE__HPP
#define CTHTML__VALUE__HPP

#include "treebuild.hpp" // classification + entity/text helpers (also pulls types, bind, entities)
#ifndef CTHTML_IN_A_MODULE
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#endif

// A VALUE document, parsed from a std::string_view.
//
// The compile-time entry, cthtml::parse<Src>(), needs its source as a
// template argument and hands back a TYPE. This is its sibling for
// source known only as a value: cthtml::parse(sv) runs the SAME HTML5
// tree construction over an owned tree of std::string/std::vector - and
// because those are constexpr on this toolchain, one function serves
// runtime strings AND still folds inside a static_assert:
//
//   std::string_view html = load();          // runtime-only
//   cthtml::document d = cthtml::parse(html);
//   d.body()["ul"].count("li");
//   for (auto a : d.query_all("#nav > li > a")) { use(a.attribute("href")); }
//
//   static_assert(cthtml::parse("<p>hi</p>").body().text() == "hi");
//
// It reproduces cthtml::parse<>()'s result exactly - implied
// html>(head,body), void elements, optional end tags, raw-text and
// RCDATA elements, character references, and the SAME author-mistake
// rejection (a document that parse<>() would refuse leaves ok()==false
// with the reason on error()). The one difference is surface: a mistake
// is a value here (ok()/error()), not a compile error.

namespace cthtml {

// --- the owned tree

CTLL_EXPORT struct dom_attribute {
	std::string name;  // canonical lowercase
	std::string value; // character references decoded; "" for a boolean attribute
};

CTLL_EXPORT struct dom_node {
	kind type = kind::element;
	std::string name;                     // element tag (lowercase); empty for text
	std::string content;                  // text-node bytes (decoded); empty for element
	std::vector<dom_attribute> attributes;
	std::vector<std::size_t> children;         // indices into document::nodes()
	std::size_t parent = static_cast<std::size_t>(-1);
};

class document;

namespace detail {
constexpr bool class_list_has(std::string_view list, std::string_view cls) noexcept {
	std::size_t i = 0;
	while (i < list.size()) {
		while (i < list.size() && is_html_blank(list[i])) { ++i; }
		const std::size_t start = i;
		while (i < list.size() && !is_html_blank(list[i])) { ++i; }
		if (i > start && list.substr(start, i - start) == cls) { return true; }
	}
	return false;
}
} // namespace detail

// A lightweight handle into a document - the value analogue of
// node_view, but able to walk to parents and run selectors. Copyable
// and cheap; valid only while its document lives.
CTLL_EXPORT class node {
public:
	constexpr node() noexcept = default;
	constexpr node(const document * doc, std::size_t index) noexcept : doc_(doc), index_(index) { }

	constexpr bool valid() const noexcept { return doc_ != nullptr; }
	constexpr explicit operator bool() const noexcept { return valid(); }
	constexpr std::size_t index() const noexcept { return index_; }
	constexpr const document * document_ptr() const noexcept { return doc_; }

	constexpr kind type() const noexcept;
	constexpr bool is_element() const noexcept { return valid() && type() == kind::element; }
	constexpr bool is_text() const noexcept { return valid() && type() == kind::text; }

	// element tag (empty for a text node)
	constexpr std::string_view name() const noexcept;
	// a text node's bytes, or an element's concatenated direct text
	// children ("<a>x<b/>y</a>" -> "xy") - matching element::text()
	constexpr std::string text() const noexcept;

	// --- children (element and non-blank text nodes, in order)
	constexpr std::size_t child_count() const noexcept;
	constexpr bool empty() const noexcept { return child_count() == 0; }
	constexpr node child(std::size_t i) const noexcept;
	constexpr node operator[](std::size_t i) const noexcept { return child(i); }
	// the first child element with this tag (invalid node if none)
	constexpr node operator[](std::string_view tag) const noexcept;
	constexpr bool contains(std::string_view tag) const noexcept;
	constexpr std::size_t count(std::string_view tag) const noexcept;

	// --- attributes
	constexpr std::size_t attribute_count() const noexcept;
	constexpr bool has_attribute(std::string_view name) const noexcept;
	constexpr std::string_view attribute(std::string_view name) const noexcept;
	constexpr const std::vector<dom_attribute> & attributes() const noexcept;

	// --- navigation
	constexpr node parent() const noexcept;

	// --- selectors (CSS subset: tag, #id, .class, [attr], [attr=val],
	// '*', descendant (space) and child ('>') combinators), matched over
	// this element's descendants
	constexpr node query(std::string_view selector) const noexcept;
	constexpr std::vector<node> query_all(std::string_view selector) const noexcept;

	constexpr bool operator==(const node & o) const noexcept {
		return doc_ == o.doc_ && index_ == o.index_;
	}

	// range-for over children
	class iterator {
	public:
		constexpr iterator(const document * d, const std::size_t * p) noexcept : doc_(d), p_(p) { }
		constexpr node operator*() const noexcept { return node{doc_, *p_}; }
		constexpr iterator & operator++() noexcept { ++p_; return *this; }
		constexpr bool operator!=(const iterator & o) const noexcept { return p_ != o.p_; }
	private:
		const document * doc_;
		const std::size_t * p_;
	};
	constexpr iterator begin() const noexcept;
	constexpr iterator end() const noexcept;

private:
	const document * doc_ = nullptr;
	std::size_t index_ = 0;
};

CTLL_EXPORT class document {
public:
	document() = default;

	// well-formedness mirrors cthtml::is_valid<>: the input both lexes/
	// parses AND passes tree construction. A rejected document still
	// exposes whatever tree was built (the builder is total).
	constexpr bool ok() const noexcept { return wellformed_ && reason_ == bind_reason::none; }
	// the tree-construction failure, when ok() is false because of one
	// (a stray/crossing close tag, duplicate attribute, self-closed
	// non-void); none when the failure was purely lexical, or ok()
	constexpr bind_reason error() const noexcept { return reason_; }
	// the offending token as written (empty when ok() or on a lex error)
	constexpr std::string_view error_where() const noexcept { return where_; }

	constexpr node root() const noexcept { return node{this, root_}; }
	constexpr node head() const noexcept { return root()["head"]; }
	constexpr node body() const noexcept { return root()["body"]; }
	constexpr std::string_view title() const noexcept {
		const node t = head()["title"];
		return t.valid() ? title_ : std::string_view{};
	}

	// document-wide selector search (over the whole tree)
	constexpr node query(std::string_view selector) const noexcept { return root().query(selector); }
	constexpr std::vector<node> query_all(std::string_view selector) const noexcept {
		return root().query_all(selector);
	}
	constexpr node get_element_by_id(std::string_view id) const noexcept;

	// arena access (used by node)
	constexpr const dom_node & at(std::size_t i) const noexcept { return nodes_[i]; }
	constexpr std::size_t size() const noexcept { return nodes_.size(); }
	constexpr const std::vector<dom_node> & nodes() const noexcept { return nodes_; }

	// internals filled by the parser
	std::vector<dom_node> nodes_;
	std::size_t root_ = 0;
	bool wellformed_ = true;
	bind_reason reason_ = bind_reason::none;
	std::string where_;
	std::string title_; // cached <title> text
};

// ============================ node methods ============================

constexpr kind node::type() const noexcept { return doc_->at(index_).type; }
constexpr std::string_view node::name() const noexcept { return doc_->at(index_).name; }
constexpr std::size_t node::child_count() const noexcept { return doc_->at(index_).children.size(); }
constexpr std::size_t node::attribute_count() const noexcept { return doc_->at(index_).attributes.size(); }
constexpr const std::vector<dom_attribute> & node::attributes() const noexcept {
	return doc_->at(index_).attributes;
}
constexpr node::iterator node::begin() const noexcept {
	const auto & c = doc_->at(index_).children;
	return iterator{doc_, c.data()};
}
constexpr node::iterator node::end() const noexcept {
	const auto & c = doc_->at(index_).children;
	return iterator{doc_, c.data() + c.size()};
}

constexpr std::string node::text() const noexcept {
	const dom_node & n = doc_->at(index_);
	if (n.type == kind::text) { return n.content; }
	std::string out;
	for (const std::size_t ci : n.children) {
		const dom_node & c = doc_->at(ci);
		if (c.type == kind::text) { out += c.content; }
	}
	return out;
}

constexpr node node::child(std::size_t i) const noexcept {
	const auto & c = doc_->at(index_).children;
	return i < c.size() ? node{doc_, c[i]} : node{};
}
constexpr node node::operator[](std::string_view tag) const noexcept {
	for (const std::size_t ci : doc_->at(index_).children) {
		const dom_node & c = doc_->at(ci);
		if (c.type == kind::element && detail::ascii_iequals(c.name, tag)) { return node{doc_, ci}; }
	}
	return node{};
}
constexpr bool node::contains(std::string_view tag) const noexcept {
	return (*this)[tag].valid();
}
constexpr std::size_t node::count(std::string_view tag) const noexcept {
	std::size_t n = 0;
	for (const std::size_t ci : doc_->at(index_).children) {
		const dom_node & c = doc_->at(ci);
		n += (c.type == kind::element && detail::ascii_iequals(c.name, tag));
	}
	return n;
}
constexpr bool node::has_attribute(std::string_view nm) const noexcept {
	for (const dom_attribute & a : doc_->at(index_).attributes) {
		if (detail::ascii_iequals(a.name, nm)) { return true; }
	}
	return false;
}
constexpr std::string_view node::attribute(std::string_view nm) const noexcept {
	for (const dom_attribute & a : doc_->at(index_).attributes) {
		if (detail::ascii_iequals(a.name, nm)) { return a.value; }
	}
	return {};
}
constexpr node node::parent() const noexcept {
	const std::size_t p = doc_->at(index_).parent;
	return p == static_cast<std::size_t>(-1) ? node{} : node{doc_, p};
}

// --- selector engine

namespace detail {

struct sel_compound {
	std::string_view tag;                                    // "" or "*" = any
	std::string_view id;                                     // "" = none
	std::vector<std::string_view> classes;
	std::vector<std::pair<std::string_view, std::string_view>> attrs; // value "" via has-only
	std::vector<bool> attr_has_value;
};
struct sel_step {
	sel_compound comp;
	int combinator = 0; // link to PREVIOUS step: 0 = first, 1 = descendant, 2 = child
};

// parse "ul > li.item#x[data-k=v] a" into steps
constexpr std::vector<sel_step> parse_selector(std::string_view s) noexcept {
	std::vector<sel_step> steps;
	std::size_t i = 0;
	int pending_comb = 0; // for the next compound
	auto skip_ws_track = [&]() {
		bool saw = false;
		while (i < s.size() && is_html_blank(s[i])) { ++i; saw = true; }
		return saw;
	};
	while (i < s.size()) {
		const bool saw_ws = skip_ws_track();
		if (i >= s.size()) { break; }
		if (s[i] == '>') {
			pending_comb = 2;
			++i;
			continue;
		}
		if (saw_ws && !steps.empty() && pending_comb == 0) { pending_comb = 1; }
		// read one compound
		sel_compound c;
		bool any = false;
		while (i < s.size() && !is_html_blank(s[i]) && s[i] != '>') {
			const char ch = s[i];
			if (ch == '#') {
				++i;
				const std::size_t st = i;
				while (i < s.size() && !is_html_blank(s[i]) &&
				       s[i] != '#' && s[i] != '.' && s[i] != '[' && s[i] != '>') { ++i; }
				c.id = s.substr(st, i - st);
				any = true;
			} else if (ch == '.') {
				++i;
				const std::size_t st = i;
				while (i < s.size() && !is_html_blank(s[i]) &&
				       s[i] != '#' && s[i] != '.' && s[i] != '[' && s[i] != '>') { ++i; }
				c.classes.push_back(s.substr(st, i - st));
				any = true;
			} else if (ch == '[') {
				++i;
				const std::size_t st = i;
				while (i < s.size() && s[i] != '=' && s[i] != ']') { ++i; }
				std::string_view an = s.substr(st, i - st);
				std::string_view av;
				bool has_v = false;
				if (i < s.size() && s[i] == '=') {
					++i;
					has_v = true;
					if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
						const char q = s[i++];
						const std::size_t vs = i;
						while (i < s.size() && s[i] != q) { ++i; }
						av = s.substr(vs, i - vs);
						if (i < s.size()) { ++i; }
					} else {
						const std::size_t vs = i;
						while (i < s.size() && s[i] != ']') { ++i; }
						av = s.substr(vs, i - vs);
					}
				}
				if (i < s.size() && s[i] == ']') { ++i; }
				c.attrs.emplace_back(an, av);
				c.attr_has_value.push_back(has_v);
				any = true;
			} else { // tag or '*'
				const std::size_t st = i;
				while (i < s.size() && !is_html_blank(s[i]) &&
				       s[i] != '#' && s[i] != '.' && s[i] != '[' && s[i] != '>') { ++i; }
				c.tag = s.substr(st, i - st);
				any = true;
			}
		}
		if (any) {
			steps.push_back(sel_step{std::move(c), steps.empty() ? 0 : pending_comb});
			pending_comb = 0;
		}
	}
	return steps;
}

constexpr bool matches_compound(const document & d, std::size_t idx, const sel_compound & c) noexcept {
	const dom_node & n = d.at(idx);
	if (n.type != kind::element) { return false; }
	if (!c.tag.empty() && c.tag != "*" && !ascii_iequals(n.name, c.tag)) { return false; }
	const node h{&d, idx};
	if (!c.id.empty() && h.attribute("id") != c.id) { return false; }
	for (const std::string_view cls : c.classes) {
		if (!class_list_has(h.attribute("class"), cls)) { return false; }
	}
	for (std::size_t k = 0; k < c.attrs.size(); ++k) {
		if (!h.has_attribute(c.attrs[k].first)) { return false; }
		if (c.attr_has_value[k] && h.attribute(c.attrs[k].first) != c.attrs[k].second) { return false; }
	}
	return true;
}

// does the chain steps[0..=k] match, ending at element idx?
constexpr bool matches_chain(const document & d, std::size_t idx,
                             const std::vector<sel_step> & steps, std::size_t k) noexcept {
	if (!matches_compound(d, idx, steps[k].comp)) { return false; }
	if (k == 0) { return true; }
	const int comb = steps[k].combinator;
	std::size_t p = d.at(idx).parent;
	if (comb == 2) { // child: immediate parent
		return p != static_cast<std::size_t>(-1) && matches_chain(d, p, steps, k - 1);
	}
	// descendant: any ancestor
	while (p != static_cast<std::size_t>(-1)) {
		if (matches_chain(d, p, steps, k - 1)) { return true; }
		p = d.at(p).parent;
	}
	return false;
}

// pre-order DFS collecting matches strictly below `rootIdx`
constexpr void query_dfs(const document & d, std::size_t idx, const std::vector<sel_step> & steps,
                         std::vector<node> & out, bool stop_first, node & first) noexcept {
	for (const std::size_t ci : d.at(idx).children) {
		if (d.at(ci).type == kind::element) {
			if (!steps.empty() && matches_chain(d, ci, steps, steps.size() - 1)) {
				if (stop_first) {
					if (!first.valid()) { first = node{&d, ci}; }
					if (first.valid()) { return; }
				} else {
					out.push_back(node{&d, ci});
				}
			}
			if (stop_first && first.valid()) { return; }
			query_dfs(d, ci, steps, out, stop_first, first);
			if (stop_first && first.valid()) { return; }
		}
	}
}

} // namespace detail

constexpr node node::query(std::string_view selector) const noexcept {
	const auto steps = detail::parse_selector(selector);
	std::vector<node> unused;
	node first;
	if (!steps.empty()) { detail::query_dfs(*doc_, index_, steps, unused, true, first); }
	return first;
}
constexpr std::vector<node> node::query_all(std::string_view selector) const noexcept {
	const auto steps = detail::parse_selector(selector);
	std::vector<node> out;
	node ignore;
	if (!steps.empty()) { detail::query_dfs(*doc_, index_, steps, out, false, ignore); }
	return out;
}

constexpr node document::get_element_by_id(std::string_view id) const noexcept {
	for (std::size_t i = 0; i < nodes_.size(); ++i) {
		const dom_node & n = nodes_[i];
		if (n.type != kind::element) { continue; }
		for (const dom_attribute & a : n.attributes) {
			if (detail::ascii_iequals(a.name, "id") && a.value == id) { return node{this, i}; }
		}
	}
	return node{};
}

// ============================ the parser ============================

namespace detail {

// character references decoded into an owned string (the value twin of
// decode_entities; same HTML5 rules: named refs, numeric refs,
// windows-1252 C1 remap, U+FFFD for NUL/surrogates/out-of-range)
inline constexpr char32_t value_win1252[32] = {
    0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
    0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};

constexpr void value_put_utf8(std::string & o, char32_t cp) noexcept {
	if (cp < 0x80) {
		o.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}
constexpr void value_put_numeric(std::string & o, char32_t cp) noexcept {
	if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
		cp = 0xFFFD;
	} else if (cp >= 0x80 && cp <= 0x9F) {
		cp = value_win1252[cp - 0x80];
	}
	value_put_utf8(o, cp);
}
// decode raw[From..raw.size()-To) into out
constexpr void decode_into(std::string & out, std::string_view raw, std::size_t from, std::size_t to) noexcept {
	std::size_t i = from;
	const std::size_t end = raw.size() - to;
	while (i < end) {
		const char c = raw[i];
		if (c != '&') { out.push_back(c); ++i; continue; }
		if (i + 1 < end && raw[i + 1] == '#') {
			const bool hex = i + 2 < end && (raw[i + 2] == 'x' || raw[i + 2] == 'X');
			std::size_t j = i + (hex ? 3 : 2);
			char32_t cp = 0;
			std::size_t digits = 0;
			while (j < end && (hex ? is_ascii_hex(raw[j]) : is_ascii_digit(raw[j]))) {
				if (cp <= 0x110000) {
					cp = hex ? cp * 16 + static_cast<char32_t>(bind_hexval(raw[j]))
					         : cp * 10 + static_cast<char32_t>(raw[j] - '0');
				}
				++j;
				++digits;
			}
			if (digits > 0 && j < end && raw[j] == ';') {
				value_put_numeric(out, cp);
				i = j + 1;
				continue;
			}
			out.push_back('&');
			++i;
			continue;
		}
		std::size_t j = i + 1;
		while (j < end && is_ascii_alnum(raw[j])) { ++j; }
		if (j > i + 1 && j < end && raw[j] == ';') {
			const entity_ref * e = find_entity(raw.substr(i + 1, j - i - 1));
			if (e != nullptr) {
				value_put_utf8(out, e->first);
				if (e->second != 0) { value_put_utf8(out, e->second); }
				i = j + 1;
				continue;
			}
		}
		out.push_back('&');
		++i;
	}
}

// --- the token stream (value twin of the flat chunk stream)

struct value_chunk {
	int kind = ck_none;                   // ck_open/ck_self/ck_close/ck_text/ck_raw
	std::string name;                     // tag (lowercased) for open/self/close/raw
	std::vector<dom_attribute> attrs;     // for open/self/raw
	std::string text;                     // decoded text (ck_text) or body (ck_raw)
	std::string raw;                      // token span as written, for error reporting
	bind_reason local = bind_reason::none;
	std::string local_where;
};

constexpr bool is_name_start(char c) noexcept {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
constexpr bool is_tag_char(char c) noexcept {
	return is_ascii_alnum(c) || c == ':' || c == '.' || c == '_' || c == '-';
}
constexpr bool ci_starts_with(std::string_view s, std::size_t i, std::string_view lit) noexcept {
	if (i + lit.size() > s.size()) { return false; }
	for (std::size_t k = 0; k < lit.size(); ++k) {
		if (ascii_lower(s[i + k]) != lit[k]) { return false; }
	}
	return true;
}
constexpr bool is_rcdata_tag(std::string_view t) noexcept {
	return ascii_iequals(t, "title") || ascii_iequals(t, "textarea");
}

// find the matching close of a raw-text/RCDATA element: "</name" (ci)
// then optional ASCII whitespace then ">". Returns npos if unclosed.
constexpr std::size_t find_raw_close(std::string_view s, std::size_t from, std::string_view name) noexcept {
	for (std::size_t i = from; i + 2 <= s.size(); ++i) {
		if (s[i] != '<' || i + 1 >= s.size() || s[i + 1] != '/') { continue; }
		std::size_t k = i + 2;
		if (!ci_starts_with(s, k, name)) { continue; }
		k += name.size();
		// only ASCII whitespace may sit between the name and '>' - anything
		// else ("</scriptx>") means this is not the matching close
		while (k < s.size() && is_html_blank(s[k])) { ++k; }
		if (k < s.size() && s[k] == '>' && (k == i + 2 + name.size() || is_html_blank(s[i + 2 + name.size()]))) {
			return i; // i = start of "</name...>"
		}
	}
	return std::string_view::npos;
}

// scan attributes starting at s[i] (i just past the tag name); collect
// into `attrs`, advance i, set self-closing. Returns false on a lexical
// mistake (a bare '/', an unterminated tag) or true with i past '>'.
constexpr bool scan_attributes(std::string_view s, std::size_t & i, std::vector<dom_attribute> & attrs,
                               bool & self_close, bool allow_self) noexcept {
	self_close = false;
	while (true) {
		while (i < s.size() && is_html_blank(s[i])) { ++i; }
		if (i >= s.size()) { return false; }
		if (s[i] == '>') { ++i; return true; }
		if (s[i] == '/') {
			if (allow_self && i + 1 < s.size() && s[i + 1] == '>') {
				self_close = true;
				i += 2;
				return true;
			}
			return false; // a stray '/' is not lexable HTML here
		}
		// attribute name: NAME = [^ \t\n\r\f\/>="'<]+
		const std::size_t ns = i;
		while (i < s.size() && !is_html_blank(s[i]) && s[i] != '/' && s[i] != '>' &&
		       s[i] != '=' && s[i] != '"' && s[i] != '\'' && s[i] != '<') { ++i; }
		if (i == ns) { return false; }
		dom_attribute a;
		a.name.reserve(i - ns);
		for (std::size_t k = ns; k < i; ++k) { a.name.push_back(ascii_lower(s[k])); }
		std::size_t save = i;
		while (save < s.size() && is_html_blank(s[save])) { ++save; }
		if (save < s.size() && s[save] == '=') {
			i = save + 1;
			while (i < s.size() && is_html_blank(s[i])) { ++i; }
			if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
				const char q = s[i++];
				const std::size_t vs = i;
				while (i < s.size() && s[i] != q) { ++i; }
				if (i >= s.size()) { return false; } // unterminated quote
				decode_into(a.value, s.substr(vs, i - vs), 0, 0);
				++i; // past closing quote
			} else {
				// UNQVAL = [^ \t\n\r\f"'=<>`]+
				const std::size_t vs = i;
				while (i < s.size() && !is_html_blank(s[i]) && s[i] != '"' && s[i] != '\'' &&
				       s[i] != '=' && s[i] != '<' && s[i] != '>' && s[i] != '`') { ++i; }
				if (i == vs) { return false; }
				decode_into(a.value, s.substr(vs, i - vs), 0, 0);
			}
		}
		attrs.push_back(std::move(a));
	}
}

constexpr bind_error_t value_dup_attr(const std::vector<dom_attribute> & attrs) noexcept {
	for (std::size_t a = 0; a < attrs.size(); ++a) {
		for (std::size_t b = a + 1; b < attrs.size(); ++b) {
			if (ascii_iequals(attrs[a].name, attrs[b].name)) {
				return bind_error_t{bind_reason::duplicate_attribute, attrs[b].name};
			}
		}
	}
	return bind_error_t{};
}

// tokenize the whole input; returns false on a lexical error (a stray
// '<', an unterminated tag/comment/raw element)
constexpr bool scan_chunks(std::string_view s, std::vector<value_chunk> & out) noexcept {
	std::size_t i = 0;
	const std::size_t n = s.size();
	while (i < n) {
		if (s[i] != '<') {
			const std::size_t start = i;
			while (i < n && s[i] != '<') { ++i; }
			value_chunk c;
			c.kind = ck_text;
			c.raw = std::string{s.substr(start, i - start)};
			decode_into(c.text, s.substr(start, i - start), 0, 0);
			out.push_back(std::move(c));
			continue;
		}
		// markup
		if (ci_starts_with(s, i, "<!--")) {
			const std::size_t e = s.find("-->", i + 4);
			if (e == std::string_view::npos) { return false; }
			i = e + 3;
			continue;
		}
		if (ci_starts_with(s, i, "<!doctype")) {
			const std::size_t e = s.find('>', i);
			if (e == std::string_view::npos) { return false; }
			i = e + 1;
			continue;
		}
		if (ci_starts_with(s, i, "<![cdata[")) {
			const std::size_t e = s.find("]]>", i);
			if (e == std::string_view::npos) { return false; }
			i = e + 3;
			continue;
		}
		if (i + 1 < n && s[i + 1] == '/') { // close tag
			std::size_t k = i + 2;
			if (k >= n || !is_name_start(s[k])) { return false; }
			const std::size_t ns = k;
			while (k < n && is_tag_char(s[k])) { ++k; }
			std::string name;
			for (std::size_t x = ns; x < k; ++x) { name.push_back(ascii_lower(s[x])); }
			while (k < n && is_html_blank(s[k])) { ++k; }
			if (k >= n || s[k] != '>') { return false; }
			value_chunk c;
			c.kind = ck_close;
			c.name = std::move(name);
			c.raw = std::string{s.substr(i, k - i + 1)};
			out.push_back(std::move(c));
			i = k + 1;
			continue;
		}
		if (i + 1 < n && is_name_start(s[i + 1])) { // open / raw-text element
			const std::size_t tag_start = i + 1;
			std::size_t k = tag_start;
			while (k < n && is_tag_char(s[k])) { ++k; }
			std::string name;
			for (std::size_t x = tag_start; x < k; ++x) { name.push_back(ascii_lower(s[x])); }
			const bool raw = is_raw_text_tag(name);     // script/style
			const bool rc = is_rcdata_tag(name);        // title/textarea
			std::vector<dom_attribute> attrs;
			bool self = false;
			std::size_t after = k;
			if (!scan_attributes(s, after, attrs, self, !(raw || rc))) { return false; }
			const std::string raw_open{s.substr(i, after - i)};
			if (raw || rc) {
				// body up to the matching close; unclosed = lex error
				const std::size_t close = find_raw_close(s, after, name);
				if (close == std::string_view::npos) { return false; }
				value_chunk c;
				c.kind = ck_raw;
				c.name = std::move(name);
				c.attrs = std::move(attrs);
				c.raw = raw_open;
				const bind_error_t d = value_dup_attr(c.attrs);
				c.local = d.reason;
				c.local_where = std::string{d.where};
				std::string_view body = s.substr(after, close - after);
				if (raw) {
					c.text = std::string{body}; // verbatim
				} else {                        // RCDATA: decode; textarea drops one leading NL
					std::size_t drop = 0;
					if (ascii_iequals(c.name, "textarea")) {
						if (body.size() >= 2 && body[0] == '\r' && body[1] == '\n') { drop = 2; }
						else if (!body.empty() && (body[0] == '\n' || body[0] == '\r')) { drop = 1; }
					}
					decode_into(c.text, body, drop, 0);
				}
				out.push_back(std::move(c));
				// advance past "</name ...>"
				std::size_t z = close + 2 + out.back().name.size();
				while (z < n && is_html_blank(s[z])) { ++z; }
				i = (z < n && s[z] == '>') ? z + 1 : close; // close always has '>'
				continue;
			}
			value_chunk c;
			c.kind = self ? ck_self : ck_open;
			c.name = std::move(name);
			c.attrs = std::move(attrs);
			c.raw = raw_open;
			const bind_error_t d = value_dup_attr(c.attrs);
			c.local = d.reason;
			c.local_where = std::string{d.where};
			out.push_back(std::move(c));
			i = after;
			continue;
		}
		return false; // a '<' that begins no valid markup
	}
	return true;
}

// --- the validator (value twin of validate_chunks)

constexpr bool value_validate(const std::vector<value_chunk> & cs, bind_reason & reason,
                              std::string & where) noexcept {
	std::vector<std::string_view> stack;
	int mode = 0;
	auto fail = [&](bind_reason r, std::string_view w) {
		reason = r;
		where = std::string{w};
		return false;
	};
	for (const value_chunk & c : cs) {
		if (c.local != bind_reason::none) { return fail(c.local, c.local_where); }
		if (c.kind == ck_text) {
			if (mode == 0 && stack.empty() && !span_blank(c.raw)) { mode = 1; }
			continue;
		}
		if (c.kind == ck_open || c.kind == ck_self || c.kind == ck_raw) {
			const std::string_view t = c.name;
			if (tag_in(t, {"body", "head", "html"})) {
				if (c.kind == ck_self) { return fail(bind_reason::self_closing_non_void, c.raw); }
				if (ascii_iequals(t, "body")) { mode = 1; }
				continue;
			}
			if (c.kind == ck_self && !is_void_tag(t)) {
				return fail(bind_reason::self_closing_non_void, c.raw);
			}
			while (!stack.empty() && closed_by(stack.back(), t)) { stack.pop_back(); }
			if (mode == 0 && stack.empty() && !is_metadata_tag(t)) { mode = 1; }
			if (c.kind == ck_open && !is_void_tag(t)) {
				if (stack.size() == tb_depth_cap) { return fail(bind_reason::depth_overflow, c.raw); }
				stack.push_back(t);
			}
			continue;
		}
		// ck_close
		const std::string_view t = c.name;
		if (ascii_iequals(t, "body") || ascii_iequals(t, "html")) {
			for (const std::string_view open : stack) {
				if (!end_omissible(open)) { return fail(bind_reason::mismatched_tag, c.raw); }
			}
			stack.clear();
			mode = 1;
			continue;
		}
		if (ascii_iequals(t, "head")) {
			if (mode == 0 && stack.empty()) { mode = 1; continue; }
			return fail(bind_reason::stray_end_tag, c.raw);
		}
		if (is_void_tag(t)) { return fail(bind_reason::stray_end_tag, c.raw); }
		while (!stack.empty() && !ascii_iequals(stack.back(), t) && end_omissible(stack.back())) {
			stack.pop_back();
		}
		if (!stack.empty() && ascii_iequals(stack.back(), t)) { stack.pop_back(); continue; }
		bool open_below = false;
		for (const std::string_view open : stack) {
			if (ascii_iequals(open, t)) { open_below = true; }
		}
		return fail(open_below ? bind_reason::mismatched_tag : bind_reason::stray_end_tag, c.raw);
	}
	return true;
}

// --- the builder (value twin of the tb_* fold)

struct build_frame {
	std::string name;
	std::vector<dom_attribute> attrs;
	std::vector<std::size_t> kids;
	std::string pending;
	bool has_pending = false;
	bool pre = false;
};

struct builder {
	document & doc;
	std::vector<build_frame> stack;
	int mode = 0;
	std::vector<dom_attribute> html_attrs;
	std::size_t head_index = static_cast<std::size_t>(-1);

	constexpr explicit builder(document & d) : doc(d) {
		stack.push_back(build_frame{"head", {}, {}, {}, false, false});
	}

	constexpr std::size_t new_node(dom_node && n) {
		doc.nodes_.push_back(std::move(n));
		return doc.nodes_.size() - 1;
	}
	constexpr bool blank(std::string_view v) const noexcept { return span_blank(v); }

	constexpr void flush(build_frame & f) {
		if (!f.has_pending) { return; }
		if (f.pre || !blank(f.pending)) {
			dom_node t;
			t.type = kind::text;
			t.content = std::move(f.pending);
			f.kids.push_back(new_node(std::move(t)));
		}
		f.pending.clear();
		f.has_pending = false;
	}
	constexpr void append_node(build_frame & f, std::size_t idx) {
		flush(f);
		f.kids.push_back(idx);
	}
	constexpr void add_text(build_frame & f, std::string_view piece) {
		if (!f.has_pending) {
			if (f.pre && f.kids.empty() && f.name == "pre") {
				std::size_t drop = 0;
				if (piece.size() >= 2 && piece[0] == '\r' && piece[1] == '\n') { drop = 2; }
				else if (!piece.empty() && (piece[0] == '\n' || piece[0] == '\r')) { drop = 1; }
				f.pending = std::string{piece.substr(drop)};
			} else {
				f.pending = std::string{piece};
			}
			f.has_pending = true;
		} else {
			f.pending += piece;
		}
	}
	constexpr std::size_t finalize(build_frame & f) {
		flush(f);
		dom_node e;
		e.type = kind::element;
		e.name = std::move(f.name);
		e.attributes = std::move(f.attrs);
		e.children = std::move(f.kids);
		return new_node(std::move(e));
	}
	// pop the top frame, attach its element to the frame below
	constexpr void close_top() {
		build_frame top = std::move(stack.back());
		stack.pop_back();
		const std::size_t idx = finalize(top);
		append_node(stack.back(), idx);
	}
	constexpr void close_all() {
		while (stack.size() >= 2) { close_top(); }
	}
	constexpr void to_body() {
		if (mode == 0 && stack.size() == 1) {
			head_index = finalize(stack.back());
			stack.back() = build_frame{"body", {}, {}, {}, false, false};
			mode = 1;
		}
	}
	constexpr void autoclose(std::string_view incoming) {
		while (stack.size() >= 2 && closed_by(stack.back().name, incoming)) { close_top(); }
	}
	constexpr void merge_attrs(std::vector<dom_attribute> & dst, std::vector<dom_attribute> & src) {
		for (auto & a : src) { dst.push_back(std::move(a)); }
	}

	constexpr void open(std::string name, std::vector<dom_attribute> attrs, bool self) {
		if (name == "html") { merge_attrs(html_attrs, attrs); return; }
		if (name == "head") {
			if (stack.back().name == "head") { merge_attrs(stack.back().attrs, attrs); }
			return;
		}
		if (name == "body") {
			to_body();
			if (stack.back().name == "body") { merge_attrs(stack.back().attrs, attrs); }
			return;
		}
		const bool trans = mode == 0 && stack.size() == 1 && !is_metadata_tag(name);
		if (trans) { to_body(); }
		autoclose(name);
		if (is_void_tag(name) || self) {
			dom_node e;
			e.type = kind::element;
			e.name = std::move(name);
			e.attributes = std::move(attrs);
			append_node(stack.back(), new_node(std::move(e)));
		} else {
			const bool pre = name == "pre" || stack.back().pre;
			stack.push_back(build_frame{std::move(name), std::move(attrs), {}, {}, false, pre});
		}
	}
	constexpr void raw(std::string name, std::vector<dom_attribute> attrs, std::string body) {
		const bool meta = is_metadata_tag(name); // script/style/title true; textarea false
		const bool trans = mode == 0 && stack.size() == 1 && !meta;
		if (trans) { to_body(); }
		autoclose(name);
		dom_node e;
		e.type = kind::element;
		e.name = std::move(name);
		e.attributes = std::move(attrs);
		if (!body.empty()) {
			dom_node t;
			t.type = kind::text;
			t.content = std::move(body);
			// child index assigned after element node exists; build child first
			const std::size_t ti = new_node(std::move(t));
			e.children.push_back(ti);
		}
		append_node(stack.back(), new_node(std::move(e)));
	}
	constexpr void close(std::string_view name) {
		if (ascii_iequals(name, "body") || ascii_iequals(name, "html")) { close_all(); to_body(); return; }
		if (ascii_iequals(name, "head")) { to_body(); return; }
		if (is_void_tag(name)) { return; }
		while (stack.size() >= 2) {
			if (ascii_iequals(stack.back().name, name)) { close_top(); break; }
			if (end_omissible(stack.back().name)) { close_top(); continue; }
			break;
		}
	}
	constexpr void text(std::string_view piece) {
		if (mode == 0 && stack.size() == 1) {
			if (span_blank(piece)) { return; }
			to_body();
			add_text(stack.back(), piece);
		} else {
			add_text(stack.back(), piece);
		}
	}
	constexpr void finish() {
		close_all();
		to_body(); // ensures head_index set + an (empty) body frame present
		const std::size_t body_index = finalize(stack.back());
		dom_node html;
		html.type = kind::element;
		html.name = "html";
		html.attributes = std::move(html_attrs);
		html.children = {head_index, body_index};
		doc.root_ = new_node(std::move(html));
	}
};

constexpr void build_document(document & doc, const std::vector<value_chunk> & cs) {
	builder b{doc};
	for (const value_chunk & c : cs) {
		switch (c.kind) {
			case ck_open: b.open(c.name, c.attrs, false); break;
			case ck_self: b.open(c.name, c.attrs, true); break;
			case ck_raw: b.raw(c.name, c.attrs, c.text); break;
			case ck_close: b.close(c.name); break;
			case ck_text:
				if (!c.text.empty()) { b.text(c.text); }
				break;
			default: break;
		}
	}
	b.finish();
	// parent links + cached <title>
	for (std::size_t i = 0; i < doc.nodes_.size(); ++i) {
		for (const std::size_t ci : doc.nodes_[i].children) { doc.nodes_[ci].parent = i; }
	}
}

} // namespace detail

// Parse a value into an owned document. Invalid HTML is reported through
// document::ok()/error() rather than as a compile error - the value
// twin of cthtml::parse<>(), running the identical tree construction.
CTLL_EXPORT constexpr document parse(std::string_view input) {
	document doc;
	std::vector<detail::value_chunk> chunks;
	if (!detail::scan_chunks(input, chunks)) {
		doc.wellformed_ = false;
		// still build a (best-effort empty) tree so root()/body() are usable
		detail::build_document(doc, {});
		return doc;
	}
	detail::value_validate(chunks, doc.reason_, doc.where_);
	detail::build_document(doc, chunks);
	// cache <title>
	const node t = doc.head()["title"];
	if (t.valid()) { doc.title_ = t.text(); }
	return doc;
}

// --- serialize a value node back to minified HTML (same rules as the
// type-level serialize: void elements get no close tag, empty values
// render bare, script/style bodies pass through raw). Handy on its own,
// and the basis for checking parse(sv) against parse<Src>().

namespace detail {
constexpr void append_escaped(std::string & out, std::string_view s, bool in_attribute) noexcept {
	for (const char c : s) {
		switch (c) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': in_attribute ? out.push_back('>') : (void)(out += "&gt;"); break;
			case '"': in_attribute ? (void)(out += "&quot;") : out.push_back('"'); break;
			default: out.push_back(c);
		}
	}
}
constexpr void serialize_node(std::string & out, const document & d, std::size_t idx) noexcept {
	const dom_node & n = d.at(idx);
	if (n.type == kind::text) { append_escaped(out, n.content, false); return; }
	out.push_back('<');
	out += n.name;
	for (const dom_attribute & a : n.attributes) {
		out.push_back(' ');
		out += a.name;
		if (!a.value.empty()) {
			out += "=\"";
			append_escaped(out, a.value, true);
			out.push_back('"');
		}
	}
	out.push_back('>');
	const bool raw = is_raw_text_tag(n.name);
	if (n.children.empty()) {
		if (!is_void_tag(n.name)) { out += "</"; out += n.name; out.push_back('>'); }
		return;
	}
	for (const std::size_t ci : n.children) {
		if (raw && d.at(ci).type == kind::text) { out += d.at(ci).content; }
		else { serialize_node(out, d, ci); }
	}
	out += "</";
	out += n.name;
	out.push_back('>');
}
} // namespace detail

CTLL_EXPORT constexpr std::string serialize(node n) noexcept {
	std::string out;
	if (n.valid()) { detail::serialize_node(out, *n.document_ptr(), n.index()); }
	return out;
}
CTLL_EXPORT constexpr std::string serialize(const document & d) noexcept {
	std::string out;
	detail::serialize_node(out, d, d.root().index());
	return out;
}

} // namespace cthtml

#endif
