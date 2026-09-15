#pragma once
#include "../ui/gfx.h"
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

// Core Graphics / Core Text implementation of Gfx. Expects a flipped (top-left origin) context.
class CGGfx : public Gfx {
public:
    CGGfx();
    ~CGGfx() override;
    void begin(CGContextRef ctx, float width, float height, float scale);

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
    CTLineRef makeLine(const std::string& s, Font f);
    CGContextRef ctx_ = nullptr;
    float w_ = 0, h_ = 0, scale_ = 1;
    CTFontRef fonts_[(int)Font::Count] = {};
};
