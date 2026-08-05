$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$reference = Join-Path $repoRoot "gamedata\reference"
$archive = Join-Path $reference "archive"
$target = Join-Path $reference "cs2_cs_script_api_2026-08-03.json"
$temporaryGzip = Join-Path $env:TEMP "luacs-cs2-api-$PID.json.gz"
$temporaryJson = "$target.tmp"
$expectedJsonHash = "6820C16D48D256D1265A3C03D5146536E98ADCD5EC4FF52BA5AE50CBAC2E5E37"
$expectedParts = [ordered]@{
    "cs2_cs_script_api_2026-08-03.json.gz.part-00" = "682F92F6F88317ED913F665E0D4812B90F264FECD3874A6FB8E6EF83A66A2167"
    "cs2_cs_script_api_2026-08-03.json.gz.part-01" = "664E327412AD8FD37BC14294F887B01094A04FF531DFF59B4B6B1119A2606822"
    "cs2_cs_script_api_2026-08-03.json.gz.part-02" = "07FD0CCDED83CCE2B473F1530D249EE15C98E98002854BB174AED9454387E1CE"
    "cs2_cs_script_api_2026-08-03.json.gz.part-03" = "5B618551271559A20C8B29AC5C5EF4B3566564D77DF180349814999188ED1122"
}

if (Test-Path $target) {
    if ((Get-FileHash $target -Algorithm SHA256).Hash -eq $expectedJsonHash) {
        Write-Host "LuaCS reference API is already restored and verified."
        exit 0
    }
    Remove-Item $target -Force
}

try {
    $output = [System.IO.File]::Open($temporaryGzip, [System.IO.FileMode]::Create)
    try {
        foreach ($entry in $expectedParts.GetEnumerator()) {
            $path = Join-Path $archive $entry.Key
            if (-not (Test-Path $path)) { throw "Missing reference archive chunk: $path" }
            $actual = (Get-FileHash $path -Algorithm SHA256).Hash
            if ($actual -ne $entry.Value) { throw "Reference archive chunk hash mismatch: $($entry.Key)" }
            $input = [System.IO.File]::OpenRead($path)
            try { $input.CopyTo($output) } finally { $input.Dispose() }
        }
    } finally { $output.Dispose() }

    $compressed = [System.IO.File]::OpenRead($temporaryGzip)
    try {
        $gzip = New-Object System.IO.Compression.GZipStream($compressed, [System.IO.Compression.CompressionMode]::Decompress)
        try {
            $json = [System.IO.File]::Open($temporaryJson, [System.IO.FileMode]::Create)
            try { $gzip.CopyTo($json) } finally { $json.Dispose() }
        } finally { $gzip.Dispose() }
    } finally { $compressed.Dispose() }

    $actualJsonHash = (Get-FileHash $temporaryJson -Algorithm SHA256).Hash
    if ($actualJsonHash -ne $expectedJsonHash) { throw "Restored reference JSON hash mismatch." }
    Move-Item $temporaryJson $target -Force
    Write-Host "Restored and verified gamedata\reference\cs2_cs_script_api_2026-08-03.json"
} finally {
    Remove-Item $temporaryGzip -Force -ErrorAction SilentlyContinue
    Remove-Item $temporaryJson -Force -ErrorAction SilentlyContinue
}
