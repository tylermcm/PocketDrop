#include "gfx_d2d.h"
#include <shellapi.h>

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

D2D1_RECT_F d2r(const RectF& r) { return D2D1::RectF(r.left, r.top, r.right, r.bottom); }
D2D1_COLOR_F d2c(Color c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

std::wstring pickFamily(IDWriteFactory* dw, std::initializer_list<const wchar_t*> names) {
    ComPtr<IDWriteFontCollection> col;
    dw->GetSystemFontCollection(&col);
    for (auto n : names) {
        UINT32 idx;
        BOOL exists = FALSE;
        if (col && SUCCEEDED(col->FindFamilyName(n, &idx, &exists)) && exists) return n;
    }
    return L"Segoe UI";
}

} // namespace

D2DGfx::~D2DGfx() {
    for (auto& [path, e] : icons_)
        if (e.hicon) DestroyIcon(e.hicon);
}

bool D2DGfx::init() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   (IUnknown**)dw_.GetAddressOf())))
        return false;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));

    auto props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                             D2D1_LINE_JOIN_ROUND);
    d2d_->CreateStrokeStyle(props, nullptr, 0, &round_);
    props.dashStyle = D2D1_DASH_STYLE_CUSTOM;
    const float dashes[] = {2.5f, 3.0f};
    d2d_->CreateStrokeStyle(props, dashes, 2, &dashed_);

    std::wstring ui = pickFamily(dw_.Get(), {L"Segoe UI Variable Text", L"Segoe UI"});
    std::wstring display = pickFamily(dw_.Get(), {L"Segoe UI Variable Display", L"Segoe UI"});
    std::wstring mono = pickFamily(dw_.Get(), {L"Cascadia Mono", L"Consolas"});
    struct Spec {
        const std::wstring* family;
        DWRITE_FONT_WEIGHT weight;
        float size;
    } specs[] = {
        {&display, DWRITE_FONT_WEIGHT_SEMI_BOLD, 16.5f}, // Title
        {&ui, DWRITE_FONT_WEIGHT_NORMAL, 13.5f},         // Body
        {&ui, DWRITE_FONT_WEIGHT_SEMI_BOLD, 13.5f},      // BodyBold
        {&ui, DWRITE_FONT_WEIGHT_NORMAL, 12.0f},         // Small
        {&ui, DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.0f},      // SmallBold
        {&mono, DWRITE_FONT_WEIGHT_NORMAL, 12.0f},       // Mono
        {&display, DWRITE_FONT_WEIGHT_SEMI_BOLD, 19.0f}, // Big
    };
    for (int i = 0; i < (int)Font::Count; i++) {
        auto& f = formats_[i];
        if (FAILED(dw_->CreateTextFormat(specs[i].family->c_str(), nullptr, specs[i].weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, specs[i].size, L"en-us", &f)))
            return false;
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        dw_->CreateEllipsisTrimmingSign(f.Get(), &ellipsis_[i]);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        f->SetTrimming(&trim, ellipsis_[i].Get());
    }
    return true;
}

bool D2DGfx::begin(HWND hwnd, float dpi) {
    hwnd_ = hwnd;
    dpi_ = dpi;
    if (!rt_) {
        RECT cr;
        GetClientRect(hwnd, &cr);
        auto props = D2D1::RenderTargetProperties();
        props.dpiX = props.dpiY = dpi;
        if (FAILED(d2d_->CreateHwndRenderTarget(
                props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(cr.right, cr.bottom)), &rt_)))
            return false;
        rt_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_);
        gen_++;
    }
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

