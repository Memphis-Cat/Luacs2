$ErrorActionPreference = "Stop"

$root = (Resolve-Path "$PSScriptRoot\..").Path
$deploy = Join-Path $root "deploy.bat"
$buildStampTool = Join-Path $root "tools\build-stamp.ps1"
$packageAddons = Join-Path $root "build\package\game\csgo\addons"
$packageLuaCs = Join-Path $packageAddons "LuaCS"
$packageStamp = Join-Path $packageLuaCs "build_commit.txt"
$packageGamedata = Join-Path $packageLuaCs "gamedata\reference\official_windows_gamedata.json"
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "luacs-deploy-smoke-" + [Guid]::NewGuid().ToString("N"))

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

function Invoke-ExternalPowerShell {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    Invoke-NativeCaptured {
        & powershell -NoProfile -ExecutionPolicy Bypass @Arguments
    }
}

function Invoke-Deploy {
    param([Parameter(Mandatory = $true)][string]$GameRoot)
    Invoke-NativeCaptured {
        & $deploy $GameRoot
    }
}

function New-FakeServer {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [ValidateSet("Complete", "Missing", "Incomplete")]
        [string]$Gamedata = "Complete",
        [bool]$WithMetamod = $true
    )

    $gameRoot = Join-Path $tempRoot $Name
    $addons = Join-Path $gameRoot "addons"
    [System.IO.Directory]::CreateDirectory($addons) | Out-Null
    if ($WithMetamod) {
        [System.IO.Directory]::CreateDirectory(
            (Join-Path $addons "metamod")) | Out-Null
    }

    if ($Gamedata -ne "Missing") {
        $cssDirectory = Join-Path $addons "counterstrikesharp\gamedata"
        [System.IO.Directory]::CreateDirectory($cssDirectory) | Out-Null
        $target = Join-Path $cssDirectory "gamedata.json"
        if ($Gamedata -eq "Complete") {
            Copy-Item -LiteralPath $packageGamedata -Destination $target
        }
        else {
            [System.IO.File]::WriteAllText(
                $target,
                '{"ClientPrint":{"signatures":{"windows":"48 85 C9"}}}',
                [System.Text.UTF8Encoding]::new($false))
        }
    }
    return $gameRoot
}

if (-not (Test-Path -LiteralPath $deploy -PathType Leaf)) {
    throw "deploy.bat is missing"
}
if (-not (Test-Path -LiteralPath $buildStampTool -PathType Leaf)) {
    throw "build-stamp.ps1 is missing"
}
if (-not (Test-Path -LiteralPath $packageStamp -PathType Leaf)) {
    throw "package build stamp is missing; run build.bat --no-deploy first"
}
if (-not (Test-Path -LiteralPath $packageGamedata -PathType Leaf)) {
    throw "packaged Windows gamedata is missing"
}

$currentCommit = (& git -C $root rev-parse HEAD 2>$null | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $currentCommit -notmatch '^[0-9a-fA-F]{40}$') {
    throw "could not resolve the current Git commit"
}

$stampBackup = [System.IO.File]::ReadAllBytes($packageStamp)
[System.IO.Directory]::CreateDirectory($tempRoot) | Out-Null

