#ifndef CTHTML__TYPES__HPP
#define CTHTML__TYPES__HPP

#include "../ctll/utilities.hpp"
#ifndef CTHTML_IN_A_MODULE
#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <type_traits>
#endif

// Shared vocabulary for the value parser: the node kind enum, the
// case-insensitive name helpers and HTML classification predicates, and
// the bind_reason/bind_error_t diagnostics the parser reports through.
// Text content is stored as UTF-8 bytes with entities already decoded.

namespace cthtml {

enum class kind {
	element,
	text
};


namespace detail {

// HTML names are case-insensitive; cthtml stores them lowercase and
// folds lookups, so doc.get<"DIV">() and doc["Div"] both hit
constexpr char ascii_lower(char c) noexcept {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// HTML "ASCII whitespace": space, tab, LF, FF, CR. Lives here (not in the
// grammar-binding header) so the value path shares it without a grammar include.
constexpr bool is_html_blank(char c) noexcept {
	return c == ' ' || c == '\x09' || c == '\x0A' || c == '\x0C' || c == '\x0D';
}

// ASCII character-class predicates + hex value, shared grammar-free helpers
// (the character-reference decoders on both the type and value paths use them).
constexpr int bind_hexval(char c) noexcept {
	if (c >= '0' && c <= '9') { return c - '0'; }
	if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
	return c - 'A' + 10;
}
constexpr bool is_ascii_digit(char c) noexcept {
	return c >= '0' && c <= '9';
}
constexpr bool is_ascii_hex(char c) noexcept {
	return is_ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
constexpr bool is_ascii_alnum(char c) noexcept {
	return is_ascii_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (ascii_lower(a[i]) != ascii_lower(b[i])) {
			return false;
		}
	}
	return true;
}

// HTML's void elements take no close tag; script/style hold raw text
// (never entity-encoded). Both the tree builder and the serializer care.
constexpr bool is_void_tag(std::string_view t) noexcept {
	for (const std::string_view s : {"area", "base", "br", "col", "embed", "hr", "img",
	                                 "input", "link", "meta", "source", "track", "wbr"}) {
		if (ascii_iequals(t, s)) {
			return true;
		}
	}
	return false;
}

constexpr bool is_raw_text_tag(std::string_view t) noexcept {
	return ascii_iequals(t, "script") || ascii_iequals(t, "style");
}

} // namespace detail

// why tree construction rejected a document that PARSES - author mistakes
// cthtml refuses to repair. Grammar-free (both the TYPE builder and the runtime
// VALUE parser report through these), so it lives here, not in bind.hpp.
CTLL_EXPORT enum class bind_reason : unsigned char {
	none,
	stray_end_tag,         // a close tag with no matching open element
	mismatched_tag,        // a close tag crossing a still-open element
	duplicate_attribute,   // the same attribute name twice in one tag
	self_closing_non_void, // <div/> - only void elements may self-close
	depth_overflow         // more than 256 nested open elements
};

CTLL_EXPORT constexpr std::string_view to_string(bind_reason r) noexcept {
	switch (r) {
		case bind_reason::none: return "none";
		case bind_reason::stray_end_tag: return "a close tag with no matching open element";
		case bind_reason::mismatched_tag: return "a close tag crossing a still-open element";
		case bind_reason::duplicate_attribute: return "duplicate attribute name in a tag";
		case bind_reason::self_closing_non_void: return "self-closing syntax on a non-void element";
		case bind_reason::depth_overflow: return "more than 256 nested open elements";
	}
	return "unknown";
}

// the first tree-construction failure: which rule broke, and the raw offending
// token as written in the input
CTLL_EXPORT struct bind_error_t {
	bind_reason reason = bind_reason::none;
	std::string_view where{};

	constexpr bool ok() const noexcept { return reason == bind_reason::none; }
};

} // namespace cthtml

#endif