void D2DGfx::end() {
    if (rt_ && rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
        brush_.Reset();
        rt_.Reset();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void D2DGfx::resize(UINT w, UINT h) {
    if (rt_) rt_->Resize(D2D1::SizeU(w, h));
}

void D2DGfx::setDpi(float dpi) {
    dpi_ = dpi;
    if (rt_) rt_->SetDpi(dpi, dpi);
}

ID2D1SolidColorBrush* D2DGfx::brush(Color c) {
    brush_->SetColor(d2c(c));
    return brush_.Get();
}

void D2DGfx::clear(Color c) { rt_->Clear(d2c(c)); }

void D2DGfx::fillRect(const RectF& r, Color c) { rt_->FillRectangle(d2r(r), brush(c)); }

void D2DGfx::fillRound(const RectF& r, float radius, Color c) {
    rt_->FillRoundedRectangle(D2D1::RoundedRect(d2r(r), radius, radius), brush(c));
}

void D2DGfx::strokeRound(const RectF& r, float radius, Color c, float width, bool dashed) {
    float h = width / 2;
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + h, r.top + h, r.right - h, r.bottom - h), radius, radius),
                              brush(c), width, dashed ? dashed_.Get() : nullptr);
}

void D2DGfx::fillCircle(PointF p, float radius, Color c) {
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(p.x, p.y), radius, radius), brush(c));
}

void D2DGfx::gradientRound(const RectF& r, float radius, Color a, Color b) {
    D2D1_GRADIENT_STOP stops[] = {{0.0f, d2c(a)}, {1.0f, d2c(b)}};
    ComPtr<ID2D1GradientStopCollection> col;
    rt_->CreateGradientStopCollection(stops, 2, &col);
    ComPtr<ID2D1LinearGradientBrush> g;
    rt_->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.right, r.bottom)), col.Get(),
        &g);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(d2r(r), radius, radius), g.Get());
}

void D2DGfx::strokePolyline(const std::vector<PointF>& pts, bool closed, Color c, float width) {
    if (pts.size() < 2) return;
    ComPtr<ID2D1PathGeometry> geo;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(d2d_->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) return;
    sink->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y), D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < pts.size(); i++) sink->AddLine(D2D1::Point2F(pts[i].x, pts[i].y));
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close();
    rt_->DrawGeometry(geo.Get(), brush(c), width, round_.Get());
}

void D2DGfx::text(const std::string& s, const RectF& r, Font f, Color c, Align align) {
    auto* fmt = formats_[(int)f].Get();
    fmt->SetTextAlignment(align == Align::Left     ? DWRITE_TEXT_ALIGNMENT_LEADING
                          : align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                   : DWRITE_TEXT_ALIGNMENT_TRAILING);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    std::wstring w = wide(s);
    rt_->DrawText(w.c_str(), (UINT32)w.size(), fmt, d2r(r), brush(c), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

float D2DGfx::measure(const std::string& s, Font f) {
    std::wstring w = wide(s);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw_->CreateTextLayout(w.c_str(), (UINT32)w.size(), formats_[(int)f].Get(), 4096, 64, &layout))) return 0;
    DWRITE_TEXT_METRICS m;
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void D2DGfx::setAliased(bool aliased) {
    rt_->SetAntialiasMode(aliased ? D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void D2DGfx::pushClip(const RectF& r) { rt_->PushAxisAlignedClip(d2r(r), D2D1_ANTIALIAS_MODE_ALIASED); }
void D2DGfx::popClip() { rt_->PopAxisAlignedClip(); }

void D2DGfx::fileIcon(const std::string& path, const RectF& r) {
    IconEntry& e = icons_[path];
    if (!e.hicon) {
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(wide(path).c_str(), 0, &sfi, sizeof sfi, SHGFI_ICON | SHGFI_LARGEICON)) e.hicon = sfi.hIcon;
    }
    if (e.hicon && wic_ && (!e.bmp || e.gen != gen_)) {
        e.bmp.Reset();
        ComPtr<IWICBitmap> wb;
        ComPtr<IWICFormatConverter> conv;
        if (SUCCEEDED(wic_->CreateBitmapFromHICON(e.hicon, &wb)) && SUCCEEDED(wic_->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(wb.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                       WICBitmapPaletteTypeMedianCut)))
            rt_->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &e.bmp);
        e.gen = gen_;
    }
    if (e.bmp) rt_->DrawBitmap(e.bmp.Get(), d2r(r), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
