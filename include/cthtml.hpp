#ifndef CTHTML__HPP
#define CTHTML__HPP

#include <cstddef>

#include "cthtml/types.hpp"
#include "cthtml/entities.hpp"
#include "cthtml/value.hpp"

// cthtml: compile-time HTML5.
//
//   constexpr cthtml::document doc = cthtml::parse(R"(
//       <!DOCTYPE html>
//       <title>Hi</title>
//       <ul id=nav>
//           <li>Docs
//           <li>Code
//       </ul>)");
//
//   static_assert(doc.ok());
//   static_assert(doc.title() == "Hi");
//   static_assert(doc.body()["ul"].count("li") == 2);
//
// One parser, usable both ways: cthtml::parse(std::string_view) is a
// constexpr VALUE parser - fold a page in a static_assert, or load a
// runtime string with the same call. The result is an owned document
// (html > (head, body), like a browser DOM) with navigation, a
// CSS-subset selector engine (query/query_all/get_element_by_id) and
// serialize() back to minified HTML. A mistake is a value - ok() /
// error() / error_where() - never a compile error.
//
// HTML5's conveniences are understood: void elements (<br>), optional
// end tags (<li>, <p>, <td>...), implied <html>/<head>/<body>,
// case-insensitive names, boolean and unquoted attributes, DOCTYPE,
// raw-text <script>/<style> and RCDATA <title>/<textarea>, named and
// numeric character references (entities.hpp - the generated WHATWG
// table). Tree construction runs the same insertion logic a browser
// parser would, over values.

#endif
