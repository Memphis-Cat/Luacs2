$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$tests = @(
    "compiler-smoke.ps1",
    "generated-source-smoke.ps1",
    "deploy-smoke.ps1"
)

foreach ($test in $tests) {
    $path = Join-Path $PSScriptRoot $test
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "required LuaCS smoke test is missing: $path"
    }

    Write-Host ""
    Write-Host "=== Running $test ==="
    $output = (& powershell -NoProfile -ExecutionPolicy Bypass -File $path 2>&1 |
        Out-String)
    Write-Host $output.TrimEnd()
    if ($LASTEXITCODE -ne 0) {
        throw "$test failed with exit code $LASTEXITCODE"
    }
}

Write-Host ""
Write-Host (
    "All LuaCS smoke suites passed: compiler/package integrity, generated " +
    "signature scanner diagnostics, native error logging, build stamps, and " +
    "deployment failure paths.")
