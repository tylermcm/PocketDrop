@echo off
rem Builds build\PocketDrop.exe (static CRT, no runtime dependencies).
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio C++ Build Tools not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
cd /d "%~dp0"
if not exist build\obj mkdir build\obj
if not exist src\win\app.ico python tools\make_icon.py || exit /b 1

set SRC=src\core\qr.cpp src\core\deflate.cpp src\core\bundle.cpp src\core\http.cpp src\core\tunnel.cpp ^
 src\ui\ui.cpp src\ui\icons.cpp src\win\platform_win.cpp src\win\gfx_d2d.cpp src\win\main_win.cpp
set LIBS=user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib d2d1.lib dwrite.lib ^
 windowscodecs.lib ws2_32.lib iphlpapi.lib winhttp.lib wintrust.lib crypt32.lib bcrypt.lib uxtheme.lib advapi32.lib

rc /nologo /fo build\app.res src\win\app.rc || exit /b 1
cl /nologo /std:c++20 /O2 /GL /Gy /EHsc /W4 /utf-8 /MT /DUNICODE /D_UNICODE /DNDEBUG ^
   /Fo:build\obj\ /Fe:build\PocketDrop.exe %SRC% build\app.res ^
   /link /LTCG /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MANIFEST:NO %LIBS% || exit /b 1
echo Built build\PocketDrop.exe
