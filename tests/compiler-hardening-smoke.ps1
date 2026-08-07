$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$luaCs = Join-Path $root "build\package\game\csgo\addons\LuaCS"
$compiler = Join-Path $luaCs "scripting\compile.exe"
$plugins = Join-Path $luaCs "plugins"
$deprecatedFile = Join-Path $luaCs "gamedata\deprecated_symbols.txt"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) (
    "luacs-compiler-hardening-" + [Guid]::NewGuid().ToString("N"))

if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw "compile.exe is missing: $compiler"
}

[System.IO.Directory]::CreateDirectory($temp) | Out-Null

function Invoke-Compiler {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $output = (& $compiler --no-pause @Arguments 2>&1 | Out-String)
    return [PSCustomObject]@{
        ExitCode = $LASTEXITCODE
        Output = $output
    }
}

function Require-Failure {
    param(
        [Parameter(Mandatory = $true)]$Result,
        [Parameter(Mandatory = $true)][string]$Label
    )
    if ($Result.ExitCode -eq 0) {
        throw "$Label unexpectedly succeeded:`n$($Result.Output)"
    }
}

$createdOutputs = New-Object System.Collections.Generic.List[string]
try {
    $missing = Join-Path $temp "missing.lua"
    $result = Invoke-Compiler @($missing)
    Require-Failure $result "missing source"
    if ($result.Output -notmatch "readable .lua file|resolve the requested path") {
        throw "missing-source failure did not explain the path problem:`n$($result.Output)"
    }

    $invalidUtf8 = Join-Path $temp "invalid_utf8.lua"
    [System.IO.File]::WriteAllBytes(
        $invalidUtf8,
        [byte[]](0x70,0x72,0x69,0x6E,0x74,0x28,0x22,0xC3,0x28,0x22,0x29))
    $result = Invoke-Compiler @($invalidUtf8)
    Require-Failure $result "invalid UTF-8 source"
    if ($result.Output -notmatch "valid UTF-8") {
        throw "invalid UTF-8 failure was not diagnosed:`n$($result.Output)"
    }

    $nulSource = Join-Path $temp "embedded_nul.lua"
    [System.IO.File]::WriteAllBytes(
        $nulSource,
        [byte[]](0x70,0x72,0x69,0x6E,0x74,0x28,0x31,0x29,0x00))
    $result = Invoke-Compiler @($nulSource)
    Require-Failure $result "embedded NUL source"
    if ($result.Output -notmatch "NUL byte") {
        throw "embedded-NUL failure was not diagnosed:`n$($result.Output)"
    }

    $semantic = Join-Path $temp "semantic_probe.lua"
    [System.IO.File]::WriteAllText(
        $semantic,
        "local events = require(`"cs2.events`")`nprint(events ~= nil)`n",
        [System.Text.UTF8Encoding]::new($false))
    $result = Invoke-Compiler @($semantic)
    if ($result.ExitCode -ne 0) {
        throw "valid semantic baseline did not compile:`n$($result.Output)"
    }
    $semanticOutput = Join-Path $plugins "semantic_probe.smg"
    if (-not (Test-Path -LiteralPath $semanticOutput -PathType Leaf)) {
        throw "semantic baseline SMG was not created"
    }
    $createdOutputs.Add($semanticOutput)
    $baselineHash =
        (Get-FileHash -LiteralPath $semanticOutput -Algorithm SHA256).Hash

    [System.IO.File]::WriteAllText(
        $semantic,
        "local bad = require(`"cs2.module_that_does_not_exist`")`nprint(bad)`n",
        [System.Text.UTF8Encoding]::new($false))
    $result = Invoke-Compiler @($semantic)
    Require-Failure $result "unknown LuaCS module source"
    if ($result.Output -notmatch "unknown LuaCS module") {
        throw "unknown-module failure was not diagnosed:`n$($result.Output)"
    }
    $afterSemanticHash =
        (Get-FileHash -LiteralPath $semanticOutput -Algorithm SHA256).Hash
    if ($afterSemanticHash -ne $baselineHash) {
        throw "semantic compiler failure replaced the previous valid SMG"
    }

    $upperSource = Join-Path $temp "uppercase_probe.LUA"
    [System.IO.File]::WriteAllText(
        $upperSource,
        "return 42`n",
        [System.Text.UTF8Encoding]::new($false))
    $result = Invoke-Compiler @($upperSource)
    if ($result.ExitCode -ne 0) {
        throw "uppercase .LUA source was rejected:`n$($result.Output)"
    }
    $upperOutput = Join-Path $plugins "uppercase_probe.smg"
    if (-not (Test-Path -LiteralPath $upperOutput -PathType Leaf)) {
        throw "uppercase .LUA source did not produce an SMG"
    }
    $createdOutputs.Add($upperOutput)

    $writeSource = Join-Path $temp "write_failure_probe.lua"
    [System.IO.File]::WriteAllText(
        $writeSource,
        "return 1`n",
        [System.Text.UTF8Encoding]::new($false))
    $result = Invoke-Compiler @($writeSource)
    if ($result.ExitCode -ne 0) {
        throw "write-failure baseline did not compile:`n$($result.Output)"
    }
    $writeOutput = Join-Path $plugins "write_failure_probe.smg"
    if (-not (Test-Path -LiteralPath $writeOutput -PathType Leaf)) {
        throw "write-failure baseline SMG was not created"
    }
    $createdOutputs.Add($writeOutput)
    $writeHash = (Get-FileHash -LiteralPath $writeOutput -Algorithm SHA256).Hash
    (Get-Item -LiteralPath $writeOutput).IsReadOnly = $true
    [System.IO.File]::WriteAllText(
        $writeSource,
        "return 2`n",
        [System.Text.UTF8Encoding]::new($false))
    $result = Invoke-Compiler @($writeSource)
    Require-Failure $result "authenticated output write failure"
    (Get-Item -LiteralPath $writeOutput).IsReadOnly = $false
    $writeHashAfter =
        (Get-FileHash -LiteralPath $writeOutput -Algorithm SHA256).Hash
    if ($writeHashAfter -ne $writeHash) {
        throw "failed atomic output replacement modified the previous valid SMG"
    }

    if (Test-Path -LiteralPath $deprecatedFile -PathType Leaf) {
        $deprecated = Get-Content -LiteralPath $deprecatedFile |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ -and -not $_.StartsWith("#") } |
            Select-Object -First 1
        if ($deprecated) {
            $warningSource = Join-Path $temp "warning_cache_probe.lua"
            [System.IO.File]::WriteAllText(
                $warningSource,
                "-- $deprecated`nreturn true`n",
                [System.Text.UTF8Encoding]::new($false))
            $first = Invoke-Compiler @($warningSource)
            if ($first.ExitCode -ne 0) {
                throw "deprecation warning baseline failed:`n$($first.Output)"
            }
            $warningOutput = Join-Path $plugins "warning_cache_probe.smg"
            $createdOutputs.Add($warningOutput)
            $second = Invoke-Compiler @($warningSource)
            if ($second.ExitCode -ne 0 -or
                $second.Output -notmatch "ALREADY COMPILED" -or
                $second.Output -notmatch "deprecated API name") {
                throw (
                    "cached compile did not preserve deprecation diagnostics:`n" +
                    $second.Output)
            }
        }
    }

    Write-Host (
        "LuaCS compiler hardening tests passed: missing paths, invalid UTF-8, " +
        "embedded NUL bytes, unknown literal LuaCS modules, uppercase .LUA " +
        "sources, atomic output failures, previous-SMG preservation, and cached " +
        "deprecation diagnostics were exercised.")
}
finally {
    foreach ($output in $createdOutputs) {
        if (Test-Path -LiteralPath $output) {
            try { (Get-Item -LiteralPath $output).IsReadOnly = $false } catch {}
            Remove-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
