#pragma once
#include <algorithm>

// Outer window size in DIPs. Keep the familiar launch size while allowing the
// compact UI to fit a small laptop screen; use the same limits when restoring.
inline constexpr int WINDOW_DEFAULT_WIDTH = 400, WINDOW_DEFAULT_HEIGHT = 700;
inline constexpr int WINDOW_MIN_WIDTH = 320, WINDOW_MIN_HEIGHT = 380;

// Outer window pixels; clamp before centering so DPI scaling cannot put controls
// below a laptop's taskbar. Also used when Windows changes display/work-area size.
struct WindowBounds { int x, y, width, height; };
inline WindowBounds fitWindow(int x, int y, int width, int height, int left, int top, int right, int bottom) {
    width = std::clamp(width, 1, std::max(1, right - left));
    height = std::clamp(height, 1, std::max(1, bottom - top));
    return {std::clamp(x, left, right - width), std::clamp(y, top, bottom - height), width, height};
}
