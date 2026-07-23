// Walking a document generically: every node knows its name, text,
// children and attributes, so a recursive visitor can pretty-print (or
// transform) any document. The same walk that runs below at runtime
// also folds inside a static_assert.
//
// Build: make introspection

#include <cthtml.hpp>
#include <iostream>
#include <string>

inline constexpr std::string_view src = R"(<!DOCTYPE html>
<title>feed</title>
<section id=s1 class=starred>
	<h2>Compile-time everything</h2>
	<p>types &amp; templates
</section>
<section id=s2>
	<h2>Parsers as tables</h2>
</section>
<hr>)";

static_assert(cthtml::parse(src).ok());
static_assert(cthtml::parse(src).body().count("section") == 2);

void print(cthtml::node n, int indent = 0) {
	const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
	std::cout << pad << '<' << n.name();
	for (const cthtml::dom_attribute & a : n.attributes()) {
		std::cout << ' ' << a.name << "=\"" << a.value << '"';
	}
	std::cout << ">\n";
	if (!n.text().empty()) {
		std::cout << pad << "  \"" << n.text() << '"' << "\n";
	}
	for (cthtml::node child : n) {
		print(child, indent + 1);
	}
	std::cout << pad << "</" << n.name() << ">\n";
}

int main() {
	const cthtml::document doc = cthtml::parse(src);
	print(doc.root());

	// the same document, re-serialized to minified form
	const std::string minified = cthtml::serialize(doc);
	std::cout << "\nminified (" << minified.size() << " bytes):\n" << minified << "\n";
}
