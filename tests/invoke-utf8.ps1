param(
    [Parameter(Mandatory = $true)]
    [string]$Script
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Script -PathType Leaf)) {
    throw "smoke test script is missing: $Script"
}

$utf8 = [System.Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$global:OutputEncoding = $utf8

$scriptPath = (Resolve-Path -LiteralPath $Script).Path
$global:LASTEXITCODE = 0

try {
    & $scriptPath
    if (-not $?) {
        exit 1
    }
}
catch {
    Write-Error $_
    exit 1
}

# A successful PowerShell test may intentionally run native commands that
# return nonzero while verifying failure paths. Do not leak that stale native
# exit code after the script itself completed successfully.
exit 0
