#include <cthtml.hpp>

void empty_symbol_value() { }

// The value parser (cthtml::parse(std::string_view) -> cthtml::document)
// runs HTML5 tree construction over an owned std::string/std::vector
// tree. Because those are constexpr, every check below is a
// static_assert - the parser folds entirely at compile time - and the
// same calls work on a runtime std::string_view (see runtime_demo).

#include <string>
#include <string_view>
using namespace std::literals;

// ============ frozen differential: parse(sv) vs the retired type parser ============
// The last build of the type-level parse<Src>() produced these exact
// serializations; the value parser must keep reproducing them
// byte-for-byte. (Frozen 2026-07-23 when the grammar path was removed.)

#define SAME(str, expect)                                                                \
	static_assert(cthtml::serialize(cthtml::parse(std::string_view{str})) == (expect))

SAME("<p>hi", "<html><head></head><body><p>hi</p></body></html>");
SAME("<br>", "<html><head></head><body><br></body></html>");
SAME("", "<html><head></head><body></body></html>");
SAME("   \n  ", "<html><head></head><body></body></html>");
SAME("<p>fragments are fine", "<html><head></head><body><p>fragments are fine</p></body></html>");
SAME("<div><p>EOF closes everything", "<html><head></head><body><div><p>EOF closes everything</p></div></body></html>");
SAME("<ul id=nav><li>Docs<li>Code</ul>", "<html><head></head><body><ul id=\"nav\"><li>Docs</li><li>Code</li></ul></body></html>");
SAME("<ul><li>a<li>b<li>c</ul>", "<html><head></head><body><ul><li>a</li><li>b</li><li>c</li></ul></body></html>");
SAME("<p>one<p>two<p>three", "<html><head></head><body><p>one</p><p>two</p><p>three</p></body></html>");
SAME("<dl><dt>term<dd>def<dt>t2<dd>d2</dl>", "<html><head></head><body><dl><dt>term</dt><dd>def</dd><dt>t2</dt><dd>d2</dd></dl></body></html>");
SAME("<table><tr><td>a<td>b<tr><td>c<td>d</table>", "<html><head></head><body><table><tr><td>a</td><td>b</td></tr><tr><td>c</td><td>d</td></tr></table></body></html>");
SAME("<table><thead><tr><th>H<tbody><tr><td>x</table>", "<html><head></head><body><table><thead><tr><th>H</th></tr></thead><tbody><tr><td>x</td></tr></tbody></table></body></html>");
SAME("<select><option>a<option>b</select>", "<html><head></head><body><select><option>a</option><option>b</option></select></body></html>");
SAME("<!DOCTYPE html><title>t</title>", "<html><head><title>t</title></head><body></body></html>");
SAME("<!-- comment --><p>after", "<html><head></head><body><p>after</p></body></html>");
SAME("<html lang=en><head><meta charset=utf-8><title>T</title></head><body><p>x</body></html>", "<html lang=\"en\"><head><meta charset=\"utf-8\"><title>T</title></head><body><p>x</p></body></html>");
SAME("<meta charset=utf-8><link rel=stylesheet href=/a.css><p>body starts here", "<html><head><meta charset=\"utf-8\"><link rel=\"stylesheet\" href=\"/a.css\"></head><body><p>body starts here</p></body></html>");
SAME("<style>p em{color:#333}</style><p>x", "<html><head><style>p em{color:#333}</style></head><body><p>x</p></body></html>");
SAME(R"(<script type=module>if(a<b){go("</div>")}</script><p>x)", "<html><head><script type=\"module\">if(a<b){go(\"</div>\")}</script></head><body><p>x</p></body></html>");
SAME("<title>Demo &amp; Docs &mdash; v2</title>", "<html><head><title>Demo &amp; Docs — v2</title></head><body></body></html>");
SAME("<textarea>\nline1\nline2</textarea>", "<html><head></head><body><textarea>line1\nline2</textarea></body></html>");
SAME("<p>a &amp; b &lt; c &#65; &#x42;</p>", "<html><head></head><body><p>a &amp; b &lt; c A B</p></body></html>");
SAME("<input type=text disabled required>", "<html><head></head><body><input type=\"text\" disabled required></body></html>");
SAME("<a href='/x' title=\"q&amp;a\">link</a>", "<html><head></head><body><a href=\"/x\" title=\"q&amp;a\">link</a></body></html>");
SAME("<DIV CLASS=Big><P>Mixed CASE</P></DIV>", "<html><head></head><body><div class=\"Big\"><p>Mixed CASE</p></div></body></html>");
SAME("<pre>  spaced\n  text  </pre>", "<html><head></head><body><pre>  spaced\n  text  </pre></body></html>");
SAME("<div><span>a</span> <span>b</span></div>", "<html><head></head><body><div><span>a</span><span>b</span></div></body></html>");
SAME("<p>x</p>\n\n<p>y</p>", "<html><head></head><body><p>x</p><p>y</p></body></html>");
SAME("<section><h1>t</h1><p>para<ul><li>i</ul></section>", "<html><head></head><body><section><h1>t</h1><p>para</p><ul><li>i</li></ul></section></body></html>");
SAME("<b><i>nested</i></b>", "<html><head></head><body><b><i>nested</i></b></body></html>");
SAME("<img src=a.png alt='an image'><br><hr>", "<html><head></head><body><img src=\"a.png\" alt=\"an image\"><br><hr></body></html>");
SAME("<custom-element data-x=1><child></child></custom-element>", "<html><head></head><body><custom-element data-x=\"1\"><child></child></custom-element></body></html>");

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

