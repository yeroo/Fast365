# Fast365

A very fast, zero-dependency command-line converter from Microsoft Word
`.docx` to HTML, written in C++17 for Windows.

Everything is implemented from scratch in this repository — the ZIP reader,
the DEFLATE decompressor, the XML parser and the WordprocessingML-to-HTML
conversion. No zlib, no libxml, no runtime DLLs beyond the OS: the result is
a single small `fast365.exe`.

## Build

From a regular command prompt (the script finds Visual Studio by itself):

```bat
build.bat
```

Or with CMake:

```bat
cmake -B build-cmake -G "Visual Studio 17 2022"
cmake --build build-cmake --config Release
```

## Usage

```text
fast365 <input.docx> [options]

  -o <file>      output path (default: input name with .html; "-" for stdout)
  --fragment     emit body content only, without the <html> wrapper
  --no-images    do not embed images
  --title <t>    override the document title
  --quiet        suppress the timing summary
  --version      print version and exit
```

Example:

```bat
fast365 report.docx
fast365 report.docx -o - --fragment > body.html
```

## What is supported (v0.2)

- Paragraphs, headings (style name/id mapping for `Heading 1`–`6` and
  `Title`, plus `w:outlineLvl` fallback for custom heading styles)
- Run formatting: bold, italic, underline, strikethrough,
  superscript/subscript, text color, highlight, caps, small caps,
  hidden text; character styles (`w:rStyle`) as the formatting base
- Paragraph alignment, indentation (`w:ind`), shading, RTL (`w:bidi`)
- Hyperlinks: `w:hyperlink` (external + anchors + tooltips), simple fields
  (`w:fldSimple`) and complex `HYPERLINK` fields
  (`w:fldChar`/`w:instrText`); bookmarks become anchor targets
- Bullet and numbered lists with nesting (`numbering.xml` aware):
  letter/roman formats map to `<ol type>`, `w:start`/`startOverride` and
  interrupted lists map to `<ol start>`, style-bound numbering
  (List Bullet / List Number) resolves through `styles.xml`
- Tables: horizontal merges (`gridSpan` → colspan), vertical merges
  (`vMerge` → rowspan), cell shading, vertical alignment, header rows
  (`w:tblHeader` → `<th>`), nested tables
- Footnotes and endnotes with numbered references and backlinks,
  resolved through their own relationship parts
- Images (DrawingML and legacy VML), embedded as base64 data URIs with
  pixel dimensions taken from the document
- Line breaks, tabs, soft/no-break hyphens, symbol runs (`w:sym`),
  text boxes, content controls (`w:sdt`), `mc:AlternateContent`
  (fallback branch, no duplication), tracked-change insertions
  (deletions are dropped, as Word renders them accepted), math
  (linearized text fallback)
- Document title from `docProps/core.xml`; main part located via
  `_rels/.rels` (non-standard part names work)

Robustness: depth-limited recursion (hostile nesting cannot overflow the
stack), decompression output caps (zip bombs fail fast), graceful errors
on corrupt or encrypted input.

Known limitations: field instructions other than HYPERLINK (TOC, PAGEREF)
emit only their cached result text; floating-object positioning, columns
and page headers/footers are not reproduced; OMML math is flattened to
plain text.

## Tests

```powershell
pwsh tests/make_sample.ps1       # generates tests/sample.docx
build\fast365.exe tests/sample.docx
pwsh tests/run_corpus.ps1        # converts every .docx under tests/corpus
```

`tests/run_corpus.ps1` expects real-world fixtures under `tests/corpus`
(not checked in). Pass `-CheckBalance` to additionally verify that every
produced HTML file has balanced open/close tags. The suite used during
development — 3,250 documents from ten open-source test suites:

```bash
cd tests/corpus
git clone --depth 1 https://github.com/mwilliamson/mammoth.js mammoth
git clone --depth 1 https://github.com/mwilliamson/python-mammoth python-mammoth
git clone --depth 1 https://github.com/python-openxml/python-docx python-docx
git clone --depth 1 https://github.com/ShayHill/docx2python docx2python
git clone --depth 1 --filter=blob:none --no-checkout https://github.com/jgm/pandoc pandoc \
  && git -C pandoc sparse-checkout set test/docx && git -C pandoc checkout
git clone --depth 1 --filter=blob:none --no-checkout https://github.com/apache/poi poi \
  && git -C poi sparse-checkout set test-data/document && git -C poi checkout
# for the rest, fetch only the .docx blobs via sparse checkout:
for repo in LibreOffice/core apache/tika dotnet/Open-XML-SDK plutext/docx4j; do
  dir=$(basename "$repo" | tr '[:upper:]' '[:lower:]')
  git clone --depth 1 --filter=blob:none --no-checkout "https://github.com/$repo" "$dir" \
    && git -C "$dir" sparse-checkout set --no-cone '**/*.docx' \
    && git -C "$dir" checkout
done
```

The LibreOffice harvest alone contributes ~2,200 regression documents
filed from real-world bugs.

Current results: **3,218 OK, 0 crashes, 0 timeouts, 0 unbalanced outputs**;
32 graceful errors, all of them correct rejections — password-protected
documents (OLE2 containers), truncated archives, fuzzer-minimized garbage,
and ODF files mislabeled as `.docx` in LibreOffice's format-detection
tests. fast365 identifies the latter two categories explicitly in its
error messages.

## License

MIT — free to use, modify and distribute. See [LICENSE](LICENSE).

## Layout

```text
src/inflate.*    DEFLATE (RFC 1951) decoder
src/zip.*        ZIP central-directory reader (stored + deflate)
src/xml.*        zero-copy XML pull parser
src/docx.*       WordprocessingML -> HTML conversion
src/html_util.*  escaping and base64
src/main.cpp     CLI
```
