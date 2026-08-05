$ErrorActionPreference = "Stop"

$root = Resolve-Path "$PSScriptRoot\.."
$luaCs = Join-Path $root "build\package\game\csgo\addons\LuaCS"
$compiler = Join-Path $luaCs "scripting\compile.exe"
$source = Join-Path $luaCs "scripting\example_welcome.lua"
$output = Join-Path $luaCs "plugins\example_welcome.smg"

if (-not (Test-Path $compiler)) { throw "compile.exe was not built" }

& $compiler
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $output)) {
    throw "initial example_welcome.lua compilation failed"
}
$firstHash = (Get-FileHash $output -Algorithm SHA256).Hash

$second = (& $compiler 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $second -notmatch "unchanged; skipped") {
    throw "unchanged source was not skipped"
}

$bytes = [System.IO.File]::ReadAllBytes($output)
$bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 0x5A
[System.IO.File]::WriteAllBytes($output, $bytes)
& $compiler
if ($LASTEXITCODE -ne 0) { throw "compiler did not recover from a corrupted SMG" }
$repairedHash = (Get-FileHash $output -Algorithm SHA256).Hash
if ($repairedHash -eq $firstHash) {
    throw "corrupted SMG was not replaced with a newly encrypted package"
}

$badSource = Join-Path $luaCs "scripting\syntax_failure.lua"
$badOutput = Join-Path $luaCs "plugins\syntax_failure.smg"
Set-Content -Path $badSource -Encoding UTF8 -NoNewline -Value "print('valid')"
& $compiler $badSource
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $badOutput)) {
    throw "valid syntax_failure baseline did not compile"
}
$baselineHash = (Get-FileHash $badOutput -Algorithm SHA256).Hash

Set-Content -Path $badSource -Encoding UTF8 -NoNewline -Value "local broken = function("
& $compiler $badSource
if ($LASTEXITCODE -eq 0) { throw "invalid Lua unexpectedly compiled" }
$afterFailureHash = (Get-FileHash $badOutput -Algorithm SHA256).Hash
if ($baselineHash -ne $afterFailureHash) {
    throw "syntax error replaced the previous SMG"
}

Write-Host "LuaCS compiler smoke tests passed."
exit 0
