#include "gfx_cg.h"
#import <AppKit/AppKit.h>
#include <algorithm>

namespace {

CGRect cgr(const RectF& r) { return CGRectMake(r.left, r.top, r.right - r.left, r.bottom - r.top); }

void fill(CGContextRef ctx, Color c) { CGContextSetRGBFillColor(ctx, c.r, c.g, c.b, c.a); }
void stroke(CGContextRef ctx, Color c) { CGContextSetRGBStrokeColor(ctx, c.r, c.g, c.b, c.a); }

// CGPathCreateWithRoundedRect aborts if the radius exceeds half the rect.
CGPathRef roundedPath(CGRect r, float radius) {
    CGFloat rad = std::max<CGFloat>(0, std::min<CGFloat>({(CGFloat)radius, r.size.width / 2, r.size.height / 2}));
    return CGPathCreateWithRoundedRect(r, rad, rad, nullptr);
}

NSMutableDictionary<NSString*, NSImage*>* iconCache() {
    static NSMutableDictionary* cache = [NSMutableDictionary dictionary];
    return cache;
}

} // namespace

CGGfx::CGGfx() {
    struct Spec {
        CGFloat size;
        NSFontWeight weight;
        bool mono;
    } specs[] = {
        {16.0, NSFontWeightSemibold, false}, // Title
        {13.0, NSFontWeightRegular, false},  // Body
        {13.0, NSFontWeightSemibold, false}, // BodyBold
        {11.5, NSFontWeightRegular, false},  // Small
        {11.5, NSFontWeightSemibold, false}, // SmallBold
        {11.5, NSFontWeightRegular, true},   // Mono
        {18.5, NSFontWeightSemibold, false}, // Big
    };
    for (int i = 0; i < (int)Font::Count; i++) {
        NSFont* f = specs[i].mono ? [NSFont monospacedSystemFontOfSize:specs[i].size weight:specs[i].weight]
                                  : [NSFont systemFontOfSize:specs[i].size weight:specs[i].weight];
        fonts_[i] = (CTFontRef)CFBridgingRetain(f);
    }
}

CGGfx::~CGGfx() {
    for (auto f : fonts_)
        if (f) CFRelease(f);
}

void CGGfx::begin(CGContextRef ctx, float width, float height, float scale) {
    ctx_ = ctx;
    w_ = width;
    h_ = height;
    scale_ = scale;
    CGContextSetLineCap(ctx_, kCGLineCapRound);
    CGContextSetLineJoin(ctx_, kCGLineJoinRound);
}

void CGGfx::clear(Color c) {
    fill(ctx_, c);
    CGContextFillRect(ctx_, CGRectMake(0, 0, w_, h_));
}

void CGGfx::fillRect(const RectF& r, Color c) {
    fill(ctx_, c);
    CGContextFillRect(ctx_, cgr(r));
}

void CGGfx::fillRound(const RectF& r, float radius, Color c) {
    CGPathRef p = roundedPath(cgr(r), radius);
    fill(ctx_, c);
    CGContextAddPath(ctx_, p);
    CGContextFillPath(ctx_);
    CGPathRelease(p);
}

void CGGfx::strokeRound(const RectF& r, float radius, Color c, float width, bool dashed) {
    CGPathRef p = roundedPath(CGRectInset(cgr(r), width / 2, width / 2), radius);
    CGContextSaveGState(ctx_);
    stroke(ctx_, c);
    CGContextSetLineWidth(ctx_, width);
    if (dashed) {
        const CGFloat dashes[] = {2.5 * width, 3.0 * width};
        CGContextSetLineDash(ctx_, 0, dashes, 2);
    }
    CGContextAddPath(ctx_, p);
    CGContextStrokePath(ctx_);
    CGContextRestoreGState(ctx_);
    CGPathRelease(p);
}

void CGGfx::fillCircle(PointF p, float radius, Color c) {
    fill(ctx_, c);
    CGContextFillEllipseInRect(ctx_, CGRectMake(p.x - radius, p.y - radius, radius * 2, radius * 2));
}

