param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Write", "Validate")]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$StampPath,

    [string]$RepositoryRoot = (Resolve-Path "$PSScriptRoot\..").Path,

    [string]$Commit
)

$ErrorActionPreference = "Stop"
$shaPattern = '^[0-9a-fA-F]{40}$'

function Stop-BuildStamp {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Error $Message
    exit 1
}

$fullStampPath = [System.IO.Path]::GetFullPath($StampPath)

if ($Mode -eq "Write") {
    if ([string]::IsNullOrWhiteSpace($Commit)) {
        Stop-BuildStamp "A Git commit is required when writing the LuaCS build stamp."
    }

    $normalizedCommit = $Commit.Trim()
    if ($normalizedCommit -notmatch $shaPattern) {
        Stop-BuildStamp "The LuaCS build commit must be exactly 40 hexadecimal characters."
    }

    $parent = Split-Path -Parent $fullStampPath
    [System.IO.Directory]::CreateDirectory($parent) | Out-Null
    [System.IO.File]::WriteAllText(
        $fullStampPath,
        $normalizedCommit,
        [System.Text.UTF8Encoding]::new($false))

    $roundTrip = [System.IO.File]::ReadAllText($fullStampPath)
    if ($roundTrip -cne $normalizedCommit) {
        Stop-BuildStamp "The LuaCS build stamp changed while being written."
    }

    Write-Host "Wrote exact LuaCS build stamp: $normalizedCommit"
    exit 0
}

if (-not (Test-Path -LiteralPath $fullStampPath -PathType Leaf)) {
    Stop-BuildStamp "Build commit stamp is missing: $fullStampPath"
}

$storedCommit = [System.IO.File]::ReadAllText($fullStampPath)
if ($storedCommit -notmatch $shaPattern) {
    $printable = $storedCommit.Replace("`r", "\r").Replace("`n", "\n").Replace("`t", "\t")
    Stop-BuildStamp (
        "Build commit stamp is malformed. It must contain exactly one 40-character " +
        "Git SHA with no BOM, newline, or surrounding whitespace. " +
        "Length=$($storedCommit.Length), value='$printable'.")
}

$resolvedRepository = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$currentCommit = (& git -C $resolvedRepository rev-parse HEAD 2>$null | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $currentCommit -notmatch $shaPattern) {
    Stop-BuildStamp "Could not resolve the current Git commit in '$resolvedRepository'."
}

if (-not [string]::Equals(
        $storedCommit,
        $currentCommit,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    Stop-BuildStamp (
        "Refusing to deploy a stale LuaCS package.`n" +
        "Package was built from: $storedCommit`n" +
        "Current source commit:  $currentCommit`n" +
        "Run build.bat --no-deploy, then deploy again.")
}

Write-Host "Validated LuaCS package commit: $storedCommit"
exit 0
