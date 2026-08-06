$ErrorActionPreference = "Stop"

$utf8 = [System.Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$global:OutputEncoding = $utf8

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

    foreach ($badSequence in @(
        "Γò", "Γö", "ΓÇ", "Γù", "Ã", "â", [string][char]0xFFFD
    )) {
        if ($output.Contains($badSequence)) {
            throw (
                "$test produced mojibake sequence '$badSequence':`n" +
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
