@echo off
rem Builds fast365.exe with MSVC. Locates Visual Studio via vswhere if cl
rem is not already on PATH (i.e. when not run from a developer prompt).
setlocal enabledelayedexpansion

where cl >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo error: MSVC not found. Install Visual Studio with the C++ workload.
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`""!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath"`) do set "VSDIR=%%i"
    if not defined VSDIR (
        echo error: no Visual Studio installation with C++ tools found.
        exit /b 1
    )
    call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
)

if not exist build mkdir build

cl /nologo /std:c++17 /O2 /GL /W4 /EHsc /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   src\main.cpp src\docx.cpp src\xml.cpp src\zip.cpp src\inflate.cpp src\html_util.cpp ^
   /Fo:build\ /Fe:build\fast365.exe /link /LTCG /STACK:8388608
if errorlevel 1 exit /b 1

echo.
echo Built build\fast365.exe
