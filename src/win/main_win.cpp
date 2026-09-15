// Windows host: window, message loop, drag & drop, and the Shell services the shared UI uses.
#include "gfx_d2d.h"
#include "../ui/ui.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <algorithm>

namespace {

const wchar_t* CLASS_NAME = L"PocketDropWindow";
const wchar_t* REG_KEY = L"Software\\PocketDrop";
const ULONG_PTR COPYDATA_PATHS = 0x5044; // 'PD'
const UINT WM_APP_CALL = WM_APP + 1;
const UINT_PTR TIMER_FAST = 1, TIMER_SLOW = 2;
const int ID_SENDTO = 1000;

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::wstring sendToLink() {
    PWSTR p = nullptr;
    std::wstring s;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SendTo, 0, nullptr, &p))) s = std::wstring(p) + L"\\PocketDrop.lnk";
    CoTaskMemFree(p);
    return s;
}

bool createShortcut(const std::wstring& lnk) {
    ComPtr<IShellLinkW> sl;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    sl->SetPath(exe);
    sl->SetDescription(L"Share to your phone with PocketDrop");
    sl->SetIconLocation(exe, 0);
    ComPtr<IPersistFile> pf;
    return SUCCEEDED(sl.As(&pf)) && SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
}

std::vector<std::string> hdropPaths(HDROP h) {
    std::vector<std::string> list;
    UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; i++) {
        UINT len = DragQueryFileW(h, i, nullptr, 0);
        std::wstring s(len, L'\0');
        DragQueryFileW(h, i, s.data(), len + 1);
        list.push_back(utf8(s));
    }
    return list;
}

