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

& (Resolve-Path -LiteralPath $Script).Path
exit $LASTEXITCODE
