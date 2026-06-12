# Generates tests/sample.docx — a synthetic document exercising headings,
# run formatting, hyperlinks, bullet/numbered lists and a table.
$ErrorActionPreference = 'Stop'
$outPath = Join-Path $PSScriptRoot 'sample.docx'

$contentTypes = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
<Override PartName="/word/numbering.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"/>
<Override PartName="/word/footnotes.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml"/>
</Types>
'@

$rootRels = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>
'@

$docRels = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId10" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/?a=1&amp;b=2" TargetMode="External"/>
</Relationships>
'@

$styles = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/></w:style>
<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/></w:style>
<w:style w:type="character" w:styleId="Strong"><w:name w:val="Strong"/><w:rPr><w:b/></w:rPr></w:style>
</w:styles>
'@

$footnotes = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:footnotes xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:footnote w:type="separator" w:id="-1"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>
<w:footnote w:id="1"><w:p><w:r><w:footnoteRef/></w:r><w:r><w:t xml:space="preserve">A footnote with </w:t></w:r><w:r><w:rPr><w:i/></w:rPr><w:t>formatting</w:t></w:r><w:r><w:t>.</w:t></w:r></w:p></w:footnote>
</w:footnotes>
'@

$numbering = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:numbering xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:abstractNum w:abstractNumId="0"><w:lvl w:ilvl="0"><w:numFmt w:val="bullet"/></w:lvl><w:lvl w:ilvl="1"><w:numFmt w:val="bullet"/></w:lvl></w:abstractNum>
<w:abstractNum w:abstractNumId="1"><w:lvl w:ilvl="0"><w:numFmt w:val="decimal"/></w:lvl></w:abstractNum>
<w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num>
<w:num w:numId="2"><w:abstractNumId w:val="1"/></w:num>
</w:numbering>
'@

