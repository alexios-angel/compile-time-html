#ifndef CTHTML__CLASSIFY__HPP
#define CTHTML__CLASSIFY__HPP

#include <cstddef>

#include "types.hpp"
#ifndef CTHTML_IN_A_MODULE
#include <initializer_list>
#include <string_view>
#endif

// HTML5 tag classification + whitespace helpers, as plain VALUE functions
// (std::string_view in, bool out). They live here - not in treebuild.hpp -
// because BOTH the compile-time TYPE builder (treebuild) and the runtime VALUE
// parser (value.hpp) need them, and the value path must reach them WITHOUT the
// lark grammar (so CTHTML_NO_GRAMMAR can skip the grammar entirely).

namespace cthtml::detail {

// case-insensitive membership (builder names are canonical lowercase, validator
// names arrive as written)
constexpr bool tag_in(std::string_view t, std::initializer_list<std::string_view> set) noexcept {
	for (const std::string_view s : set) {
		if (ascii_iequals(t, s)) { return true; }
	}
	return false;
}

// is_void_tag lives in types.hpp (the serializer needs it too)

// elements that live in <head>: seeing anything else there moves insertion to
// <body>
constexpr bool is_metadata_tag(std::string_view t) noexcept {
	return tag_in(t, {"base", "basefont", "bgsound", "link", "meta", "noframes", "noscript",
	                  "script", "style", "template", "title"});
}

// end tags the spec lets a document omit; a close tag (and </body>, </html> and
// EOF) may close through these silently
constexpr bool end_omissible(std::string_view t) noexcept {
	return tag_in(t, {"caption", "colgroup", "dd", "dt", "li", "optgroup", "option", "p",
	                  "rp", "rt", "tbody", "td", "tfoot", "th", "thead", "tr"});
}

// does an incoming open tag t auto-close a still-open element? (the HTML5
// optional-end-tag table, applied at the top of the open stack)
constexpr bool closed_by(std::string_view top, std::string_view t) noexcept {
	if (ascii_iequals(top, "p")) {
		return tag_in(t, {"address", "article", "aside", "blockquote", "details", "div",
		                  "dl", "fieldset", "figcaption", "figure", "footer", "form",
		                  "h1", "h2", "h3", "h4", "h5", "h6", "header", "hgroup", "hr",
		                  "main", "menu", "nav", "ol", "p", "pre", "section", "table", "ul"});
	}
	if (ascii_iequals(top, "li")) { return ascii_iequals(t, "li"); }
	if (ascii_iequals(top, "dt") || ascii_iequals(top, "dd")) {
		return tag_in(t, {"dd", "dt"});
	}
	if (ascii_iequals(top, "td") || ascii_iequals(top, "th")) {
		return tag_in(t, {"tbody", "td", "tfoot", "th", "thead", "tr"});
	}
	if (ascii_iequals(top, "tr")) { return tag_in(t, {"tbody", "tfoot", "thead", "tr"}); }
	if (ascii_iequals(top, "thead") || ascii_iequals(top, "tbody")) {
		return tag_in(t, {"tbody", "tfoot"});
	}
	if (ascii_iequals(top, "option")) { return tag_in(t, {"optgroup", "option"}); }
	if (ascii_iequals(top, "optgroup")) { return ascii_iequals(t, "optgroup"); }
	if (ascii_iequals(top, "caption") || ascii_iequals(top, "colgroup")) {
		return tag_in(t, {"caption", "colgroup", "tbody", "td", "tfoot", "th", "thead", "tr"});
	}
	if (ascii_iequals(top, "rt") || ascii_iequals(top, "rp")) { return tag_in(t, {"rp", "rt"}); }
	return false;
}

// is the span all HTML whitespace?
constexpr bool span_blank(std::string_view v) noexcept {
	for (const char c : v) {
		if (!is_html_blank(c)) { return false; }
	}
	return true;
}

// the kinds a lowered chunk can be. Shared: the TYPE builder (treebuild) and
// the runtime VALUE parser (value.hpp) both tag chunks with these.
enum chunk_kind : int { ck_none, ck_open, ck_self, ck_close, ck_text, ck_raw };

// nesting cap (shared: both parsers reject documents deeper than this)
inline constexpr std::size_t tb_depth_cap = 256;

} // namespace cthtml::detail

#endif
