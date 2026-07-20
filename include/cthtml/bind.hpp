#ifndef CTHTML__BIND__HPP
#define CTHTML__BIND__HPP

#include "grammar.hpp"
#include "types.hpp"
#include "entities.hpp"
#ifndef CTHTML_IN_A_MODULE
#include <cstddef>
#include <string_view>
#include <utility>
#endif

// Lowering the flat ctlark chunk stream into cthtml's building blocks:
// tag and attribute names (canonicalized to ASCII lowercase, the way
// browsers store them), attribute values (double-quoted, single-quoted,
// unquoted or bare boolean), text with character references decoded,
// and the bodies of raw-text elements with their trailing close tag
// stripped. The tree-construction pass that assembles these chunks
// into a document - and decides what is an error - lives in
// treebuild.hpp.
//
// Character references follow HTML5, so decoding never fails: known
// named references (entities.hpp) and numeric references decode to
// UTF-8, numeric references in 0x80-0x9F remap through windows-1252,
// NUL / surrogates / out-of-range become U+FFFD, and anything
// unrecognized - a bare &, an unknown &name;, a reference without its
// semicolon - stays literal.

// bind_reason / to_string / bind_error_t now live in types.hpp (grammar-free;
// both the TYPE builder and the runtime VALUE parser report through them).

