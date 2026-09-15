#include "gfx_cairo.h"
#include <gtk/gtk.h>
#include <algorithm>
#include <cmath>
#include <sys/stat.h>

namespace {

void source(cairo_t* cr, Color c) { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a); }

void rounded(cairo_t* cr, const RectF& r, float radius) {
    double w = std::max(0.0f, r.right - r.left), h = std::max(0.0f, r.bottom - r.top);
    double d = std::min<double>(std::max(radius, 0.0f), std::min(w, h) / 2.0), k = 0.5522847498;
    double l = r.left, t = r.top, rr = r.right, b = r.bottom;
    cairo_new_sub_path(cr);
    cairo_move_to(cr, l + d, t);
    cairo_line_to(cr, rr - d, t);
    cairo_curve_to(cr, rr - d + d * k, t, rr, t + d - d * k, rr, t + d);
    cairo_line_to(cr, rr, b - d);
    cairo_curve_to(cr, rr, b - d + d * k, rr - d + d * k, b, rr - d, b);
    cairo_line_to(cr, l + d, b);
    cairo_curve_to(cr, l + d - d * k, b, l, b - d + d * k, l, b - d);
    cairo_line_to(cr, l, t + d);
    cairo_curve_to(cr, l, t + d - d * k, l + d - d * k, t, l + d, t);
    cairo_close_path(cr);
}

} // namespace

CairoGfx::CairoGfx() {
    struct Spec {
        const char* family;
        float size;
        PangoWeight weight;
    } specs[] = {
        {"Sans", 16.0f, PANGO_WEIGHT_SEMIBOLD}, {"Sans", 13.0f, PANGO_WEIGHT_NORMAL},
        {"Sans", 13.0f, PANGO_WEIGHT_SEMIBOLD}, {"Sans", 11.5f, PANGO_WEIGHT_NORMAL},
        {"Sans", 11.5f, PANGO_WEIGHT_SEMIBOLD}, {"Monospace", 11.5f, PANGO_WEIGHT_NORMAL},
        {"Sans", 18.5f, PANGO_WEIGHT_SEMIBOLD},
    };
    for (int i = 0; i < (int)Font::Count; i++) {
        fonts_[i] = pango_font_description_new();
        pango_font_description_set_family(fonts_[i], specs[i].family);
        pango_font_description_set_absolute_size(fonts_[i], specs[i].size * PANGO_SCALE);
        pango_font_description_set_weight(fonts_[i], specs[i].weight);
    }
}

CairoGfx::~CairoGfx() {
    for (auto* f : fonts_) pango_font_description_free(f);
}

void CairoGfx::begin(cairo_t* cr, float scale) {
    cr_ = cr;
    scale_ = std::max(1.0f, scale);
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr_, CAIRO_LINE_JOIN_ROUND);
}

void CairoGfx::clear(Color c) {
    cairo_save(cr_);
    cairo_set_operator(cr_, CAIRO_OPERATOR_SOURCE);
    source(cr_, c);
    cairo_paint(cr_);
    cairo_restore(cr_);
}

void CairoGfx::fillRect(const RectF& r, Color c) {
    source(cr_, c);
    cairo_rectangle(cr_, r.left, r.top, r.right - r.left, r.bottom - r.top);
    cairo_fill(cr_);
}

void CairoGfx::fillRound(const RectF& r, float radius, Color c) {
    rounded(cr_, r, radius);
    source(cr_, c);
    cairo_fill(cr_);
}

void CairoGfx::strokeRound(const RectF& r, float radius, Color c, float width, bool dashed) {
    cairo_save(cr_);
    RectF x{r.left + width / 2, r.top + width / 2, r.right - width / 2, r.bottom - width / 2};
    rounded(cr_, x, radius);
    source(cr_, c);
    cairo_set_line_width(cr_, width);
    if (dashed) {
        double d[] = {2.5 * width, 3.0 * width};
        cairo_set_dash(cr_, d, 2, 0);
    }
    cairo_stroke(cr_);
    cairo_restore(cr_);
}

