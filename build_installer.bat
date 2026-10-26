@echo off
setlocal
cd /d "%~dp0"
if not exist build\PocketDrop.exe call build.bat || exit /b 1
if not defined ISCC if exist "build\tools\inno\ISCC.exe" set "ISCC=%CD%\build\tools\inno\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC (
    echo Inno Setup 6 not found. Set ISCC to the full path to ISCC.exe.
    exit /b 1
)
"%ISCC%" installer\PocketDrop.iss || exit /b 1
echo Built build\PocketDrop-Setup.exe
