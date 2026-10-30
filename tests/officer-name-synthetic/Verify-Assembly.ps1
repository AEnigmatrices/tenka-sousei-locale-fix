param([Parameter(Mandatory=$true)][string]$Listing)
$ErrorActionPreference = 'Stop'
$text = Get-Content -LiteralPath $Listing -Raw
$expectations = @{
    '_MockEvaluatorAbi' = 12
    '_AdapterAbi' = 12
    '_MockAssignmentAbi' = 8
}
foreach ($name in $expectations.Keys) {
    $pattern = '(?ms)^' + [regex]::Escape($name) + ' PROC(.*?)^' + [regex]::Escape($name) + ' ENDP'
    $body = [regex]::Match($text, $pattern)
    if (-not $body.Success) { throw "Missing emitted function $name" }
    $instructions = ($body.Groups[1].Value -split '\r?\n' | Where-Object { $_ -notmatch '^\s*;' }) -join [Environment]::NewLine
    $cleanup = $expectations[$name]
    if ($instructions -notmatch ('\bret\s+' + $cleanup + '\b')) { throw "Incorrect emitted cleanup for $name" }
    if ($instructions -notmatch '\bcall\s+_' -or $instructions -notmatch '\bpush\s+ecx\b') {
        throw "Missing explicit ECX bridge for $name"
    }
}
Write-Output 'Verified emitted RET12/RET8 and ECX bridges for the three synthetic ABI entries.'