$document = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<w:body>
<w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>Fast365 Sample</w:t></w:r></w:p>
<w:p><w:r><w:t xml:space="preserve">Plain, </w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>bold</w:t></w:r><w:r><w:t xml:space="preserve">, </w:t></w:r><w:r><w:rPr><w:i/></w:rPr><w:t>italic</w:t></w:r><w:r><w:t xml:space="preserve"> and </w:t></w:r><w:r><w:rPr><w:b/><w:i/><w:u w:val="single"/></w:rPr><w:t>all three</w:t></w:r><w:r><w:t xml:space="preserve">. Special chars: &amp; &lt; &gt; caf&#233;.</w:t></w:r></w:p>
<w:p><w:r><w:t xml:space="preserve">Visit </w:t></w:r><w:hyperlink r:id="rId10"><w:r><w:rPr><w:u w:val="single"/></w:rPr><w:t>example.com</w:t></w:r></w:hyperlink><w:r><w:t xml:space="preserve"> and a </w:t></w:r><w:r><w:rPr><w:color w:val="FF0000"/></w:rPr><w:t>red word</w:t></w:r><w:r><w:t xml:space="preserve"> plus x</w:t></w:r><w:r><w:rPr><w:vertAlign w:val="superscript"/></w:rPr><w:t>2</w:t></w:r><w:r><w:t>.</w:t></w:r></w:p>
<w:p><w:pPr><w:pStyle w:val="Heading2"/></w:pPr><w:r><w:t>Lists</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>Bullet one</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="1"/><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>Nested bullet</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>Bullet two</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="2"/></w:numPr></w:pPr><w:r><w:t>Step one</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="2"/></w:numPr></w:pPr><w:r><w:t>Step two</w:t></w:r></w:p>
<w:p><w:pPr><w:pStyle w:val="Heading2"/></w:pPr><w:r><w:t>Table</w:t></w:r></w:p>
<w:tbl>
<w:tr><w:tc><w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Name</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:rPr><w:b/></w:rPr><w:t>Value</w:t></w:r></w:p></w:tc></w:tr>
<w:tr><w:tc><w:p><w:r><w:t>Speed</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>Fast</w:t></w:r></w:p></w:tc></w:tr>
<w:tr><w:tc><w:tcPr><w:gridSpan w:val="2"/></w:tcPr><w:p><w:pPr><w:jc w:val="center"/></w:pPr><w:r><w:t>Merged centered cell</w:t></w:r></w:p></w:tc></w:tr>
</w:tbl>
<w:p><w:pPr><w:jc w:val="right"/></w:pPr><w:r><w:t>Right-aligned closing line.</w:t></w:r><w:r><w:br/></w:r><w:r><w:t>After a line break.</w:t></w:r></w:p>
<w:p><w:pPr><w:pStyle w:val="Heading2"/></w:pPr><w:r><w:t>Advanced</w:t></w:r></w:p>
<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="2"/></w:numPr></w:pPr><w:r><w:t>Step three, continued after the table</w:t></w:r></w:p>
<w:p><w:r><w:t xml:space="preserve">A footnote ref</w:t></w:r><w:r><w:rPr><w:vertAlign w:val="superscript"/></w:rPr><w:footnoteReference w:id="1"/></w:r><w:r><w:t xml:space="preserve"> and a </w:t></w:r><w:r><w:rPr><w:rStyle w:val="Strong"/></w:rPr><w:t>char-styled run</w:t></w:r><w:r><w:t xml:space="preserve"> and </w:t></w:r><w:r><w:rPr><w:highlight w:val="yellow"/></w:rPr><w:t>highlighted</w:t></w:r><w:r><w:t xml:space="preserve"> and </w:t></w:r><w:r><w:rPr><w:caps/></w:rPr><w:t>shouted</w:t></w:r><w:r><w:t> text.</w:t></w:r></w:p>
<w:p><w:r><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText xml:space="preserve"> HYPERLINK "https://anthropic.com" </w:instrText></w:r><w:r><w:fldChar w:fldCharType="separate"/></w:r><w:r><w:t>A field hyperlink</w:t></w:r><w:r><w:fldChar w:fldCharType="end"/></w:r><w:r><w:t xml:space="preserve"> and a </w:t></w:r><w:fldSimple w:instr=" HYPERLINK &quot;https://example.org&quot; "><w:r><w:t>simple field link</w:t></w:r></w:fldSimple><w:r><w:t>.</w:t></w:r></w:p>
<w:tbl>
<w:tr><w:tc><w:tcPr><w:vMerge w:val="restart"/><w:shd w:val="clear" w:fill="DDEEFF"/></w:tcPr><w:p><w:r><w:t>Spans two rows</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>R1C2</w:t></w:r></w:p></w:tc></w:tr>
<w:tr><w:tc><w:tcPr><w:vMerge/></w:tcPr><w:p/></w:tc><w:tc><w:p><w:r><w:t>R2C2</w:t></w:r></w:p></w:tc></w:tr>
</w:tbl>
<w:sectPr/>
</w:body>
</w:document>
'@

if (Test-Path $outPath) { Remove-Item $outPath }
$fs = [System.IO.File]::Create($outPath)
$zip = [System.IO.Compression.ZipArchive]::new($fs, [System.IO.Compression.ZipArchiveMode]::Create)
$entries = [ordered]@{
    '[Content_Types].xml'             = $contentTypes
    '_rels/.rels'                     = $rootRels
    'word/document.xml'               = $document
    'word/_rels/document.xml.rels'    = $docRels
    'word/styles.xml'                 = $styles
    'word/numbering.xml'              = $numbering
    'word/footnotes.xml'              = $footnotes
}
foreach ($name in $entries.Keys) {
    $entry = $zip.CreateEntry($name)
    $writer = [System.IO.StreamWriter]::new($entry.Open(), [System.Text.UTF8Encoding]::new($false))
    $writer.Write($entries[$name])
    $writer.Dispose()
}
$zip.Dispose()
$fs.Dispose()
Write-Host "Wrote $outPath"
