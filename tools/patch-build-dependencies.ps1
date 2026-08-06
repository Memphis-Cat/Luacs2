$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$strictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)

function Read-TextWithEncodingFallback([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    try {
        return $strictUtf8.GetString($bytes)
    }
    catch [System.Text.DecoderFallbackException] {
        return [System.Text.Encoding]::GetEncoding(1252).GetString($bytes)
    }
}

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, $utf8NoBom)
}

$sourceHookHeader = Join-Path $repoRoot 'deps\metamod-source\core\sourcehook\sh_pagealloc.h'
if (-not (Test-Path -LiteralPath $sourceHookHeader)) {
    throw "Metamod source header was not found: $sourceHookHeader"
}
$sourceHookText = Read-TextWithEncodingFallback $sourceHookHeader
$sourceHookText = $sourceHookText.Replace([char]0x2018, "'")
$sourceHookText = $sourceHookText.Replace([char]0x2019, "'")
$sourceHookText = $sourceHookText.Replace([char]0x201C, '"')
$sourceHookText = $sourceHookText.Replace([char]0x201D, '"')
Write-Utf8NoBom $sourceHookHeader $sourceHookText

$protoDirectory = Join-Path $repoRoot 'deps\protobufs\csgo'
$protoNames = @(
    'network_connection.proto',
    'networkbasetypes.proto',
    'cs_gameevents.proto',
    'engine_gcmessages.proto',
    'gcsdk_gcmessages.proto',
    'cstrike15_gcmessages.proto',
    'cstrike15_usermessages.proto',
    'netmessages.proto',
    'steammessages.proto',
    'usermessages.proto',
    'gameevents.proto',
    'clientmessages.proto',
    'te.proto'
)

foreach ($protoName in $protoNames) {
    $protoPath = Join-Path $protoDirectory $protoName
    if (-not (Test-Path -LiteralPath $protoPath)) {
        throw "Pinned protobuf source was not found: $protoPath"
    }

    $text = Read-TextWithEncodingFallback $protoPath
    if ($text -notmatch '(?m)^\s*syntax\s*=') {
        $text = "syntax = `"proto2`";`r`n`r`n" + $text
    }

    if ($protoName -eq 'networkbasetypes.proto') {
        $text = [regex]::Replace(
            $text,
            '(?m)^\s*import\s+"network_connection\.proto";\s*\r?\n',
            '')
    }
    elseif ($protoName -eq 'engine_gcmessages.proto') {
        $text = [regex]::Replace(
            $text,
            '(?m)^\s*import\s+"google/protobuf/descriptor\.proto";\s*\r?\n',
            '')
    }

    Write-Utf8NoBom $protoPath $text
}

Write-Host 'Normalized pinned Metamod and protobuf sources for a warning-free build.'
