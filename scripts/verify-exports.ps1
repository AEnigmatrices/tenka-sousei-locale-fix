param(
    [Parameter(Mandatory = $true)][string]$ExportsPath,
    [Parameter(Mandatory = $true)][string]$DllPath
)

$ErrorActionPreference = 'Stop'
$exports = Get-Content -LiteralPath $ExportsPath
$expected = @{
    Direct3D8EnableMaximizedWindowedModeShim = 1
    ValidatePixelShader = 2
    ValidateVertexShader = 3
    DebugSetMute = 4
    Direct3DCreate8 = 5
}
foreach ($name in $expected.Keys) {
    $ordinal = $expected[$name]
    $pattern = "^\s*$ordinal\s+\d+\s+[0-9a-fA-F]+\s+$name\s*$"
    if (-not ($exports -match $pattern)) {
        throw "Missing or changed D3D8 export: $name at ordinal $ordinal"
    }
}
if (-not ($exports -match "^\s*5 number of functions\s*$") -or
    -not ($exports -match "^\s*5 number of names\s*$")) {
    throw 'Unexpected number of Direct3D 8 exports.'
}
$dll = Get-Item -LiteralPath $DllPath
$stream = [System.IO.File]::OpenRead($dll.FullName)
$algorithm = [System.Security.Cryptography.SHA256]::Create()
try {
    $sha = ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
} finally {
    $algorithm.Dispose()
    $stream.Dispose()
}
Write-Output "Compiled x86 d3d8.dll: $($dll.Length) bytes; SHA-256: $sha"
if ($env:GITHUB_STEP_SUMMARY) {
    $summary = "### Windows x86 DLL build`nCompiled DLL SHA-256: $sha`n"
    [System.IO.File]::AppendAllText($env:GITHUB_STEP_SUMMARY, $summary, (New-Object System.Text.UTF8Encoding($false)))
}