void CGGfx::gradientRound(const RectF& r, float radius, Color a, Color b) {
    CGPathRef p = roundedPath(cgr(r), radius);
    CGContextSaveGState(ctx_);
    CGContextAddPath(ctx_, p);
    CGContextClip(ctx_);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    const CGFloat comps[] = {a.r, a.g, a.b, a.a, b.r, b.g, b.b, b.a};
    const CGFloat locs[] = {0, 1};
    CGGradientRef g = CGGradientCreateWithColorComponents(cs, comps, locs, 2);
    CGContextDrawLinearGradient(ctx_, g, CGPointMake(r.left, r.top), CGPointMake(r.right, r.bottom), 0);
    CGGradientRelease(g);
    CGColorSpaceRelease(cs);
    CGContextRestoreGState(ctx_);
    CGPathRelease(p);
}

void CGGfx::strokePolyline(const std::vector<PointF>& pts, bool closed, Color c, float width) {
    if (pts.size() < 2) return;
    stroke(ctx_, c);
    CGContextSetLineWidth(ctx_, width);
    CGContextBeginPath(ctx_);
    CGContextMoveToPoint(ctx_, pts[0].x, pts[0].y);
    for (size_t i = 1; i < pts.size(); i++) CGContextAddLineToPoint(ctx_, pts[i].x, pts[i].y);
    if (closed) CGContextClosePath(ctx_);
    CGContextStrokePath(ctx_);
}

CTLineRef CGGfx::makeLine(const std::string& s, Font f) {
    NSString* str = [NSString stringWithUTF8String:s.c_str()] ?: @"";
    NSDictionary* attrs = @{
        (__bridge NSString*)kCTFontAttributeName : (__bridge id)fonts_[(int)f],
        (__bridge NSString*)kCTForegroundColorFromContextAttributeName : @YES,
    };
    NSAttributedString* as = [[NSAttributedString alloc] initWithString:str attributes:attrs];
    return CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)as);
}

void CGGfx::text(const std::string& s, const RectF& r, Font f, Color c, Align align) {
    if (s.empty()) return;
    CTLineRef line = makeLine(s, f);
    CGFloat avail = r.right - r.left;
    if (CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr) > avail) {
        CTLineRef ell = makeLine("\xE2\x80\xA6", f);
        CTLineRef cut = CTLineCreateTruncatedLine(line, std::max<CGFloat>(avail, 1), kCTLineTruncationEnd, ell);
        CFRelease(ell);
        if (cut) {
            CFRelease(line);
            line = cut;
        }
    }
    CGFloat ascent = 0, descent = 0;
    CGFloat width = CTLineGetTypographicBounds(line, &ascent, &descent, nullptr);
    CGFloat x = align == Align::Left ? r.left : align == Align::Center ? (r.left + r.right - width) / 2 : r.right - width;
    CGFloat baseline = r.top + ((r.bottom - r.top) - (ascent + descent)) / 2 + ascent;
    CGContextSaveGState(ctx_);
    fill(ctx_, c);
    CGContextSetTextMatrix(ctx_, CGAffineTransformMakeScale(1.0, -1.0)); // flipped view
    CGContextSetTextPosition(ctx_, x, baseline);
    CTLineDraw(line, ctx_);
    CGContextRestoreGState(ctx_);
    CFRelease(line);
}

float CGGfx::measure(const std::string& s, Font f) {
    if (s.empty()) return 0;
    CTLineRef line = makeLine(s, f);
    float w = (float)CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
    CFRelease(line);
    return w;
}

void CGGfx::setAliased(bool aliased) { CGContextSetShouldAntialias(ctx_, !aliased); }

void CGGfx::pushClip(const RectF& r) {
    CGContextSaveGState(ctx_);
    CGContextClipToRect(ctx_, cgr(r));
}

void CGGfx::popClip() { CGContextRestoreGState(ctx_); }

void CGGfx::fileIcon(const std::string& path, const RectF& r) {
    NSString* key = [NSString stringWithUTF8String:path.c_str()];
    if (!key) return;
    NSImage* img = iconCache()[key];
    if (!img) {
        img = [[NSWorkspace sharedWorkspace] iconForFile:key];
        if (img) iconCache()[key] = img;
    }
    [img drawInRect:NSMakeRect(r.left, r.top, r.right - r.left, r.bottom - r.top)
           fromRect:NSZeroRect
          operation:NSCompositingOperationSourceOver
           fraction:1.0
     respectFlipped:YES
              hints:nil];
}
