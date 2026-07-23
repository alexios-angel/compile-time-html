#ifndef CTHTML__TREEBUILD__HPP
#define CTHTML__TREEBUILD__HPP

#include "classify.hpp" // shared grammar-free tag classification helpers
#ifndef CTHTML_NO_GRAMMAR
#include "bind.hpp"
#endif
#ifndef CTHTML_IN_A_MODULE
#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <type_traits>
#endif

// Tree construction: the HTML5 insertion logic that turns the flat
// chunk stream (open tags, close tags, text, whole raw-text elements)
// into ONE document type, the way a browser's tree builder does.
// Every parse yields  html > (head, body):  metadata elements collect
// into head, everything after into body, and fragments like "<p>hi"
// land in body - explicit <html>/<head>/<body> tags contribute their
// attributes to the synthesized frames instead of nesting.
//
// HTML5 conveniences are applied silently: void elements never need a
// close tag, an incoming open tag auto-closes the elements the spec
// lets it close (<li> closes <li>, <td> closes <td>, a block closes
// <p>, ...), a close tag closes intervening elements whose end tags
// are omissible, and EOF closes everything. Author mistakes the spec
// would silently *repair* are compile errors instead: a stray end tag,
// a close tag crossing a still-open element (<b><i></b>), a duplicate
// attribute, self-closing syntax on a non-void element (<div/>).
//
// Two passes over the same stream, one job each: a cheap value-level
// VALIDATOR (validate_chunks) walks a fixed-capacity name stack and
// reports the first bind_error_t - this is what is_valid/bind_error
// cost - and a type-level BUILDER (a fold of tb_step over the chunks,
// with the open elements as a stack of frames) constructs the document
// type, assuming the validator passed. The builder is total: on inputs
// the validator rejected it still produces *a* type, never a hard
// compile error of its own.

