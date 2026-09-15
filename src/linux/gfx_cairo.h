#pragma once
#include "../ui/gfx.h"
#include <cairo.h>
#include <pango/pangocairo.h>

// Cairo/Pango implementation of the shared drawing interface. GTK supplies a
// top-left-origin context whose coordinates are already logical pixels.
class CairoGfx : public Gfx {
public:
    CairoGfx();
    ~CairoGfx() override;
    void begin(cairo_t* cr, float scale);

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
    float scale() const override { return scale_; }

private:
    PangoLayout* makeLayout(const std::string& s, Font f);
    cairo_t* cr_ = nullptr;
    float scale_ = 1;
    PangoFontDescription* fonts_[(int)Font::Count] = {};
};
