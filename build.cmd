@echo off
rem Builds bin\x64\SaitekFF.dll and bin\x86\SaitekFF.dll with the newest installed Visual Studio (C++ workload)
setlocal
set PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (
    echo Visual Studio with C++ tools not found
    exit /b 1
)
call :build x64 || exit /b 1
call :build x86 || exit /b 1
echo Build OK
exit /b 0

:build
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %1 >nul || exit /b 1
set OUT=%~dp0bin\%1
set OBJ=%~dp0obj\%1
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"
cl /nologo /c /O2 /MT /W4 /EHsc /std:c++17 /DUNICODE /D_UNICODE /DNOMINMAX /Fo"%OBJ%\\" "%~dp0src\*.cpp" || exit /b 1
rc /nologo /fo"%OBJ%\SaitekFF.res" "%~dp0src\SaitekFF.rc" || exit /b 1
link /nologo /DLL /DEF:"%~dp0src\SaitekFF.def" /OUT:"%OUT%\SaitekFF.dll" "%OBJ%\*.obj" "%OBJ%\SaitekFF.res" hid.lib setupapi.lib dinput8.lib dxguid.lib cfgmgr32.lib newdev.lib msi.lib ole32.lib advapi32.lib user32.lib || exit /b 1
exit /b 0
