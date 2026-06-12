# Runs fast365 over every .docx in tests/corpus and classifies the results:
#   OK       - exit 0 (and, with -CheckBalance, structurally balanced HTML)
#   UNBAL    - converted but open/close tag counts differ (emitter bug!)
#   ERROR    - graceful nonzero exit (corrupt/encrypted input is expected to fail)
#   CRASH    - abnormal termination (negative/NTSTATUS exit code)
#   TIMEOUT  - did not finish within 30 s
param(
    [string]$Exe  = (Join-Path $PSScriptRoot '..\build\fast365.exe'),
    [string]$Root = (Join-Path $PSScriptRoot 'corpus'),
    [switch]$CheckBalance
)
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path $Exe).Path
$outFile = Join-Path $env:TEMP 'f365_corpus_out.html'

$files = Get-ChildItem $Root -Recurse -Filter *.docx | Where-Object { $_.Length -gt 0 }
$results = @{ OK = 0; UNBAL = 0; ERROR = 0; CRASH = 0; TIMEOUT = 0 }
$bad = [System.Collections.Generic.List[object]]::new()
$totalMs = 0.0
$tags = 'p','table','tr','td','th','ul','ol','li','a','strong','em','u','s','sup','sub','span','section'
$i = 0

foreach ($f in $files) {
    $i++
    if ($i % 500 -eq 0) { Write-Host "  ...$i / $($files.Count)" }
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    $psi.Arguments = "`"$($f.FullName)`" -o `"$outFile`" --quiet"
    $psi.UseShellExecute = $false
    $psi.RedirectStandardError = $true
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = [System.Diagnostics.Process]::Start($psi)
    $stderr = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(30000)) {
        $p.Kill()
        $results.TIMEOUT++
        $bad.Add([pscustomobject]@{ Kind = 'TIMEOUT'; File = $f.FullName; Detail = '' })
        continue
    }
    $sw.Stop()
    $totalMs += $sw.Elapsed.TotalMilliseconds
    $code = $p.ExitCode
    if ($code -eq 0) {
        if ($CheckBalance) {
            $html = [System.IO.File]::ReadAllText($outFile)
            $imbalance = @()
            foreach ($t in $tags) {
                $open = [regex]::Matches($html, "<$t[ >]").Count
                $close = [regex]::Matches($html, "</$t>").Count
                if ($open -ne $close) { $imbalance += "${t}:$open/$close" }
            }
            if ($imbalance.Count -gt 0) {
                $results.UNBAL++
                $bad.Add([pscustomobject]@{ Kind = 'UNBAL'; File = $f.FullName; Detail = ($imbalance -join ' ') })
                continue
            }
        }
        $results.OK++
    } elseif ($code -eq 1 -or $code -eq 2) {
        $results.ERROR++
        $bad.Add([pscustomobject]@{ Kind = 'ERROR'; File = $f.FullName; Detail = $stderr.Result.Trim() })
    } else {
        $results.CRASH++
        $bad.Add([pscustomobject]@{ Kind = 'CRASH'; File = $f.FullName; Detail = "exit 0x$('{0:X8}' -f $code)" })
    }
}

"files: $($files.Count)   OK: $($results.OK)   unbalanced: $($results.UNBAL)   graceful errors: $($results.ERROR)   CRASHES: $($results.CRASH)   TIMEOUTS: $($results.TIMEOUT)"
"total convert time: $([math]::Round($totalMs)) ms"
""
foreach ($b in $bad | Sort-Object Kind) {
    "[{0}] {1}`n        {2}" -f $b.Kind, $b.File.Replace("$Root\", ''), $b.Detail
}
