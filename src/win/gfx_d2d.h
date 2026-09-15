#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../ui/gfx.h"
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <map>

using Microsoft::WRL::ComPtr;

// Direct2D/DirectWrite implementation of Gfx.
class D2DGfx : public Gfx {
public:
    ~D2DGfx() override;
    bool init();
    bool begin(HWND hwnd, float dpi);
    void end();
    void resize(UINT w, UINT h);
    void setDpi(float dpi);

    void clear(Color c) override;
    void fillRect(const RectF& r, Color c) override;
    void fillRound(const RectF& r, float radius, Color c) override;
    void strokeRound(const RectF& r, float radius, Color c, float width, bool dashed) override;
    void fillCircle(PointF p, float radius, Color c) override;
    void gradientRound(const RectF& r, float radius, Color a, Color b) override;
    void strokePolyline(const std::vector<PointF>& pts, bool closed, Color c, float width) override;
    void text(const std::string& s, const RectF& r, Font f, Color c, Align align) override;
    float measure(const std::string& s, Font f) override;
    void setAliased(bool aliased) override;
    void pushClip(const RectF& r) override;
    void popClip() override;
    void fileIcon(const std::string& path, const RectF& r) override;
    float scale() const override { return dpi_ / 96.0f; }

private:
    struct IconEntry {
        HICON hicon = nullptr;
        ComPtr<ID2D1Bitmap> bmp;
        UINT gen = 0;
    };
    ID2D1SolidColorBrush* brush(Color c);

    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dw_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<ID2D1HwndRenderTarget> rt_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1StrokeStyle> round_, dashed_;
    ComPtr<IDWriteTextFormat> formats_[(int)Font::Count];
    ComPtr<IDWriteInlineObject> ellipsis_[(int)Font::Count];
    std::map<std::string, IconEntry> icons_;
    HWND hwnd_ = nullptr;
    float dpi_ = 96.0f;
    UINT gen_ = 0;
};
