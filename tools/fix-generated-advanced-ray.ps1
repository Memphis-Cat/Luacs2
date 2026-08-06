param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "generated advanced source does not exist: $Path"
}

$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$source = [System.IO.File]::ReadAllText($resolvedPath)

$legacyPattern = '(?ms)^            NativeRay ray;\r?\n            auto\* bounds = reinterpret_cast<Vector\*>\(ray\.data\.data\(\)\);\r?\n            bounds\[0\] = Vector\(request\.mins\.x, request\.mins\.y, request\.mins\.z\);\r?\n            bounds\[1\] = Vector\(request\.maxs\.x, request\.maxs\.y, request\.maxs\.z\);\r?\n            ray\.type = 2;'
$matches = [System.Text.RegularExpressions.Regex]::Matches($source, $legacyPattern)
if ($matches.Count -ne 1) {
    throw "expected exactly one legacy NativeRay hull construction, found $($matches.Count)"
}

$replacement = @'
            NativeRay ray(
                Vector(request.mins.x, request.mins.y, request.mins.z),
                Vector(request.maxs.x, request.maxs.y, request.maxs.z));
'@

$updated = [System.Text.RegularExpressions.Regex]::Replace(
    $source,
    $legacyPattern,
    $replacement,
    [System.Text.RegularExpressions.RegexOptions]::Multiline -bor
        [System.Text.RegularExpressions.RegexOptions]::Singleline)

if ($updated.Contains('ray.data.data()') -or $updated.Contains('ray.type = 2')) {
    throw 'legacy opaque NativeRay field access remains after generation fix'
}
if (-not $updated.Contains('NativeRay ray(')) {
    throw 'real Source 2 NativeRay constructor was not emitted'
}

[System.IO.File]::WriteAllText(
    $resolvedPath,
    $updated,
    [System.Text.UTF8Encoding]::new($false))

Write-Host "Replaced the legacy opaque hull ray with the real Source 2 Ray_t constructor."
