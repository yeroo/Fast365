# Fast365

Zero-dependency C++17 Windows CLI that converts `.docx` to HTML. Speed is a
core requirement; the binary must stay free of third-party libraries (no
zlib, no libxml — everything is implemented in `src/`).

## Build & test

```bat
build.bat                                 :: MSVC, output: build\fast365.exe
pwsh tests/make_sample.ps1                :: regenerate tests\sample.docx
build\fast365.exe tests\sample.docx       :: smoke test, writes tests\sample.html
pwsh tests/run_corpus.ps1                 :: 312-file real-world corpus (see README
                                          ::  for the clone commands; not checked in)
```

The corpus run must stay at 0 crashes / 0 timeouts. The 15 "graceful
errors" are POI's intentionally corrupt/encrypted fixtures — those must
keep failing with a clean message, not crash.

CMake also works (`cmake -B build-cmake && cmake --build build-cmake --config Release`).

## Architecture

One streaming pass, bottom-up layering — each layer knows nothing about the
ones above it:

- `src/inflate.*` — DEFLATE decoder (RFC 1951), puff-style canonical Huffman.
- `src/zip.*` — ZIP reader over an in-memory buffer; trusts the central
  directory, supports stored + deflate only (all DOCX ever uses).
- `src/xml.*` — zero-copy pull parser. All `string_view`s point into the
  input buffer. Element names keep their prefix (`"w:p"`); attributes are
  valid only immediately after a `Start` event. Self-closing tags deliver
  `Start` then `End`.
- `src/docx.*` — the `Converter` class walks `word/document.xml` recursively
  (one method per construct), with lookup tables prebuilt from `styles.xml`
  (headings, character styles, style-bound numbering), `numbering.xml`
  (list formats, starts, per-instance overrides) and the `.rels` parts
  (hyperlinks/images; footnotes/endnotes swap in their own rels map).
  Lists are emitted via a small stack machine (`setListState`) that keeps
  `<li>` open so nested lists stay valid HTML; per-(numId,ilvl) item
  counters drive `<ol start>` for interrupted/restarted lists. Tables are
  buffered into a grid model (`collectRows`/`emitTable`) so `vMerge`
  resolves to rowspan; cell content is captured by swapping `m_out`.
  Complex fields (`w:fldChar`) run through a small stack in `m_fields`;
  only HYPERLINK becomes markup, everything else falls back to its cached
  result text. Recursion is depth-capped (`kMaxDepth`) — keep the guard
  when adding new recursive paths.
- `src/html_util.*` — escaping, base64. `src/main.cpp` — CLI only.

## Conventions

- Text inside `w:t` is passed through raw: XML escaping is valid HTML
  escaping. Only decoded strings (attributes, URLs) need re-escaping.
- Unknown elements in content are treated as *transparent containers*
  (recurse into them) rather than skipped, so text in unanticipated wrappers
  survives. Skip explicitly only what must not render (`w:del`,
  `w:instrText`, property bags).
- Performance habits: no allocation in the XML parser, `reserve` output
  buffers, single pass over the document part.
- MSVC `/W4` must stay clean.
