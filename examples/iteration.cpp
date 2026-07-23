// Brackets and iteration: operator[] accepts ordinary tags and indexes
// (case-insensitively); range-for walks an element's children; the
// attributes() vector iterates the same way. Everything below is
// proven in the compiler, then reused at runtime.
//
// Build: make iteration

#include <cthtml.hpp>
#include <iostream>

inline constexpr std::string_view src = R"(<main id=releases data-count=2>
	<article id=a1><h2>Brackets</h2></article>
	<article id=a2><h2>Iterators</h2></article>
	<footer>fin</footer>
</main>)";

// --- operator[]: first child with the tag, and child by position

static_assert(cthtml::parse(src).body()["main"]["article"].attribute("id") == "a1");
static_assert(cthtml::parse(src).body()["MAIN"]["ARTICLE"]["h2"].text() == "Brackets");
static_assert(cthtml::parse(src).body()["main"][1].attribute("id") == "a2");
static_assert(cthtml::parse(src).body()["main"][2].text() == "fin");

// --- iteration in constant evaluation

constexpr std::size_t article_count() noexcept {
	const cthtml::document doc = cthtml::parse(src); // C++20 range-for: keep the owner alive
	std::size_t total = 0;
	for (cthtml::node n : doc.body()["main"]) {
		if (n.name() == "article") { ++total; }
	}
	return total;
}
static_assert(article_count() == 2);

constexpr std::size_t attribute_chars() noexcept {
	const cthtml::document doc = cthtml::parse(src);
	std::size_t total = 0;
	for (const cthtml::dom_attribute & a : doc.body()["main"].attributes()) {
		total += a.name.size() + a.value.size();
	}
	return total;
}
static_assert(attribute_chars() == (2 + 8) + (10 + 1));

int main() {
	const cthtml::document doc = cthtml::parse(src);
	for (cthtml::node n : doc.body()["main"]) {
		std::cout << "<" << n.name() << "> " << n.text() << "\n";
	}
	for (const cthtml::dom_attribute & a : doc.body()["main"].attributes()) {
		std::cout << "@" << a.name << " = " << a.value << "\n";
	}
}
