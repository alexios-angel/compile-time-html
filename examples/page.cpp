// The classic use: a web page proven correct while the binary compiles.
// A typo in the markup, a crossing close tag or a missing attribute
// fails the static_asserts below at build time; the same parse call
// then loads the page at runtime. The document is written the way HTML
// is actually written - void elements, unquoted attributes, <li>
// without </li> - and lands in a browser-shaped DOM: html > (head, body).
//
// Build: make page

#include <cthtml.hpp>
#include <iostream>

inline constexpr std::string_view src = R"(<!DOCTYPE html>
<html lang=en>
<head>
	<meta charset=utf-8>
	<title>demo &mdash; releases</title>
</head>
<body data-build=42>
	<h1>Releases</h1>
	<ul id=nav>
		<li><a href=/docs>docs &amp; guides</a>
		<li><a href=/code>code</a>
		<li><a href=/chat>chat</a>
	</ul>
</body>
</html>)";

// requirements checked at build time (the parse folds in the compiler;
// an owned constexpr document cannot outlive constant evaluation, so
// scalar facts are extracted instead)
static_assert([] {
	const cthtml::document d = cthtml::parse(src); // named: gcc folds handles
	return d.ok() && d.title() == "demo \xe2\x80\x94 releases" &&
	       d.body()["ul"].count("li") == 3 &&
	       d.body()["ul"]["li"]["a"].has_attribute("href");
}());

// values usable as constants
constexpr int build = [] {
	const cthtml::document d = cthtml::parse(src);
	int value = 0;
	for (const char c : d.body().attribute("data-build")) {
		value = value * 10 + (c - '0');
	}
	return value;
}();
int build_slots[build];

int main() {
	const cthtml::document page = cthtml::parse(src);
	std::cout << "title: " << page.title() << "\n";
	std::cout << "build: " << build << " (slots: " << sizeof(build_slots) / sizeof(int) << ")\n";

	std::cout << "nav:\n";
	for (cthtml::node a : page.query_all("#nav > li > a")) {
		std::cout << "  " << a.attribute("href") << "  (" << a.text() << ")\n";
	}
}