namespace cthtml::detail {

// grammar names as ctlark text types, for matching parse-tree nodes
using bt_open_tag = ctlark::text<'o', 'p', 'e', 'n', '_', 't', 'a', 'g'>;
using bt_self_tag = ctlark::text<'s', 'e', 'l', 'f', '_', 't', 'a', 'g'>;
using bt_script_el = ctlark::text<'s', 'c', 'r', 'i', 'p', 't', '_', 'e', 'l'>;
using bt_style_el = ctlark::text<'s', 't', 'y', 'l', 'e', '_', 'e', 'l'>;
using bt_title_el = ctlark::text<'t', 'i', 't', 'l', 'e', '_', 'e', 'l'>;
using bt_textarea_el = ctlark::text<'t', 'e', 'x', 't', 'a', 'r', 'e', 'a', '_', 'e', 'l'>;
using bt_attr = ctlark::text<'a', 't', 't', 'r'>;
using bt_TEXT = ctlark::text<'T', 'E', 'X', 'T'>;
using bt_CLOSE = ctlark::text<'C', 'L', 'O', 'S', 'E'>;
using bt_DQVAL = ctlark::text<'D', 'Q', 'V', 'A', 'L'>;
using bt_SQVAL = ctlark::text<'S', 'Q', 'V', 'A', 'L'>;

// is_html_blank / ascii_lower / ascii_iequals live in types.hpp (grammar-free;
// the value path and the lookup side need them too)

// bind_hexval / is_ascii_digit / is_ascii_hex / is_ascii_alnum now live in
// types.hpp (grammar-free; the value path's char-reference decoder needs them).

// --- decoding character references in a span (From, To are the number
// of characters to strip from either end: quotes for attribute values).
// HTML decoding never fails; the buffer is 2x because a short named
// reference can decode to two code points ("&nGt;" is 6 UTF-8 bytes).

template <typename Text, size_t From, size_t To> struct decode_entities {
	struct out_t {
		char buf[Text::size() * 2 + 1]{};
		size_t len = 0;
	};

	// the windows-1252 remap HTML5 applies to numeric references
	// 0x80-0x9F (entries mapping to themselves stay as-is)
	static constexpr unsigned long win1252[32] = {
	    0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
	    0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};

	static constexpr void put_utf8(out_t & o, unsigned long cp) noexcept {
		if (cp < 0x80) {
			o.buf[o.len++] = static_cast<char>(cp);
		} else if (cp < 0x800) {
			o.buf[o.len++] = static_cast<char>(0xC0 | (cp >> 6));
			o.buf[o.len++] = static_cast<char>(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			o.buf[o.len++] = static_cast<char>(0xE0 | (cp >> 12));
			o.buf[o.len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			o.buf[o.len++] = static_cast<char>(0x80 | (cp & 0x3F));
		} else {
			o.buf[o.len++] = static_cast<char>(0xF0 | (cp >> 18));
			o.buf[o.len++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			o.buf[o.len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			o.buf[o.len++] = static_cast<char>(0x80 | (cp & 0x3F));
		}
	}

	static constexpr void put_numeric(out_t & o, unsigned long cp) noexcept {
		// HTML5: NUL, surrogates and beyond-Unicode become U+FFFD,
		// C1 controls remap through windows-1252
		if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
			cp = 0xFFFD;
		} else if (cp >= 0x80 && cp <= 0x9F) {
			cp = win1252[cp - 0x80];
		}
		put_utf8(o, cp);
	}

	static constexpr out_t compute() noexcept {
		out_t o{};
		constexpr std::string_view raw = Text::view();
		size_t i = From;
		const size_t end = raw.size() - To;
		while (i < end) {
			const char c = raw[i];
			if (c != '&') {
				o.buf[o.len++] = c;
				++i;
				continue;
			}
			// numeric reference: &#123; or &#x1F600;
			if (i + 1 < end && raw[i + 1] == '#') {
				const bool hex = i + 2 < end && (raw[i + 2] == 'x' || raw[i + 2] == 'X');
				size_t j = i + (hex ? 3 : 2);
				unsigned long cp = 0;
				size_t digits = 0;
				while (j < end && (hex ? is_ascii_hex(raw[j]) : is_ascii_digit(raw[j]))) {
					if (cp <= 0x110000) {
						cp = hex ? cp * 16 + static_cast<unsigned long>(bind_hexval(raw[j]))
						         : cp * 10 + static_cast<unsigned long>(raw[j] - '0');
					}
					++j;
					++digits;
				}
				if (digits > 0 && j < end && raw[j] == ';') {
					put_numeric(o, cp);
					i = j + 1;
					continue;
				}
				o.buf[o.len++] = '&';
				++i;
				continue;
			}
			// named reference: &name; - unknown names stay literal
			size_t j = i + 1;
			while (j < end && is_ascii_alnum(raw[j])) { ++j; }
			if (j > i + 1 && j < end && raw[j] == ';') {
				const entity_ref * e = find_entity(raw.substr(i + 1, j - i - 1));
				if (e != nullptr) {
					put_utf8(o, e->first);
					if (e->second != 0) { put_utf8(o, e->second); }
					i = j + 1;
					continue;
				}
			}
			o.buf[o.len++] = '&';
			++i;
		}
		return o;
	}

	static constexpr out_t data = compute();

	template <size_t... I> static constexpr auto lift(std::index_sequence<I...>) noexcept {
		return cthtml::text<data.buf[I]...>{};
	}
	using type = decltype(lift(std::make_index_sequence<data.len>{}));
};

// --- spans lifted verbatim, and lifted with names folded to lowercase

template <typename Text, size_t From, size_t To> struct strip_span {
	static constexpr size_t length = Text::size() - From - To;
	template <size_t... I> static constexpr auto lift(std::index_sequence<I...>) noexcept {
		return cthtml::text<Text::view()[From + I]...>{};
	}
	using type = decltype(lift(std::make_index_sequence<length>{}));
};

template <typename Text, size_t From, size_t To> struct lower_span {
	static constexpr size_t length = Text::size() - From - To;
	template <size_t... I> static constexpr auto lift(std::index_sequence<I...>) noexcept {
		return cthtml::text<ascii_lower(Text::view()[From + I])...>{};
	}
	using type = decltype(lift(std::make_index_sequence<length>{}));
};

// "<name" -> name, lowercased (this covers "<script" etc. too)
template <typename Token> struct open_name {
	using type = typename lower_span<typename Token::value_type, 1, 0>::type;
};
// "</name  >" -> name, lowercased (trailing whitespace and > trimmed)
template <typename Token> struct close_name {
	static constexpr size_t trailing() noexcept {
		constexpr std::string_view raw = Token::value_type::view();
		size_t n = 1; // the >
		while (is_html_blank(raw[raw.size() - 1 - n])) { ++n; }
		return n;
	}
	using type = typename lower_span<typename Token::value_type, 2, trailing()>::type;
};

// the same trim as a plain value computation, for the validator
constexpr std::string_view close_tag_name(std::string_view raw) noexcept {
	std::string_view n = raw.substr(2, raw.size() - 3);
	while (!n.empty() && is_html_blank(n[n.size() - 1])) {
		n = n.substr(0, n.size() - 1);
	}
	return n;
}

// --- text pieces and helpers

template <typename Value> struct text_piece {
	using type = typename decode_entities<Value, 0, 0>::type;
};

template <auto... A, auto... B> constexpr auto text_cat(cthtml::text<A...>, cthtml::text<B...>) noexcept {
	return cthtml::text<A..., B...>{};
}

template <typename Text> constexpr bool text_blank(Text) noexcept {
	for (const char c : Text::view()) {
		if (!is_html_blank(c)) { return false; }
	}
	return true;
}

// a single leading newline is dropped inside <pre> and <textarea>
constexpr size_t lead_newline(std::string_view v) noexcept {
	if (v.size() >= 2 && v[0] == '\r' && v[1] == '\n') { return 2; }
	if (!v.empty() && (v[0] == '\n' || v[0] == '\r')) { return 1; }
	return 0;
}

template <typename Text> using drop_lead_newline =
    typename strip_span<Text, lead_newline(Text::view()), 0>::type;

// --- attributes: name= with a quoted or unquoted value, or bare boolean

template <typename Node> struct bind_attr;
// bare boolean: value is the empty string, as the DOM reports it
template <typename Name> struct bind_attr<ctlark::tree<bt_attr, Name>> {
	using name_type = typename lower_span<typename Name::value_type, 0, 0>::type;
	using type = cthtml::attribute<name_type, cthtml::text<>>;
};
template <typename Name, typename VName, typename VText>
struct bind_attr<ctlark::tree<bt_attr, Name, ctlark::token<VName, VText>>> {
	static constexpr size_t quote =
	    (std::is_same_v<VName, bt_DQVAL> || std::is_same_v<VName, bt_SQVAL>) ? 1 : 0;
	using name_type = typename lower_span<typename Name::value_type, 0, 0>::type;
	using decoded = decode_entities<VText, quote, quote>;
	using type = cthtml::attribute<name_type, typename decoded::type>;
};

template <typename T> struct is_attr_node : std::false_type { };
template <typename... Ks> struct is_attr_node<ctlark::tree<bt_attr, Ks...>> : std::true_type { };

// collect the attr* prefix of a chunk's kids; whatever follows (the
// body token of a raw-text element) is kept in Rest
template <typename AttrList, typename... Rest> struct attr_phase {
	using attrs = AttrList;
};

template <typename... As> constexpr auto take_attrs(ctll::list<As...>) noexcept {
	return attr_phase<ctll::list<As...>>{};
}
template <typename... As, typename Head, typename... Rest>
constexpr auto take_attrs(ctll::list<As...>, Head, Rest... rest) noexcept {
	if constexpr (is_attr_node<Head>::value) {
		return take_attrs(ctll::list<As..., typename bind_attr<Head>::type>{}, rest...);
	} else {
		return attr_phase<ctll::list<As...>, Head, Rest...>{};
	}
}

template <typename... Kids> using attrs_of =
    typename decltype(take_attrs(ctll::list<>{}, Kids{}...))::attrs;

// --- raw-text bodies: the *_BODY token carries the content AND the
// close tag ("...</script>"); rfind("</") locates the close because
// the content can never contain a "</" + first-letter prefix of its
// own tag (the terminal's unrolled regex guarantees it)

template <typename Value> constexpr size_t body_close_at() noexcept {
	return Value::view().rfind("</");
}

// script/style: content taken verbatim
template <typename Value> struct raw_body {
	using type = typename strip_span<Value, 0, Value::size() - body_close_at<Value>()>::type;
};
// title/textarea: RCDATA - character references decode; textarea also
// drops a single leading newline, like <pre>
template <typename Value, bool StripNewline> struct rcdata_body {
	static constexpr size_t to = Value::size() - body_close_at<Value>();
	static constexpr size_t from() noexcept {
		if constexpr (StripNewline) {
			return lead_newline(Value::view().substr(0, Value::size() - to));
		} else {
			return 0;
		}
	}
	using type = typename decode_entities<Value, from(), to>::type;
};

} // namespace cthtml::detail

#endif
