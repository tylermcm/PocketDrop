@echo off
setlocal
call "%~dp0..\tools\msvc_env.bat" || exit /b 1
cd /d "%~dp0.."
if not exist build\desktop-obj mkdir build\desktop-obj
if not exist build\tests mkdir build\tests
cl /nologo /std:c++20 /O2 /EHsc /W4 /utf-8 /MT /DUNICODE /D_UNICODE /DPOCKETDROP_RENDER_TEST ^
 /Fo:build\desktop-obj\ /Fe:build\tests\desktop_test.exe tests\desktop_test.cpp tests\window_test.cpp ^
 src\core\qr.cpp src\core\deflate.cpp src\core\bundle.cpp src\core\http.cpp src\core\tunnel.cpp src\core\nearby.cpp src\core\pc_transfer.cpp ^
 src\ui\ui.cpp src\ui\icons.cpp src\win\platform_win.cpp src\win\hotspot_win.cpp src\win\gfx_d2d.cpp src\win\update_win.cpp ^
 user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib d2d1.lib dwrite.lib windowscodecs.lib ^
 ws2_32.lib iphlpapi.lib winhttp.lib wintrust.lib crypt32.lib bcrypt.lib advapi32.lib runtimeobject.lib dwmapi.lib uxtheme.lib || exit /b 1
