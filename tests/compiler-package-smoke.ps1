$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$addons = Join-Path $root "build\package\game\csgo\addons"
$luaCs = Join-Path $addons "LuaCS"
$bin = Join-Path $luaCs "bin\win64"
$compiler = Join-Path $luaCs "scripting\compile.exe"
$source = Join-Path $luaCs "scripting\example_welcome.lua"
$output = Join-Path $luaCs "plugins\example_welcome.smg"
$key = Join-Path $luaCs "config\luacs.key"
$badSource = Join-Path $luaCs "scripting\syntax_failure.lua"
$badOutput = Join-Path $luaCs "plugins\syntax_failure.smg"
$vdf = Join-Path $addons "metamod\luacs2.vdf"
$advancedGamedata = Join-Path $luaCs "gamedata\reference\advanced_windows_gamedata.json"

function Invoke-Compiler {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $text = (& $compiler --no-pause @Arguments 2>&1 | Out-String)
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = $text
    }
}

try {
    foreach ($name in @(
        "luacs2.dll", "lua55.dll", "events.dll", "timers.dll",
        "players.dll", "commands.dll", "math.dll", "weapons.dll",
        "hud.dll", "cvars.dll", "teams.dll", "rounds.dll",
        "entities.dll", "sounds.dll", "properties.dll", "traces.dll",
        "grenades.dll"
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $name) -PathType Leaf)) {
            throw "required native file is missing from bin\win64: $name"
        }
    }

    foreach ($required in @($compiler, $source, $vdf, $advancedGamedata)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "required packaged file is missing: $required"
        }
    }

    if ((Get-Content -LiteralPath $vdf -Raw) -notmatch
        'addons/LuaCS/bin/win64/luacs2') {
        throw "luacs2.vdf does not point to the packaged win64 DLL"
    }

    $advancedText = Get-Content -LiteralPath $advancedGamedata -Raw
    foreach ($entry in @("GameTraceManager", "TraceFunc", "TraceShape")) {
        if ($advancedText -notmatch [regex]::Escape($entry)) {
            throw "advanced gamedata is missing '$entry'"
        }
    }

    Remove-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
    $first = Invoke-Compiler @($source)
    if ($first.ExitCode -ne 0 -or
        -not (Test-Path -LiteralPath $output -PathType Leaf)) {
        throw "initial example_welcome.lua compilation failed:`n$($first.Output)"
    }
    foreach ($label in @(
        "Code size", "Data size", "Stack/heap size",
        "Total requirements", "Compilation time"
    )) {
        if ($first.Output -notmatch [regex]::Escape($label)) {
            throw "modern compiler report did not contain '$label'"
        }
    }

    $second = Invoke-Compiler @($source)
    if ($second.ExitCode -ne 0 -or
        $second.Output -notmatch "ALREADY COMPILED") {
        throw "unchanged source was not reported as already compiled:`n$($second.Output)"
    }

    $bytes = [System.IO.File]::ReadAllBytes($output)
    if ($bytes.Length -lt 2) {
        throw "compiled SMG is unexpectedly too small to corrupt safely"
    }
    $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 0x5A
    [System.IO.File]::WriteAllBytes($output, $bytes)
    $corruptedHash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash

    $repair = Invoke-Compiler @($source)
    if ($repair.ExitCode -ne 0) {
        throw "compiler did not recover from a corrupted SMG:`n$($repair.Output)"
    }
    $repairedHash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
    if ($repairedHash -eq $corruptedHash) {
        throw "corrupted SMG was not replaced"
    }

    Set-Content -LiteralPath $badSource -Encoding UTF8 -NoNewline -Value "print('valid')"
    $baseline = Invoke-Compiler @($badSource)
    if ($baseline.ExitCode -ne 0 -or
        -not (Test-Path -LiteralPath $badOutput -PathType Leaf)) {
        throw "valid syntax_failure baseline did not compile:`n$($baseline.Output)"
    }
    $baselineHash = (Get-FileHash -LiteralPath $badOutput -Algorithm SHA256).Hash

    Set-Content -LiteralPath $badSource -Encoding UTF8 -NoNewline -Value "local broken = function("
    $failure = Invoke-Compiler @($badSource)
    if ($failure.ExitCode -eq 0) {
        throw "invalid Lua unexpectedly compiled"
    }
    if (-not (Test-Path -LiteralPath $badOutput -PathType Leaf)) {
        throw "syntax failure removed the previous valid SMG"
    }
    $afterFailureHash = (Get-FileHash -LiteralPath $badOutput -Algorithm SHA256).Hash
    if ($baselineHash -ne $afterFailureHash) {
        throw "syntax error replaced the previous valid SMG"
    }

    Write-Host (
        "LuaCS compiler/package smoke tests passed: required binaries and " +
        "gamedata are packaged, example compilation and cache detection work, " +
        "corrupted SMGs are repaired, and syntax failures preserve the last " +
        "valid package.")
}
finally {
    Remove-Item -LiteralPath $output, $badOutput, $badSource, $key `
        -Force -ErrorAction SilentlyContinue
}

exit 0
