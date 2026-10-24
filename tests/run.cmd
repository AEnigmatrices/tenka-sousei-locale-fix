@echo off
setlocal
set "comparison=%~f1"
if not exist "%~dp0..\dist\tests" mkdir "%~dp0..\dist\tests"
if errorlevel 1 exit /b 1
pushd "%~dp0..\dist\tests"
if errorlevel 1 exit /b 1

cl.exe /nologo /TC /W4 /WX /wd4505 /O1 /D_WIN32_WINNT=0x0602 /Folocale_init.obj /Felocale_init.exe ..\..\tests\locale_init.c
if errorlevel 1 goto failed
cl.exe /nologo /TC /W4 /WX /O1 /D_WIN32_WINNT=0x0602 /Foforwarding_smoke.obj /Feforwarding_smoke.exe ..\..\tests\forwarding_smoke.c
if errorlevel 1 goto failed
cl.exe /nologo /TC /W4 /WX /O1 /D_WIN32_WINNT=0x0602 /wd4505 /Fopath_validation.obj /Fepath_validation.exe ..\..\tests\path_validation.c
if errorlevel 1 goto failed
cl.exe /nologo /TC /W4 /WX /O1 /D_WIN32_WINNT=0x0602 /wd4505 /Fointegrity_memory.obj /Feintegrity_memory.exe ..\..\tests\integrity_memory.c bcrypt.lib
if errorlevel 1 goto failed
locale_init.exe
if errorlevel 1 goto failed
path_validation.exe
if errorlevel 1 goto failed
integrity_memory.exe
if errorlevel 1 goto failed
forwarding_smoke.exe "..\..\source\d3d8.dll"
if errorlevel 1 goto failed
if not defined comparison goto success
forwarding_smoke.exe "%comparison%"
if errorlevel 1 goto failed

:success
popd
endlocal
exit /b 0

:failed
popd
endlocal
exit /b 1
