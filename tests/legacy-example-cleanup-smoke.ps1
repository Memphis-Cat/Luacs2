$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$deploy = Join-Path $root "deploy.bat"
$buildStampTool = Join-Path $root "tools\build-stamp.ps1"
$packageLuaCs = Join-Path $root "build\package\game\csgo\addons\LuaCS"
$packageStamp = Join-Path $packageLuaCs "build_commit.txt"
$packageGamedata = Join-Path $packageLuaCs (
    "gamedata\reference\official_windows_gamedata.json")
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "luacs-legacy-example-cleanup-" + [Guid]::NewGuid().ToString("N"))

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) { throw $Message }
}

function Invoke-NativeCaptured {
    param([Parameter(Mandatory = $true)][scriptblock]$Command)

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $lines = & $Command 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }

    [pscustomobject]@{
        ExitCode = $exitCode
        Output = ($lines | Out-String)
    }
}

foreach ($required in @(
    $deploy,
    $buildStampTool,
    $packageStamp,
    $packageGamedata,
    (Join-Path $packageLuaCs "bin\win64\luacs2.dll"),
    (Join-Path $packageLuaCs "scripting\example_welcome.lua")
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "required legacy-example cleanup test file is missing: $required"
    }
}

$currentCommit = (& git -C $root rev-parse HEAD 2>$null | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $currentCommit -notmatch '^[0-9a-fA-F]{40}$') {
    throw "could not resolve the current Git commit"
}

$stampBackup = [System.IO.File]::ReadAllBytes($packageStamp)
$stampBackupBase64 = [Convert]::ToBase64String($stampBackup)
[System.IO.Directory]::CreateDirectory($tempRoot) | Out-Null

try {
    $stampWrite = Invoke-NativeCaptured {
        & powershell -NoProfile -ExecutionPolicy Bypass `
            -File $buildStampTool `
            -Mode Write `
            -StampPath $packageStamp `
            -Commit $currentCommit
    }
    Assert-True ($stampWrite.ExitCode -eq 0) (
        "could not create current-stamp cleanup fixture:`n" +
        $stampWrite.Output)

    $gameRoot = Join-Path $tempRoot "game\csgo"
    $addons = Join-Path $gameRoot "addons"
    $metamod = Join-Path $addons "metamod"
    $cssGamedataDirectory = Join-Path $addons (
        "counterstrikesharp\gamedata")
    $installedPlugins = Join-Path $addons "LuaCS\plugins"

    [System.IO.Directory]::CreateDirectory($metamod) | Out-Null
    [System.IO.Directory]::CreateDirectory($cssGamedataDirectory) | Out-Null
    [System.IO.Directory]::CreateDirectory($installedPlugins) | Out-Null
    Copy-Item -LiteralPath $packageGamedata -Destination (
        Join-Path $cssGamedataDirectory "gamedata.json")

    $legacyExample = Join-Path $installedPlugins "example_welcome.smg"
    $customPlugin = Join-Path $installedPlugins "custom_keep.smg"
    [System.IO.File]::WriteAllText(
        $legacyExample,
        "obsolete auto-loaded example",
        [System.Text.ASCIIEncoding]::new())
    [System.IO.File]::WriteAllText(
        $customPlugin,
        "user plugin must survive deployment",
        [System.Text.ASCIIEncoding]::new())

    $deployResult = Invoke-NativeCaptured { & $deploy $gameRoot }
    Assert-True ($deployResult.ExitCode -eq 0) (
        "deployment cleanup fixture failed:`n" + $deployResult.Output)
    Assert-True ($deployResult.Output -match (
        "Removed stale auto-loaded example_welcome.smg")) (
        "deployment did not report stale example cleanup")
    Assert-True (-not (Test-Path -LiteralPath $legacyExample)) (
        "deployment preserved obsolete example_welcome.smg")
    Assert-True (Test-Path -LiteralPath $customPlugin -PathType Leaf) (
        "deployment removed an unrelated user plugin")
    Assert-True (
        ([System.IO.File]::ReadAllText($customPlugin) -ceq
            "user plugin must survive deployment")) (
        "deployment changed an unrelated user plugin")

    $installedSource = Join-Path $addons (
        "LuaCS\scripting\example_welcome.lua")
    Assert-True (Test-Path -LiteralPath $installedSource -PathType Leaf) (
        "deployment removed the example source documentation")
    Assert-True ($deployResult.Output -match "It is not auto-loaded") (
        "deployment did not explain that the example is source-only")

    Write-Host (
        "LuaCS legacy-example cleanup test passed: obsolete " +
        "example_welcome.smg was removed, user plugins were preserved, and " +
        "the example Lua source remained available without auto-loading.")
}
finally {
    [System.IO.File]::WriteAllBytes($packageStamp, $stampBackup)
    $restoredBase64 = [Convert]::ToBase64String(
        [System.IO.File]::ReadAllBytes($packageStamp))
    if ($restoredBase64 -cne $stampBackupBase64) {
        throw "legacy-example cleanup test did not restore the package stamp"
    }
    Remove-Item -LiteralPath $tempRoot -Recurse -Force `
        -ErrorAction SilentlyContinue
}
