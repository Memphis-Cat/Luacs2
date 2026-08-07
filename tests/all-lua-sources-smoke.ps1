$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$compiler = Join-Path $root "build\package\game\csgo\addons\LuaCS\scripting\compile.exe"
$plugins = Join-Path $root "build\package\game\csgo\addons\LuaCS\plugins"
$sourceRoot = Join-Path $root "packaging\LuaCS\scripting"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) (
    "luacs-all-lua-sources-" + [Guid]::NewGuid().ToString("N"))

if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw "compile.exe is missing: $compiler"
}
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "packaging Lua source directory is missing: $sourceRoot"
}

[System.IO.Directory]::CreateDirectory($temp) | Out-Null
$sources = @(Get-ChildItem -LiteralPath $sourceRoot -Filter "*.lua" -File -Recurse |
    Sort-Object FullName)
if ($sources.Count -eq 0) {
    throw "no checked-in packaging Lua sources were found"
}

$backups = @{}
$generated = New-Object System.Collections.Generic.List[string]
try {
    foreach ($source in $sources) {
        $output = Join-Path $plugins ($source.BaseName + ".smg")
        if (-not $backups.ContainsKey($output)) {
            if (Test-Path -LiteralPath $output -PathType Leaf) {
                $backup = Join-Path $temp ([Guid]::NewGuid().ToString("N") + ".smg")
                Copy-Item -LiteralPath $output -Destination $backup
                $backups[$output] = $backup
            } else {
                $backups[$output] = $null
            }
        }

        $result = (& $compiler --no-pause $source.FullName 2>&1 | Out-String)
        if ($LASTEXITCODE -ne 0) {
            throw (
                "checked-in Lua source failed compiler/semantic/package audit: " +
                $source.FullName + "`n" + $result)
        }
        if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
            throw "compiler succeeded but did not create: $output"
        }
        $generated.Add($output)
    }

    Write-Host (
        "LuaCS checked-in Lua source audit passed: all " + $sources.Count +
        " packaging .lua source(s) passed the real compiler, semantic preflight, " +
        "bytecode dump, authentication, encryption, and SMG output path.")
}
finally {
    foreach ($output in $generated) {
        Remove-Item -LiteralPath $output -Force -ErrorAction SilentlyContinue
    }
    foreach ($entry in $backups.GetEnumerator()) {
        if ($null -ne $entry.Value) {
            Copy-Item -LiteralPath $entry.Value -Destination $entry.Key -Force
        }
    }
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
