@echo off
rem Builds core_test, server_test and tunnel_test into the given directory.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
cd /d "%~dp0.."
set "OUT=%~1"
if "%OUT%"=="" set "OUT=build\tests"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
set FLAGS=/nologo /std:c++20 /O2 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /Fo:"%OUT%\obj\\"
set CORE=src\core\qr.cpp src\core\deflate.cpp src\core\bundle.cpp src\core\http.cpp src\core\tunnel.cpp src\win\platform_win.cpp
set LIBS=ws2_32.lib iphlpapi.lib winhttp.lib wintrust.lib crypt32.lib bcrypt.lib ole32.lib oleaut32.lib shell32.lib windowscodecs.lib gdi32.lib user32.lib advapi32.lib
cl %FLAGS% /c %CORE% || exit /b 1
for %%t in (core_test server_test tunnel_test) do (
    cl %FLAGS% /Fe:"%OUT%\%%t.exe" tests\%%t.cpp "%OUT%\obj\qr.obj" "%OUT%\obj\deflate.obj" "%OUT%\obj\bundle.obj" "%OUT%\obj\http.obj" "%OUT%\obj\tunnel.obj" "%OUT%\obj\platform_win.obj" %LIBS% || exit /b 1
)
echo Tests built in %OUT%
