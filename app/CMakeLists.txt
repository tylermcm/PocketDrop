# The standalone PocketDrop app: Qt Quick window around the engine in ../src and the screen in ../qml.
#   cmake -S app -B build/app -DCMAKE_BUILD_TYPE=Release && cmake --build build/app
cmake_minimum_required(VERSION 3.21)
project(PocketDrop LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(ENGINE
    ${SRC}/core/bundle.cpp ${SRC}/core/deflate.cpp ${SRC}/core/http.cpp ${SRC}/core/nearby.cpp
    ${SRC}/core/pc_transfer.cpp ${SRC}/core/qr.cpp ${SRC}/core/tunnel.cpp
    ${SRC}/ui/ui.cpp ${SRC}/ui/icons.cpp)

if(WIN32)
    list(APPEND ENGINE ${SRC}/win/platform_win.cpp ${SRC}/win/hotspot_win.cpp)
    set(PLATFORM_LIBS user32 gdi32 shell32 ole32 oleaut32 uuid ws2_32 iphlpapi winhttp wintrust crypt32 bcrypt advapi32
        windowscodecs runtimeobject)
elseif(APPLE)
    enable_language(OBJCXX)
    list(APPEND ENGINE ${SRC}/mac/platform_mac.mm ${SRC}/core/hotspot_stub.cpp)
    find_library(APPKIT AppKit)
    find_library(QUICKLOOK QuickLookThumbnailing)
    find_library(COREGRAPHICS CoreGraphics)
    find_package(CURL REQUIRED)
    set(PLATFORM_LIBS ${APPKIT} ${QUICKLOOK} ${COREGRAPHICS} CURL::libcurl)
else()
    list(APPEND ENGINE ${SRC}/linux/platform_linux.cpp ${SRC}/linux/hotspot_linux.cpp)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET libcurl gdk-pixbuf-2.0)
    set(PLATFORM_LIBS PkgConfig::DEPS pthread)
endif()

qt_add_executable(PocketDrop WIN32 MACOSX_BUNDLE main.cpp backend.cpp backend.h ${ENGINE})
target_link_libraries(PocketDrop PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick ${PLATFORM_LIBS})
target_compile_definitions(PocketDrop PRIVATE $<$<BOOL:${WIN32}>:UNICODE _UNICODE NOMINMAX>)

# The screen travels inside the executable.
qt_add_resources(PocketDrop views PREFIX "/pd" BASE ${CMAKE_CURRENT_SOURCE_DIR}/../qml FILES
    ../qml/PocketDropView.qml ../qml/PdIcon.qml ../qml/PdButton.qml ../qml/PdChip.qml ../qml/PdSwitch.qml
    ../qml/PdMenu.qml ../qml/PdPairing.qml ../qml/theme.json)
qt_add_resources(PocketDrop host PREFIX "/pd" BASE ${CMAKE_CURRENT_SOURCE_DIR} FILES main.qml)
