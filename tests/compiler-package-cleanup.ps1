$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$key = Join-Path $root "build\package\game\csgo\addons\LuaCS\config\luacs.key"

if (Test-Path -LiteralPath $key) {
    Remove-Item -LiteralPath $key -Force
}
if (Test-Path -LiteralPath $key) {
    throw "compiler-generated test key remained in the deployable package"
}

Write-Host "LuaCS compiler package cleanup passed: no test luacs.key remains in the deployable package."
