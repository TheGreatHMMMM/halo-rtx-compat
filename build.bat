@echo off
setlocal enabledelayedexpansion

REM -----------------------------------------------------------------------
REM  build.bat -- compile d3d9.dll directly with cl.exe (no IDE required)
REM  Usage:  build.bat [debug|release]   (default: Release)
REM  Requires: VS 2022 or VS 2022 Build Tools
REM            "Desktop development with C++" workload
REM -----------------------------------------------------------------------

cd /d "%~dp0"

set "CONFIG=Release"
if /i "%~1"=="debug"   set "CONFIG=Debug"
if /i "%~1"=="release" set "CONFIG=Release"

echo Configuration: %CONFIG%
echo.

REM --- Locate vcvarsall.bat via vswhere ----------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found.
    echo Install VS 2022 Build Tools from:
    echo   https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
    pause & exit /b 1
)

for /f "usebackq delims=" %%i in (
    `"%VSWHERE%" -latest -property installationPath 2^>nul`
) do set "VSDIR=%%i"

if not defined VSDIR (
    echo ERROR: No Visual Studio / Build Tools installation found.
    pause & exit /b 1
)

set "VCVARSALL=%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARSALL%" (
    echo ERROR: vcvarsall.bat not found. Install the C++ workload.
    pause & exit /b 1
)

echo Setting up x86 build environment...
call "%VCVARSALL%" x86 >nul 2>&1
echo.

REM --- Output directories ------------------------------------------------
set "OUTDIR=build\bin\%CONFIG%"
set "OBJDIR=build\obj\%CONFIG%"

mkdir "%OUTDIR%"         2>nul
mkdir "%OBJDIR%\minhook" 2>nul
mkdir "%OBJDIR%\imgui"   2>nul
mkdir "%OBJDIR%\shared"  2>nul
mkdir "%OBJDIR%\comp"    2>nul

REM --- Include paths (no spaces in any of these) -------------------------
set "INC=/I deps\dxsdk\Include /I deps\minhook\include /I deps\imgui /I deps\bridge_api /I src"

REM --- Compiler flags ----------------------------------------------------
set "WARN_OFF=/wd4239 /wd4369 /wd4505 /wd4996 /wd5311 /wd6001 /wd6385 /wd6386 /wd26812"
set "DEFS=/D WIN32 /D _WINDOWS /D _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS"
set "CXXBASE=/nologo /c /std:c++latest /EHsc /Zi /W3 %DEFS% %WARN_OFF%"
set "CBASE=/nologo /c /Zi /W0 /D WIN32 /D _WINDOWS"

if /i "%CONFIG%"=="Release" (
    set "RT=/MT"
    set "OPT=/O2 /D NDEBUG /MP"
    set "LTCG="
) else (
    set "RT=/MTd"
    set "OPT=/Od /D DEBUG /D _DEBUG /MP"
    set "LTCG="
)

set "CXXFLAGS=%CXXBASE% %RT% %OPT%"
set "CFLAGS=%CBASE% %RT% %OPT%"
set "SHARED_PCH=%OBJDIR%\shared\std_include.pch"
set "COMP_PCH=%OBJDIR%\comp\std_include.pch"

REM ========================================================================
REM  1. MinHook  (C, no PCH)
REM ========================================================================
echo [1/4] Compiling MinHook...
cl.exe %CFLAGS% /I deps\minhook\include /Fo%OBJDIR%\minhook\ ^
    deps\minhook\src\buffer.c ^
    deps\minhook\src\hook.c ^
    deps\minhook\src\trampoline.c ^
    deps\minhook\src\hde\hde32.c
if errorlevel 1 goto :error

REM ========================================================================
REM  2. ImGui  (C++, no PCH)
REM ========================================================================
echo [2/4] Compiling ImGui...
cl.exe %CXXFLAGS% %INC% /Fo%OBJDIR%\imgui\ ^
    deps\imgui\imgui.cpp ^
    deps\imgui\imgui_demo.cpp ^
    deps\imgui\imgui_draw.cpp ^
    deps\imgui\imgui_tables.cpp ^
    deps\imgui\imgui_widgets.cpp ^
    deps\imgui\backends\imgui_impl_dx9.cpp ^
    deps\imgui\backends\imgui_impl_win32.cpp ^
    deps\imgui\misc\cpp\imgui_stdlib.cpp
