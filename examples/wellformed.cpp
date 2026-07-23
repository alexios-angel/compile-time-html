// Acceptability as a compile-time property: parse(...).ok() answers as
// a bool without failing the build, so shipping broken markup becomes
// impossible - the checks below run in the compiler, not in
// production. HTML5's conveniences are not errors; author mistakes
// browsers would silently repair are.
//
// Build: make wellformed

#include <cthtml.hpp>
#include <iostream>

// the conveniences: all valid
static_assert(cthtml::parse("<p>no closing tag needed").ok());
static_assert(cthtml::parse("<br>").ok());                       // void element
static_assert(cthtml::parse("<ul><li>a<li>b</ul>").ok());        // li auto-closes
static_assert(cthtml::parse("<table><tr><td>x<td>y</table>").ok());
static_assert(cthtml::parse("<INPUT type=checkbox CHECKED>").ok()); // case, unquoted, boolean
static_assert(cthtml::parse("<!DOCTYPE html><title>t</title>").ok());
static_assert(cthtml::parse(R"(<script>if(a<b)say("</p>")</script>)").ok()); // raw text
static_assert(cthtml::parse("<p>&copy; &#169; &notaref;</p>").ok()); // refs never fail

// the mistakes: all false (and each carries its reason)
static_assert(!cthtml::parse("<b><i>crossed</b></i>").ok());  // crossing close tag
static_assert(!cthtml::parse("<p>x</p></p>").ok());           // stray close tag
static_assert(!cthtml::parse("<a x='1' x='2'></a>").ok());    // duplicate attribute
static_assert(!cthtml::parse("<div/>").ok());                 // self-closed non-void
static_assert(!cthtml::parse("</br>").ok());                  // closing a void
static_assert(!cthtml::parse("<div><b>x</div>").ok());        // </div> cannot close <b>
static_assert(!cthtml::parse("a < b").ok());                  // write &lt;

// and the reason is queryable
static_assert(cthtml::parse("<div/>").error() ==
              cthtml::bind_reason::self_closing_non_void);

int main() {
	std::cout << "every claim in this file was proven during compilation\n";
}
