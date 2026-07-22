@echo off
setlocal enabledelayedexpansion
rem
rem Build altairsim from a fresh clone, in one command -- the Windows twin of build.sh.
rem
rem   build.bat              a plain Release build -- no dependencies, no flags
rem   build.bat --with-sdl   also link a static SDL3 so the video boards open a window
rem   build.bat --help       this text
rem
rem NO DEVELOPER SHELL IS REQUIRED, AND THAT IS THE WHOLE POINT ON WINDOWS. CMake's default
rem Visual Studio generator invokes MSBuild, which locates the MSVC toolchain itself, so a
rem plain PowerShell or cmd works. The current documentation makes it look like a Developer
rem shell is mandatory; it is not, and this script is the proof. MSVC is the only supported
rem Windows toolchain (DISTRIBUTION.md, docs/building-windows.md).
rem
rem SDL3 STAYS OPTIONAL. A plain `build.bat` builds against a null display with nothing
rem installed. --with-sdl builds a private static SDL3 (tools\build-sdl3-static.bat) and
rem links it -- and on Windows also matches the C runtime (/MT), so the .exe needs no VC++
rem redistributable and asks nothing of the machine it runs on.
rem
rem VERIFIED on Windows 10 (MSVC 2022 Build Tools), 2026-07-22, from a clean build dir: a plain
rem run built altairsim.exe and printed its version with no Developer shell; --with-sdl linked a
rem static SDL3 and a static /MT CRT (dumpbin /dependents showed system DLLs only -- no SDL3.dll,
rem no VCRUNTIME140); --help rendered and a bad argument exited 2. The macOS twin is build.sh.

rem Run from anywhere: everything is relative to the repository this script lives in.
set "root=%~dp0"
if "%root:~-1%"=="\" set "root=%root:~0,-1%"
set "build=%root%\build"

rem Where tools\build-sdl3-static.bat installs its static SDL3. Same default as that script.
set "SDL3_PREFIX=%USERPROFILE%\opt\sdl3-static"
rem The static CRT that build-sdl3-static.bat uses. Both halves must agree, or the link
rem fails on the /MT vs /MD mismatch. Only used on --with-sdl.
set "MSVC_RUNTIME=MultiThreaded"

set "with_sdl=no"
:parseargs
if "%~1"=="" goto doneargs
if /i "%~1"=="--with-sdl" set "with_sdl=yes" & shift & goto parseargs
if /i "%~1"=="-h" goto help
if /i "%~1"=="--help" goto help
echo build.bat: unknown argument '%~1' (try --help) 1>&2
exit /b 2
:doneargs

rem CMake is the one hard prerequisite. Name it plainly and say where to get it.
where cmake >nul 2>&1
if errorlevel 1 (
    echo build.bat: CMake is required and was not found on your PATH. 1>&2
    echo   Install it with:  winget install Kitware.CMake   ^(or from https://cmake.org/download/^) 1>&2
    echo   then open a NEW terminal so the PATH change takes effect. 1>&2
    exit /b 1
)

set "extra="
if /i "%with_sdl%"=="yes" (
    echo build.bat: --with-sdl -- building a static SDL3 first ^(once per machine^)
    echo.
    call "%root%\tools\build-sdl3-static.bat" "%SDL3_PREFIX%"
    if errorlevel 1 (
        echo build.bat: static SDL3 build failed -- see the output above. 1>&2
        exit /b 1
    )
    echo.
    set "extra=-DCMAKE_PREFIX_PATH=%SDL3_PREFIX% -DCMAKE_MSVC_RUNTIME_LIBRARY=%MSVC_RUNTIME%"
)

echo build.bat: configuring a Release build in %build%
cmake -S "%root%" -B "%build%" %extra%
if errorlevel 1 (
    echo build.bat: configure failed -- see the output above. 1>&2
    exit /b 1
)

echo.
echo build.bat: building
rem --config Release is required here: the Visual Studio generator is multi-config, so the
rem build type is chosen at BUILD time, not configure time. Omit it and you get a Debug build.
cmake --build "%build%" --config Release --parallel
if errorlevel 1 (
    echo build.bat: build failed -- see the output above. 1>&2
    exit /b 1
)

rem Find what landed. The Visual Studio generator puts it under build\Release\.
set "bin="
if exist "%build%\Release\altairsim.exe" set "bin=%build%\Release\altairsim.exe"
if not defined bin if exist "%build%\altairsim.exe" set "bin=%build%\altairsim.exe"

echo.
if defined bin (
    echo build.bat: done. The binary is at:
    echo     !bin!
    echo.
    <nul set /p "=build.bat: it reports version "
    "!bin!" --version
    echo.
    echo Run it with:   !bin!            ^(the default machine^)
    echo List machines: !bin! --list
) else (
    echo build.bat: the build finished but no altairsim.exe was found under %build%. 1>&2
    echo This is a bug in build.bat -- please report it. 1>&2
    exit /b 1
)

endlocal
exit /b 0

:help
echo build.bat -- build altairsim from a fresh clone, in one command.
echo.
echo   build.bat              a plain Release build -- no dependencies, no flags
echo   build.bat --with-sdl   also link a static SDL3 so the video boards open a window
echo   build.bat --help       this text
echo.
echo SDL3 is optional: a plain build works with nothing installed. --with-sdl builds a
echo private static SDL3 ^(tools\build-sdl3-static.bat^) once per machine and links it. No
echo Developer shell is needed -- CMake's Visual Studio generator finds MSVC itself.
exit /b 0
