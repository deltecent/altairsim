@echo off
setlocal enabledelayedexpansion
rem
rem Build a STATIC SDL3 for packaging, into a fixed prefix. Run ONCE per build machine.
rem
rem   usage: tools\build-sdl3-static.bat [prefix]     default: %%USERPROFILE%%\opt\sdl3-static
rem
rem This is the Windows half of tools/build-sdl3-static.sh; the two pin the SAME SDL3
rem version on purpose. See that file for why static linking rather than bundling, and
rem DISTRIBUTION.md 3.2 for the measurements.
rem
rem   *** UNVERIFIED. Written 2026-07-20 from the working macOS script and never run on
rem   *** Windows. It is here to be TESTED, not trusted -- see docs/building-windows.md 6,
rem   *** which says how to report what actually happens. Expect to correct this file.
rem
rem WHY A .BAT AND NOT A .PS1. PowerShell's execution policy blocks unsigned .ps1 files by
rem default, so a script somebody just cloned will not run without them changing a machine
rem setting first. A .bat has no such gate. curl.exe and tar.exe have shipped in Windows
rem since 10 1803, so the download and unpack need nothing installed either.
rem
rem NO DEVELOPER SHELL IS REQUIRED. The Visual Studio generator invokes MSBuild, which
rem locates the toolchain itself -- see docs/building-windows.md.

set SDL3_VERSION=3.4.12

rem THE CRT MUST MATCH, AND THIS IS THE WINDOWS-ONLY TRAP. MSVC links the C runtime
rem dynamically by default (/MD), so the .exe then needs the VC++ redistributable -- which
rem is present on most Windows 10 machines and absent on a clean one, and a package that
rem requires an install first is a broken package. Building everything with the STATIC CRT
rem (/MT) removes that. But SDL3 and altairsim must agree: mixing /MT and /MD gives
rem duplicate-symbol link errors at best, and two separate C runtime heaps at worst.
rem So it is set here AND must be set on the altairsim build. Both, or neither.
set MSVC_RUNTIME=MultiThreaded

set "prefix=%~1"
if "%prefix%"=="" set "prefix=%USERPROFILE%\opt\sdl3-static"
set "stamp=%prefix%\.altairsim-sdl3-version"

rem Already built at the version we want? Nothing to do. Rebuilding is harmless but slow,
rem and this script is named in a release runbook where "already fine" is the normal case.
if exist "%stamp%" (
    set /p have=<"%stamp%"
    if "!have!"=="%SDL3_VERSION%" (
        if exist "%prefix%\lib\SDL3-static.lib" (
            echo build-sdl3-static: SDL3 %SDL3_VERSION% already installed in %prefix%
            call :printusage
            exit /b 0
        )
    )
)

where cmake >nul 2>&1 || (echo build-sdl3-static: needs cmake on PATH >&2 & exit /b 1)
where curl  >nul 2>&1 || (echo build-sdl3-static: needs curl.exe ^(Windows 10 1803+^) >&2 & exit /b 1)
where tar   >nul 2>&1 || (echo build-sdl3-static: needs tar.exe ^(Windows 10 1803+^) >&2 & exit /b 1)

set "work=%TEMP%\altairsim-sdl3-%RANDOM%"
mkdir "%work%" || (echo build-sdl3-static: cannot create %work% >&2 & exit /b 1)

set "tarball=SDL3-%SDL3_VERSION%.tar.gz"
set "url=https://github.com/libsdl-org/SDL/releases/download/release-%SDL3_VERSION%/%tarball%"

echo build-sdl3-static: fetching SDL3 %SDL3_VERSION%
curl -fsSL -o "%work%\%tarball%" "%url%" || (echo build-sdl3-static: download failed >&2 & goto :fail)
tar xzf "%work%\%tarball%" -C "%work%" || (echo build-sdl3-static: unpack failed >&2 & goto :fail)

echo build-sdl3-static: building ^(this takes a few minutes^)

rem SDL_SHARED=OFF as well as SDL_STATIC=ON, deliberately: with both present in the prefix,
rem find_package resolves SDL3::SDL3 to the SHARED one and the whole exercise is silently
rem undone. A prefix holding only a static library is what makes the outcome unambiguous.
cmake -S "%work%\SDL3-%SDL3_VERSION%" -B "%work%\b" ^
      -DSDL_STATIC=ON -DSDL_SHARED=OFF ^
      -DCMAKE_MSVC_RUNTIME_LIBRARY=%MSVC_RUNTIME% ^
      -DCMAKE_INSTALL_PREFIX="%prefix%" >"%work%\configure.log" 2>&1 ^
   || (echo build-sdl3-static: configure failed: >&2 & type "%work%\configure.log" >&2 & goto :fail)

cmake --build "%work%\b" --config Release --parallel >"%work%\build.log" 2>&1 ^
   || (echo build-sdl3-static: build failed: >&2 & type "%work%\build.log" >&2 & goto :fail)

cmake --install "%work%\b" --config Release >"%work%\install.log" 2>&1 ^
   || (echo build-sdl3-static: install failed: >&2 & type "%work%\install.log" >&2 & goto :fail)

rem A static library must actually be what landed. If SDL ever renames its targets, this is
rem where we find out -- rather than three steps later, when a package that was supposed to
rem be self-contained turns out to want SDL3.dll after all.
if not exist "%prefix%\lib\SDL3-static.lib" (
    echo build-sdl3-static: no SDL3-static.lib in %prefix%\lib -- SDL3 did not build static >&2
    dir /b "%prefix%\lib" >&2
    goto :fail
)
if exist "%prefix%\bin\SDL3.dll" (
    echo build-sdl3-static: a SHARED SDL3.dll is also present, which defeats the purpose: >&2
    dir /b "%prefix%\bin" >&2
    goto :fail
)

>"%stamp%" echo %SDL3_VERSION%
rmdir /s /q "%work%" 2>nul

echo.
echo build-sdl3-static: SDL3 %SDL3_VERSION% installed static in %prefix%
call :printusage
exit /b 0

:printusage
echo.
echo Build altairsim against it with ^(the CRT setting must match this script's^):
echo     cmake -B build -DCMAKE_PREFIX_PATH="%prefix%" ^^
echo           -DCMAKE_MSVC_RUNTIME_LIBRARY=%MSVC_RUNTIME%
echo     cmake --build build --config Release
echo.
echo Then confirm it took:
echo     dumpbin /dependents build\Release\altairsim.exe
echo         must list NO SDL3.dll ^(SDL is static^) and NO VCRUNTIME140.dll / MSVCP140.dll
echo         ^(the CRT is static too, so the package needs no VC++ redistributable^).
echo     build\Release\altairsim.exe -n -x "SHOW VERSION"
echo         must show:  video  SDL3 -- windowed
echo.
echo SHOW VERSION -- not symbols -- is what tells a static-windowed build from a HEADLESS
echo one, since both lack SDL3.dll. Do NOT use `dumpbin /symbols ^| findstr SDL_`: on a
echo linked RELEASE .exe the COFF symbol table is empty ^(symbols live in the PDB^), so it
echo finds nothing on a GOOD build. `strings` is no good either: SDL_CreateWindow is a symbol.
exit /b 0

:fail
rmdir /s /q "%work%" 2>nul
exit /b 1
