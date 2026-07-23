# CLAUDE.md — compile-time-html (cthtml)

Header-only, constexpr HTML5 VALUE parser. One entry point:
`cthtml::parse(std::string_view) -> cthtml::document` — folds a page in
a `static_assert` and parses runtime strings with the same call. The
result is an owned std::string/std::vector DOM, always shaped
`html > (head, body)` like a browser. A mistake is a value
(`ok()`/`error() -> bind_reason`/`error_where()`), never a compile
error. Namespace `cthtml`. C++20+. Work on `main`. Prefer `rg`.

(History: the original type-level parser — a ctlark Earley grammar
producing the document as a TYPE — was removed 2026-07; the value
parser had reproduced it byte-for-byte and is now the only path. The
frozen differential table in tests/value.cpp is the type parser's last
testimony — keep it passing.)

## Build & test — "compiling the tests IS the test"
`tests/value.cpp` is a `static_assert` suite; compiling it = passing.
```bash
make                # C++20+, seconds (no grammar bake exists anymore)
make CXX=clang++
cmake -B build && cmake --build build && ctest --test-dir build
```
Flags: `-O2 -pedantic -Wall -Wextra -Werror -Wconversion` — stay clean.

## Layout
- `include/cthtml.hpp` — umbrella (types, entities, value).
- `include/cthtml/value.hpp` — THE parser: tokenizer + HTML5 tree
  construction over values, `document`/`node` handles, the CSS-subset
  selector engine (`query`/`query_all`/`get_element_by_id`),
  `serialize(node|document)`.
- `include/cthtml/types.hpp` — shared vocabulary: `kind`, ASCII/name
  helpers, `is_void_tag`/`is_raw_text_tag`, `bind_reason`/`bind_error_t`.
- `include/cthtml/classify.hpp` — tag classification predicates.
- `include/cthtml/entities.hpp` — GENERATED WHATWG named-reference
  table (`python3 tools/gen-entities.py`; never edit by hand).
- `external/compile-time-lark/` — git SUBMODULE; only
  `ctll/utilities.hpp` (the `CTLL_EXPORT` macro) is consumed now.
- `tests/value.cpp`, `examples/` (page, wellformed, introspection,
  iteration — all value API), `single-header/cthtml.hpp` (quom),
  `cthtml.cppm` (module).

## Gotchas
- **Constexpr lifetime idioms**: an owned constexpr `document` cannot
  escape constant evaluation — extract scalars inside a constexpr
  lambda. gcc additionally wants the document BOUND TO A NAMED LOCAL
  before using node handles in constant expressions (temporaries make
  it lose the pointer identity), and C++20 range-for over
  `parse(x).body()[...]` dangles — bind first. See examples/page.cpp.
- **The frozen differential table** (tests/value.cpp): expected
  serializations captured from the retired type parser. Changing
  serialization or tree construction must keep it green, or the change
  is a documented semantic break.
- **entities.hpp is generated** — regenerate, commit, never hand-edit.
- **HTML semantics decisions** (keep consistent): every parse
  synthesizes html > (head, body); auto-close applies at the TOP of the
  open stack only; whitespace-only text dropped except in
  pre/textarea (one leading newline stripped); references decode
  leniently, entity names case-SENSITIVE, tag/attr names fold lower;
  raw-text `</script` needs `>`; `a < b` in text is an error (write
  `&lt;`).
- **Attribution** — CTLL is Hana Dusíková's (via notre, from CTRE); the
  entity data is the WHATWG's. Preserve `NOTICE` and `LICENSE`.
