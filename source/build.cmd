@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1

cl.exe /nologo /c /TC /O1 /Oi- /GS- /Zl /W4 /WX /D_WIN32_WINNT=0x0602 /FoD3D8Proxy.obj D3D8Proxy.c
if errorlevel 1 goto failed
link.exe /NOLOGO /DLL /NODEFAULTLIB /ENTRY:DllMain@12 /SUBSYSTEM:WINDOWS /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /INCREMENTAL:NO /MANIFEST:NO /OPT:REF /OPT:ICF /DEF:d3d8.def /MAP:d3d8.map /OUT:d3d8.dll D3D8Proxy.obj kernel32.lib bcrypt.lib
if errorlevel 1 goto failed
dumpbin.exe /headers d3d8.dll | findstr /C:"machine (x86)"
if errorlevel 1 goto failed
dumpbin.exe /exports d3d8.dll > exports.txt
if errorlevel 1 goto failed
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "..\scripts\verify-exports.ps1" -ExportsPath exports.txt -DllPath d3d8.dll
if errorlevel 1 goto failed

popd
endlocal
exit /b 0

:failed
popd
endlocal
exit /b 1