namespace cthtml::detail {

// tag classification helpers (tag_in / is_metadata_tag / end_omissible /
// closed_by / span_blank) now live in classify.hpp - shared, grammar-free -
// so the runtime value parser reaches them without the lark grammar.

// --- the validator: chunks lowered to plain values, walked once. Everything
// below is the compile-time TYPE builder and needs the grammar (ctlark::tree
// nodes); CTHTML_NO_GRAMMAR compiles it out. (chunk_kind lives in classify.hpp,
// shared with the runtime value parser.)
#ifndef CTHTML_NO_GRAMMAR

struct chunk_info {
	int kind = ck_none;
	std::string_view name{};  // tag name as written, for open/self/close/raw
	std::string_view raw{};   // the raw token span, for error reporting
	bind_error_t local{};     // per-chunk failure (duplicate attribute)
};

template <typename T> struct attr_name_of {
	static constexpr std::string_view value{};
};
template <typename N, typename... Vs> struct attr_name_of<ctlark::tree<bt_attr, N, Vs...>> {
	static constexpr std::string_view value = N::value_type::view();
};

template <typename... Kids> constexpr bind_error_t dup_attr_check() noexcept {
	constexpr std::size_t n = sizeof...(Kids);
	const std::string_view names[] = {attr_name_of<Kids>::value..., std::string_view{}};
	for (std::size_t i = 0; i < n; ++i) {
		if (names[i].empty()) { continue; }
		for (std::size_t j = i + 1; j < n; ++j) {
			if (ascii_iequals(names[i], names[j])) {
				return bind_error_t{bind_reason::duplicate_attribute, names[j]};
			}
		}
	}
	return bind_error_t{};
}

template <typename Chunk> struct chunk_of;

template <typename Open, typename... Kids>
struct chunk_of<ctlark::tree<bt_open_tag, Open, Kids...>> {
	static constexpr chunk_info info() noexcept {
		const std::string_view raw = Open::value_type::view();
		return {ck_open, raw.substr(1), raw, dup_attr_check<Kids...>()};
	}
};
template <typename Open, typename... Kids>
struct chunk_of<ctlark::tree<bt_self_tag, Open, Kids...>> {
	static constexpr chunk_info info() noexcept {
		const std::string_view raw = Open::value_type::view();
		return {ck_self, raw.substr(1), raw, dup_attr_check<Kids...>()};
	}
};
template <typename RuleName> struct raw_chunk_of {
	template <typename Open, typename... Kids> struct impl {
		static constexpr chunk_info info() noexcept {
			const std::string_view raw = Open::value_type::view();
			return {ck_raw, raw.substr(1), raw, dup_attr_check<Kids...>()};
		}
	};
};
template <typename Open, typename... Kids>
struct chunk_of<ctlark::tree<bt_script_el, Open, Kids...>>
    : raw_chunk_of<bt_script_el>::template impl<Open, Kids...> { };
template <typename Open, typename... Kids>
struct chunk_of<ctlark::tree<bt_style_el, Open, Kids...>>
    : raw_chunk_of<bt_style_el>::template impl<Open, Kids...> { };
template <typename Open, typename... Kids>
struct chunk_of<ctlark::tree<bt_title_el, Open, Kids...>>
    : raw_chunk_of<bt_title_el>::template impl<Open, Kids...> { };
template <typename Open, typename... Kids>
struct chunk_of<ctlark::tree<bt_textarea_el, Open, Kids...>>
    : raw_chunk_of<bt_textarea_el>::template impl<Open, Kids...> { };
template <typename V> struct chunk_of<ctlark::token<bt_CLOSE, V>> {
	static constexpr chunk_info info() noexcept {
		return {ck_close, close_tag_name(V::view()), V::view(), bind_error_t{}};
	}
};
template <typename V> struct chunk_of<ctlark::token<bt_TEXT, V>> {
	static constexpr chunk_info info() noexcept {
		return {ck_text, std::string_view{}, V::view(), bind_error_t{}};
	}
};

// tb_depth_cap and span_blank now live in classify.hpp (shared, grammar-free)

template <typename... Chunks> constexpr bind_error_t validate_chunks() noexcept {
	constexpr std::size_t n = sizeof...(Chunks);
	const chunk_info cs[] = {chunk_of<Chunks>::info()..., chunk_info{}};
	std::string_view stack[tb_depth_cap]{};
	std::size_t sp = 0;
	int mode = 0; // 0 = collecting head, 1 = in body
	for (std::size_t idx = 0; idx < n; ++idx) {
		const chunk_info & c = cs[idx];
		if (c.local.reason != bind_reason::none) { return c.local; }
		if (c.kind == ck_text) {
			if (mode == 0 && sp == 0 && !span_blank(c.raw)) { mode = 1; }
			continue;
		}
		if (c.kind == ck_open || c.kind == ck_self || c.kind == ck_raw) {
			const std::string_view t = c.name;
			if (tag_in(t, {"body", "head", "html"})) {
				if (c.kind == ck_self) {
					return bind_error_t{bind_reason::self_closing_non_void, c.raw};
				}
				if (ascii_iequals(t, "body")) { mode = 1; }
				continue; // attributes merge into the synthesized frames
			}
			if (c.kind == ck_self && !is_void_tag(t)) {
				return bind_error_t{bind_reason::self_closing_non_void, c.raw};
			}
			while (sp > 0 && closed_by(stack[sp - 1], t)) { --sp; }
			if (mode == 0 && sp == 0 && !is_metadata_tag(t)) { mode = 1; }
			if (c.kind == ck_open && !is_void_tag(t)) {
				if (sp == tb_depth_cap) {
					return bind_error_t{bind_reason::depth_overflow, c.raw};
				}
				stack[sp++] = t;
			}
			continue;
		}
		// ck_close
		const std::string_view t = c.name;
		if (ascii_iequals(t, "body") || ascii_iequals(t, "html")) {
			// closes everything still open - but only through omissible
			// end tags; anything else is a real mistake
			for (std::size_t k = 0; k < sp; ++k) {
				if (!end_omissible(stack[k])) {
					return bind_error_t{bind_reason::mismatched_tag, c.raw};
				}
			}
			sp = 0;
			mode = 1;
			continue;
		}
		if (ascii_iequals(t, "head")) {
			if (mode == 0 && sp == 0) {
				mode = 1;
				continue;
			}
			return bind_error_t{bind_reason::stray_end_tag, c.raw};
		}
		if (is_void_tag(t)) {
			return bind_error_t{bind_reason::stray_end_tag, c.raw};
		}
		while (sp > 0 && !ascii_iequals(stack[sp - 1], t) && end_omissible(stack[sp - 1])) {
			--sp;
		}
		if (sp > 0 && ascii_iequals(stack[sp - 1], t)) {
			--sp;
			continue;
		}
		// no match: closing something never opened is a stray; closing
		// across a still-open element is a mismatch
		bool open_below = false;
		for (std::size_t k = 0; k < sp; ++k) {
			if (ascii_iequals(stack[k], t)) { open_below = true; }
		}
		return bind_error_t{open_below ? bind_reason::mismatched_tag
		                               : bind_reason::stray_end_tag,
		                    c.raw};
	}
	return bind_error_t{};
}

// --- the builder: a fold of tb_step over the chunks; the state is the
// stack of open elements as frames, plus the html attributes and the
// completed head

using tb_html_name = cthtml::text<'h', 't', 'm', 'l'>;
using tb_head_name = cthtml::text<'h', 'e', 'a', 'd'>;
using tb_body_name = cthtml::text<'b', 'o', 'd', 'y'>;
using tb_pre_name = cthtml::text<'p', 'r', 'e'>;

struct tb_no_text { };
struct tb_no_head { };

// an open element: name, attributes, finished children, pending text
// (merged until a child element or the close flushes it), and whether
// whitespace is preserved (inside <pre>)
template <typename Name, typename Attrs, typename Done, typename Pending, bool Pre>
struct tb_frame {
	using name = Name;
	using attrs = Attrs;
	static constexpr bool pre = Pre;
};

// Mode 0: Head is tb_no_head and the stack bottom is the head frame.
// Mode 1: Head is the completed <head> element and the bottom is body.
template <int Mode, typename HtmlAttrs, typename Head, typename Frames> struct tb_state { };

template <typename S> struct tb_traits;
template <int M, typename HA, typename H, typename... Fs>
struct tb_traits<tb_state<M, HA, H, ctll::list<Fs...>>> {
	static constexpr int mode = M;
	static constexpr std::size_t depth = sizeof...(Fs);
};

template <typename... A, typename... B>
constexpr auto attrs_cat(ctll::list<A...>, ctll::list<B...>) noexcept {
	return ctll::list<A..., B...>{};
}

// flush a frame's pending text into its children (blank runs are
// dropped unless the frame preserves whitespace)
template <typename N, typename A, typename... Ds, typename P, bool Pre>
constexpr auto tb_flush(tb_frame<N, A, ctll::list<Ds...>, P, Pre>) noexcept {
	if constexpr (std::is_same_v<P, tb_no_text>) {
		return tb_frame<N, A, ctll::list<Ds...>, tb_no_text, Pre>{};
	} else if constexpr (!Pre && text_blank(P{})) {
		return tb_frame<N, A, ctll::list<Ds...>, tb_no_text, Pre>{};
	} else {
		return tb_frame<N, A, ctll::list<Ds..., P>, tb_no_text, Pre>{};
	}
}

// a flushed frame as an element
template <typename N, typename A, typename... Ds, bool Pre>
constexpr auto tb_elem(tb_frame<N, A, ctll::list<Ds...>, tb_no_text, Pre>) noexcept {
	return cthtml::element<N, A, Ds...>{};
}

// append a finished node to a frame (pending text flushes first)
template <typename Node, typename N, typename A, typename... Ds, bool Pre>
constexpr auto tb_append(tb_frame<N, A, ctll::list<Ds...>, tb_no_text, Pre>, Node) noexcept {
	return tb_frame<N, A, ctll::list<Ds..., Node>, tb_no_text, Pre>{};
}
template <typename F, typename Node> constexpr auto tb_attach_f(F f, Node n) noexcept {
	return tb_append(tb_flush(f), n);
}

// append text to a frame, merging with pending text; the first text
// inside <pre> itself drops one leading newline
template <typename T, typename N, typename A, typename... Ds, typename P, bool Pre>
constexpr auto tb_text(tb_frame<N, A, ctll::list<Ds...>, P, Pre>, T) noexcept {
	if constexpr (std::is_same_v<P, tb_no_text>) {
		if constexpr (Pre && sizeof...(Ds) == 0 && std::is_same_v<N, tb_pre_name>) {
			return tb_frame<N, A, ctll::list<Ds...>, drop_lead_newline<T>, Pre>{};
		} else {
			return tb_frame<N, A, ctll::list<Ds...>, T, Pre>{};
		}
	} else {
		return tb_frame<N, A, ctll::list<Ds...>, decltype(text_cat(P{}, T{})), Pre>{};
	}
}

// --- state operations

// pop the top frame, attach its element to the next frame down
template <int M, typename HA, typename H, typename Top, typename Next, typename... Rest>
constexpr auto tb_close_top(tb_state<M, HA, H, ctll::list<Top, Next, Rest...>>) noexcept {
	using elem = decltype(tb_elem(tb_flush(Top{})));
	return tb_state<M, HA, H, ctll::list<decltype(tb_attach_f(Next{}, elem{})), Rest...>>{};
}

// attach a finished node to the top frame
template <typename Node, int M, typename HA, typename H, typename Top, typename... Rest>
constexpr auto tb_attach_top(tb_state<M, HA, H, ctll::list<Top, Rest...>>, Node n) noexcept {
	return tb_state<M, HA, H, ctll::list<decltype(tb_attach_f(Top{}, n)), Rest...>>{};
}

// push a new open frame (whitespace preservation inherits, <pre> sets it)
template <typename NameT, typename Attrs, int M, typename HA, typename H, typename Top,
          typename... Rest>
constexpr auto tb_push(tb_state<M, HA, H, ctll::list<Top, Rest...>>) noexcept {
	constexpr bool pre = std::is_same_v<NameT, tb_pre_name> || Top::pre;
	return tb_state<
	    M, HA, H,
	    ctll::list<tb_frame<NameT, Attrs, ctll::list<>, tb_no_text, pre>, Top, Rest...>>{};
}

// append text to the top frame
template <typename T, int M, typename HA, typename H, typename Top, typename... Rest>
constexpr auto tb_append_text(tb_state<M, HA, H, ctll::list<Top, Rest...>>, T t) noexcept {
	return tb_state<M, HA, H, ctll::list<decltype(tb_text(Top{}, t)), Rest...>>{};
}

// head -> body: fires only from mode 0 with exactly the head frame
// open; anywhere else it is the identity
template <typename HA, typename HeadF>
constexpr auto tb_to_body(tb_state<0, HA, tb_no_head, ctll::list<HeadF>>) noexcept {
	using head_elem = decltype(tb_elem(tb_flush(HeadF{})));
	return tb_state<1, HA, head_elem,
	                ctll::list<tb_frame<tb_body_name, ctll::list<>, ctll::list<>, tb_no_text,
	                                    false>>>{};
}
template <int M, typename HA, typename H, typename Fs>
constexpr auto tb_to_body(tb_state<M, HA, H, Fs> s) noexcept {
	return s;
}

template <bool Do, typename S> constexpr auto tb_maybe_body(S s) noexcept {
	if constexpr (Do) {
		return tb_to_body(s);
	} else {
		return s;
	}
}

// pop every element the incoming open tag auto-closes
template <typename Incoming, int M, typename HA, typename H, typename Top, typename... Rest>
constexpr auto tb_autoclose(tb_state<M, HA, H, ctll::list<Top, Rest...>> s) noexcept {
	if constexpr (sizeof...(Rest) >= 1 && closed_by(Top::name::view(), Incoming::view())) {
		return tb_autoclose<Incoming>(tb_close_top(s));
	} else {
		return s;
	}
}

// close the named element, popping omissible end tags on the way; on
// inputs the validator rejected this is a no-op (the builder is total)
template <typename Tag, int M, typename HA, typename H, typename Top, typename... Rest>
constexpr auto tb_close(tb_state<M, HA, H, ctll::list<Top, Rest...>> s) noexcept {
	if constexpr (sizeof...(Rest) >= 1 && std::is_same_v<typename Top::name, Tag>) {
		return tb_close_top(s);
	} else if constexpr (sizeof...(Rest) >= 1 && end_omissible(Top::name::view())) {
		return tb_close<Tag>(tb_close_top(s));
	} else {
		return s;
	}
}

// close every open element down to the synthesized frame
template <int M, typename HA, typename H, typename Top, typename... Rest>
constexpr auto tb_close_all(tb_state<M, HA, H, ctll::list<Top, Rest...>> s) noexcept {
	if constexpr (sizeof...(Rest) >= 1) {
		return tb_close_all(tb_close_top(s));
	} else {
		return s;
	}
}

// merge attributes into the html slot / into the top frame when it is
// the named synthesized frame
template <typename New, int M, typename HA, typename H, typename Fs>
constexpr auto tb_merge_html(tb_state<M, HA, H, Fs>) noexcept {
	return tb_state<M, decltype(attrs_cat(HA{}, New{})), H, Fs>{};
}
template <typename TagT, typename New, int M, typename HA, typename H, typename N, typename A,
          typename D, typename P, bool Pre, typename... Rest>
constexpr auto tb_merge_top(tb_state<M, HA, H, ctll::list<tb_frame<N, A, D, P, Pre>, Rest...>> s) noexcept {
	if constexpr (std::is_same_v<N, TagT>) {
		return tb_state<M, HA, H,
		                ctll::list<tb_frame<N, decltype(attrs_cat(A{}, New{})), D, P, Pre>,
		                           Rest...>>{};
	} else {
		return s;
	}
}

// --- the per-chunk step

template <typename NameT, typename Attrs, bool SelfClosing, typename S>
constexpr auto tb_open_impl(S s) noexcept {
	constexpr std::string_view nv = NameT::view();
	if constexpr (nv == std::string_view{"html"}) {
		return tb_merge_html<Attrs>(s);
	} else if constexpr (nv == std::string_view{"head"}) {
		return tb_merge_top<tb_head_name, Attrs>(s);
	} else if constexpr (nv == std::string_view{"body"}) {
		return tb_merge_top<tb_body_name, Attrs>(tb_to_body(s));
	} else {
		constexpr bool trans =
		    tb_traits<S>::mode == 0 && tb_traits<S>::depth == 1 && !is_metadata_tag(nv);
		auto s2 = tb_autoclose<NameT>(tb_maybe_body<trans>(s));
		if constexpr (is_void_tag(nv) || SelfClosing) {
			return tb_attach_top(s2, cthtml::element<NameT, Attrs>{});
		} else {
			return tb_push<NameT, Attrs>(s2);
		}
	}
}

template <typename NameT, typename Attrs, typename BodyT, bool Meta, typename S>
constexpr auto tb_raw_impl(S s) noexcept {
	constexpr bool trans = tb_traits<S>::mode == 0 && tb_traits<S>::depth == 1 && !Meta;
	auto s2 = tb_autoclose<NameT>(tb_maybe_body<trans>(s));
	if constexpr (BodyT::size() == 0) {
		return tb_attach_top(s2, cthtml::element<NameT, Attrs>{});
	} else {
		return tb_attach_top(s2, cthtml::element<NameT, Attrs, BodyT>{});
	}
}

// the body token of a raw-text element (whatever follows the attrs)
template <typename AP> struct rest_head;
template <typename L, typename R0, typename... R> struct rest_head<attr_phase<L, R0, R...>> {
	using type = R0;
};
template <typename... Kids> using body_token_of =
    typename rest_head<decltype(take_attrs(ctll::list<>{}, Kids{}...))>::type;

template <int M, typename HA, typename H, typename Fr, typename Open, typename... Kids>
constexpr auto tb_step(tb_state<M, HA, H, Fr> s, ctlark::tree<bt_open_tag, Open, Kids...>) noexcept {
	return tb_open_impl<typename open_name<Open>::type, attrs_of<Kids...>, false>(s);
}
template <int M, typename HA, typename H, typename Fr, typename Open, typename... Kids>
constexpr auto tb_step(tb_state<M, HA, H, Fr> s, ctlark::tree<bt_self_tag, Open, Kids...>) noexcept {
	return tb_open_impl<typename open_name<Open>::type, attrs_of<Kids...>, true>(s);
}
template <int M, typename HA, typename H, typename Fr, typename Open, typename... Kids>
constexpr auto tb_step(tb_state<M, HA, H, Fr> s, ctlark::tree<bt_script_el, Open, Kids...>) noexcept {
	using body = typename raw_body<typename body_token_of<Kids...>::value_type>::type;
	return tb_raw_impl<typename open_name<Open>::type, attrs_of<Kids...>, body, true>(s);
}
template <int M, typename HA, typename H, typename Fr, typename Open, typename... Kids>
constexpr auto tb_step(tb_state<M, HA, H, Fr> s, ctlark::tree<bt_style_el, Open, Kids...>) noexcept {
	using body = typename raw_body<typename body_token_of<Kids...>::value_type>::type;
	return tb_raw_impl<typename open_name<Open>::type, attrs_of<Kids...>, body, true>(s);
}
template <int M, typename HA, typename H, typename Fr, typename Open, typename... Kids>
constexpr auto tb_step(tb_state<M, HA, H, Fr> s, ctlark::tree<bt_title_el, Open, Kids...>) noexcept {
	using body = typename rcdata_body<typename body_token_of<Kids...>::value_type, false>::type;
	return tb_raw_impl<typename open_name<Open>::type, attrs_of<Kids...>, body, true>(s);
}
template <int M, typename HA, typename H, typename Fr, typename Open, typename... Kids>
constexpr auto tb_step(tb_state<M, HA, H, Fr> s, ctlark::tree<bt_textarea_el, Open, Kids...>) noexcept {
	using body = typename rcdata_body<typename body_token_of<Kids...>::value_type, true>::type;
	return tb_raw_impl<typename open_name<Open>::type, attrs_of<Kids...>, body, false>(s);
}
template <int M, typename HA, typename H, typename Fr, typename V>
constexpr auto tb_step(tb_state<M, HA, H, Fr> s, ctlark::token<bt_CLOSE, V>) noexcept {
	using tag = typename close_name<ctlark::token<bt_CLOSE, V>>::type;
	constexpr std::string_view nv = tag::view();
	if constexpr (nv == std::string_view{"body"} || nv == std::string_view{"html"}) {
		return tb_to_body(tb_close_all(s));
	} else if constexpr (nv == std::string_view{"head"}) {
		return tb_to_body(s);
	} else if constexpr (is_void_tag(nv)) {
		return s; // validator already rejected this
	} else {
		return tb_close<tag>(s);
	}
}
template <int M, typename HA, typename H, typename... Fs, typename V>
constexpr auto tb_step(tb_state<M, HA, H, ctll::list<Fs...>> s, ctlark::token<bt_TEXT, V>) noexcept {
	using piece = typename text_piece<V>::type;
	if constexpr (M == 0 && sizeof...(Fs) == 1) {
		if constexpr (text_blank(piece{})) {
			return s; // whitespace between head elements
		} else {
			return tb_append_text(tb_to_body(s), piece{});
		}
	} else {
		return tb_append_text(s, piece{});
	}
}

// --- fold, finish, entry

template <typename S> constexpr auto tb_fold(S s) noexcept {
	return s;
}
template <typename S, typename C, typename... Cs>
constexpr auto tb_fold(S s, C c, Cs... cs) noexcept {
	return tb_fold(tb_step(s, c), cs...);
}

template <typename HA, typename HeadE, typename BodyF>
constexpr auto tb_assemble(tb_state<1, HA, HeadE, ctll::list<BodyF>>) noexcept {
	using body_elem = decltype(tb_elem(tb_flush(BodyF{})));
	return cthtml::element<tb_html_name, HA, HeadE, body_elem>{};
}

using tb_init = tb_state<0, ctll::list<>, tb_no_head,
                         ctll::list<tb_frame<tb_head_name, ctll::list<>, ctll::list<>,
                                             tb_no_text, false>>>;

template <typename Tree> struct treebuild;
template <typename SName, typename... Chunks> struct treebuild<ctlark::tree<SName, Chunks...>> {
	static constexpr bind_error_t fail = validate_chunks<Chunks...>();
	static constexpr bool ok = fail.reason == bind_reason::none;
	using type = decltype(tb_assemble(tb_to_body(tb_close_all(tb_fold(tb_init{}, Chunks{}...)))));
};

#endif // CTHTML_NO_GRAMMAR (compile-time TYPE builder)

} // namespace cthtml::detail

#endif
