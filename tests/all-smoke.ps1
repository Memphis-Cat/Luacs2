$ErrorActionPreference = "Stop"

$utf8 = [System.Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$global:OutputEncoding = $utf8

function ConvertFrom-CodePoints {
    param(
        [Parameter(Mandatory = $true)]
        [int[]]$CodePoints
    )

    return -join ($CodePoints | ForEach-Object { [char]$_ })
}

function Assert-AsciiScript {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    for ($index = 0; $index -lt $bytes.Length; ++$index) {
        if ($bytes[$index] -gt 0x7F) {
            $message = (
                "PowerShell 5.1 compatibility requires ASCII-only test " +
                "sources; '{0}' contains byte 0x{1:X2} at offset {2}") -f
                $Path, $bytes[$index], $index
            throw $message
        }
    }
}

$mojibakeSequences = @(
    (ConvertFrom-CodePoints @(0x0393, 0x00F2)),
    (ConvertFrom-CodePoints @(0x0393, 0x00F6)),
    (ConvertFrom-CodePoints @(0x0393, 0x00C7)),
    (ConvertFrom-CodePoints @(0x0393, 0x00F9)),
    (ConvertFrom-CodePoints @(0x00CE, 0x201C)),
    (ConvertFrom-CodePoints @(0x00C3)),
    (ConvertFrom-CodePoints @(0x00E2)),
    (ConvertFrom-CodePoints @(0xFFFD))
)

$root = (Resolve-Path "$PSScriptRoot\..").Path
$utf8Runner = Join-Path $PSScriptRoot "invoke-utf8.ps1"
$tests = @(
    "compiler-smoke.ps1",
    "generated-source-smoke.ps1",
    "logging-encoding-smoke.ps1",
    "deploy-smoke.ps1"
)

if (-not (Test-Path -LiteralPath $utf8Runner -PathType Leaf)) {
    throw "required strict UTF-8 runner is missing: $utf8Runner"
}

foreach ($script in Get-ChildItem -LiteralPath $PSScriptRoot -Filter "*.ps1") {
    Assert-AsciiScript $script.FullName
}

$probePath = Join-Path ([System.IO.Path]::GetTempPath()) (
    "luacs-stale-exit-probe-" + [Guid]::NewGuid().ToString("N") + ".ps1")
try {
    $probeSource = @'
& cmd.exe /d /c exit 7
Write-Host "stale native exit-code probe completed"
'@
    [System.IO.File]::WriteAllText(
        $probePath,
        $probeSource,
        [System.Text.ASCIIEncoding]::new())

    $probeOutput = (& powershell -NoProfile -ExecutionPolicy Bypass `
        -File $utf8Runner -Script $probePath 2>&1 | Out-String)
    $probeExitCode = $LASTEXITCODE
    if ($probeExitCode -ne 0) {
        throw (
            "UTF-8 wrapper leaked stale native exit code " +
            "$probeExitCode after a successful PowerShell test:`n" +
            $probeOutput)
    }
}
finally {
    Remove-Item -LiteralPath $probePath -Force -ErrorAction SilentlyContinue
}

foreach ($test in $tests) {
    $path = Join-Path $PSScriptRoot $test
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "required LuaCS smoke test is missing: $path"
    }

    Write-Host ""
    Write-Host "=== Running $test ==="
    $output = (& powershell -NoProfile -ExecutionPolicy Bypass `
        -File $utf8Runner -Script $path 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE

    foreach ($badSequence in $mojibakeSequences) {
        if ($output.Contains($badSequence)) {
            $codePoints = -join ($badSequence.ToCharArray() | ForEach-Object {
                "U+{0:X4} " -f [int]$_
            })
            throw (
                "$test produced mojibake code points $($codePoints.Trim()):`n" +
                $output)
        }
    }

    Write-Host $output.TrimEnd()
    if ($exitCode -ne 0) {
        throw "$test failed with exit code $exitCode"
    }
}

Write-Host ""
Write-Host (
    "All LuaCS smoke suites passed: compiler/package integrity, generated " +
    "signature scanner diagnostics, UTF-8 output, current-session error " +
    "logging, safe spawn readiness, build stamps, and deployment failure paths.")