void CairoGfx::fillCircle(PointF p, float radius, Color c) {
    cairo_arc(cr_, p.x, p.y, radius, 0, 2 * M_PI);
    source(cr_, c);
    cairo_fill(cr_);
}

void CairoGfx::gradientRound(const RectF& r, float radius, Color a, Color b) {
    cairo_save(cr_);
    rounded(cr_, r, radius);
    cairo_clip(cr_);
    cairo_pattern_t* g = cairo_pattern_create_linear(r.left, r.top, r.right, r.bottom);
    cairo_pattern_add_color_stop_rgba(g, 0, a.r, a.g, a.b, a.a);
    cairo_pattern_add_color_stop_rgba(g, 1, b.r, b.g, b.b, b.a);
    cairo_set_source(cr_, g);
    cairo_paint(cr_);
    cairo_pattern_destroy(g);
    cairo_restore(cr_);
}

void CairoGfx::strokePolyline(const std::vector<PointF>& pts, bool closed, Color c, float width) {
    if (pts.size() < 2) return;
    cairo_new_path(cr_);
    cairo_move_to(cr_, pts[0].x, pts[0].y);
    for (size_t i = 1; i < pts.size(); i++) cairo_line_to(cr_, pts[i].x, pts[i].y);
    if (closed) cairo_close_path(cr_);
    source(cr_, c);
    cairo_set_line_width(cr_, width);
    cairo_stroke(cr_);
}

PangoLayout* CairoGfx::makeLayout(const std::string& s, Font f) {
    PangoLayout* layout = pango_cairo_create_layout(cr_);
    pango_layout_set_font_description(layout, fonts_[(int)f]);
    pango_layout_set_text(layout, s.c_str(), (int)s.size());
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    return layout;
}

void CairoGfx::text(const std::string& s, const RectF& r, Font f, Color c, Align align) {
    if (s.empty()) return;
    PangoLayout* layout = makeLayout(s, f);
    int avail = std::max(1, (int)std::floor(r.right - r.left));
    pango_layout_set_width(layout, avail * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    double x = align == Align::Left ? r.left : align == Align::Center ? (r.left + r.right - w) / 2 : r.right - w;
    double y = r.top + ((r.bottom - r.top) - h) / 2;
    source(cr_, c);
    cairo_move_to(cr_, x, y);
    pango_cairo_show_layout(cr_, layout);
    g_object_unref(layout);
}

float CairoGfx::measure(const std::string& s, Font f) {
    if (s.empty()) return 0;
    PangoLayout* layout = makeLayout(s, f);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    g_object_unref(layout);
    return (float)w;
}

void CairoGfx::setAliased(bool aliased) {
    cairo_set_antialias(cr_, aliased ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_DEFAULT);
}

void CairoGfx::pushClip(const RectF& r) {
    cairo_save(cr_);
    cairo_rectangle(cr_, r.left, r.top, r.right - r.left, r.bottom - r.top);
    cairo_clip(cr_);
}

void CairoGfx::popClip() { cairo_restore(cr_); }

void CairoGfx::fileIcon(const std::string& path, const RectF& r) {
    struct stat st {};
    const char* name = stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) ? "folder" : "text-x-generic";
    int size = std::max(16, (int)std::round(std::min(r.right - r.left, r.bottom - r.top)));
    GError* error = nullptr;
    GdkPixbuf* pix = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), name, size, GTK_ICON_LOOKUP_FORCE_SIZE, &error);
    if (error) g_error_free(error);
    if (!pix) return;
    int pw = gdk_pixbuf_get_width(pix), ph = gdk_pixbuf_get_height(pix);
    double x = r.left + ((r.right - r.left) - pw) / 2, y = r.top + ((r.bottom - r.top) - ph) / 2;
    gdk_cairo_set_source_pixbuf(cr_, pix, x, y);
    cairo_paint(cr_);
    g_object_unref(pix);
}
