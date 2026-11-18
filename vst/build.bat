@echo off
rem ==========================================================================
rem  Builds the E-Piano VST3 + Standalone (Release).
rem  Usage:  build.bat [Debug|Release]
rem  Build output and the JUCE download go to %LOCALAPPDATA%\EPianoVST so
rem  OneDrive doesn't try to sync ~1 GB of intermediate files.
rem ==========================================================================
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "WORK=%LOCALAPPDATA%\EPianoVST"
set "BUILD=%WORK%\build"

rem --- find CMake (PATH first, then the copy bundled with Visual Studio) ---
set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

rem --- git is needed to fetch JUCE ---
where git >nul 2>nul
if errorlevel 1 if exist "%ProgramFiles%\Git\cmd\git.exe" set "PATH=%ProgramFiles%\Git\cmd;%PATH%"

echo [E-Piano] configuring...
"%CMAKE%" -S "%~dp0." -B "%BUILD%" -DFETCHCONTENT_BASE_DIR="%WORK%\deps" || goto :fail

echo [E-Piano] building %CONFIG%...
"%CMAKE%" --build "%BUILD%" --config %CONFIG% --target EPianoVST_VST3 EPianoVST_Standalone --parallel || goto :fail

echo.
echo [E-Piano] Done.
echo   VST3       : %BUILD%\EPianoVST_artefacts\%CONFIG%\VST3\E-Piano.vst3
echo   Standalone : %BUILD%\EPianoVST_artefacts\%CONFIG%\Standalone\E-Piano.exe
echo   Run install.bat to copy the VST3 into C:\Program Files\Common Files\VST3
exit /b 0

:fail
echo [E-Piano] BUILD FAILED
exit /b 1