if errorlevel 1 goto :error

REM ========================================================================
REM  3. _shared  (C++, PCH)
REM ========================================================================
echo [3/4] Compiling _shared...

cl.exe %CXXFLAGS% %INC% /I src\shared ^
    /Yc"std_include.hpp" /Fp%SHARED_PCH% /Fo%OBJDIR%\shared\std_include_pch.obj ^
    src\shared\std_include.cpp
if errorlevel 1 goto :error

cl.exe %CXXFLAGS% %INC% /I src\shared ^
    /Yu"std_include.hpp" /Fp%SHARED_PCH% /Fo%OBJDIR%\shared\ ^
    src\shared\globals.cpp ^
    src\shared\common\config.cpp ^
    src\shared\common\dinput_hook_v1.cpp ^
    src\shared\common\dinput_hook_v2.cpp ^
    src\shared\common\ffp_state.cpp ^
    src\shared\common\flags.cpp ^
    src\shared\common\imgui_helper.cpp ^
    src\shared\common\loader.cpp ^
    src\shared\common\remix_api.cpp ^
    src\shared\utils\hooking.cpp ^
    src\shared\utils\memory.cpp ^
    src\shared\utils\utils.cpp
if errorlevel 1 goto :error

REM ========================================================================
REM  4. comp  (C++, PCH)  ->  d3d9.dll
REM ========================================================================
echo [4/4] Compiling comp...

cl.exe %CXXFLAGS% %INC% /I src\comp ^
    /Yc"std_include.hpp" /Fp%COMP_PCH% /Fo%OBJDIR%\comp\std_include_pch.obj ^
    src\comp\std_include.cpp
if errorlevel 1 goto :error

cl.exe %CXXFLAGS% %INC% /I src\comp ^
    /Yu"std_include.hpp" /Fp%COMP_PCH% /Fo%OBJDIR%\comp\ ^
    src\comp\comp.cpp ^
    src\comp\proxy\d3d9_proxy.cpp ^
    src\comp\main.cpp ^
    src\comp\game\game.cpp ^
    src\comp\chimera\extend_limits.cpp ^
    src\comp\chimera\window.cpp ^
    src\comp\modules\borderless.cpp ^
    src\comp\modules\d3d9ex.cpp ^
    src\comp\modules\imgui.cpp ^
    src\comp\modules\renderer.cpp ^
    src\comp\modules\skinning.cpp
if errorlevel 1 goto :error

REM ========================================================================
REM  Link -> d3d9.dll
REM ========================================================================
echo Linking...

REM Build a response file from all compiled .obj files
(
    for %%f in ("%OBJDIR%\minhook\*.obj") do echo "%%f"
    for %%f in ("%OBJDIR%\imgui\*.obj")   do echo "%%f"
    for %%f in ("%OBJDIR%\shared\*.obj")  do echo "%%f"
    for %%f in ("%OBJDIR%\comp\*.obj")    do echo "%%f"
) > %OBJDIR%\link.rsp

link.exe /nologo /DLL %LTCG% ^
    /DEF:src\comp\proxy\d3d9.def ^
    /OUT:%OUTDIR%\d3d9.dll ^
    /DEBUG /PDB:%OUTDIR%\d3d9.pdb ^
    /MACHINE:X86 ^
    /LIBPATH:deps\dxsdk\Lib\x86 ^
    /NODEFAULTLIB:d3d9.lib ^
    @%OBJDIR%\link.rsp ^
    d3dx9.lib dinput8.lib dxguid.lib psapi.lib ^
    kernel32.lib user32.lib gdi32.lib shell32.lib advapi32.lib
if errorlevel 1 goto :error

echo.
echo === Build succeeded ===
echo Output: %OUTDIR%\d3d9.dll
echo.
pause
exit /b 0

:error
echo.
echo === BUILD FAILED ===
pause
exit /b 1