try {
    $validateCurrent = Invoke-ExternalPowerShell @(
        "-File", $buildStampTool,
        "-Mode", "Validate",
        "-StampPath", $packageStamp,
        "-RepositoryRoot", $root)
    Assert-True ($validateCurrent.ExitCode -eq 0) (
        "current package stamp was rejected:`n" + $validateCurrent.Output)

    $writtenStamp = Join-Path $tempRoot "written-build-commit.txt"
    $writeResult = Invoke-ExternalPowerShell @(
        "-File", $buildStampTool,
        "-Mode", "Write",
        "-StampPath", $writtenStamp,
        "-Commit", $currentCommit)
    Assert-True ($writeResult.ExitCode -eq 0) (
        "exact build stamp writer failed:`n" + $writeResult.Output)
    $writtenBytes = [System.IO.File]::ReadAllBytes($writtenStamp)
    $writtenText = [System.IO.File]::ReadAllText($writtenStamp)
    Assert-True ($writtenBytes.Length -eq 40) (
        "build stamp must contain exactly 40 bytes, got $($writtenBytes.Length)")
    Assert-True ($writtenText -ceq $currentCommit) (
        "build stamp writer added hidden bytes or whitespace")

    $successRoot = New-FakeServer -Name "success" -Gamedata Complete
    $success = Invoke-Deploy $successRoot
    Assert-True ($success.ExitCode -eq 0) (
        "current package deployment failed:`n" + $success.Output)
    Assert-True ($success.Output -match [regex]::Escape($currentCommit)) (
        "successful deployment did not print the deployed commit")
    Assert-True ($success.Output -match "Imported current CounterStrikeSharp gamedata") (
        "successful deployment did not import validated live gamedata")
    $installedDll = Join-Path $successRoot "addons\LuaCS\bin\win64\luacs2.dll"
    $installedStamp = Join-Path $successRoot "addons\LuaCS\build_commit.txt"
    Assert-True (Test-Path -LiteralPath $installedDll -PathType Leaf) (
        "successful deployment did not install luacs2.dll")
    Assert-True (
        ([System.IO.File]::ReadAllText($installedStamp) -ceq $currentCommit)) (
        "successful deployment installed the wrong build stamp")

    $fallbackRoot = New-FakeServer -Name "fallback" -Gamedata Missing
    $fallback = Invoke-Deploy $fallbackRoot
    Assert-True ($fallback.ExitCode -eq 0) (
        "packaged-gamedata fallback deployment failed:`n" + $fallback.Output)
    Assert-True ($fallback.Output -match "live gamedata was not found") (
        "fallback deployment did not report the missing live gamedata")

    $incompleteRoot = New-FakeServer -Name "incomplete" -Gamedata Incomplete
    $incomplete = Invoke-Deploy $incompleteRoot
    Assert-True ($incomplete.ExitCode -ne 0) (
        "deployment accepted incomplete CounterStrikeSharp gamedata")
    Assert-True ($incomplete.Output -match "missing required entry") (
        "incomplete gamedata failure did not name the missing entry")
    Assert-True (-not (Test-Path -LiteralPath (
        Join-Path $incompleteRoot "addons\LuaCS\bin\win64\luacs2.dll"))) (
        "deployment copied LuaCS before rejecting incomplete gamedata")

    $missingMetamodRoot = New-FakeServer -Name "missing-metamod" `
        -Gamedata Complete -WithMetamod $false
    $missingMetamod = Invoke-Deploy $missingMetamodRoot
    Assert-True ($missingMetamod.ExitCode -ne 0) (
        "deployment accepted a game root without Metamod")
    Assert-True ($missingMetamod.Output -match "Metamod folder was not found") (
        "missing Metamod failure was not explicit")

    [System.IO.File]::WriteAllText(
        $packageStamp,
        ('0' * 40),
        [System.Text.UTF8Encoding]::new($false))
    $staleRoot = New-FakeServer -Name "stale" -Gamedata Complete
    $stale = Invoke-Deploy $staleRoot
    Assert-True ($stale.ExitCode -ne 0) "deployment accepted a stale package"
    Assert-True ($stale.Output -match "stale LuaCS package") (
        "stale package failure did not explain the mismatch")
    Assert-True (-not (Test-Path -LiteralPath (
        Join-Path $staleRoot "addons\LuaCS\bin\win64\luacs2.dll"))) (
        "deployment copied a stale package before rejecting it")

    [System.IO.File]::WriteAllText(
        $packageStamp,
        $currentCommit + " ",
        [System.Text.UTF8Encoding]::new($false))
    $malformedRoot = New-FakeServer -Name "malformed" -Gamedata Complete
    $malformed = Invoke-Deploy $malformedRoot
    Assert-True ($malformed.ExitCode -ne 0) (
        "deployment accepted a build stamp with trailing whitespace")
    Assert-True ($malformed.Output -match "stamp is malformed") (
        "malformed build stamp failure did not expose hidden whitespace")

    Remove-Item -LiteralPath $packageStamp -Force
    $missingStampRoot = New-FakeServer -Name "missing-stamp" -Gamedata Complete
    $missingStamp = Invoke-Deploy $missingStampRoot
    Assert-True ($missingStamp.ExitCode -ne 0) (
        "deployment accepted a package without a build stamp")
    Assert-True ($missingStamp.Output -match "stamp is missing") (
        "missing build stamp failure was not explicit")

    Write-Host (
        "LuaCS deployment smoke tests passed: exact stamp writer, current " +
        "deployment, packaged-gamedata fallback, incomplete gamedata, missing " +
        "Metamod, stale package, malformed stamp, and missing stamp.")
}
finally {
    [System.IO.File]::WriteAllBytes($packageStamp, $stampBackup)
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
