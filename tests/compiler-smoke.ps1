$ErrorActionPreference = "Stop"

$root = Resolve-Path "$PSScriptRoot\.."
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

try {
    $requiredNativeFiles = @(
        "luacs2.dll",
        "lua55.dll",
        "events.dll",
        "timers.dll",
        "players.dll",
        "commands.dll",
        "math.dll",
        "weapons.dll",
        "hud.dll",
        "cvars.dll",
        "teams.dll",
        "rounds.dll",
        "entities.dll",
        "sounds.dll",
        "properties.dll",
        "traces.dll",
        "grenades.dll"
    )
    foreach ($name in $requiredNativeFiles) {
        $path = Join-Path $bin $name
        if (-not (Test-Path $path)) {
            throw "required native file is missing from bin\win64: $name"
        }
    }

    if (-not (Test-Path $advancedGamedata)) {
        throw "advanced Windows gamedata was not packaged"
    }
    $advancedText = Get-Content $advancedGamedata -Raw
    foreach ($entry in @("GameTraceManager", "TraceFunc", "TraceShape")) {
        if ($advancedText -notmatch [regex]::Escape($entry)) {
            throw "advanced gamedata is missing '$entry'"
        }
    }

    if (-not (Test-Path $vdf)) { throw "luacs2.vdf was not packaged" }
    $vdfText = Get-Content $vdf -Raw
    if ($vdfText -notmatch 'addons/LuaCS/bin/win64/luacs2') {
        throw "luacs2.vdf does not point to the packaged win64 DLL"
    }

    if (-not (Test-Path $compiler)) { throw "compile.exe was not built" }

    $first = (& $compiler --no-pause 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $output)) {
        throw "initial example_welcome.lua compilation failed`n$first"
    }
    foreach ($label in @("Code size", "Data size", "Stack/heap size",
                          "Total requirements", "Compilation time")) {
        if ($first -notmatch [regex]::Escape($label)) {
            throw "modern compiler report did not contain '$label'"
        }
    }
    $firstHash = (Get-FileHash $output -Algorithm SHA256).Hash

    $second = (& $compiler --no-pause 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or $second -notmatch "ALREADY COMPILED") {
        throw "unchanged source was not reported as already compiled"
    }

    $bytes = [System.IO.File]::ReadAllBytes($output)
    $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 0x5A
    [System.IO.File]::WriteAllBytes($output, $bytes)
    & $compiler --no-pause
    if ($LASTEXITCODE -ne 0) { throw "compiler did not recover from a corrupted SMG" }
    $repairedHash = (Get-FileHash $output -Algorithm SHA256).Hash
    if ($repairedHash -eq $firstHash) {
        throw "corrupted SMG was not replaced with a newly encrypted package"
    }

    Set-Content -Path $badSource -Encoding UTF8 -NoNewline -Value "print('valid')"
    & $compiler --no-pause $badSource
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $badOutput)) {
        throw "valid syntax_failure baseline did not compile"
    }
    $baselineHash = (Get-FileHash $badOutput -Algorithm SHA256).Hash

    Set-Content -Path $badSource -Encoding UTF8 -NoNewline -Value "local broken = function("
    & $compiler --no-pause $badSource
    if ($LASTEXITCODE -eq 0) { throw "invalid Lua unexpectedly compiled" }
    $afterFailureHash = (Get-FileHash $badOutput -Algorithm SHA256).Hash
    if ($baselineHash -ne $afterFailureHash) {
        throw "syntax error replaced the previous SMG"
    }

    Write-Host "LuaCS package and compiler smoke tests passed."
}
finally {
    Remove-Item $output, $badOutput, $badSource, $key -Force -ErrorAction SilentlyContinue
}

exit 0
