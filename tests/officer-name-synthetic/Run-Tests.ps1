param([switch]$IncludeAsan)
$ErrorActionPreference = 'Stop'
$programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
$vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'Installed MSVC x86 tools required; no installation is attempted.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
$root = $PSScriptRoot
$configurations = @('Debug','Release')
if ($IncludeAsan) { $configurations += 'Asan' }
$summary = @()
foreach ($configuration in $configurations) {
    $output = Join-Path $root "out\$configuration"
    [System.IO.Directory]::CreateDirectory($output) | Out-Null
    $flags = '/nologo /std:c++20 /EHsc /W4 /WX /sdl /GS /MT /Zi /FAcs'
    if ($configuration -eq 'Debug') { $flags += ' /Od /RTC1 /analyze' }
    if ($configuration -eq 'Release') { $flags += ' /O2' }
    if ($configuration -eq 'Asan') { $flags += ' /Od /fsanitize=address' }
    $sources = @('model.cpp','abi.cpp','tests.cpp') | ForEach-Object { '"' + (Join-Path $root $_) + '"' }
    $commands = @(
        '@echo off',
        ('call "' + $vcvars + '" x86 > toolchain.log 2>&1'),
        'if errorlevel 1 exit /b 1',
        ('cl.exe ' + $flags + ' ' + ($sources -join ' ') + ' /Fe:synthetic-tests.exe /Fd:synthetic-tests.pdb /link /MACHINE:X86 /NXCOMPAT /DYNAMICBASE /INCREMENTAL:NO /MAP:synthetic-tests.map /MANIFEST:EMBED /MANIFESTINPUT:"' + (Join-Path $root 'synthetic-tests.manifest') + '" > build.log 2>&1'),
        'if errorlevel 1 exit /b 1',
        'dumpbin.exe /headers synthetic-tests.exe > headers.txt',
        'if errorlevel 1 exit /b 1',
        'dumpbin.exe /disasm synthetic-tests.exe > disassembly.txt',
        'if errorlevel 1 exit /b 1',
        'synthetic-tests.exe > results.json 2> runtime-errors.log',
        'exit /b %errorlevel%'
    )
    $script = Join-Path $output 'build-and-test.cmd'
    $commandText = ($commands -join [Environment]::NewLine) + [Environment]::NewLine
    [System.IO.File]::WriteAllText($script, $commandText, [System.Text.Encoding]::ASCII)
    Push-Location $output
    try {
        & $env:ComSpec /d /c $script
        $exit = $LASTEXITCODE
        if ($exit -ne 0) {
            Get-Content -LiteralPath (Join-Path $output 'build.log') -ErrorAction SilentlyContinue
            Get-Content -LiteralPath (Join-Path $output 'runtime-errors.log') -ErrorAction SilentlyContinue
            Get-Content -LiteralPath (Join-Path $output 'results.json') -ErrorAction SilentlyContinue
            throw "$configuration build/test failed with exit $exit; diagnostics retained in out."
        }
        & (Join-Path $root 'Verify-Assembly.ps1') -Listing (Join-Path $output 'abi.asm')
        $result = Get-Content -LiteralPath (Join-Path $output 'results.json') -Raw | ConvertFrom-Json
        if ($result.failures -ne 0 -or $result.architecture -ne 'x86' -or $result.tests -lt 70) { throw 'Unexpected or incomplete native test result.' }
        $summary += [pscustomobject]@{
            configuration = $configuration
            tests = $result.tests
            assertions = $result.assertions
            failures = $result.failures
            executable_sha256 = (Get-FileHash -LiteralPath (Join-Path $output 'synthetic-tests.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    } finally { Pop-Location }
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'out\summary.json') -Encoding utf8
$summary | Format-Table
