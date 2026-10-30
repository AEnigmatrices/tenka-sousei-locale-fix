# Check release artifacts produced by scripts/package-release.ps1.
param([switch]$AllowDirty)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$dist = Join-Path $repo 'dist'
$zipName = 'tenka-sousei-locale-fix-windows-x86.zip'
$payloadNames = @('d3d8.dll', 'INSTALL.txt', 'LICENSE', 'CHANGELOG.md', 'release.json')
$zipNames = @($payloadNames) + @('SHA256SUMS.txt')
$assetNames = @($zipName, 'd3d8.dll', 'release.json')

function Hash-Bytes([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Assert-Same($Actual, $Expected, [string]$What) {
    if ($Actual -ne $Expected) { throw "$What mismatch: expected '$Expected', got '$Actual'." }
}
function Parse-Sums([byte[]]$Bytes, [string[]]$Names, [string]$Description) {
    $found = @{}
    $lines = @(([Text.Encoding]::UTF8.GetString($Bytes) -split '\r?\n') | Where-Object { $_.Length -gt 0 })
    Assert-Same $lines.Count $Names.Count "$Description entry count"
    foreach ($line in $lines) {
        if ($line -cnotmatch '^([0-9a-f]{64})  ([^\r\n]+)$') {
            throw "Invalid line in $($Description): $line"
        }
        $digest = $Matches[1]
        $name = $Matches[2]
        if ($name -cnotin $Names -or $found.ContainsKey($name)) {
            throw "Unexpected or duplicate name in $($Description): $name"
        }
        $found[$name] = $digest
    }
    foreach ($name in $Names) {
        if (-not $found.ContainsKey($name)) { throw "$Description is missing $name." }
    }
    return $found
}
$revision = & git -c "safe.directory=$repo" -C $repo rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -notmatch '^[0-9a-f]{40}$') {
    throw 'Cannot determine repository revision.'
}
$changes = & git -c "safe.directory=$repo" -C $repo status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect working tree.' }
$dirty = [bool]$changes
if ($dirty -and -not $AllowDirty) { throw 'Clean checkout required unless -AllowDirty is used.' }
$version = (Get-Content -LiteralPath (Join-Path $repo 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION is invalid.' }

# Verify the checksums accompanying separately published assets.
$assets = Parse-Sums ([IO.File]::ReadAllBytes((Join-Path $dist 'SHA256SUMS.txt'))) $assetNames 'Asset checksums'
foreach ($name in $assetNames) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Same $hash $assets[$name] "Asset $name SHA-256"
}

# Reject all unexpected, missing, duplicate, or nested ZIP members.
Add-Type -AssemblyName System.IO.Compression
$zipData = @{}
$file = [IO.File]::OpenRead((Join-Path $dist $zipName))
$zip = $null
try {
    $zip = [IO.Compression.ZipArchive]::new($file, [IO.Compression.ZipArchiveMode]::Read)
    Assert-Same $zip.Entries.Count $zipNames.Count 'ZIP entry count'
    foreach ($entry in $zip.Entries) {
        $name = $entry.FullName
        if ($name -cnotin $zipNames -or $zipData.ContainsKey($name)) {
            throw "Unexpected or duplicate ZIP member: $name"
        }
        $input = $entry.Open()
        $memory = [IO.MemoryStream]::new()
        try {
            $input.CopyTo($memory)
            $zipData[$name] = $memory.ToArray()
        } finally {
            $memory.Dispose()
            $input.Dispose()
        }
    }
} finally {
    if ($zip) { $zip.Dispose() }
    $file.Dispose()
}
foreach ($name in $zipNames) {
    if (-not $zipData.ContainsKey($name)) { throw "ZIP is missing $name." }
}
$payload = Parse-Sums $zipData['SHA256SUMS.txt'] $payloadNames 'ZIP checksums'
foreach ($name in $payloadNames) {
    Assert-Same (Hash-Bytes $zipData[$name]) $payload[$name] "ZIP $name SHA-256"
}
foreach ($name in @('d3d8.dll', 'INSTALL.txt', 'LICENSE', 'CHANGELOG.md')) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $repo $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Same (Hash-Bytes $zipData[$name]) $hash "ZIP $name vs tracked source"
}
foreach ($name in @('d3d8.dll', 'release.json')) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Same (Hash-Bytes $zipData[$name]) $hash "ZIP $name vs published asset"
}

$manifest = [Text.Encoding]::UTF8.GetString($zipData['release.json']) | ConvertFrom-Json
Assert-Same $manifest.version $version 'Manifest version'
Assert-Same $manifest.tag "v$version" 'Manifest tag'
Assert-Same $manifest.source_commit $revision 'Manifest commit'
Assert-Same ([bool]$manifest.working_tree_dirty) $dirty 'Manifest dirty flag'
Assert-Same $manifest.binary.path 'd3d8.dll' 'Manifest binary path'
Assert-Same $manifest.binary.origin 'checked-in repository file' 'Manifest binary origin'
$dll = Get-Item -LiteralPath (Join-Path $repo 'd3d8.dll')
$hash = (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
Assert-Same ([long]$manifest.binary.size_bytes) $dll.Length 'Manifest DLL length'
Assert-Same $manifest.binary.sha256 $hash 'Manifest DLL SHA-256'
Write-Output "Verified release package v$version ($revision), ZIP and both checksum layers."
