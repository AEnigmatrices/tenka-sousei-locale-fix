param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$version = (Get-Content -LiteralPath (Join-Path $repo 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw 'VERSION must use major.minor.patch format.'
}

$revision = & git -c "safe.directory=$repo" -C $repo rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -notmatch '^[0-9a-f]{40}$') {
    throw 'Cannot determine the repository commit.'
}
$changes = & git -c "safe.directory=$repo" -C $repo status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the working tree.' }
$dirty = [bool]$changes
if ($dirty -and -not $AllowDirty) {
    throw 'Commit changes before packaging, or use -AllowDirty for a local check.'
}

$changelog = Get-Content -LiteralPath (Join-Path $repo 'CHANGELOG.md') -Raw
$entryPattern = '(?ms)^## \[' + [regex]::Escape($version) + '\][^\r\n]*\r?\n(.*?)(?=^## |\z)'
$entry = [regex]::Match($changelog, $entryPattern)
if (-not $entry.Success -or -not $entry.Groups[1].Value.Trim()) {
    throw "CHANGELOG.md needs an entry for $version."
}

$utf8 = New-Object System.Text.UTF8Encoding($false)
$dllPath = Join-Path $repo 'd3d8.dll'
$dll = Get-Item -LiteralPath $dllPath
$dllHash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash.ToLowerInvariant()
$repository = 'AEnigmatrices/tenka-sousei-locale-fix'
$manifest = [ordered]@{
    project = 'Tenka Sousei Locale Fix'
    version = $version
    tag = "v$version"
    repository = "https://github.com/$repository"
    source_commit = $revision
    working_tree_dirty = $dirty
    binary = [ordered]@{
        path = 'd3d8.dll'
        origin = 'checked-in repository file'
        size_bytes = $dll.Length
        sha256 = $dllHash
    }
} | ConvertTo-Json -Depth 4

$payload = [ordered]@{}
foreach ($name in @('d3d8.dll', 'INSTALL.txt', 'LICENSE', 'CHANGELOG.md')) {
    $payload[$name] = [System.IO.File]::ReadAllBytes((Join-Path $repo $name))
}
$payload['release.json'] = $utf8.GetBytes($manifest + "`n")

function Get-BytesHash([byte[]]$Bytes) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

$packageSums = foreach ($name in $payload.Keys) {
    '{0}  {1}' -f (Get-BytesHash $payload[$name]), $name
}
$payload['SHA256SUMS.txt'] = $utf8.GetBytes(($packageSums -join "`n") + "`n")

$dist = Join-Path $repo 'dist'
[System.IO.Directory]::CreateDirectory($dist) | Out-Null
$zipName = 'tenka-sousei-locale-fix-windows-x86.zip'
$zipPath = Join-Path $dist $zipName
Add-Type -AssemblyName System.IO.Compression
$stream = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::Create)
$archive = $null
try {
    $archive = [System.IO.Compression.ZipArchive]::new($stream, [System.IO.Compression.ZipArchiveMode]::Create)
    foreach ($name in $payload.Keys) {
        $zipEntry = $archive.CreateEntry($name, [System.IO.Compression.CompressionLevel]::Optimal)
        $entryStream = $zipEntry.Open()
        try {
            $bytes = $payload[$name]
            $entryStream.Write($bytes, 0, $bytes.Length)
        } finally {
            $entryStream.Dispose()
        }
    }
} finally {
    if ($archive) { $archive.Dispose() }
    $stream.Dispose()
}

[System.IO.File]::WriteAllBytes((Join-Path $dist 'd3d8.dll'), $payload['d3d8.dll'])
[System.IO.File]::WriteAllBytes((Join-Path $dist 'release.json'), $payload['release.json'])
$assetSums = foreach ($name in @($zipName, 'd3d8.dll', 'release.json')) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    '{0}  {1}' -f $hash, $name
}
[System.IO.File]::WriteAllText((Join-Path $dist 'SHA256SUMS.txt'), ($assetSums -join "`n") + "`n", $utf8)

$notes = $entry.Groups[1].Value.Trim()
$notes += "`n`nSource: [$revision](https://github.com/$repository/commit/$revision)`n"
$notes += "`nDLL SHA-256: `"$dllHash`"`n"
[System.IO.File]::WriteAllText((Join-Path $dist 'release-notes.md'), $notes, $utf8)
Write-Output "Packaged v$version from $revision"
Write-Output "DLL SHA-256: $dllHash"
Write-Output "Assets: $dist"
