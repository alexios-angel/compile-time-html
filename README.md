> **Attribution:** this library follows the architecture of its siblings
> [compile-time-xml](https://github.com/alexios-angel/compile-time-xml),
> [compile-time-json](https://github.com/alexios-angel/compile-time-json) and
> [compile-time-json5](https://github.com/alexios-angel/compile-time-json5).
> The named-character-reference table is the
> [WHATWG](https://html.spec.whatwg.org/multipage/named-characters.html)'s.
> Apache License 2.0 with LLVM Exceptions; see [NOTICE](NOTICE).

# cthtml — compile-time HTML

HTML5 parsed while your code compiles. `cthtml::parse(std::string_view)`
is a constexpr *value* parser: fold a page inside a `static_assert`, or
hand it a runtime string — a file, a socket, a form field — with the
same call. The result is an owned, browser-shaped document
(`html > (head, body)`, always) with navigation, a CSS-subset selector
engine and `serialize()` back to minified HTML. Write HTML the way HTML
is written — void elements, `<li>` without `</li>`, unquoted attributes
— and a mistake is a *value* (`ok()` / `error()`), never a mystery.

```c++
#include <cthtml.hpp>

inline constexpr std::string_view src = R"(<!DOCTYPE html>
<title>demo &mdash; releases</title>
<ul id=nav>
    <li><a href=/docs>docs &amp; guides</a>
    <li><a href=/code>code</a>
</ul>)";

static_assert(cthtml::parse(src).ok());
static_assert(cthtml::parse(src).title() == "demo — releases");
static_assert(cthtml::parse(src).body()["ul"].count("li") == 2);
static_assert(cthtml::parse(src).body()["ul"]["li"]["a"].attribute("href") == "/docs");

// author mistakes are a queryable property:
static_assert(!cthtml::parse("<b><i>crossed</b></i>").ok());  // crossing close tag
static_assert(!cthtml::parse("<p x='1' x='2'></p>").ok());    // duplicate attribute
static_assert(cthtml::parse("<div/>").error() ==
              cthtml::bind_reason::self_closing_non_void);    // only voids self-close
```

## What is supported

HTML5 the way browsers read it, minus the repairs that hide bugs:

* **implied structure**: every parse yields `html > (head, body)` like
  a browser DOM — fragments land in body, metadata (`<meta>`,
  `<title>`, `<link>`, `<style>`, `<script>`, ...) written before any
  content collects into head, and explicit `<html>`/`<head>`/`<body>`
  tags contribute their attributes to the synthesized elements

* **void elements** (`<br>`, `<img>`, `<meta>`, ...) — no close tag,
  `<br/>` tolerated and identical

* **optional end tags**: the HTML5 auto-close table — `<li>` closes
  `<li>`, `<td>`/`<tr>` close each other, a block element closes
  `<p>`, `<option>`/`<optgroup>`, `<dt>`/`<dd>`, table sections,
  ruby annotations — and EOF closes everything (`<div>hi` is valid)

* **case-insensitive names**, stored canonically lowercase;
  `["Div"]`, `["DIV"]` and `attribute("ID")` all hit

* **attributes** double-quoted, single-quoted, unquoted (`width=100`)
  or bare boolean (`disabled`, reported as the empty string)

* **`<!DOCTYPE html>`** accepted and skipped, any case, legacy strings
  included

* **raw text**:
  - `<script>`/`<style>` content is never parsed as markup (`if (a<b)`, `"</div>"` — fine)
  - `<title>`/`<textarea>` are RCDATA (references decode)
  - `<pre>`/`<textarea>` preserve whitespace, minus the single newline right after the open tag

* **character references, never an error**: the full WHATWG named
  table (2125 references, two-code-point ones included), decimal and
  hex numeric references with the spec's windows-1252 remap and
  `U+FFFD` fallbacks — all decoded to UTF-8 at parse time; unknown
  names and bare `&` stay literal

* `<!-- comments -->` (HTML rules: `--` inside is fine) and
  `<![CDATA[...]]>` sections are dropped; whitespace-only text between
  elements is dropped (except inside `<pre>`/`<textarea>`)

**Where cthtml is stricter than a browser** — the spec makes browsers
*repair* these; cthtml makes them compile errors, because markup you
compile in is markup you control:

* a stray end tag (`</p>` with no `<p>`, `</br>` at all)

* a close tag crossing a still-open element (`<b><i>x</b></i>`,
  `<div><b>x</div>`)

* a duplicate attribute name (case-insensitively)

* self-closing syntax on a non-void element (`<div/>`)

* a raw `<` in text (write `&lt;`), and a raw-text element that never
  reaches its close tag

* elements nested deeper than 256 levels

Not supported (yet): tag-omission rules that need more than the top of
the open stack (`<p>` is closed by a block element only when it is the
innermost open element), foreign content (SVG/MathML) semantics, and
encodings other than UTF-8/ASCII.

## API

One entry point, one document type (`value.hpp`):

- `cthtml::parse(std::string_view) -> cthtml::document` — constexpr;
  never throws, never fails the build. `document::{ok, error,
  error_where, root, head, body, title, query, query_all,
  get_element_by_id}`.
- `cthtml::node` — a cheap handle: `{name, text, operator[](tag | index),
  count, attribute, has_attribute, attributes, parent, query,
  query_all}`, plus range-`for` over children.
- `cthtml::serialize(node | document) -> std::string` — minified HTML
  (voids bare, boolean attributes bare, raw `<script>`/`<style>` bodies
  unescaped).
- `cthtml::bind_reason` — why a document that lexes was rejected:
  `stray_end_tag`, `mismatched_tag`, `duplicate_attribute`,
  `self_closing_non_void`, `depth_overflow` (+ `error_where()`).
- Selectors (`query`/`query_all`): tag, `#id`, `.class`, `[attr]`,
  `*`, descendant and `>` combinators.

Constant evaluation note: an owned constexpr `document` cannot outlive
constant evaluation (non-transient allocation), so a `static_assert`
extracts scalar facts — bind the document to a local inside a constexpr
lambda/function and return the check.

## How it works

A hand-written HTML5 tokenizer feeds the same tree-construction logic a
browser parser runs — implied `<html>/<head>/<body>`, optional end
tags, void elements, raw-text and RCDATA elements, case folding — over
an owned `std::string`/`std::vector` tree, entirely in `constexpr`
(`value.hpp`). Named and numeric character references decode through
the generated WHATWG table (`entities.hpp` — regenerate with
`python3 tools/gen-entities.py`, never edit by hand). Shared
classification predicates live in `classify.hpp`/`types.hpp`.

(The original implementation additionally encoded documents as C++
*types* via a compile-time Earley grammar; that path was retired in
2026-07 in favour of the single value parser — same tree construction,
one code path, seconds to compile.)

## Building and integrating

Header-only; C++20 or later (constexpr `std::string`/`std::vector`).

```bash
cmake --preset default          # Ninja + Release (use --preset clang for clang++)
cmake --build --preset default  # compiling the suite IS the test
ctest --preset default
```

Vendor `include/` (self-contained - no submodules), or use the
amalgamated `single-header/cthtml.hpp` (regenerate with
`cmake --build build --target single-header`; needs the
[quom](https://pypi.org/project/quom/) tool).

## Roadmap

cthtml is the first brick of a compile-time web stack:
**compile-time-javascript** and **compile-time-css** come next, and
they meet in **compile-time-browser** — HTML, CSS and JS parsed at
compile time and lowered into an SDL3 application, as if the page had
been hand-written as native code.

## License

Apache License 2.0 with LLVM Exceptions (see [LICENSE](LICENSE)).
The named character references are the WHATWG's; the repo's historical
CTLL/CTRE lineage is recorded in [NOTICE](NOTICE).
