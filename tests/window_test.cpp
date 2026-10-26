// Exercise the production native frame handlers on a real, hidden HWND. The
// test host routes only frame messages, so no server, settings or user window
// is started or changed.
#define wWinMain PocketDropUnusedEntryPoint
#include "../src/win/main_win.cpp"
#undef wWinMain
#include <cassert>
#include <iostream>

namespace {
LRESULT CALLBACK frameTestProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETMINMAXINFO || msg == WM_NCHITTEST) return WndProc(h, msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}
}

void testNativeWindow() {
    auto previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = frameTestProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"PocketDropResizeTest";
    assert(RegisterClassW(&wc));
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"Resize test", WINDOW_STYLE,
                                 100, 100, 400, 700, nullptr, nullptr, instance, nullptr);
    assert(window);
    auto style = GetWindowLongPtrW(window, GWL_STYLE);
    assert((style & WS_THICKFRAME) && (style & WS_MAXIMIZEBOX));
    MINMAXINFO limits{};
    SendMessageW(window, WM_GETMINMAXINFO, 0, (LPARAM)&limits);
    UINT dpi = GetDpiForWindow(window);
    MONITORINFO monitor{sizeof monitor};
    assert(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor));
    assert(limits.ptMinTrackSize.x == std::min(MulDiv(WINDOW_MIN_WIDTH, dpi, 96),
                                             (int)(monitor.rcWork.right - monitor.rcWork.left)));
    assert(limits.ptMinTrackSize.y == std::min(MulDiv(WINDOW_MIN_HEIGHT, dpi, 96),
                                             (int)(monitor.rcWork.bottom - monitor.rcWork.top)));
    for (int extra : {0, 120, 300, 0}) {
        int width = limits.ptMinTrackSize.x + MulDiv(extra, dpi, 96);
        int height = limits.ptMinTrackSize.y + MulDiv(extra, dpi, 96);
        assert(SetWindowPos(window, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        RECT outer, client;
        assert(GetWindowRect(window, &outer));
        assert(outer.right - outer.left == width && outer.bottom - outer.top == height);
        assert(GetClientRect(window, &client));
        auto hit = [&](int x, int y) {
            POINT p{x, y};
            assert(ClientToScreen(window, &p));
            return SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y));
        };
        assert(hit(1, client.bottom / 2) == HTLEFT);
        assert(hit(client.right - 2, client.bottom / 2) == HTRIGHT);
        assert(hit(client.right / 2, client.bottom - 2) == HTBOTTOM);
        assert(hit(1, client.bottom - 2) == HTBOTTOMLEFT);
        assert(hit(client.right - 2, client.bottom - 2) == HTBOTTOMRIGHT);
        assert(hit(client.right / 2, client.bottom / 2) == HTCLIENT);
        // Native non-client top edge remains available too.
        assert(SendMessageW(window, WM_NCHITTEST, 0,
                            MAKELPARAM((outer.left + outer.right) / 2, outer.top + 1)) == HTTOP);
    }
    assert(DestroyWindow(window));
    assert(UnregisterClassW(wc.lpszClassName, instance));
    if (previousDpi) SetThreadDpiAwarenessContext(previousDpi);
    std::cout << "Native frame: shrink/grow, DPI minimum and resize edge/corner hit tests passed\n";
}
