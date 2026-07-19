#include <cthtml.hpp>

void empty_symbol_value() { }

// The value parser (cthtml::parse(std::string_view) -> cthtml::document)
// runs the SAME HTML5 tree construction as the type-level parse<Src>(),
// over an owned std::string/std::vector tree. Because those are
// constexpr on this toolchain, every check below is a static_assert -
// the parser folds entirely at compile time - and the same calls work
// on a runtime std::string_view (see runtime_demo at the end).

#if CTLL_CNTTP_COMPILER_CHECK

#include <string>
#include <string_view>
using namespace std::literals;

// ============ differential: parse(sv) matches parse<Src>() ============
// serialize both ways and require byte-identical output. This pins the
// value parser to the type parser across every HTML5 convenience.

#define SAME(str)                                                                        \
	static_assert(cthtml::serialize(cthtml::parse<str>()) ==                             \
	              cthtml::serialize(cthtml::parse(std::string_view{str})))

SAME("<p>hi");
SAME("<br>");
SAME("");
SAME("   \n  ");
SAME("<p>fragments are fine");
SAME("<div><p>EOF closes everything");
SAME("<ul id=nav><li>Docs<li>Code</ul>");
SAME("<ul><li>a<li>b<li>c</ul>");
SAME("<p>one<p>two<p>three");
SAME("<dl><dt>term<dd>def<dt>t2<dd>d2</dl>");
SAME("<table><tr><td>a<td>b<tr><td>c<td>d</table>");
SAME("<table><thead><tr><th>H<tbody><tr><td>x</table>");
SAME("<select><option>a<option>b</select>");
SAME("<!DOCTYPE html><title>t</title>");
SAME("<!-- comment --><p>after");
SAME("<html lang=en><head><meta charset=utf-8><title>T</title></head><body><p>x</body></html>");
SAME("<meta charset=utf-8><link rel=stylesheet href=/a.css><p>body starts here");
SAME("<style>p em{color:#333}</style><p>x");
SAME(R"(<script type=module>if(a<b){go("</div>")}</script><p>x)");
SAME("<title>Demo &amp; Docs &mdash; v2</title>");
SAME("<textarea>\nline1\nline2</textarea>");
SAME("<p>a &amp; b &lt; c &#65; &#x42;</p>");
SAME("<input type=text disabled required>");
SAME("<a href='/x' title=\"q&amp;a\">link</a>");
SAME("<DIV CLASS=Big><P>Mixed CASE</P></DIV>");
SAME("<pre>  spaced\n  text  </pre>");
SAME("<div><span>a</span> <span>b</span></div>");
SAME("<p>x</p>\n\n<p>y</p>");
SAME("<section><h1>t</h1><p>para<ul><li>i</ul></section>");
SAME("<b><i>nested</i></b>");
SAME("<img src=a.png alt='an image'><br><hr>");
SAME("<custom-element data-x=1><child></child></custom-element>");

// ============ ok()/error() mirrors is_valid<> ============

// valid inputs: ok()
static_assert(cthtml::parse("<p>fragments are fine").ok());
static_assert(cthtml::parse("<br>").ok());
static_assert(cthtml::parse("").ok());
static_assert(cthtml::parse("<!DOCTYPE html><title>t</title>").ok());
static_assert(cthtml::parse("<div><p>EOF closes everything").ok());

// author mistakes: !ok(), with the same reason the type parser reports
static_assert(!cthtml::parse("<b><i>x</b></i>").ok());
static_assert(cthtml::parse("<b><i>x</b></i>").error() == cthtml::bind_reason::mismatched_tag);
static_assert(!cthtml::parse("<p>x</p></p>").ok());
static_assert(cthtml::parse("<p>x</p></p>").error() == cthtml::bind_reason::stray_end_tag);
static_assert(!cthtml::parse("<a x='1' x='2'></a>").ok());
static_assert(cthtml::parse("<a x='1' x='2'></a>").error() == cthtml::bind_reason::duplicate_attribute);
static_assert(!cthtml::parse("<div/>").ok());
static_assert(cthtml::parse("<div/>").error() == cthtml::bind_reason::self_closing_non_void);
static_assert(!cthtml::parse("</br>").ok());          // closing a void
static_assert(!cthtml::parse("a < b").ok());          // raw < in text (lexical)
static_assert(!cthtml::parse("<script>unclosed").ok()); // raw text needs its close

// ============ the owned DOM: navigation + accessors ============
// A document owns std::string/std::vector, so it cannot be a `constexpr`
// variable (non-literal, would retain allocation) - but it lives freely
// as a local inside a constexpr function whose result IS a constant.

constexpr std::string_view kSrc = R"(
<!DOCTYPE html>
<html lang="en">
<head><title>Releases</title></head>
<body>
	<ul id=nav class="menu primary">
		<li><a href=/docs class=item>Docs</a>
		<li><a href=/code class=item>Code</a>
		<li><a href=/blog class="item featured">Blog</a>
	</ul>
	<main id=main><p>Hello &amp; welcome</p></main>
</body>
</html>)";

constexpr bool dom_checks() {
	const cthtml::document page = cthtml::parse(kSrc);
	return page.ok() && page.root().name() == "html" && page.root().attribute("lang") == "en" &&
	       page.head()["title"].text() == "Releases" && page.title() == "Releases" &&
	       page.body()["ul"].attribute("id") == "nav" && page.body()["ul"].count("li") == 3 &&
	       page.body()["ul"].has_attribute("class") &&
	       page.body()["main"]["p"].text() == "Hello & welcome" &&
	       page.body()["ul"][0][0].name() == "a" &&              // ul > li#0 > a
	       page.body()["ul"][0]["a"].attribute("href") == "/docs";
}
static_assert(dom_checks());

// ============ selectors ============

constexpr bool selector_checks() {
	const cthtml::document page = cthtml::parse(kSrc);
	return page.get_element_by_id("nav").name() == "ul" &&
	       page.get_element_by_id("main")["p"].text() == "Hello & welcome" &&
	       page.query("#nav").attribute("id") == "nav" &&
	       page.query(".featured").text() == "Blog" &&
	       page.query("a.item").attribute("href") == "/docs" &&  // first in order
	       page.query("ul#nav > li > a").attribute("href") == "/docs" &&
	       page.query_all(".item").size() == 3 &&
	       page.query_all("#nav a").size() == 3 &&
	       page.query_all("li > a").size() == 3 &&
	       page.query_all("[href]").size() == 3 &&
	       page.query_all("a[href=/blog]").size() == 1 &&
	       page.query("nav").valid() == false &&                 // no <nav> element
	       page.query(".missing").valid() == false &&
	       page.query_all("body a").size() == 3 &&               // deep descendants
	       page.query_all("body > a").size() == 0 &&             // none are direct kids
	       page.query_all("ul .featured").size() == 1;
}
static_assert(selector_checks());

// ============ round-trip a runtime string through the same API ============
// (compiles => the identical calls serve runtime-only sources)

[[maybe_unused]] static std::string runtime_demo(std::string_view html) {
	cthtml::document d = cthtml::parse(html);
	if (!d.ok()) { return std::string{"invalid: "} + std::string{cthtml::to_string(d.error())}; }
	std::string out;
	for (cthtml::node a : d.query_all("a[href]")) { out += a.attribute("href"); out += '\n'; }
	out += cthtml::serialize(d.body());
	return out;
}

#endif // CTLL_CNTTP_COMPILER_CHECK