// Clipboard bitmaps become PNG files so they can be shared like any other file.
std::string savePastedImage(HBITMAP hbm) {
    std::string dir = util::path_join(plat::app_data_dir(), "Pasted");
    plat::make_dir(dir);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char name[96];
    snprintf(name, sizeof name, "Pasted image %04d-%02d-%02d %02d.%02d.%02d.png", st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    std::wstring path = wide(util::path_join(dir, name));
    ComPtr<IWICImagingFactory> f;
    ComPtr<IWICBitmap> bmp;
    ComPtr<IWICFormatConverter> conv;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    UINT w = 0, h = 0;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
              SUCCEEDED(f->CreateBitmapFromHBITMAP(hbm, nullptr, WICBitmapIgnoreAlpha, &bmp)) &&
              SUCCEEDED(bmp->GetSize(&w, &h)) && SUCCEEDED(f->CreateFormatConverter(&conv)) &&
              SUCCEEDED(conv->Initialize(bmp.Get(), fmt, WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom)) &&
              SUCCEEDED(f->CreateStream(&stream)) &&
              SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
              SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
              SUCCEEDED(frame->SetSize(w, h)) && SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
              SUCCEEDED(frame->WriteSource(conv.Get(), nullptr)) && SUCCEEDED(frame->Commit()) &&
              SUCCEEDED(enc->Commit());
    if (!ok) {
        stream.Reset();
        DeleteFileW(path.c_str());
        return {};
    }
    return utf8(path);
}

HMENU buildMenu(const std::vector<MenuItem>& items) {
    HMENU m = CreatePopupMenu();
    for (const auto& it : items) {
        if (it.separator) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        else if (!it.submenu.empty()) AppendMenuW(m, MF_POPUP, (UINT_PTR)buildMenu(it.submenu), wide(it.label).c_str());
        else
            AppendMenuW(m, MF_STRING | (it.checked ? MF_CHECKED : 0) | (it.enabled ? 0 : MF_GRAYED), (UINT_PTR)it.id,
                        wide(it.label).c_str());
    }
    return m;
}

class WinShell : public Shell {
public:
    HWND hwnd = nullptr;
    float dpi = 96.0f;

    void invalidate() override { InvalidateRect(hwnd, nullptr, FALSE); }
    void setAnimating(bool on) override {
        if (on) SetTimer(hwnd, TIMER_FAST, 33, nullptr);
        else KillTimer(hwnd, TIMER_FAST);
    }
    void post(std::function<void()> fn) override {
        auto* p = new std::function<void()>(std::move(fn));
        if (!PostMessageW(hwnd, WM_APP_CALL, 0, (LPARAM)p)) delete p;
    }
    void clientSize(float& w, float& h) override {
        RECT r;
        GetClientRect(hwnd, &r);
        float s = dpi / 96.0f;
        w = r.right / s;
        h = r.bottom / s;
    }

    void copyText(const std::string& text) override {
        if (!OpenClipboard(hwnd)) return;
        std::wstring w = wide(text);
        EmptyClipboard();
        if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
            memcpy(GlobalLock(g), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(g);
            if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
        }
        CloseClipboard();
    }

    void readClipboard(std::vector<std::string>& paths, std::string& text) override {
        if (!OpenClipboard(hwnd)) return;
        if (HANDLE h = GetClipboardData(CF_HDROP)) {
            paths = hdropPaths((HDROP)h);
        } else if (HANDLE hb = IsClipboardFormatAvailable(CF_BITMAP) ? GetClipboardData(CF_BITMAP) : nullptr) {
            std::string png = savePastedImage((HBITMAP)hb);
            if (!png.empty()) paths.push_back(png);
        } else if (HANDLE ht = GetClipboardData(CF_UNICODETEXT)) {
            if (auto* w = (const wchar_t*)GlobalLock(ht)) {
                text = utf8(w);
                GlobalUnlock(ht);
            }
        }
        CloseClipboard();
    }

    void browse(bool folders, std::function<void(const std::vector<std::string>&)> done) override {
        ComPtr<IFileOpenDialog> d;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return;
        FILEOPENDIALOGOPTIONS o = 0;
        d->GetOptions(&o);
        d->SetOptions(o | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | (folders ? FOS_PICKFOLDERS : 0));
        d->SetTitle(folders ? L"Choose folders to share" : L"Choose files to share");
        if (FAILED(d->Show(hwnd))) return;
        ComPtr<IShellItemArray> items;
        if (FAILED(d->GetResults(&items))) return;
        DWORD n = 0;
        items->GetCount(&n);
        std::vector<std::string> list;
        for (DWORD i = 0; i < n; i++) {
            ComPtr<IShellItem> it;
            PWSTR p = nullptr;
            if (SUCCEEDED(items->GetItemAt(i, &it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                list.push_back(utf8(p));
                CoTaskMemFree(p);
            }
        }
        done(list);
    }

    void openUrl(const std::string& url) override {
        ShellExecuteW(hwnd, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void chooseFolder(const std::string& title, std::function<void(const std::string&)> done) override {
        ComPtr<IFileOpenDialog> d;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return;
        FILEOPENDIALOGOPTIONS o = 0;
        d->GetOptions(&o);
        d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        d->SetTitle(wide(title).c_str());
        if (FAILED(d->Show(hwnd))) return;
        ComPtr<IShellItem> it;
        PWSTR p = nullptr;
        if (SUCCEEDED(d->GetResult(&it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
            std::string path = utf8(p);
            CoTaskMemFree(p);
            done(path);
        }
    }

    void revealPath(const std::string& path) override {
        if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(wide(path).c_str())) {
            SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            ILFree(pidl);
        }
    }

    void attention() override {
        if (GetForegroundWindow() == hwnd) return;
        FLASHWINFO fi{sizeof fi, hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
        FlashWindowEx(&fi);
    }

    int popupMenu(const std::vector<MenuItem>& items, float x, float y) override {
        HMENU m = buildMenu(items);
        float s = dpi / 96.0f;
        POINT pt{(LONG)(x * s), (LONG)(y * s)};
        ClientToScreen(hwnd, &pt);
        int cmd = (int)TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, pt.x, pt.y,
                                        hwnd, nullptr);
        DestroyMenu(m); // also destroys submenus
        return cmd;
    }

    void alert(const std::string& title, const std::string& message) override {
        MessageBoxW(hwnd, wide(message).c_str(), wide(title).c_str(), MB_ICONINFORMATION);
    }

    int loadSetting(const char* key, int def) override {
        DWORD v = 0, sz = sizeof v;
        return RegGetValueW(HKEY_CURRENT_USER, REG_KEY, wide(key).c_str(), RRF_RT_REG_DWORD, nullptr, &v, &sz) ==
                       ERROR_SUCCESS
                   ? (int)v
                   : def;
    }

    void saveSetting(const char* key, int value) override {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
            return;
        DWORD v = (DWORD)value;
        RegSetValueExW(k, wide(key).c_str(), 0, REG_DWORD, (const BYTE*)&v, sizeof v);
        RegCloseKey(k);
    }

    std::string loadString(const char* key, const std::string& def) override {
        wchar_t buf[4096];
        DWORD sz = sizeof buf;
        if (RegGetValueW(HKEY_CURRENT_USER, REG_KEY, wide(key).c_str(), RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS)
            return utf8(buf);
        return def;
    }

    void saveString(const char* key, const std::string& value) override {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
            return;
        std::wstring w = wide(value);
        RegSetValueExW(k, wide(key).c_str(), 0, REG_SZ, (const BYTE*)w.c_str(), (DWORD)((w.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }

    void setTopmost(bool on) override {
        SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    std::vector<MenuItem> extraMenuItems() override {
        std::wstring lnk = sendToLink();
        MenuItem m;
        m.id = ID_SENDTO;
        m.label = "Show in \xE2\x80\x9CSend to\xE2\x80\x9D menu";
        m.checked = !lnk.empty() && exists(lnk);
        m.enabled = !lnk.empty();
        return {m};
    }

    void handleExtraMenu(int id) override {
        if (id != ID_SENDTO) return;
        std::wstring lnk = sendToLink();
        if (lnk.empty()) return;
        if (exists(lnk)) DeleteFileW(lnk.c_str());
        else createShortcut(lnk);
    }

    std::string cleanPath(const std::string& path) override {
        std::wstring p = wide(path);
        if (p.size() >= 2 && p.front() == L'"' && p.back() == L'"') p = p.substr(1, p.size() - 2);
        if (p.empty()) return {};
        wchar_t full[32768];
        DWORD n = GetFullPathNameW(p.c_str(), 32768, full, nullptr);
        if (n && n < 32768) p = full;
        while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
        return exists(p) ? utf8(p) : std::string();
    }
};

struct Window {
    WinShell shell;
    D2DGfx gfx;
    Ui ui{shell};
    bool tracking = false;
};

class DropTarget : public IDropTarget {
public:
    explicit DropTarget(Window* w) : w_(w) {
        CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper_));
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (!r) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* d, DWORD, POINTL pt, DWORD* effect) override {
        FORMATETC files{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        FORMATETC text{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        ok_ = d->QueryGetData(&files) == S_OK || d->QueryGetData(&text) == S_OK;
        *effect = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        POINT p{pt.x, pt.y};
        if (helper_) helper_->DragEnter(w_->shell.hwnd, d, &p, *effect);
        w_->ui.setDragOver(ok_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD* effect) override {
        *effect = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        POINT p{pt.x, pt.y};
        if (helper_) helper_->DragOver(&p, *effect);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        if (helper_) helper_->DragLeave();
        w_->ui.setDragOver(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* d, DWORD, POINTL pt, DWORD* effect) override {
        *effect = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        POINT p{pt.x, pt.y};
        if (helper_) helper_->Drop(d, &p, *effect);
        w_->ui.setDragOver(false);
        FORMATETC files{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        FORMATETC text{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM m{};
        if (SUCCEEDED(d->GetData(&files, &m))) {
            auto list = hdropPaths((HDROP)m.hGlobal);
            ReleaseStgMedium(&m);
            w_->ui.addPaths(list);
        } else if (SUCCEEDED(d->GetData(&text, &m))) {
            std::string t;
            if (auto* w = (const wchar_t*)GlobalLock(m.hGlobal)) {
                t = utf8(w);
                GlobalUnlock(m.hGlobal);
            }
            ReleaseStgMedium(&m);
            w_->ui.addText(t);
        }
        return S_OK;
    }

private:
    LONG ref_ = 1;
    Window* w_;
    bool ok_ = false;
    ComPtr<IDropTargetHelper> helper_;
};

void enableDarkMenus() {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    using SetPreferredAppMode = int(WINAPI*)(int);
    using FlushMenuThemes = void(WINAPI*)();
    if (auto f = (SetPreferredAppMode)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(135))) f(2); // ForceDark
    if (auto f = (FlushMenuThemes)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(136))) f();
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* w = (Window*)GetWindowLongPtrW(h, GWLP_USERDATA);
    auto dip = [&](LPARAM l, float& x, float& y) {
        float s = w->shell.dpi / 96.0f;
        x = GET_X_LPARAM(l) / s;
        y = GET_Y_LPARAM(l) / s;
    };
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        break;
    case WM_CREATE: {
        w->shell.hwnd = h;
        w->shell.dpi = (float)GetDpiForWindow(h);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
        COLORREF caption = RGB(0x0F, 0x0F, 0x13), text = RGB(0xE8, 0xE8, 0xEE), border = RGB(0x2B, 0x2B, 0x35);
        DwmSetWindowAttribute(h, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof caption);
        DwmSetWindowAttribute(h, 36 /*DWMWA_TEXT_COLOR*/, &text, sizeof text);
        DwmSetWindowAttribute(h, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof border);
        w->gfx.init();
        SetTimer(h, TIMER_SLOW, 500, nullptr);
        w->ui.start();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        if (w->gfx.begin(h, w->shell.dpi)) {
            w->ui.paint(w->gfx);
            w->gfx.end();
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        w->gfx.resize(LOWORD(lp), HIWORD(lp));
        w->shell.invalidate();
        return 0;
    case WM_DPICHANGED: {
        w->shell.dpi = (float)HIWORD(wp);
        w->gfx.setDpi(w->shell.dpi);
        const RECT* r = (const RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        float x, y;
        dip(lp, x, y);
        w->ui.mouseMove(x, y);
        if (!w->tracking) {
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
            w->tracking = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        w->tracking = false;
        w->ui.mouseLeave();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, w->ui.wantsPointer() ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        float x, y;
        dip(lp, x, y);
        SetCapture(h);
        w->ui.mouseDown(x, y);
        return 0;
    }
    case WM_LBUTTONUP: {
        ReleaseCapture();
        float x, y;
        dip(lp, x, y);
        w->ui.mouseUp(x, y);
        return 0;
    }
    case WM_MOUSEWHEEL:
        w->ui.wheel(GET_WHEEL_DELTA_WPARAM(wp) / 120.0f);
        return 0;
    case WM_KEYDOWN:
        if (GetKeyState(VK_CONTROL) < 0) {
            if (wp == 'V') w->ui.paste();
            else if (wp == 'O') w->ui.browse(false);
            else if (wp == 'C') w->ui.copyLink();
        }
        return 0;
    case WM_TIMER:
        w->ui.tick();
        return 0;
    case WM_ACTIVATEAPP:
        if (wp) w->ui.refreshNetwork();
        return 0;
    case WM_COPYDATA: {
        auto* cds = (const COPYDATASTRUCT*)lp;
        if (cds->dwData == COPYDATA_PATHS && cds->lpData) {
            std::wstring s((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t));
            std::vector<std::string> list;
            size_t start = 0;
            for (size_t i = 0; i <= s.size(); i++)
                if (i == s.size() || s[i] == L'\n' || s[i] == L'\0') {
                    if (i > start) list.push_back(utf8(s.substr(start, i - start)));
                    start = i + 1;
                }
            w->ui.addPaths(list);
        }
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        return TRUE;
    }
    case WM_APP_CALL: {
        auto* fn = (std::function<void()>*)lp;
        (*fn)();
        delete fn;
        return 0;
    }
    case WM_DESTROY: {
        RevokeDragDrop(h);
        KillTimer(h, TIMER_FAST);
        KillTimer(h, TIMER_SLOW);
        w->ui.shutdown();
        MSG m;
        while (PeekMessageW(&m, h, WM_APP_CALL, WM_APP_CALL, PM_REMOVE)) delete (std::function<void()>*)m.lParam;
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    std::vector<std::wstring> args;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; i++) args.push_back(argv[i]);
        if (argv) LocalFree(argv);
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\PocketDrop.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(CLASS_NAME, nullptr)) {
            std::wstring payload;
            for (const auto& a : args) payload += a + L"\n";
            COPYDATASTRUCT cds{COPYDATA_PATHS, (DWORD)((payload.size() + 1) * sizeof(wchar_t)), payload.data()};
            AllowSetForegroundWindow(ASFW_ANY);
            SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cds);
        }
        return 0;
    }

    OleInitialize(nullptr);
    enableDarkMenus();

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hbrBackground = CreateSolidBrush(RGB(0x0F, 0x0F, 0x13));
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    auto win = std::make_unique<Window>();
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"PocketDrop", style, CW_USEDEFAULT, CW_USEDEFAULT, 385, 750, nullptr,
                                nullptr, hi, win.get());
    if (!hwnd) return 1;

    // 385x750 at the window's actual DPI, centred on its monitor's work area.
    UINT dpi = GetDpiForWindow(hwnd);
    int w = MulDiv(385, (int)dpi, 96), hgt = MulDiv(750, (int)dpi, 96);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - hgt) / 2;
    SetWindowPos(hwnd, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);

    auto* drop = new DropTarget(win.get());
    RegisterDragDrop(hwnd, drop);
    drop->Release();

    if (!args.empty()) {
        std::vector<std::string> paths;
        for (const auto& a : args) paths.push_back(utf8(a));
        win->ui.addPaths(paths);
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    win.reset();
    OleUninitialize();
    CloseHandle(mutex);
    return 0;
}
