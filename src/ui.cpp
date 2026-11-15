#include "ui.h"
#include "app.h"
#include "services.h"
#include "settings.h"

#include <windows.h>
#include <thread>
#include <atomic>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <ctime>

using namespace Gdiplus;

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

namespace {

const float SIDEBAR_W = 236.0f;
const float PAD = 24.0f;
const float TOPBAR_H = 76.0f;
const float TABS_Y = 82.0f;
const float CONTENT_Y = 138.0f;

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool has(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    Rect inset(float d) const { return { x + d, y + d, w - 2 * d, h - 2 * d }; }
    Rect shrunk(float dx, float dy) const { return { x + dx, y + dy, w - 2 * dx, h - 2 * dy }; }
};
static RectF rf(const Rect& r) { return RectF(r.x, r.y, r.w, r.h); }
static RectF rf(float x, float y, float w, float h) { return RectF(x, y, w, h); }

struct Pal {
    bool dark = true;
    Color bg1, bg2, text, sub, faint, accent, accent2, border, chip;
    Color heroA, heroB, danger, good, warn, scrim;
};

static Pal palette(const AppState& s) {
    Pal p;
    p.dark = s.settings.dark;
    if (p.dark) {
        p.bg1 = Color(255, 8, 14, 25);
        p.bg2 = Color(255, 15, 26, 42);
        p.text = Color(255, 240, 246, 252);
        p.sub = Color(255, 168, 184, 202);
        p.faint = Color(255, 126, 144, 165);
        p.accent = Color(255, 77, 163, 255);
        p.accent2 = Color(255, 139, 124, 255);
        p.border = Color(255, 38, 53, 75);
        p.chip = Color(255, 27, 40, 58);
        p.heroA = Color(255, 26, 65, 108);
        p.heroB = Color(255, 13, 31, 51);
        p.danger = Color(255, 255, 112, 118);
        p.good = Color(255, 78, 211, 160);
        p.warn = Color(255, 255, 192, 84);
        p.scrim = Color(205, 3, 8, 15);
    } else {
        p.bg1 = Color(255, 244, 248, 253);
        p.bg2 = Color(255, 230, 238, 249);
        p.text = Color(255, 18, 31, 49);
        p.sub = Color(255, 79, 97, 120);
        p.faint = Color(255, 112, 130, 153);
        p.accent = Color(255, 45, 127, 249);
        p.accent2 = Color(255, 107, 92, 255);
        p.border = Color(255, 218, 228, 240);
        p.chip = Color(255, 238, 244, 252);
        p.heroA = Color(255, 225, 238, 255);
        p.heroB = Color(255, 207, 226, 250);
        p.danger = Color(255, 224, 74, 84);
        p.good = Color(255, 31, 165, 118);
        p.warn = Color(255, 224, 143, 33);
        p.scrim = Color(125, 18, 31, 50);
    }
    return p;
}

static Color mix(Color a, Color b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return Color(255,
                 (BYTE)(a.GetR() + (b.GetR() - a.GetR()) * t),
                 (BYTE)(a.GetG() + (b.GetG() - a.GetG()) * t),
                 (BYTE)(a.GetB() + (b.GetB() - a.GetB()) * t));
}

static Color withAlpha(Color c, int a) {
    return Color((BYTE)std::max(0, std::min(255, a)), c.GetR(), c.GetG(), c.GetB());
}

// ---------------- fonts / text ----------------
static HWND g_hwnd = nullptr;
static std::map<int, Font*> g_fonts;

static Font* font(int px, bool bold = false, int family = 0) {
    int key = (family << 20) | (px << 2) | (bold ? 1 : 0);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    const wchar_t* name = family == 1 ? L"Segoe UI"
                        : family == 2 ? L"Segoe UI Light"
                        : family == 3 ? L"Segoe MDL2 Assets"
                        : L"Microsoft YaHei UI";
    Font* f = new Font(name, (REAL)px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    if (f->GetLastStatus() != Ok) {
        delete f;
        f = new Font(L"Segoe UI", (REAL)px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    }
    g_fonts[key] = f;
    return f;
}

static void text(Graphics& g, const std::wstring& s, Font* f, Rect r, Color c,
                 int align = 0, int valign = 0, bool wrap = false) {
    if (s.empty() || r.w <= 0 || r.h <= 0) return;
    StringFormat fmt;
    fmt.SetAlignment(align == 0 ? StringAlignmentNear : (align == 1 ? StringAlignmentCenter : StringAlignmentFar));
    fmt.SetLineAlignment(valign == 0 ? StringAlignmentNear : (valign == 1 ? StringAlignmentCenter : StringAlignmentFar));
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    if (!wrap) fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush br(c);
    g.DrawString(s.c_str(), -1, f, rf(r), &fmt, &br);
}

static float tw(Graphics& g, const std::wstring& s, Font* f) {
    if (s.empty()) return 0;
    RectF b;
    StringFormat fmt(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    g.MeasureString(s.c_str(), -1, f, PointF(0, 0), &fmt, &b);
    return b.Width;
}

static std::wstring wfmt(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 512, fmt, ap);
    va_end(ap);
    return buf;
}

static std::wstring oneDecimal(double v) {
    wchar_t b[32];
    swprintf(b, 32, L"%.1f", v);
    return b;
}

static std::wstring zeroDecimal(double v) {
    return std::to_wstring((int)std::lround(v));
}

static std::wstring unitTemp(const AppState& s) { return s.settings.celsius ? L"°C" : L"°F"; }

// ---------------- shapes ----------------
static void roundPath(GraphicsPath& p, const Rect& r, float rad) {
    rad = std::max(0.0f, std::min(rad, std::min(r.w, r.h) * 0.5f));
    if (rad <= 0.1f) { p.AddRectangle(rf(r)); return; }
    float d = rad * 2;
    p.AddArc(r.x, r.y, d, d, 180, 90);
    p.AddArc(r.right() - d, r.y, d, d, 270, 90);
    p.AddArc(r.right() - d, r.bottom() - d, d, d, 0, 90);
    p.AddArc(r.x, r.bottom() - d, d, d, 90, 90);
    p.CloseFigure();
}

static void fillRound(Graphics& g, const Rect& r, float rad, Brush& b) {
    GraphicsPath p;
    roundPath(p, r, rad);
    g.FillPath(&b, &p);
}

static void fillRound(Graphics& g, const Rect& r, float rad, Color c) {
    SolidBrush b(c);
    fillRound(g, r, rad, b);
}

static void strokeRound(Graphics& g, const Rect& r, float rad, Color c, float width = 1.0f) {
    GraphicsPath p;
    roundPath(p, r, rad);
    Pen pen(c, width);
    g.DrawPath(&pen, &p);
}

static void ellipse(Graphics& g, float x, float y, float w, float h, Color c) {
    SolidBrush b(c);
    g.FillEllipse(&b, x, y, w, h);
}

static void line(Graphics& g, float x1, float y1, float x2, float y2, Color c, float width = 1.0f) {
    Pen p(c, width);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    g.DrawLine(&p, x1, y1, x2, y2);
}

static void arc(Graphics& g, const Rect& r, float start, float sweep, Color c, float width) {
    Pen p(c, width);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    g.DrawArc(&p, r.x, r.y, r.w, r.h, start, sweep);
}

static void shadow(Graphics& g, const Rect& r, float rad, const Pal& p) {
    int a = p.dark ? 28 : 18;
    fillRound(g, { r.x - 2, r.y + 7, r.w + 4, r.h + 4 }, rad + 4, Color((BYTE)a, 0, 0, 0));
    fillRound(g, { r.x + 2, r.y + 12, r.w - 4, r.h }, rad + 8, Color((BYTE)(a / 2), 0, 0, 0));
}

static void drawBackground(Graphics& g, const AppState& s, const Pal& p);

// ---------------- blur source / panel ----------------
static Bitmap* g_back = nullptr;
static Bitmap* g_bgCache = nullptr;
static int g_backW = 0, g_backH = 0;
static int g_bgCacheW = 0, g_bgCacheH = 0;
static bool g_bgCacheDark = true;
static bool g_bgCacheValid = false;

static void clearBackgroundCache() {
    delete g_bgCache;
    g_bgCache = nullptr;
    g_bgCacheW = g_bgCacheH = 0;
    g_bgCacheValid = false;
}

static void ensureBack(int pw, int ph) {
    if (!g_back || g_backW != pw || g_backH != ph) {
        delete g_back;
        // Opaque target enables ClearType and avoids alpha-compositing text fuzz.
        g_back = new Bitmap(pw, ph, PixelFormat32bppRGB);
        g_backW = pw;
        g_backH = ph;
        clearBackgroundCache();
    }
}

static void ensureStaticBackground(const AppState& s, const Pal& p, int pw, int ph) {
    if (g_bgCacheValid && g_bgCache && g_bgCacheW == pw && g_bgCacheH == ph && g_bgCacheDark == p.dark)
        return;
    clearBackgroundCache();
    g_bgCache = new Bitmap(pw, ph, PixelFormat32bppPARGB);
    Graphics bg(g_bgCache);
    bg.SetSmoothingMode(SmoothingModeAntiAlias);
    bg.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    bg.SetCompositingQuality(CompositingQualityHighQuality);
    bg.ScaleTransform(s.dpi, s.dpi);
    drawBackground(bg, s, p);
    bg.Flush();
    g_bgCacheW = pw;
    g_bgCacheH = ph;
    g_bgCacheDark = p.dark;
    g_bgCacheValid = true;
}

static std::map<std::string, Bitmap*> g_panelCache;
static const int PANEL_MARGIN = 16;

static Bitmap* buildPanelBitmap(const Pal& p, const Rect& r, float rad, int tint) {
    int w = std::max(1, (int)std::lround(r.w));
    int h = std::max(1, (int)std::lround(r.h));
    Bitmap* bmp = new Bitmap(w + PANEL_MARGIN * 2, h + PANEL_MARGIN * 2, PixelFormat32bppPARGB);
    Graphics pg(bmp);
    pg.SetSmoothingMode(SmoothingModeAntiAlias);
    pg.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    pg.SetCompositingQuality(CompositingQualityHighQuality);
    pg.Clear(Color(0, 0, 0, 0));
    Rect cr = { (float)PANEL_MARGIN, (float)PANEL_MARGIN, (float)w, (float)h };
    shadow(pg, cr, rad, p);
    GraphicsPath path;
    roundPath(path, cr, rad);
    if (p.dark) {
        Color top = tint > 40 ? Color(255, 26, 51, 78) : Color(255, 24, 36, 51);
        Color bottom = tint > 40 ? Color(255, 17, 35, 55) : Color(255, 18, 28, 42);
        LinearGradientBrush fill(PointF(cr.x, cr.y), PointF(cr.x, cr.bottom()), top, bottom);
        pg.FillPath(&fill, &path);
        Pen pen(p.border, 1.0f);
        pg.DrawPath(&pen, &path);
    } else {
        LinearGradientBrush fill(PointF(cr.x, cr.y), PointF(cr.x, cr.bottom()),
                                 Color(255, 255, 255, 255), Color(255, 250, 253, 255));
        pg.FillPath(&fill, &path);
        Pen pen(p.border, 1.0f);
        pg.DrawPath(&pen, &path);
    }
    pg.Flush();
    return bmp;
}

static void panel(Graphics& g, const AppState& s, const Pal& p, const Rect& r, float rad, int tint = 0) {
    if (r.w <= 0 || r.h <= 0) return;
    char key[96];
    snprintf(key, sizeof key, "%d_%d_%d_%d_%d", (int)std::lround(r.w), (int)std::lround(r.h),
             (int)std::lround(rad), p.dark ? 1 : 0, tint);
    auto it = g_panelCache.find(key);
    Bitmap* bmp = nullptr;
    if (it == g_panelCache.end()) {
        bmp = buildPanelBitmap(p, r, rad, tint);
        g_panelCache[key] = bmp;
        if (g_panelCache.size() > 48) {
            auto old = g_panelCache.begin();
            if (old->second != bmp) { delete old->second; g_panelCache.erase(old); }
        }
    } else {
        bmp = it->second;
    }
    g.DrawImage(bmp, RectF(r.x - PANEL_MARGIN, r.y - PANEL_MARGIN,
                           r.w + PANEL_MARGIN * 2, r.h + PANEL_MARGIN * 2),
                0, 0, (REAL)bmp->GetWidth(), (REAL)bmp->GetHeight(), UnitPixel);
}

// ---------------- background ----------------
static void colorBlob(Graphics& g, float cx, float cy, float rx, float ry, Color c) {
    // Layered ellipses avoid banding/hard edges from PathGradientBrush and look
    // like a soft atmospheric glow once the background is downsampled.
    const int layers = 18;
    int baseA = c.GetA();
    for (int i = layers; i >= 1; --i) {
        float t = (float)i / (float)layers;
        float ex = rx * t, ey = ry * t;
        int a = std::max(1, (int)std::lround(baseA * 0.055f));
        ellipse(g, cx - ex, cy - ey, ex * 2, ey * 2, Color((BYTE)a, c.GetR(), c.GetG(), c.GetB()));
    }
}

static void drawBackground(Graphics& g, const AppState& s, const Pal& p) {
    float W = (float)s.W, H = (float)s.H;
    LinearGradientBrush bg(PointF(0, 0), PointF(0, H), p.bg1, p.bg2);
    g.FillRectangle(&bg, RectF(0, 0, W, H));
}

// ---------------- weather icons ----------------
static void drawSun(Graphics& g, Rect r, bool hero, double t) {
    float s = std::min(r.w, r.h);
    float cx = r.cx(), cy = r.cy();
    float rad = s * 0.22f;
    float rot = (float)t * 0.08f;
    for (int i = 0; i < 12; i++) {
        double a = rot + i * M_PI / 6.0;
        float x1 = cx + cosf((float)a) * s * 0.30f;
        float y1 = cy + sinf((float)a) * s * 0.30f;
        float x2 = cx + cosf((float)a) * s * 0.40f;
        float y2 = cy + sinf((float)a) * s * 0.40f;
        line(g, x1, y1, x2, y2, Color(i % 2 ? 150 : 235, 255, 208, 100), std::max(1.5f, s * 0.022f));
    }
    LinearGradientBrush b(PointF(cx - rad, cy - rad), PointF(cx + rad, cy + rad),
                          Color(255, 255, 243, 186), Color(255, 255, 171, 55));
    g.FillEllipse(&b, cx - rad, cy - rad, rad * 2, rad * 2);
    ellipse(g, cx - rad * 0.45f, cy - rad * 0.55f, rad * 0.55f, rad * 0.42f, Color(105, 255, 255, 255));
}

static void drawMoon(Graphics& g, Rect r, bool hero, double t, bool darkTheme) {
    float s = std::min(r.w, r.h);
    float cx = r.cx(), cy = r.cy();
    float rad = s * 0.22f;
    Color top = darkTheme ? Color(255, 255, 252, 236) : Color(255, 244, 249, 255);
    Color bot = darkTheme ? Color(255, 203, 216, 238) : Color(255, 164, 190, 220);
    LinearGradientBrush b(PointF(cx - rad, cy - rad), PointF(cx + rad, cy + rad), top, bot);
    g.FillEllipse(&b, cx - rad, cy - rad, rad * 2, rad * 2);
    if (!darkTheme) {
        Pen rim(Color(105, 130, 162, 145), std::max(1.0f, rad * 0.045f));
        g.DrawEllipse(&rim, cx - rad, cy - rad, rad * 2, rad * 2);
    }
    ellipse(g, cx - rad * 0.58f, cy - rad * 0.58f, rad * 0.42f, rad * 0.34f,
            darkTheme ? Color(120, 255, 255, 255) : Color(150, 255, 255, 255));
    Color crater = darkTheme ? Color(42, 135, 158, 198) : Color(85, 115, 152, 165);
    ellipse(g, cx + rad * 0.18f, cy + rad * 0.12f, rad * 0.22f, rad * 0.18f, crater);
    ellipse(g, cx - rad * 0.12f, cy + rad * 0.42f, rad * 0.15f, rad * 0.12f, crater);
    ellipse(g, cx + rad * 0.44f, cy - rad * 0.30f, rad * 0.10f, rad * 0.10f, crater);
    for (int i = 0; i < 4; i++) {
        float x = cx + sinf((float)t * 0.2f + i * 1.7f) * s * 0.32f;
        float y = cy + cosf((float)t * 0.17f + i * 2.1f) * s * 0.26f;
        ellipse(g, x, y, 2.0f, 2.0f, Color(170, 255, 255, 255));
    }
}

static void buildCloudPath(GraphicsPath& p, float x, float y, float s, float pad) {
    p.SetFillMode(FillModeWinding);
    p.AddEllipse(x + 0.05f * s - pad, y + 0.46f * s - pad, 0.42f * s + pad * 2, 0.34f * s + pad * 2);
    p.AddEllipse(x + 0.22f * s - pad, y + 0.27f * s - pad, 0.48f * s + pad * 2, 0.52f * s + pad * 2);
    p.AddEllipse(x + 0.50f * s - pad, y + 0.42f * s - pad, 0.40f * s + pad * 2, 0.34f * s + pad * 2);
    p.AddRectangle(RectF(x + 0.10f * s - pad, y + 0.58f * s - pad, 0.72f * s + pad * 2, 0.20f * s + pad * 2));
}

static void drawCloud(Graphics& g, Rect r, bool heavy, bool hero) {
    float s = std::min(r.w, r.h);
    float x = r.x + (r.w - s) * 0.5f, y = r.y + (r.h - s) * 0.5f;
    GraphicsPath p;
    buildCloudPath(p, x, y, s, 0.0f);
    GraphicsPath rim;
    buildCloudPath(rim, x, y, s, std::max(1.2f, s * 0.008f));
    SolidBrush rimBrush(heavy ? Color(65, 70, 95, 115) : Color(55, 93, 126, 128));
    g.FillPath(&rimBrush, &rim);
    Color top = heavy ? Color(255, 221, 235, 250) : Color(255, 255, 255, 255);
    Color bot = heavy ? Color(255, 139, 173, 205) : Color(255, 182, 209, 234);
    LinearGradientBrush b(PointF(0, y + 0.24f * s), PointF(0, y + 0.80f * s), top, bot);
    if (hero) {
        colorBlob(g, x + s * 0.5f, y + s * 0.66f, s * 0.42f, s * 0.24f, Color(45, 255, 255, 255));
    }
    ellipse(g, x + s * 0.16f, y + s * 0.79f, s * 0.68f, s * 0.075f, Color(42, 74, 104, 135));
    g.FillPath(&b, &p);
}

static void drawDrops(Graphics& g, Rect r, int count, double t, bool snow, bool hero) {
    float s = std::min(r.w, r.h);
    float x = r.x + (r.w - s) * 0.5f, y = r.y + (r.h - s) * 0.5f;
    float start = 0.5f - (count - 1) * 0.075f;
    for (int i = 0; i < count; i++) {
        float bx = x + s * (start + i * 0.15f);
        float by = y + s * 0.785f;
        float off = fmodf((float)t * 0.55f + i * 0.31f, 1.0f) * s * 0.06f;
        if (!snow) {
            float len = s * 0.055f;
            line(g, bx, by + off, bx, by + off + len, Color(205, 123, 232, 255), std::max(1.2f, s * 0.014f));
            ellipse(g, bx - s * 0.006f, by + off - s * 0.008f, s * 0.012f, s * 0.012f, Color(220, 170, 246, 255));
        } else {
            float cx = bx, cy = by + off;
            float rr = s * 0.024f;
            for (int k = 0; k < 3; k++) {
                float a = (float)(k * M_PI / 3.0 + t * 0.3);
                line(g, cx - cosf(a) * rr, cy - sinf(a) * rr, cx + cosf(a) * rr, cy + sinf(a) * rr,
                     Color(225, 235, 250, 255), std::max(0.9f, s * 0.010f));
            }
        }
    }
}

static void drawBolt(Graphics& g, Rect r) {
    float s = std::min(r.w, r.h);
    float x = r.x + (r.w - s) * 0.5f, y = r.y + (r.h - s) * 0.5f;
    GraphicsPath p;
    p.AddPolygon(std::vector<PointF>{
        PointF(x + 0.55f * s, y + 0.70f * s), PointF(x + 0.47f * s, y + 0.84f * s),
        PointF(x + 0.52f * s, y + 0.84f * s), PointF(x + 0.48f * s, y + 0.98f * s),
        PointF(x + 0.62f * s, y + 0.80f * s), PointF(x + 0.55f * s, y + 0.80f * s)
    }.data(), 6);
    LinearGradientBrush b(PointF(0, y + 0.7f * s), PointF(0, y + 0.99f * s),
                          Color(255, 255, 225, 110), Color(255, 255, 173, 47));
    g.FillPath(&b, &p);
}

static void drawWeatherIconRaw(Graphics& g, Cond cond, bool isDay, Rect r, bool hero, double t, int themeDark) {
    if (cond == Cond::Clear) {
        if (isDay) drawSun(g, r, hero, t);
        else drawMoon(g, r, hero, t, themeDark == 1);
        return;
    }
    if (cond == Cond::Fair) {
        Rect sun = { r.x + r.w * 0.52f, r.y + r.h * 0.12f, r.w * 0.41f, r.h * 0.41f };
        if (isDay) drawSun(g, sun, hero, t);
        else drawMoon(g, sun, hero, t, themeDark == 1);
        drawCloud(g, { r.x + r.w * 0.06f, r.y + r.h * 0.08f, r.w * 0.72f, r.h * 0.52f }, false, hero);
        return;
    }
    if (cond == Cond::Partly) {
        Rect sun = { r.x + r.w * 0.52f, r.y + r.h * 0.10f, r.w * 0.50f, r.h * 0.50f };
        if (isDay) drawSun(g, sun, hero, t);
        else drawMoon(g, sun, hero, t, themeDark == 1);
        drawCloud(g, { r.x + r.w * 0.04f, r.y + r.h * 0.04f, r.w * 0.86f, r.h * 0.64f }, false, hero);
        return;
    }
    if (cond == Cond::Cloudy) {
        if (!hero) {
            drawCloud(g, { r.x + r.w * 0.05f, r.y + r.h * 0.22f, r.w * 0.90f, r.h * 0.62f }, false, false);
        } else {
            drawCloud(g, { r.x + r.w * 0.38f, r.y + r.h * 0.05f, r.w * 0.52f, r.h * 0.44f }, true, true);
            drawCloud(g, { r.x + r.w * 0.04f, r.y + r.h * 0.34f, r.w * 0.78f, r.h * 0.56f }, false, true);
        }
        return;
    }
    if (cond == Cond::Fog) {
        drawCloud(g, { r.x + r.w * 0.06f, r.y + r.h * 0.16f, r.w * 0.86f, r.h * 0.62f }, false, hero);
        for (int i = 0; i < 3; i++) {
            float y = r.y + r.h * (0.70f + i * 0.10f);
            line(g, r.x + r.w * (0.16f + 0.04f * i), y, r.x + r.w * (0.84f - 0.04f * i), y,
                 Color(130, 235, 245, 255), std::max(1.2f, r.h * 0.016f));
        }
        return;
    }
    if (cond == Cond::Showers) {
        Rect small = { r.x + r.w * 0.52f, r.y + r.h * 0.10f, r.w * 0.49f, r.h * 0.49f };
        if (isDay) drawSun(g, small, hero, t);
        else drawMoon(g, small, hero, t, themeDark == 1);
        drawCloud(g, { r.x + r.w * 0.05f, r.y + r.h * 0.02f, r.w * 0.84f, r.h * 0.64f }, false, hero);
        drawDrops(g, { r.x, r.y, r.w, r.h }, 3, t, false, hero);
        return;
    }
    if (cond == Cond::Snow || cond == Cond::Sleet) {
        drawCloud(g, { r.x + r.w * 0.05f, r.y + r.h * 0.12f, r.w * 0.88f, r.h * 0.66f }, true, hero);
        drawDrops(g, { r.x, r.y, r.w, r.h }, cond == Cond::Sleet ? 2 : 4, t, true, hero);
        if (cond == Cond::Sleet) drawDrops(g, { r.x, r.y, r.w, r.h }, 2, t + 0.4, false, hero);
        return;
    }
    if (cond == Cond::Thunder) {
        drawCloud(g, { r.x + r.w * 0.05f, r.y + r.h * 0.08f, r.w * 0.88f, r.h * 0.66f }, true, hero);
        drawDrops(g, { r.x, r.y, r.w, r.h }, 2, t, false, hero);
        drawBolt(g, r);
        return;
    }
    // rain family
    drawCloud(g, { r.x + r.w * 0.04f, r.y + r.h * 0.10f, r.w * 0.90f, r.h * 0.68f },
              cond == Cond::HeavyRain, hero);
    drawDrops(g, { r.x, r.y, r.w, r.h }, cond == Cond::Drizzle ? 2 : (cond == Cond::HeavyRain ? 5 : 3), t, false, hero);
}

// Weather artwork is expensive to rebuild with GDI+ paths on every frame.
// Cache each condition/day/theme at two sizes and blit it as needed.
static std::map<int, Bitmap*> g_iconCache;

static void drawWeatherIcon(Graphics& g, Cond cond, bool isDay, Rect r, bool hero, double t, int themeDark) {
    int n = hero ? 256 : 64;
    int key = (((int)cond * 2 + (isDay ? 1 : 0)) * 2 + themeDark) * 2 + (hero ? 1 : 0);
    auto it = g_iconCache.find(key);
    Bitmap* icon = nullptr;
    if (it == g_iconCache.end()) {
        icon = new Bitmap(n, n, PixelFormat32bppPARGB);
        Graphics ig(icon);
        ig.SetSmoothingMode(SmoothingModeAntiAlias);
        ig.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        ig.SetCompositingQuality(CompositingQualityHighQuality);
        ig.Clear(Color(0, 0, 0, 0));
        drawWeatherIconRaw(ig, cond, isDay, Rect{ 0, 0, (float)n, (float)n }, hero, 0.0, themeDark);
        ig.Flush();
        g_iconCache[key] = icon;
    } else {
        icon = it->second;
    }
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(icon, RectF(r.x, r.y, r.w, r.h), 0, 0, (REAL)n, (REAL)n, UnitPixel);
}

// ---------------- small icons ----------------
enum Mini {
    MI_DROP, MI_WIND, MI_CLOUD, MI_SUN, MI_PRESSURE, MI_SUNRISE, MI_MOON,
    MI_PIN, MI_SEARCH, MI_GEAR, MI_REFRESH, MI_CLOSE, MI_MIN, MI_MAX,
    MI_LEFT, MI_RIGHT, MI_GRID, MI_CLOCK, MI_CALENDAR, MI_GLOBE, MI_PLUS,
    MI_CHECK, MI_TRASH, MI_THERMO, MI_LAYERS, MI_SLIDERS
};

static void mini(Graphics& g, int type, Rect r, Color c, float sw = 1.8f) {
    float x = r.x, y = r.y, w = r.w, h = r.h;
    float cx = r.cx(), cy = r.cy();
    Pen p(c, sw);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    p.SetLineJoin(LineJoinRound);
    switch (type) {
    case MI_DROP: {
        GraphicsPath path;
        path.AddBezier(cx, y + h * 0.12f, x + w * 0.18f, y + h * 0.46f, x + w * 0.18f, y + h * 0.64f,
                       x + w * 0.22f, y + h * 0.78f);
        path.AddBezier(x + w * 0.22f, y + h * 0.78f, x + w * 0.30f, y + h * 0.98f, x + w * 0.70f, y + h * 0.98f,
                       x + w * 0.78f, y + h * 0.78f);
        path.AddBezier(x + w * 0.78f, y + h * 0.78f, x + w * 0.82f, y + h * 0.64f, x + w * 0.82f, y + h * 0.46f,
                       cx, y + h * 0.12f);
        g.DrawPath(&p, &path);
        break;
    }
    case MI_WIND:
        for (int i = 0; i < 3; i++) {
            float yy = y + h * (0.28f + i * 0.22f);
            float x2 = x + w * (0.78f - i * 0.06f);
            line(g, x + w * 0.12f, yy, x2, yy, c, sw);
            arc(g, { x2 - w * 0.09f, yy - h * 0.08f, w * 0.18f, h * 0.16f }, -90, 180, c, sw);
        }
        break;
    case MI_CLOUD: {
        GraphicsPath path;
        path.AddArc(x + w * 0.05f, y + h * 0.45f, w * 0.40f, h * 0.34f, 140, 240);
        path.AddArc(x + w * 0.24f, y + h * 0.24f, w * 0.46f, h * 0.46f, 180, 210);
        path.AddArc(x + w * 0.52f, y + h * 0.42f, w * 0.40f, h * 0.36f, 220, 210);
        path.AddLine(x + w * 0.90f, y + h * 0.78f, x + w * 0.18f, y + h * 0.78f);
        g.DrawPath(&p, &path);
        break;
    }
    case MI_SUN:
        g.DrawEllipse(&p, cx - w * 0.20f, cy - h * 0.20f, w * 0.40f, h * 0.40f);
        for (int i = 0; i < 8; i++) {
            float a = (float)(i * M_PI / 4);
            line(g, cx + cosf(a) * w * 0.29f, cy + sinf(a) * h * 0.29f,
                 cx + cosf(a) * w * 0.44f, cy + sinf(a) * h * 0.44f, c, sw);
        }
        break;
    case MI_PRESSURE:
        g.DrawEllipse(&p, x + w * 0.10f, y + h * 0.10f, w * 0.80f, h * 0.80f);
        line(g, cx, cy, cx + w * 0.22f, cy - h * 0.18f, c, sw);
        ellipse(g, cx - 2, cy - 2, 4, 4, c);
        break;
    case MI_SUNRISE:
        arc(g, { x + w * 0.12f, y + h * 0.28f, w * 0.76f, h * 0.76f }, 180, 180, c, sw);
        line(g, x + w * 0.08f, y + h * 0.86f, x + w * 0.92f, y + h * 0.86f, c, sw);
        line(g, cx, y + h * 0.08f, cx, y + h * 0.28f, c, sw);
        line(g, cx, y + h * 0.58f, cx, y + h * 0.36f, c, sw);
        break;
    case MI_MOON: {
        GraphicsPath path;
        path.SetFillMode(FillModeAlternate);
        path.AddEllipse(cx - w * 0.34f, cy - h * 0.40f, w * 0.68f, h * 0.80f);
        path.AddEllipse(cx - w * 0.02f, cy - h * 0.43f, w * 0.68f, h * 0.86f);
        SolidBrush sb(c);
        g.FillPath(&sb, &path);
        line(g, x + w * 0.82f, y + h * 0.12f, x + w * 0.82f, y + h * 0.26f, c, sw * 0.8f);
        line(g, x + w * 0.75f, y + h * 0.19f, x + w * 0.89f, y + h * 0.19f, c, sw * 0.8f);
        break;
    }
    case MI_PIN:
        g.DrawEllipse(&p, cx - w * 0.24f, y + h * 0.10f, w * 0.48f, h * 0.48f);
        line(g, cx - w * 0.18f, y + h * 0.50f, cx, y + h * 0.90f, c, sw);
        line(g, cx + w * 0.18f, y + h * 0.50f, cx, y + h * 0.90f, c, sw);
        ellipse(g, cx - 2, y + h * 0.30f, 4, 4, c);
        break;
    case MI_SEARCH:
        g.DrawEllipse(&p, cx - w * 0.40f, cy - h * 0.44f, w * 0.60f, h * 0.60f);
        line(g, cx + w * 0.14f, cy + h * 0.14f, cx + w * 0.40f, cy + h * 0.40f, c, sw);
        break;
    case MI_GEAR:
        g.DrawEllipse(&p, cx - w * 0.18f, cy - h * 0.18f, w * 0.36f, h * 0.36f);
        for (int i = 0; i < 8; i++) {
            float a = (float)(i * M_PI / 4);
            line(g, cx + cosf(a) * w * 0.26f, cy + sinf(a) * h * 0.26f,
                 cx + cosf(a) * w * 0.40f, cy + sinf(a) * h * 0.40f, c, sw);
        }
        break;
    case MI_REFRESH:
        arc(g, { x + w * 0.14f, y + h * 0.14f, w * 0.72f, h * 0.72f }, 30, 280, c, sw);
        line(g, x + w * 0.82f, y + h * 0.20f, x + w * 0.82f, y + h * 0.48f, c, sw);
        line(g, x + w * 0.82f, y + h * 0.20f, x + w * 0.56f, y + h * 0.22f, c, sw);
        break;
    case MI_CLOSE:
        line(g, x + w * 0.28f, y + h * 0.28f, x + w * 0.72f, y + h * 0.72f, c, sw);
        line(g, x + w * 0.72f, y + h * 0.28f, x + w * 0.28f, y + h * 0.72f, c, sw);
        break;
    case MI_MIN:
        line(g, x + w * 0.25f, cy, x + w * 0.75f, cy, c, sw);
        break;
    case MI_MAX:
        g.DrawRectangle(&p, x + w * 0.27f, y + h * 0.27f, w * 0.46f, h * 0.46f);
        break;
    case MI_LEFT:
        line(g, x + w * 0.60f, y + h * 0.25f, x + w * 0.36f, cy, c, sw);
        line(g, x + w * 0.36f, cy, x + w * 0.60f, y + h * 0.75f, c, sw);
        break;
    case MI_RIGHT:
        line(g, x + w * 0.40f, y + h * 0.25f, x + w * 0.64f, cy, c, sw);
        line(g, x + w * 0.64f, cy, x + w * 0.40f, y + h * 0.75f, c, sw);
        break;
    case MI_GRID:
        for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++)
            g.DrawRectangle(&p, x + w * (0.18f + i * 0.36f), y + h * (0.18f + j * 0.36f), w * 0.26f, h * 0.26f);
        break;
    case MI_CLOCK:
        g.DrawEllipse(&p, x + w * 0.12f, y + h * 0.12f, w * 0.76f, h * 0.76f);
        line(g, cx, cy - h * 0.26f, cx, cy, c, sw);
        line(g, cx, cy, cx + w * 0.22f, cy + h * 0.12f, c, sw);
        break;
    case MI_CALENDAR:
        g.DrawRectangle(&p, x + w * 0.14f, y + h * 0.20f, w * 0.72f, h * 0.66f);
        line(g, x + w * 0.14f, y + h * 0.38f, x + w * 0.86f, y + h * 0.38f, c, sw);
        line(g, x + w * 0.32f, y + h * 0.10f, x + w * 0.32f, y + h * 0.28f, c, sw);
        line(g, x + w * 0.68f, y + h * 0.10f, x + w * 0.68f, y + h * 0.28f, c, sw);
        break;
    case MI_GLOBE:
        g.DrawEllipse(&p, x + w * 0.12f, y + h * 0.12f, w * 0.76f, h * 0.76f);
        g.DrawEllipse(&p, cx - w * 0.20f, y + h * 0.12f, w * 0.40f, h * 0.76f);
        line(g, x + w * 0.15f, cy, x + w * 0.85f, cy, c, sw);
        break;
    case MI_PLUS:
        line(g, cx, y + h * 0.20f, cx, y + h * 0.80f, c, sw);
        line(g, x + w * 0.20f, cy, x + w * 0.80f, cy, c, sw);
        break;
    case MI_CHECK:
        line(g, x + w * 0.20f, cy, x + w * 0.42f, y + h * 0.72f, c, sw);
        line(g, x + w * 0.42f, y + h * 0.72f, x + w * 0.82f, y + h * 0.28f, c, sw);
        break;
    case MI_TRASH:
        g.DrawRectangle(&p, x + w * 0.26f, y + h * 0.32f, w * 0.48f, h * 0.54f);
        line(g, x + w * 0.18f, y + h * 0.26f, x + w * 0.82f, y + h * 0.26f, c, sw);
        line(g, cx - w * 0.08f, y + h * 0.16f, cx + w * 0.08f, y + h * 0.16f, c, sw);
        break;
    case MI_THERMO:
        arc(g, { cx - w * 0.13f, y + h * 0.12f, w * 0.26f, h * 0.50f }, 180, 180, c, sw);
        g.DrawEllipse(&p, cx - w * 0.16f, y + h * 0.68f, w * 0.32f, h * 0.24f);
        line(g, cx, y + h * 0.30f, cx, y + h * 0.72f, c, sw);
        break;
    case MI_LAYERS:
        for (int i = 0; i < 3; i++) {
            GraphicsPath lp;
            lp.AddPolygon(std::vector<PointF>{
                PointF(cx, y + h * (0.18f + i * 0.16f)),
                PointF(x + w * 0.80f, y + h * (0.34f + i * 0.16f)),
                PointF(cx, y + h * (0.50f + i * 0.16f)),
                PointF(x + w * 0.20f, y + h * (0.34f + i * 0.16f))
            }.data(), 4);
            g.DrawPath(&p, &lp);
        }
        break;
    case MI_SLIDERS:
        for (int i = 0; i < 3; i++) {
            float yy = y + h * (0.24f + i * 0.26f);
            line(g, x + w * 0.16f, yy, x + w * 0.84f, yy, c, sw * 0.9f);
            float kx = x + w * (i == 0 ? 0.34f : (i == 1 ? 0.66f : 0.46f));
            ellipse(g, kx - w * 0.07f, yy - h * 0.07f, w * 0.14f, h * 0.14f, c);
        }
        break;
    }
}

static void drawLogo(Graphics& g, Rect r, const Pal& p) {
    GraphicsPath path;
    roundPath(path, r, r.w * 0.30f);
    LinearGradientBrush b(PointF(r.x, r.y), PointF(r.right(), r.bottom()),
                          Color(255, 39, 105, 255), Color(255, 23, 198, 190));
    g.FillPath(&b, &path);
    Pen border(Color(70, 255, 255, 255), 1.0f);
    g.DrawPath(&border, &path);
    Rect sun = { r.x + r.w * 0.48f, r.y + r.h * 0.12f, r.w * 0.42f, r.h * 0.42f };
    drawSun(g, sun, false, 0);
    Rect cloud = { r.x + r.w * 0.12f, r.y + r.h * 0.36f, r.w * 0.76f, r.h * 0.52f };
    drawCloud(g, cloud, false, false);
    (void)p;
}

// ---------------- layout ----------------
struct Layout {
    Rect sidebar, topbar, titleArea, search, themeBtn, settingsBtn, winMin, winMax, winClose;
    Rect tabs[5], tabBar, statusText;
    Rect autoItem, addCity, bottomTheme, bottomSettings, sidebarStatus;
    Rect city[12];
    int cityCount = 0;
    Rect content;
    Rect hero, metrics[4], hourly, weekly, weeklyCol[7], showTen, retry;
    Rect chart, details, tenCard, tenRow[10];
    Rect map, mapPrev, mapNext, mapZoomIn, mapZoomOut, mapReset, mapInfo, mapDate;
    Rect dlg, closeDlg, themeDark, themeLight, unitC, unitF, toggles[5], clearBtn, updateBtn;
    Rect searchCard, searchRow[6];
    int searchRowCount = 0;
    Rect advice, alertBar;
};

static Layout layout(const AppState& s) {
    Layout L;
    float W = (float)s.W, H = (float)s.H;
    L.sidebar = { 0, 0, SIDEBAR_W, H };
    L.topbar = { SIDEBAR_W, 0, W - SIDEBAR_W, TOPBAR_H };
    L.titleArea = { SIDEBAR_W + PAD, 14, 380, 50 };
    L.search = { W - 502, 18, 300, 40 };
    L.themeBtn = { W - 190, 21, 34, 34 };
    L.settingsBtn = { W - 148, 21, 34, 34 };
    L.winMin = { W - 102, 22, 26, 26 };
    L.winMax = { W - 69, 22, 26, 26 };
    L.winClose = { W - 36, 22, 26, 26 };
    float tabW = std::min(126.0f, (W - 400.0f - (SIDEBAR_W + PAD)) / 5.0f);
    L.tabBar = { SIDEBAR_W + PAD, TABS_Y, tabW * 5, 40 };
    for (int i = 0; i < 5; i++) L.tabs[i] = { L.tabBar.x + i * tabW, L.tabBar.y, tabW, 40 };
    L.statusText = { W - 380, TABS_Y + 4, 350, 32 };
    L.content = { SIDEBAR_W + PAD, CONTENT_Y, W - SIDEBAR_W - 2 * PAD, H - CONTENT_Y - 46 };
    L.advice = { SIDEBAR_W + PAD, H - 34, W - SIDEBAR_W - 2 * PAD, 20 };

    L.autoItem = { 18, 118, 200, 62 };
    float y = 192;
    int maxVisible = std::max(3, (int)((H - 340) / 60));
    maxVisible = std::min(maxVisible, 8);
    int start = std::max(0, std::min((int)s.settings.saved.size() - maxVisible, s.sidebarScroll));
    L.cityCount = std::max(0, std::min((int)s.settings.saved.size() - start, maxVisible));
    for (int i = 0; i < L.cityCount; i++) {
        L.city[i] = { 18, y, 200, 54 };
        y += 60;
    }
    L.addCity = { 18, y, 200, 38 };
    L.bottomTheme = { 18, H - 58, 100, 36 };
    L.bottomSettings = { 124, H - 58, 100, 36 };
    L.sidebarStatus = { 18, H - 98, 200, 32 };

    float cw = L.content.w, ch = L.content.h;
    float gap = 14;
    float top = L.content.y;
    if (s.page == 0 && !s.weather.alertText.empty()) {
        L.alertBar = { L.content.x, L.content.y, cw, 30 };
        top += 40;
        ch -= 40;
    }
    float heroH = std::max(250.0f, ch * 0.43f);
    float hourlyH = std::max(166.0f, ch * 0.26f);
    float weeklyH = ch - heroH - hourlyH - 2 * gap;
    if (weeklyH < 146) { heroH -= (146 - weeklyH); weeklyH = 146; }
    float rightW = std::max(300.0f, std::min(360.0f, cw * 0.35f));
    L.hero = { L.content.x, top, cw - rightW - gap, heroH };
    float mx = L.hero.right() + gap;
    float mw = (rightW - 12) * 0.5f;
    float mh = (heroH - 12) * 0.5f;
    L.metrics[0] = { mx, top, mw, mh };
    L.metrics[1] = { mx + mw + 12, top, mw, mh };
    L.metrics[2] = { mx, top + mh + 12, mw, mh };
    L.metrics[3] = { mx + mw + 12, top + mh + 12, mw, mh };
    L.hourly = { L.content.x, L.hero.bottom() + gap, cw, hourlyH };
    L.weekly = { L.content.x, L.hourly.bottom() + gap, cw, std::max(146.0f, L.content.bottom() - L.hourly.bottom() - gap) };
    for (int i = 0; i < 7; i++) L.weeklyCol[i] = { L.weekly.x + i * L.weekly.w / 7.0f, L.weekly.y + 38, L.weekly.w / 7.0f, L.weekly.h - 38 };
    L.showTen = { L.weekly.right() - 142, L.weekly.y + 10, 122, 26 };

    float chartH = std::max(240.0f, ch * 0.40f);
    L.chart = { L.content.x, L.content.y, cw, chartH };
    L.details = { L.content.x, L.content.y + chartH + 14, cw, ch - chartH - 14 };

    L.tenCard = L.content;
    float headH = 54;
    for (int i = 0; i < 10; i++) L.tenRow[i] = { L.content.x, L.content.y + headH + i * ((ch - headH) / 10.0f), cw, (ch - headH) / 10.0f };

    L.map = L.content;
    L.mapPrev = { L.content.x + 24, L.content.y + 94, 30, 30 };
    L.mapDate = { L.content.x + 58, L.content.y + 94, 128, 30 };
    L.mapNext = { L.content.x + 190, L.content.y + 94, 30, 30 };
    L.mapInfo = { L.content.right() - 268, L.content.y + 22, 244, 112 };
    L.mapZoomIn = { L.content.right() - 56, L.content.bottom() - 168, 32, 32 };
    L.mapZoomOut = { L.content.right() - 56, L.content.bottom() - 130, 32, 32 };
    L.mapReset = { L.content.right() - 56, L.content.bottom() - 92, 32, 32 };

    float dw = std::min(560.0f, W - 80);
    float dh = std::min(620.0f, H - 70);
    L.dlg = { (W - dw) * 0.5f, (H - dh) * 0.5f, dw, dh };
    L.closeDlg = { L.dlg.right() - 52, L.dlg.y + 20, 30, 30 };
    L.themeDark = { L.dlg.x + 28, L.dlg.y + 96, (dw - 68) * 0.5f, 40 };
    L.themeLight = { L.themeDark.right() + 12, L.dlg.y + 96, (dw - 68) * 0.5f, 40 };
    L.unitC = { L.dlg.x + 28, L.dlg.y + 168, 90, 38 };
    L.unitF = { L.unitC.right() + 10, L.dlg.y + 168, 90, 38 };
    for (int i = 0; i < 5; i++) L.toggles[i] = { L.dlg.x + 28, L.dlg.y + 242 + i * 48, dw - 56, 40 };
    L.clearBtn = { L.dlg.x + 28, L.dlg.bottom() - 58, 132, 34 };
    L.updateBtn = { L.dlg.x + 172, L.dlg.bottom() - 58, 132, 34 };

    if (!s.searchResults.empty() || s.searchLoading || s.searchNoResult) {
        int rows = std::min(6, (int)s.searchResults.size());
        float h = 10 + (rows > 0 ? rows * 58.0f : 54.0f);
        L.searchCard = { L.search.x - 56, L.search.y + 48, 380, h };
        L.searchRowCount = rows;
        for (int i = 0; i < rows; i++) L.searchRow[i] = { L.searchCard.x + 6, L.searchCard.y + 8 + i * 58.0f, L.searchCard.w - 12, 52 };
    }
    return L;
}

// ---------------- date helpers ----------------
static int locationOffset(const AppState& s) {
    if (s.hasWeather) return s.weather.loc.offset;
    if (s.settings.hasCurrent) return s.settings.current.offset;
    return ianaOffset("Asia/Shanghai", (long long)time(nullptr));
}

static std::wstring localDateLine(const AppState& s) {
    long long now = (long long)time(nullptr);
    int off = locationOffset(s);
    CivilTime c = civilLocal(now, off);
    return std::to_wstring(c.mo) + L"月" + std::to_wstring(c.d) + L"日 " + weekdayCN(c.wd) +
           wfmt(L" · %02d:%02d", c.h, c.mi);
}

static std::wstring mapDateLabel(int dayOffset) {
    long long now = (long long)time(nullptr);
    long long days = now / 86400 - dayOffset;
    int y; unsigned m, d;
    civilFromDays(days, y, m, d);
    if (dayOffset == 0) return L"今天";
    if (dayOffset == 1) return L"昨天";
    return wfmt(L"%d月%u日", (int)m, d);
}

} // namespace

namespace {

static void spinner(Graphics& g, float cx, float cy, float rad, double t, Color c) {
    for (int i = 0; i < 3; i++) {
        float a = (float)(t * 2.2 + i * 2.1);
        Pen p(withAlpha(c, 210 - i * 55), 2.0f);
        p.SetStartCap(LineCapRound);
        p.SetEndCap(LineCapRound);
        g.DrawArc(&p, cx - rad, cy - rad, rad * 2, rad * 2, a * 57.2958f, 78);
    }
}

static void chip(Graphics& g, const Pal& p, Rect r, const std::wstring& label, int iconType) {
    if (p.dark) {
        fillRound(g, r, r.h * 0.5f, Color(22, 255, 255, 255));
        strokeRound(g, r, r.h * 0.5f, Color(38, 255, 255, 255));
    } else {
        fillRound(g, r, r.h * 0.5f, p.chip);
        strokeRound(g, r, r.h * 0.5f, Color(210, 223, 238));
    }
    float iconSz = r.h * 0.48f;
    Rect ic = { r.x + 10, r.cy() - iconSz * 0.5f, iconSz, iconSz };
    mini(g, iconType, ic, p.accent, 1.4f);
    text(g, label, font(11.5f), { ic.right() + 6, r.y, r.w - (ic.right() - r.x) - 12, r.h }, p.sub, 0, 1);
}

static void drawSidebar(Graphics& g, const AppState& s, const Pal& p, const Layout& L) {
    Rect sb = L.sidebar;
    if (p.dark) {
        fillRound(g, sb, 0, Color(255, 11, 18, 30));
        line(g, sb.right() - 0.5f, 0, sb.right() - 0.5f, sb.h, p.border, 1.0f);
    } else {
        fillRound(g, sb, 0, Color(244, 255, 255, 255));
        line(g, sb.right() - 0.5f, 0, sb.right() - 0.5f, sb.h, Color(255, 223, 231, 242), 1.0f);
    }

    Rect logo = { 22, 22, 38, 38 };
    drawLogo(g, logo, p);
    text(g, L"AURA", font(22, true), { 70, 18, 150, 29 }, p.text);
    text(g, L"WEATHER STUDIO", font(8.5f, true, 1), { 71, 46, 150, 15 }, p.faint);

    text(g, L"位置", font(11, true), { 20, 93, 100, 18 }, p.faint);

    // auto location
    bool autoActive = s.settings.hasCurrent && s.settings.current.autoLoc;
    Rect ar = L.autoItem;
    if (autoActive) {
        fillRound(g, ar, 14, p.dark ? Color(30, 96, 221, 255) : Color(255, 231, 241, 255));
        strokeRound(g, ar, 14, withAlpha(p.accent, p.dark ? 95 : 90));
    } else {
        fillRound(g, ar, 14, p.dark ? Color(14, 255, 255, 255) : Color(250, 255, 255, 255));
        strokeRound(g, ar, 14, p.dark ? Color(28, 255, 255, 255) : Color(232, 239, 248));
    }
    Rect pinCircle = { ar.x + 12, ar.y + 16, 30, 30 };
    fillRound(g, pinCircle, 10, autoActive ? withAlpha(p.accent, 40) : (p.dark ? Color(22, 255, 255, 255) : Color(240, 246, 254)));
    mini(g, MI_PIN, pinCircle.inset(7), autoActive ? p.accent : p.sub, 1.7f);
    text(g, L"自动定位", font(12.5f, true), { ar.x + 52, ar.y + 10, 90, 20 }, autoActive ? p.text : p.sub);
    std::wstring sub = s.locating ? L"正在定位…" : (s.settings.hasCurrent && s.settings.current.autoLoc
                        ? widen(s.settings.current.name) : L"获取当前位置");
    text(g, sub, font(11), { ar.x + 52, ar.y + 32, 126, 18 }, autoActive ? withAlpha(p.text, 220) : p.faint);
    if (s.locating) spinner(g, ar.right() - 18, ar.cy(), 7, s.animTime, p.accent);

    int start = std::max(0, std::min((int)s.settings.saved.size() - L.cityCount, s.sidebarScroll));
    for (int i = 0; i < L.cityCount; i++) {
        int idx = start + i;
        if (idx < 0 || idx >= (int)s.settings.saved.size()) break;
        const Location& loc = s.settings.saved[idx];
        bool active = s.settings.hasCurrent && !s.settings.current.autoLoc && loc.key() == s.settings.current.key();
        Rect cr = L.city[i];
        bool hover = cr.has(s.mouseX, s.mouseY);
        if (active) {
            fillRound(g, cr, 13, p.dark ? Color(24, 82, 210, 245) : Color(232, 241, 255));
            strokeRound(g, cr, 13, withAlpha(p.accent, 80));
        } else if (hover) {
            fillRound(g, cr, 13, p.dark ? Color(18, 255, 255, 255) : Color(245, 249, 254));
        }
        text(g, widen(loc.name), font(13, active), { cr.x + 14, cr.y + 8, 128, 21 }, active ? p.text : (p.dark ? p.text : Color(255, 30, 46, 68)));
        std::wstring sub2 = widen(loc.admin.empty() ? loc.country : loc.admin);
        if (sub2.empty()) sub2 = L"已收藏";
        text(g, sub2, font(10.5f), { cr.x + 14, cr.y + 30, 126, 16 }, active ? withAlpha(p.text, 210) : p.faint);
        if (active && s.hasWeather) {
            text(g, tempText(s.weather.now.temp, s.settings.celsius, false), font(17, true, 1),
                 { cr.right() - 66, cr.y + 15, 52, 26 }, active ? p.text : p.sub, 2);
        } else if (!active) {
            mini(g, MI_RIGHT, { cr.right() - 24, cr.cy() - 7, 14, 14 }, p.faint, 1.3f);
        }
        if (hover && active) {
            Rect xr = { cr.right() - 20, cr.y + 4, 16, 16 };
            ellipse(g, xr.x, xr.y, xr.w, xr.h, p.dark ? Color(60, 0, 0, 0) : Color(225, 235, 245));
            mini(g, MI_CLOSE, xr.inset(4), p.sub, 1.2f);
        }
    }

    bool addHover = L.addCity.has(s.mouseX, s.mouseY);
    fillRound(g, L.addCity, 12, addHover ? (p.dark ? Color(20, 255, 255, 255) : Color(240, 246, 254)) : Color(0, 0, 0, 0));
    mini(g, MI_PLUS, { L.addCity.x + 12, L.addCity.y + 10, 18, 18 }, addHover ? p.accent : p.faint, 1.6f);
    text(g, L"添加城市", font(12, true), { L.addCity.x + 38, L.addCity.y + 8, 140, 22 }, addHover ? p.text : p.sub);

    // bottom status
    Rect st = L.sidebarStatus;
    Color dot = s.updating ? p.accent : (s.hasWeather ? p.good : p.warn);
    ellipse(g, st.x + 2, st.y + 6, 7, 7, dot);
    std::wstring upd = s.updating ? L"正在更新天气…" : (s.hasWeather ? relativeUpdate(s.weather.updatedUtc) : L"等待天气数据");
    text(g, upd, font(11), { st.x + 16, st.y - 2, 180, 18 }, p.sub);
    text(g, L"每 10 分钟自动检查", font(10.5f), { st.x + 16, st.y + 17, 180, 16 }, p.faint);

    bool th = L.bottomTheme.has(s.mouseX, s.mouseY);
    bool sh = L.bottomSettings.has(s.mouseX, s.mouseY);
    auto btn = [&](Rect r, bool hover, int icon, const std::wstring& label) {
        fillRound(g, r, 11, hover ? (p.dark ? Color(22, 255, 255, 255) : Color(238, 245, 254)) : (p.dark ? Color(12, 255, 255, 255) : Color(250, 252, 255)));
        strokeRound(g, r, 11, p.dark ? Color(25, 255, 255, 255) : Color(228, 236, 247));
        mini(g, icon, { r.x + 12, r.y + 10, 16, 16 }, hover ? p.accent : p.sub, 1.5f);
        text(g, label, font(11, true), { r.x + 34, r.y, r.w - 36, r.h }, hover ? p.text : p.sub, 0, 1);
    };
    btn(L.bottomTheme, th, p.dark ? MI_SUN : MI_MOON, p.dark ? L"亮色模式" : L"暗色模式");
    btn(L.bottomSettings, sh, MI_SLIDERS, L"设置");
}

static void drawTopBar(Graphics& g, const AppState& s, const Pal& p, const Layout& L) {
    static const wchar_t* titles[] = { L"天气概览", L"逐小时预报", L"未来十天", L"日出与日落", L"月相" };
    text(g, titles[std::max(0, std::min(4, s.page))], font(22, true), L.titleArea, p.text);
    text(g, localDateLine(s), font(11.5f), { L.titleArea.x + 1, L.titleArea.y + 30, 380, 19 }, p.sub);

    // search
    bool focused = GetFocus() == s.hSearch;
    fillRound(g, L.search, L.search.h * 0.5f, p.dark ? Color(255, 24, 36, 53) : Color(255, 255, 255, 255));
    strokeRound(g, L.search, L.search.h * 0.5f, focused ? withAlpha(p.accent, 210) : p.border,
                focused ? 1.4f : 1.0f);
    mini(g, MI_SEARCH, { L.search.x + 13, L.search.cy() - 9, 18, 18 }, focused ? p.accent : p.faint, 1.6f);
    if (s.searchQuery.empty()) {
        text(g, L"搜索城市 / 输入坐标", font(11.5f), { L.search.x + 40, L.search.y + 1, L.search.w - 50, L.search.h - 2 },
             p.faint, 0, 1);
    } else {
        text(g, s.searchQuery, font(12), { L.search.x + 40, L.search.y + 1, L.search.w - 50, L.search.h - 2 },
             p.text, 0, 1);
    }
    if (focused) {
        float caretX = L.search.x + 42 + (s.searchQuery.empty() ? 0 : tw(g, s.searchQuery, font(12)));
        line(g, caretX, L.search.cy() - 8, caretX, L.search.cy() + 8, p.accent, 1.2f);
    }

    auto circleButton = [&](Rect r, int icon, bool active) {
        bool hover = r.has(s.mouseX, s.mouseY);
        if (hover) fillRound(g, r, r.w * 0.5f, p.dark ? Color(26, 255, 255, 255) : Color(238, 244, 253));
        if (active) fillRound(g, r, r.w * 0.5f, withAlpha(p.accent, 35));
        mini(g, icon, r.inset(9), active || hover ? p.accent : p.sub, 1.6f);
    };
    circleButton(L.themeBtn, p.dark ? MI_SUN : MI_MOON, false);
    circleButton(L.settingsBtn, MI_SLIDERS, s.settingsOpen);

    auto winButton = [&](Rect r, int icon, bool closeBtn) {
        bool hover = r.has(s.mouseX, s.mouseY);
        if (hover) fillRound(g, r, r.w * 0.5f, closeBtn ? Color(235, 232, 73, 84) : (p.dark ? Color(28, 255, 255, 255) : Color(231, 238, 248)));
        mini(g, icon, r.inset(8), hover && closeBtn ? Color(255, 255, 255, 255) : p.sub, 1.5f);
    };
    winButton(L.winMin, MI_MIN, false);
    winButton(L.winMax, MI_MAX, false);
    winButton(L.winClose, MI_CLOSE, true);

    line(g, SIDEBAR_W, TOPBAR_H - 0.5f, (float)s.W, TOPBAR_H - 0.5f,
         p.dark ? Color(22, 255, 255, 255) : Color(232, 238, 247), 1.0f);
}

static void drawTabs(Graphics& g, const AppState& s, const Pal& p, const Layout& L) {
    fillRound(g, L.tabBar, L.tabBar.h * 0.5f, p.dark ? Color(255, 18, 28, 42) : Color(255, 255, 255, 255));
    strokeRound(g, L.tabBar, L.tabBar.h * 0.5f, p.border);
    static const wchar_t* names[] = { L"概览", L"逐小时", L"十天", L"日出", L"月相" };
    static const int icons[] = { MI_GRID, MI_CLOCK, MI_CALENDAR, MI_SUNRISE, MI_MOON };
    for (int i = 0; i < 5; i++) {
        Rect r = L.tabs[i];
        bool active = s.page == i;
        bool hover = r.has(s.mouseX, s.mouseY);
        if (active) {
            Rect ar = r.inset(3);
            fillRound(g, ar, ar.h * 0.5f, p.dark ? Color(52, 108, 211, 255) : Color(235, 242, 254));
            strokeRound(g, ar, ar.h * 0.5f, p.dark ? Color(50, 255, 255, 255) : Color(205, 220, 240));
        } else if (hover) {
            fillRound(g, r.inset(3), r.h * 0.5f, p.dark ? Color(14, 255, 255, 255) : Color(246, 249, 254));
        }
        float iconSz = 18;
        float textWidth = tw(g, names[i], font(12.5f, true));
        float total = iconSz + 8 + textWidth;
        float startX = r.cx() - total * 0.5f;
        mini(g, icons[i], { startX, r.cy() - iconSz * 0.5f, iconSz, iconSz },
             active ? (p.dark ? p.text : p.accent) : p.sub, 1.6f);
        text(g, names[i], font(12.5f, true), { startX + iconSz + 8, r.y, textWidth + 4, r.h },
             active ? (p.dark ? p.text : p.accent) : p.sub, 0, 1);
    }
    // right status
    if (s.updating) {
        spinner(g, L.statusText.x + 10, L.statusText.cy(), 7, s.animTime, p.accent);
        text(g, L"正在同步天气数据…", font(10.5f), { L.statusText.x + 24, L.statusText.y, L.statusText.w - 24, L.statusText.h }, p.faint, 2, 1);
    } else if (!s.errorText.empty()) {
        ellipse(g, L.statusText.x + 5, L.statusText.cy() - 3, 6, 6, p.danger);
        text(g, s.errorText, font(10.5f), { L.statusText.x + 18, L.statusText.y, L.statusText.w - 18, L.statusText.h }, p.danger, 2, 1);
    } else if (s.hasWeather) {
        ellipse(g, L.statusText.x + 5, L.statusText.cy() - 3, 6, 6, p.good);
        text(g, relativeUpdate(s.weather.updatedUtc) + L" · 每 10 分钟刷新", font(11.5f),
             { L.statusText.x + 18, L.statusText.y, L.statusText.w - 18, L.statusText.h }, p.sub, 2, 1);
    }
}

} // namespace

namespace {

static Rect heroRetryRect(const Layout& L) {
    return { L.hero.x + 28, L.hero.bottom() - 62, 110, 36 };
}

static Rect hourlyMoreRect(const Layout& L) {
    return { L.hourly.right() - 164, L.hourly.y + 12, 146, 26 };
}

static int timeMinutes(const std::string& s) {
    int h = 0, m = 0;
    if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2) return -1;
    return h * 60 + m;
}

static void drawSunArc(Graphics& g, const AppState& s, const Pal& p, Rect r) {
    if (s.weather.days.empty()) return;
    int rise = timeMinutes(s.weather.days[0].sunrise);
    int set = timeMinutes(s.weather.days[0].sunset);
    if (rise < 0 || set < 0 || set <= rise) return;
    CivilTime now = civilLocal((long long)time(nullptr), locationOffset(s));
    int cur = now.h * 60 + now.mi;
    float t = (float)(cur - rise) / (float)(set - rise);
    t = std::max(0.0f, std::min(1.0f, t));
    float cx = r.cx(), cy = r.bottom() - 8;
    float rx = r.w * 0.42f, ry = r.h * 0.78f;
    Pen pen(withAlpha(p.sub, 120), 1.4f);
    g.DrawArc(&pen, cx - rx, cy - ry, rx * 2, ry * 2, 180.0f, 180.0f);
    float a = (180.0f + 180.0f * t) * 3.14159265f / 180.0f;
    float sx = cx + cosf(a) * rx, sy = cy + sinf(a) * ry;
    ellipse(g, sx - 4, sy - 4, 8, 8, withAlpha(p.warn, 55));
    ellipse(g, sx - 2.5f, sy - 2.5f, 5, 5, p.warn);
    text(g, widen(s.weather.days[0].sunrise), font(11.0f), { cx - rx - 6, cy + 5, 58, 18 }, p.sub, 1);
    text(g, widen(s.weather.days[0].sunset), font(11.0f), { cx + rx - 52, cy + 5, 58, 18 }, p.sub, 1);
}

static void drawHero(Graphics& g, const AppState& s, const Pal& p, const Layout& L) {
    Rect h = L.hero;
    panel(g, s, p, h, 24, 16);
    GraphicsPath clip;
    roundPath(clip, h, 24);
    g.SetClip(&clip, CombineModeReplace);
    LinearGradientBrush tint(PointF(h.x, h.y), PointF(h.right(), h.bottom()),
                             withAlpha(p.heroA, p.dark ? 100 : 170), withAlpha(p.heroB, p.dark ? 45 : 150));
    g.FillRectangle(&tint, rf(h));
    g.ResetClip();

    if (!s.hasWeather) {
        float cx = h.x + 34;
        if (s.locating || s.updating) {
            spinner(g, cx + 10, h.cy() - 14, 16, s.animTime, p.accent);
            text(g, s.locating ? L"正在定位你的位置…" : L"正在获取天气数据…", font(18, true), { cx + 40, h.cy() - 34, h.w - 80, 30 }, p.text);
            text(g, L"通过 IP 定位，并从 MET Norway 获取逐小时预报", font(10.5f),
                 { cx + 42, h.cy() + 2, h.w - 90, 20 }, p.sub);
        } else {
            mini(g, MI_CLOUD, { cx, h.cy() - 68, 46, 46 }, withAlpha(p.accent, 180), 2.2f);
            text(g, L"暂时无法获取天气", font(22, true), { cx, h.cy() - 14, h.w - 80, 30 }, p.text);
            std::wstring errText = s.errorText.empty() ? std::wstring(L"请检查网络连接后重试") : s.errorText;
            text(g, errText, font(10.5f),
                 { cx, h.cy() + 22, h.w - 90, 40 }, p.sub, 0, 0, true);
            Rect rr = heroRetryRect(L);
            fillRound(g, rr, 12, withAlpha(p.accent, 40));
            strokeRound(g, rr, 12, withAlpha(p.accent, 110));
            mini(g, MI_REFRESH, { rr.x + 12, rr.y + 10, 16, 16 }, p.accent, 1.5f);
            text(g, L"重新尝试", font(11, true), { rr.x + 34, rr.y, rr.w - 40, rr.h }, p.text, 0, 1);
        }
        return;
    }

    const WeatherData& w = s.weather;
    std::wstring place = widen(w.loc.country.empty() ? "" : w.loc.country);
    std::wstring admin = widen(w.loc.admin);
    if (!admin.empty() && admin != place) place = admin + (place.empty() ? L"" : L" · " + place);
    std::wstring tag = w.loc.autoLoc ? L"自动定位" : L"已收藏";
    Rect pinR = { h.x + 28, h.y + 27, 14, 14 };
    mini(g, MI_PIN, pinR, p.accent, 1.4f);
    text(g, tag + (place.empty() ? L"" : L" · " + place), font(11.5f, true),
         { pinR.right() + 7, h.y + 22, h.w - 90, 19 }, withAlpha(p.text, 225));

    bool compact = h.h < 280;
    text(g, widen(w.loc.name), font(compact ? 25 : 29, true),
         { h.x + 27, h.y + (compact ? 29.0f : 37.0f), h.w * 0.62f, compact ? 34.0f : 40.0f }, p.text);
    CivilTime lc = civilLocal((long long)time(nullptr), locationOffset(s));
    text(g, weekdayCN(lc.wd) + wfmt(L" · %02d:%02d", lc.h, lc.mi), font(11),
         { h.x + 29, h.y + (compact ? 60 : 74), 280, 18 }, p.sub);

    std::wstring num = std::to_wstring((int)std::lround(s.settings.celsius ? w.now.temp : cToF(w.now.temp)));
    Font* big = font(compact ? 68 : 92, false, 2);
    float numW = tw(g, num, big);
    text(g, num, big, { h.x + 25, h.y + (compact ? 66.0f : 82.0f), numW + 16, compact ? 84.0f : 108.0f }, p.text);
    text(g, s.settings.celsius ? L"°C" : L"°F", font(compact ? 18 : 24, false, 1),
         { h.x + 33 + numW, h.y + (compact ? 82.0f : 101.0f), 54, compact ? 26.0f : 34.0f }, withAlpha(p.text, 220));
    text(g, condName(w.now.cond), font(compact ? 15 : 18, true),
         { h.x + 30, h.y + (compact ? 147 : 185), 230, 27 }, p.text);
    std::wstring hi = w.days.empty() ? L"" : (L"最高 " + tempText(w.days[0].tmax, s.settings.celsius, false) +
                                            L"     最低 " + tempText(w.days[0].tmin, s.settings.celsius, false));
    text(g, hi, font(11), { h.x + 31, h.y + (compact ? 176 : 214), 320, 19 }, p.sub);

    // oversized primary weather icon on the right
    float rightArea = h.w * 0.46f;
    float art = std::min(rightArea * 0.94f, h.h * 0.80f);
    Rect artR = { h.right() - rightArea + (rightArea - art) * 0.5f,
                  h.y + (h.h - art) * 0.5f - 4, art, art };
    drawWeatherIcon(g, w.now.cond, w.now.isDay, artR, true, s.animTime, p.dark ? 1 : 0);

    // chips
    Rect c1 = { h.x + 28, h.bottom() - 52, 108, 32 };
    Rect c2 = { c1.right() + 7, c1.y, 124, 32 };
    Rect c3 = { c2.right() + 7, c1.y, 112, 32 };
    chip(g, p, c1, L"体感 " + tempText(w.now.feels, s.settings.celsius, false), MI_THERMO);
    chip(g, p, c2, L"降水 " + oneDecimal(w.now.precip) + L" mm", MI_DROP);
    chip(g, p, c3, L"云量 " + zeroDecimal(w.now.cloud) + L"%", MI_CLOUD);
    if (h.w > 580) {
        Rect c4 = { c3.right() + 7, c1.y, 108, 32 };
        chip(g, p, c4, L"紫外线 " + oneDecimal(w.now.uv), MI_SUN);
    }

}

static void drawMetric(Graphics& g, const AppState& s, const Pal& p, Rect r, const wchar_t* label,
                       const std::wstring& value, const std::wstring& sub, int icon, Color iconColor,
                       float progress, bool warmBar = false) {
    panel(g, s, p, r, 18, 0);
    text(g, label, font(11.5f), { r.x + 15, r.y + 11, r.w - 58, 18 }, p.sub);
    text(g, value, font(27, true, 1), { r.x + 14, r.y + 33, r.w - 26, 38 }, p.text);
    text(g, sub, font(11), { r.x + 15, r.y + 73, r.w - 26, 18 }, p.sub);
    Rect ic = { r.right() - 50, r.y + 11, 36, 36 };
    fillRound(g, ic, 12, withAlpha(iconColor, p.dark ? 26 : 24));
    mini(g, icon, ic.inset(9), iconColor, 1.7f);
    if (r.h > 116) {
        float bx = r.x + 15, bw = r.w - 30, by = r.bottom() - 18;
        fillRound(g, { bx, by, bw, 5 }, 2.5f, p.dark ? Color(24, 255, 255, 255) : Color(226, 234, 245));
        float fw = std::max(0.0f, std::min(1.0f, progress)) * bw;
        if (fw > 0.5f) {
            if (warmBar) {
                LinearGradientBrush bar(PointF(bx, 0), PointF(bx + fw, 0),
                                        Color(255, 96, 214, 255), Color(255, 255, 175, 91));
                fillRound(g, { bx, by, fw, 5 }, 2.5f, bar);
            } else {
                fillRound(g, { bx, by, fw, 5 }, 2.5f, withAlpha(iconColor, 220));
            }
        }
    }
}

static void drawMetrics(Graphics& g, const AppState& s, const Pal& p, const Layout& L) {
    if (!s.hasWeather) {
        for (int i = 0; i < 4; i++) {
            panel(g, s, p, L.metrics[i], 18, 0);
            fillRound(g, { L.metrics[i].x + 15, L.metrics[i].y + 18, 48, 10 }, 5, withAlpha(p.faint, 45));
            fillRound(g, { L.metrics[i].x + 15, L.metrics[i].y + 42, 76, 22 }, 6, withAlpha(p.faint, 30));
            fillRound(g, { L.metrics[i].x + 15, L.metrics[i].y + 74, 100, 9 }, 4, withAlpha(p.faint, 25));
        }
        return;
    }
    const WeatherNow& n = s.weather.now;
    std::wstring uvSub = n.uv < 3 ? L"低" : (n.uv < 6 ? L"中等" : (n.uv < 8 ? L"高" : L"很高"));
    drawMetric(g, s, p, L.metrics[0], L"相对湿度", zeroDecimal(n.humidity) + L"%",
               L"露点 " + tempText(n.dew, s.settings.celsius, false), MI_DROP, p.accent,
               (float)(n.humidity / 100.0));
    drawMetric(g, s, p, L.metrics[1], L"风速", zeroDecimal(n.wind * 3.6) + L" km/h",
               windDirName(n.windDir) + L"风 · " + oneDecimal(n.wind) + L" m/s", MI_WIND, p.accent2,
               (float)std::min(1.0, n.wind * 3.6 / 40.0));
    drawMetric(g, s, p, L.metrics[2], L"云量", zeroDecimal(n.cloud) + L"%",
               condName(n.cond), MI_CLOUD, p.dark ? Color(255, 180, 205, 230) : Color(255, 90, 118, 150),
               (float)(n.cloud / 100.0));
    drawMetric(g, s, p, L.metrics[3], L"紫外线指数", oneDecimal(n.uv), uvSub + L" · " + L"防护建议",
               MI_SUN, p.warn, (float)std::min(1.0, n.uv / 11.0), true);
}

static void drawHourlyOverview(Graphics& g, const AppState& s, const Pal& p, const Layout& L) {
    Rect r = L.hourly;
    panel(g, s, p, r, 20, 0);
    text(g, L"逐小时", font(15, true), { r.x + 22, r.y + 12, 100, 23 }, p.text);
    text(g, L"未来 12 小时", font(11), { r.x + 98, r.y + 16, 170, 18 }, p.sub);
    Rect more = hourlyMoreRect(L);
    bool mh = more.has(s.mouseX, s.mouseY);
    fillRound(g, more, 13, mh ? (p.dark ? Color(22, 255, 255, 255) : Color(237, 244, 253)) : Color(0, 0, 0, 0));
    text(g, L"48 小时详情", font(10.5f, true), { more.x + 8, more.y, more.w - 28, more.h }, mh ? p.accent : p.sub, 0, 1);
    mini(g, MI_RIGHT, { more.right() - 19, more.cy() - 7, 15, 15 }, mh ? p.accent : p.faint, 1.5f);

    int n = s.hasWeather ? std::min(12, (int)s.weather.hours.size()) : 0;
    float left = r.x + 16, right = r.right() - 16;
    float cw = (right - left) / 12.0f;
    for (int i = 0; i < 12; i++) {
        float cx = left + cw * (i + 0.5f);
        if (i > 0) line(g, left + cw * i, r.y + 54, left + cw * i, r.bottom() - 12, p.dark ? Color(14, 255, 255, 255) : Color(18, 40, 70, 110), 1.0f);
        if (i >= n) {
            fillRound(g, { cx - 20, r.y + 61, 40, 8 }, 4, withAlpha(p.faint, 25));
            continue;
        }
        const HourItem& h = s.weather.hours[i];
        double temp = i == 0 ? s.weather.now.temp : h.temp;
        double precip = i == 0 ? s.weather.now.precip : h.precip;
        Cond cond = i == 0 ? s.weather.now.cond : h.cond;
        bool isDay = i == 0 ? s.weather.now.isDay : h.isDay;
        CivilTime c = civilLocal(h.utc, locationOffset(s));
        std::wstring label = i == 0 ? L"现在" : (wfmt(L"%02d:00", c.h));
        text(g, label, font(11, i == 0), { cx - cw * 0.5f, r.y + 50, cw, 18 }, i == 0 ? p.text : p.sub, 1);
        Rect ir = { cx - 16, r.y + 72, 32, 32 };
        drawWeatherIcon(g, cond, isDay, ir, false, s.animTime, p.dark ? 1 : 0);
        text(g, tempText(temp, s.settings.celsius, false), font(15, true, 1),
             { cx - cw * 0.5f, r.y + 107, cw, 24 }, p.text, 1);
        std::wstring pr = precip > 0.05 ? oneDecimal(precip) + L" mm" : L"—";
        Color pc = precip > 0.05 ? p.accent : p.faint;
        mini(g, MI_DROP, { cx - 26, r.y + 137, 13, 13 }, withAlpha(pc, 200), 1.2f);
        text(g, pr, font(9.5f), { cx - 10, r.y + 135, cw * 0.5f + 12, 17 }, pc);
    }
}

static void drawWeekly(Graphics& g, const AppState& s, const Pal& p, const Layout& L) {
    Rect r = L.weekly;
    panel(g, s, p, r, 20, 0);
    text(g, L"未来 7 天", font(15, true), { r.x + 22, r.y + 11, 130, 23 }, p.text);
    text(g, L"一周天气趋势", font(11), { r.x + 113, r.y + 15, 170, 18 }, p.sub);
    Rect show = L.showTen;
    bool sh = show.has(s.mouseX, s.mouseY);
    fillRound(g, show, 13, sh ? (p.dark ? Color(22, 255, 255, 255) : Color(237, 244, 253)) : Color(0, 0, 0, 0));
    text(g, L"查看 10 天", font(10.5f, true), { show.x + 9, show.y, show.w - 28, show.h }, sh ? p.accent : p.sub, 0, 1);
    mini(g, MI_RIGHT, { show.right() - 19, show.cy() - 7, 15, 15 }, sh ? p.accent : p.faint, 1.5f);

    int n = s.hasWeather ? std::min(7, (int)s.weather.days.size()) : 0;
    for (int i = 0; i < 7; i++) {
        Rect cr = L.weeklyCol[i];
        bool hover = cr.has(s.mouseX, s.mouseY);
        if (hover) fillRound(g, cr.inset(3), 14, p.dark ? Color(14, 255, 255, 255) : Color(245, 249, 254));
        if (i > 0) line(g, cr.x, cr.y + 10, cr.x, cr.bottom() - 12, p.dark ? Color(12, 255, 255, 255) : Color(20, 40, 70, 100), 1.0f);
        if (i >= n) {
            fillRound(g, { cr.cx() - 24, cr.y + 28, 48, 8 }, 4, withAlpha(p.faint, 22));
            continue;
        }
        const DayItem& d = s.weather.days[i];
        std::wstring wd = i == 0 ? L"今天" : (i == 1 ? L"明天" : weekdayCN(civilLocal(d.utcMidnight, locationOffset(s)).wd));
        text(g, wd, font(12, i == 0), { cr.x, cr.y + 7, cr.w, 21 }, i == 0 ? p.text : p.sub, 1);
        text(g, wfmt(L"%d/%d", d.m, d.d), font(10.5f), { cr.x, cr.y + 27, cr.w, 17 }, p.faint, 1);
        drawWeatherIcon(g, d.cond, true, { cr.cx() - 18, cr.y + 44, 36, 36 }, false, s.animTime, p.dark ? 1 : 0);
        std::wstring hi = tempText(d.tmax, s.settings.celsius, false);
        std::wstring lo = tempText(d.tmin, s.settings.celsius, false);
        text(g, hi, font(13.5f, true, 1), { cr.x + 10, cr.y + 81, cr.w * 0.5f - 8, 21 }, p.text, 2);
        text(g, lo, font(12, false, 1), { cr.cx() + 2, cr.y + 82, cr.w * 0.5f - 12, 20 }, p.sub, 0);
        std::wstring pp = d.precip > 0.05 ? oneDecimal(d.precip) + L" mm" : L"—";
        mini(g, MI_DROP, { cr.cx() - 27, cr.y + 108, 13, 13 }, withAlpha(d.precip > 0.05 ? p.accent : p.faint, 190), 1.2f);
        text(g, pp, font(10.5f), { cr.cx() - 11, cr.y + 105, cr.w * 0.5f + 10, 17 }, d.precip > 0.05 ? p.accent : p.faint);
    }
}

static void drawOverview(Graphics& g, AppState& s, const Pal& p, const Layout& L) {
    if (!s.weather.alertText.empty()) {
        Rect b = L.alertBar;
        fillRound(g, b, 10, withAlpha(p.danger, p.dark ? 42 : 28));
        strokeRound(g, b, 10, withAlpha(p.danger, 150));
        text(g, widen(s.weather.alertText), font(11.5f, true),
             { b.x + 12, b.y, b.w - 24, b.h }, p.danger, 1, 1);
    }
    drawHero(g, s, p, L);
    drawMetrics(g, s, p, L);
    drawHourlyOverview(g, s, p, L);
    drawWeekly(g, s, p, L);
}

} // namespace

namespace {

static void drawChartCard(Graphics& g, AppState& s, const Pal& p, const Layout& L) {
    Rect r = L.chart;
    panel(g, s, p, r, 22, 0);
    text(g, L"48 小时温度趋势", font(16, true), { r.x + 24, r.y + 15, 260, 23 }, p.text);
    text(g, L"温度曲线 · 降水柱状", font(10.5f), { r.x + 24, r.y + 41, 260, 17 }, p.faint);
    if (!s.hasWeather || s.weather.hours.empty()) {
        text(g, L"正在加载逐小时数据…", font(11), { r.x + 24, r.cy(), r.w - 48, 30 }, p.sub, 1, 1);
        return;
    }
    int n = std::min(48, (int)s.weather.hours.size());
    double mn = 1e9, mx = -1e9, pmax = 0.5;
    for (int i = 0; i < n; i++) {
        mn = std::min(mn, s.weather.hours[i].temp);
        mx = std::max(mx, s.weather.hours[i].temp);
        pmax = std::max(pmax, s.weather.hours[i].precip);
    }
    mn -= 1.5; mx += 1.5;
    float left = r.x + 28, right = r.right() - 54;
    float top = r.y + 78, bottom = r.bottom() - 56;
    float barBottom = r.bottom() - 26;
    float x0 = left, x1 = right;
    float step = n > 1 ? (x1 - x0) / (n - 1) : 0;
    auto yOf = [&](double t) -> float { return (float)(bottom - (t - mn) / (mx - mn) * (bottom - top)); };

    for (int i = 0; i <= 4; i++) {
        float yy = top + (bottom - top) * i / 4.0f;
        line(g, x0, yy, x1, yy, p.dark ? Color(16, 255, 255, 255) : Color(28, 45, 70, 120), 1.0f);
        double v = mx - (mx - mn) * i / 4.0;
        text(g, zeroDecimal(v) + L"°", font(9.5f, false, 1), { x1 + 8, yy - 8, 40, 17 }, p.faint);
    }
    // midnight labels
    for (int i = 0; i < n; i++) {
        CivilTime c = civilLocal(s.weather.hours[i].utc, locationOffset(s));
        if (c.h == 0 && i > 0) {
            float xx = x0 + step * i;
            line(g, xx, top, xx, barBottom, p.dark ? Color(20, 255, 255, 255) : Color(40, 55, 80, 100), 1.0f);
            text(g, wfmt(L"%d/%d", c.mo, c.d), font(9.5f), { xx + 5, top - 18, 60, 15 }, p.faint);
        }
    }
    // precipitation bars
    for (int i = 0; i < n; i++) {
        float xx = x0 + step * i;
        float hgt = (float)(s.weather.hours[i].precip / pmax) * 24.0f;
        if (hgt > 0.6f) {
            fillRound(g, { xx - 3, barBottom - hgt, 5.5f, hgt }, 2.4f,
                      withAlpha(p.accent, 150));
        }
    }
    // area + curve
    std::vector<PointF> pts;
    pts.reserve(n);
    for (int i = 0; i < n; i++) pts.emplace_back(x0 + step * i, yOf(s.weather.hours[i].temp));
    GraphicsPath area;
    area.AddCurve(pts.data(), n, 0.32f);
    area.AddLine(pts.back().X, bottom, pts.front().X, bottom);
    area.CloseFigure();
    LinearGradientBrush fill(PointF(0, top), PointF(0, bottom),
                             withAlpha(p.accent, p.dark ? 90 : 70), withAlpha(p.accent, 0));
    g.FillPath(&fill, &area);
    GraphicsPath curve;
    curve.AddCurve(pts.data(), n, 0.32f);
    Pen pen(p.accent2, 2.1f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawPath(&pen, &curve);
    for (int i = 0; i < n; i += 3) {
        ellipse(g, pts[i].X - 2.2f, pts[i].Y - 2.2f, 4.4f, 4.4f, p.accent);
    }
    for (int i = 0; i < n; i += 6) {
        CivilTime c = civilLocal(s.weather.hours[i].utc, locationOffset(s));
        text(g, wfmt(L"%02d:00", c.h), font(9.5f), { pts[i].X - 24, barBottom + 4, 48, 15 }, p.faint, 1);
    }

    // hover
    if (r.has(s.mouseX, s.mouseY) && s.mouseX >= x0 - 4 && s.mouseX <= x1 + 4 && s.mouseY >= top - 10 && s.mouseY <= barBottom + 10) {
        int idx = (int)std::lround((s.mouseX - x0) / (step > 0 ? step : 1));
        idx = std::max(0, std::min(n - 1, idx));
        const HourItem& h = s.weather.hours[idx];
        float xx = pts[idx].X, yy = pts[idx].Y;
        line(g, xx, top, xx, barBottom, withAlpha(p.text, 90), 1.0f);
        ellipse(g, xx - 4.5f, yy - 4.5f, 9, 9, p.text);
        ellipse(g, xx - 2.0f, yy - 2.0f, 4, 4, p.accent);
        Rect tip = { std::min(xx + 12, r.right() - 206), top + 6, 194, 100 };
        fillRound(g, tip, 12, p.dark ? Color(220, 8, 20, 34) : Color(252, 255, 255, 255));
        strokeRound(g, tip, 12, p.border);
        CivilTime c = civilLocal(h.utc, locationOffset(s));
        text(g, wfmt(L"%d月%d日 %02d:00", c.mo, c.d, c.h), font(10.5f, true), { tip.x + 12, tip.y + 10, tip.w - 24, 17 }, p.sub);
        text(g, tempText(h.temp, s.settings.celsius, false), font(23, true, 1), { tip.x + 12, tip.y + 29, 72, 34 }, p.text);
        text(g, condName(h.cond), font(11), { tip.x + 88, tip.y + 36, 90, 20 }, p.sub);
        text(g, L"降水 " + oneDecimal(h.precip) + L" mm", font(10.5f), { tip.x + 12, tip.y + 68, 160, 17 }, p.faint);
    }
}

static void drawHourlyDetails(Graphics& g, const AppState& s, const Pal& p, const Layout& L) {
    Rect r = L.details;
    panel(g, s, p, r, 22, 0);
    text(g, L"48 小时天气明细", font(15, true), { r.x + 22, r.y + 13, 240, 23 }, p.text);
    text(g, L"每 1 小时", font(10.5f), { r.x + 168, r.y + 17, 100, 17 }, p.faint);
    if (!s.hasWeather || s.weather.hours.empty()) {
        text(g, L"暂无逐小时数据", font(11), { r.x, r.cy(), r.w, 24 }, p.sub, 1, 1);
        return;
    }
    int n = std::min(48, (int)s.weather.hours.size());
    float top = r.y + 46, left = r.x + 14, colW = (r.w - 28) / 4.0f;
    float rowH = std::min(33.0f, (r.bottom() - top - 14) / 12.0f);
    for (int c = 0; c < 4; c++) {
        if (c > 0) line(g, left + colW * c, top, left + colW * c, top + 12 * rowH, p.dark ? Color(14, 255, 255, 255) : Color(24, 45, 70, 90), 1.0f);
        for (int row = 0; row < 12; row++) {
            int idx = c * 12 + row;
            if (idx >= n) continue;
            Rect cr = { left + colW * c, top + row * rowH, colW, rowH };
            if (row > 0) line(g, cr.x + 8, cr.y, cr.right() - 8, cr.y,
                              p.dark ? Color(10, 255, 255, 255) : Color(18, 45, 70, 80), 1.0f);
            const HourItem& h = s.weather.hours[idx];
            double temp = idx == 0 ? s.weather.now.temp : h.temp;
            double precip = idx == 0 ? s.weather.now.precip : h.precip;
            Cond cond = idx == 0 ? s.weather.now.cond : h.cond;
            bool isDay = idx == 0 ? s.weather.now.isDay : h.isDay;
            CivilTime ct = civilLocal(h.utc, locationOffset(s));
            text(g, wfmt(L"%02d:00", ct.h), font(10.5f, idx == 0), { cr.x + 8, cr.y, 50, cr.h }, p.sub, 0, 1);
            drawWeatherIcon(g, cond, isDay, { cr.x + 57, cr.cy() - 11, 22, 22 }, false, s.animTime, p.dark ? 1 : 0);
            text(g, tempText(temp, s.settings.celsius, false), font(12.5f, true, 1), { cr.x + 84, cr.y, 43, cr.h }, p.text, 0, 1);
            text(g, precip > 0.05 ? oneDecimal(precip) + L"mm" : L"—", font(9.5f), { cr.right() - 68, cr.y, 60, cr.h },
                 precip > 0.05 ? p.accent : p.faint, 2, 1);
        }
    }
}

static void drawHourlyPage(Graphics& g, AppState& s, const Pal& p, const Layout& L) {
    drawChartCard(g, s, p, L);
    drawHourlyDetails(g, s, p, L);
}

static void drawTenDay(Graphics& g, const AppState& s, const Pal& p, const Layout& L) {
    Rect r = L.tenCard;
    panel(g, s, p, r, 22, 0);
    text(g, L"未来 10 天", font(16, true), { r.x + 24, r.y + 13, 180, 25 }, p.text);
    std::wstring phase = s.weather.days.empty() ? std::wstring(L"—") : moonPhaseName(s.weather.days[0].moonPhase);
    std::wstring tenSub = s.hasWeather
        ? (L"共 " + std::to_wstring(s.weather.days.size()) + L" 天 · MET Norway · 月相 " + phase)
        : std::wstring(L"每日温度趋势");
    text(g, tenSub, font(10.5f), { r.right() - 390, r.y + 17, 366, 17 }, p.faint, 2);
    if (!s.hasWeather || s.weather.days.empty()) {
        text(g, L"正在加载未来天气…", font(11), { r.x, r.cy(), r.w, 24 }, p.sub, 1, 1);
        return;
    }
    int n = std::min(10, (int)s.weather.days.size());
    double gmin = 1e9, gmax = -1e9;
    for (int i = 0; i < n; i++) { gmin = std::min(gmin, s.weather.days[i].tmin); gmax = std::max(gmax, s.weather.days[i].tmax); }
    if (gmax - gmin < 6) { gmin -= 2; gmax += 2; }

    float y0 = r.y + 64;
    float rowH = (r.bottom() - y0 - 10) / 10.0f;
    float x = r.x + 22;
    float dateW = 96, condW = 170, tempW = 250, precW = 96, windW = 108, sunW = 146;
    float total = dateW + condW + tempW + precW + windW + sunW + 40;
    float scale = (r.w - 44) / total;
    dateW *= scale; condW *= scale; tempW *= scale; precW *= scale; windW *= scale; sunW *= scale;
    // header
    text(g, L"日期", font(10.5f, true), { x, y0 - 21, dateW, 17 }, p.sub);
    text(g, L"天气", font(10.5f, true), { x + dateW, y0 - 21, condW, 17 }, p.sub);
    text(g, L"温度范围", font(10.5f, true), { x + dateW + condW, y0 - 21, tempW, 17 }, p.sub);
    text(g, L"降水", font(10.5f, true), { x + dateW + condW + tempW, y0 - 21, precW, 17 }, p.sub);
    text(g, L"风速", font(10.5f, true), { x + dateW + condW + tempW + precW, y0 - 21, windW, 17 }, p.sub);
    text(g, L"日出 / 日落", font(10.5f, true), { x + dateW + condW + tempW + precW + windW, y0 - 21, sunW, 17 }, p.sub);
    line(g, r.x + 20, y0 - 2, r.right() - 20, y0 - 2, p.dark ? Color(24, 255, 255, 255) : Color(228, 235, 245), 1.0f);

    for (int i = 0; i < n; i++) {
        const DayItem& d = s.weather.days[i];
        Rect row = { r.x + 12, y0 + i * rowH, r.w - 24, rowH };
        bool hover = row.has(s.mouseX, s.mouseY);
        if (hover) fillRound(g, row, 10, p.dark ? Color(12, 255, 255, 255) : Color(245, 249, 254));
        CivilTime ct = civilLocal(d.utcMidnight, locationOffset(s));
        std::wstring wd = i == 0 ? L"今天" : (i == 1 ? L"明天" : weekdayCN(ct.wd));
        text(g, wd, font(11.5f, i == 0), { x, row.y, 58, row.h }, p.text, 0, 1);
        text(g, wfmt(L"%d/%d", d.m, d.d), font(10.5f), { x + 60, row.y, 52, row.h }, p.faint, 0, 1);
        drawWeatherIcon(g, d.cond, true, { x + dateW + 4, row.cy() - 16, 32, 32 }, false, s.animTime, p.dark ? 1 : 0);
        text(g, condName(d.cond), font(11.5f), { x + dateW + 46, row.y, condW - 52, row.h }, p.sub, 0, 1);

        float tx = x + dateW + condW;
        text(g, tempText(d.tmin, s.settings.celsius, false), font(10.5f, false, 1), { tx, row.y, 40, row.h }, p.faint, 0, 1);
        text(g, tempText(d.tmax, s.settings.celsius, false), font(11.5f, true, 1), { tx + tempW - 42, row.y, 40, row.h }, p.text, 2, 1);
        float bx = tx + 42, bw = tempW - 88;
        fillRound(g, { bx, row.cy() - 2.5f, bw, 5 }, 2.5f, p.dark ? Color(26, 255, 255, 255) : Color(228, 236, 247));
        float a = (float)((d.tmin - gmin) / (gmax - gmin));
        float b = (float)((d.tmax - gmin) / (gmax - gmin));
        a = std::max(0.0f, std::min(1.0f, a));
        b = std::max(a + 0.03f, std::min(1.0f, b));
        LinearGradientBrush tb(PointF(bx + bw * a, 0), PointF(bx + bw * b, 0),
                               Color(255, 103, 190, 255), Color(255, 255, 174, 93));
        fillRound(g, { bx + bw * a, row.cy() - 2.5f, bw * (b - a), 5 }, 2.5f, tb);
        ellipse(g, bx + bw * b - 3.5f, row.cy() - 3.5f, 7, 7, p.text);

        text(g, d.precip > 0.05 ? oneDecimal(d.precip) + L" mm" : L"—", font(11),
             { tx + tempW, row.y, precW, row.h }, d.precip > 0.05 ? p.accent : p.faint, 0, 1);
        text(g, zeroDecimal(d.windMax * 3.6) + L" km/h", font(11),
             { tx + tempW + precW, row.y, windW, row.h }, p.sub, 0, 1);
        text(g, widen(d.sunrise) + L" / " + widen(d.sunset), font(10.5f),
             { tx + tempW + precW + windW, row.y, sunW, row.h }, p.sub, 0, 1);
        line(g, r.x + 20, row.bottom() - 0.5f, r.right() - 20, row.bottom() - 0.5f,
             p.dark ? Color(10, 255, 255, 255) : Color(18, 45, 70, 70), 1.0f);
    }
}

// ---------------- satellite map ----------------
static std::map<std::string, Bitmap*> g_tiles;
static std::set<std::string> g_tilePending;
static std::map<std::string, DWORD> g_tileFailed;

static void drawToggle(Graphics& g, const Pal& p, Rect r, bool on) {
    fillRound(g, r, r.h * 0.5f, on ? withAlpha(p.accent, 220)
                                   : (p.dark ? Color(38, 255, 255, 255) : Color(210, 221, 234)));
    float knob = r.h - 6;
    float kx = on ? r.right() - knob - 3 : r.x + 3;
    ellipse(g, kx, r.y + 3, knob, knob, Color(255, 255, 255, 255));
    if (on) ellipse(g, kx + knob * 0.30f, r.y + 3 + knob * 0.30f, knob * 0.4f, knob * 0.4f, withAlpha(p.accent, 90));
}

static std::vector<int> versionParts(const std::string& tag) {
    std::vector<int> v;
    std::string cur;
    for (char ch : tag) {
        if (ch >= '0' && ch <= '9') cur += ch;
        else if (!cur.empty()) { v.push_back(atoi(cur.c_str())); cur.clear(); }
    }
    if (!cur.empty()) v.push_back(atoi(cur.c_str()));
    while (v.size() < 3) v.push_back(0);
    return v;
}

static void checkForUpdates() {
    static std::atomic<bool> checking(false);
    if (checking.exchange(true)) return;
    std::thread([]() {
        const char* urls[] = {
            "https://api.github.com/repos/Vertex-Wu/AuraWeather/releases/latest",
            "https://ghproxy.net/https://api.github.com/repos/Vertex-Wu/AuraWeather/releases/latest",
            "https://ghfast.top/https://api.github.com/repos/Vertex-Wu/AuraWeather/releases/latest"
        };
        std::string tag, page;
        for (const char* url : urls) {
            std::string err;
            if (fetchLatestRelease(url, tag, page, err)) break;
        }
        checking = false;
        if (tag.empty()) {
            MessageBoxW(nullptr, L"检查更新失败，请稍后重试。", L"AURA 天气", MB_ICONWARNING | MB_OK);
            return;
        }
        std::vector<int> remote = versionParts(tag);
        std::vector<int> local = versionParts("2.0.2");
        bool newer = remote > local;
        if (!newer) {
            std::wstring msg = L"当前已是最新版本 v2.0.2。";
            MessageBoxW(nullptr, msg.c_str(), L"AURA 天气", MB_ICONINFORMATION | MB_OK);
            return;
        }
        std::wstring wtag = widen(tag);
        std::wstring msg = L"发现新版本 " + wtag + L"！\n即将打开下载页面。";
        MessageBoxW(nullptr, msg.c_str(), L"AURA 天气", MB_ICONINFORMATION | MB_OK);
        std::wstring url = page.empty() ? L"https://github.com/Vertex-Wu/AuraWeather/releases/latest" : widen(page);
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }).detach();
}

static void drawSettings(Graphics& g, AppState& s, const Pal& p, const Layout& L) {
    fillRound(g, { 0, 0, (float)s.W, (float)s.H }, 0, p.scrim);
    Rect d = L.dlg;
    panel(g, s, p, d, 22, 118);
    text(g, L"设置", font(21, true), { d.x + 28, d.y + 17, 200, 31 }, p.text);
    text(g, L"按你的方式看天气", font(10.5f), { d.x + 28, d.y + 48, 240, 17 }, p.faint);
    bool ch = L.closeDlg.has(s.mouseX, s.mouseY);
    if (ch) fillRound(g, L.closeDlg, 9, p.dark ? Color(28, 255, 255, 255) : Color(238, 244, 252));
    mini(g, MI_CLOSE, L.closeDlg.inset(9), ch ? p.text : p.sub, 1.6f);

    text(g, L"外观主题", font(10.5f, true), { d.x + 28, d.y + 72, 160, 17 }, p.faint);
    auto themeOpt = [&](Rect r, bool darkMode, const wchar_t* label, int icon) {
        bool active = s.settings.dark == darkMode;
        bool hover = r.has(s.mouseX, s.mouseY);
        fillRound(g, r, 12, active ? withAlpha(p.accent, p.dark ? 35 : 28)
                                    : (hover ? (p.dark ? Color(20, 255, 255, 255) : Color(244, 248, 254)) : Color(0, 0, 0, 0)));
        strokeRound(g, r, 12, active ? withAlpha(p.accent, 140) : (p.dark ? Color(26, 255, 255, 255) : Color(224, 232, 243)));
        mini(g, icon, { r.x + 14, r.cy() - 8, 16, 16 }, active ? p.accent : p.sub, 1.5f);
        text(g, label, font(11.5f), { r.x + 40, r.y, r.w - 50, r.h }, active ? p.text : p.sub, 0, 1);
        if (active) {
            ellipse(g, r.right() - 20, r.cy() - 5, 10, 10, withAlpha(p.accent, 40));
            mini(g, MI_CHECK, { r.right() - 18, r.cy() - 3, 6, 6 }, p.accent, 1.2f);
        }
    };
    themeOpt(L.themeDark, true, L"暗色模式", MI_MOON);
    themeOpt(L.themeLight, false, L"亮色模式", MI_SUN);

    text(g, L"温度单位", font(10.5f, true), { d.x + 28, d.y + 146, 120, 17 }, p.faint);
    auto unitOpt = [&](Rect r, bool c, const wchar_t* label) {
        bool active = s.settings.celsius == c;
        bool hover = r.has(s.mouseX, s.mouseY);
        fillRound(g, r, 10, active ? withAlpha(p.accent, p.dark ? 42 : 30)
                                   : (hover ? (p.dark ? Color(20, 255, 255, 255) : Color(244, 248, 254)) : Color(0, 0, 0, 0)));
        strokeRound(g, r, 10, active ? withAlpha(p.accent, 150) : (p.dark ? Color(26, 255, 255, 255) : Color(224, 232, 243)));
        text(g, label, font(11.5f), r, active ? p.text : p.sub, 1, 1);
    };
    unitOpt(L.unitC, true, L"摄氏度 °C");
    unitOpt(L.unitF, false, L"华氏度 °F");

    text(g, L"通用", font(10.5f, true), { d.x + 28, d.y + 220, 120, 17 }, p.faint);
    const wchar_t* labels[] = { L"开机启动", L"关闭时最小化到托盘", L"桌面天气提醒", L"每 10 分钟自动刷新" };
    const wchar_t* subs[] = {
        L"登录 Windows 后自动在系统托盘运行",
        L"关闭主窗口后仍可在后台持续更新天气",
        L"降水或天气明显变化时发送气泡提醒",
        L"在后台静默检查最新天气，保持数据新鲜"
    };
    bool* vals[] = { &s.settings.startup, &s.settings.trayClose, &s.settings.notify, &s.settings.autoRefresh };
    for (int i = 0; i < 4; i++) {
        Rect row = L.toggles[i];
        bool hover = row.has(s.mouseX, s.mouseY);
        if (hover) fillRound(g, { row.x - 8, row.y, row.w + 16, row.h }, 10, p.dark ? Color(12, 255, 255, 255) : Color(246, 249, 254));
        text(g, labels[i], font(11.5f), { row.x, row.y + 1, row.w - 70, 19 }, p.text);
        text(g, subs[i], font(9.5f), { row.x, row.y + 21, row.w - 70, 16 }, p.faint);
        drawToggle(g, p, { row.right() - 52, row.y + 8, 44, 24 }, *vals[i]);
    }

    line(g, d.x + 28, d.y + 494, d.right() - 28, d.y + 494, p.dark ? Color(22, 255, 255, 255) : Color(230, 236, 245), 1.0f);
    text(g, L"数据来源", font(9.5f, true), { d.x + 28, d.y + 504, 100, 15 }, p.faint);
    text(g, L"MET Norway 逐小时预报 · Open-Meteo 城市搜索 · Open-Meteo 空气质量", font(9.5f),
         { d.x + 28, d.y + 521, d.w - 56, 15 }, p.sub);
    text(g, L"所有数据来自公开服务，应用仅在本机缓存天气数据，不上传个人数据。", font(9.5f),
         { d.x + 28, d.y + 538, d.w - 56, 15 }, p.faint);

    Rect cb = L.clearBtn;
    bool cbh = cb.has(s.mouseX, s.mouseY);
    fillRound(g, cb, 10, cbh ? (p.dark ? Color(28, 255, 255, 255) : Color(238, 244, 253)) : Color(0, 0, 0, 0));
    strokeRound(g, cb, 10, p.dark ? Color(35, 255, 255, 255) : Color(218, 228, 240));
    mini(g, MI_TRASH, { cb.x + 11, cb.y + 9, 16, 16 }, cbh ? p.text : p.sub, 1.4f);
    text(g, L"清除天气缓存", font(10.5f, true), { cb.x + 34, cb.y, cb.w - 40, cb.h }, cbh ? p.text : p.sub, 0, 1);
    Rect ub = L.updateBtn;
    bool ubh = ub.has(s.mouseX, s.mouseY);
    fillRound(g, ub, 10, ubh ? (p.dark ? Color(28, 255, 255, 255) : Color(238, 244, 253)) : Color(0, 0, 0, 0));
    strokeRound(g, ub, 10, p.dark ? Color(35, 255, 255, 255) : Color(218, 228, 240));
    mini(g, MI_REFRESH, { ub.x + 11, ub.y + 9, 16, 16 }, ubh ? p.text : p.sub, 1.4f);
    text(g, L"检查更新", font(10.5f, true), { ub.x + 34, ub.y, ub.w - 40, ub.h }, ubh ? p.text : p.sub, 0, 1);
    text(g, L"AURA Weather 2.0.2", font(9.5f), { d.right() - 174, cb.y + 7, 150, 17 }, p.faint, 2, 1);
}

static void drawSearch(Graphics& g, AppState& s, const Pal& p, const Layout& L) {
    if (!s.searchOpen) return;
    Rect card = L.searchCard;
    if (card.w <= 0) return;
    panel(g, s, p, card, 16, 8);
    text(g, s.searchLoading ? L"正在搜索城市…" : (s.searchNoResult ? L"未找到匹配的城市" : L"选择城市"),
         font(10.5f, true), { card.x + 16, card.y + 7, card.w - 32, 19 }, p.faint);
    if (s.searchLoading) {
        spinner(g, card.right() - 24, card.y + 20, 8, s.animTime, p.accent);
        fillRound(g, { card.x + 16, card.y + 34, card.w - 32, 1 }, 0, p.border);
        return;
    }
    if (s.searchNoResult) {
        text(g, L"试试城市中文名，或输入「纬度, 经度」", font(10.5f), { card.x + 16, card.y + 32, card.w - 32, 17 }, p.sub);
        return;
    }
    for (int i = 0; i < L.searchRowCount && i < (int)s.searchResults.size(); i++) {
        Rect row = L.searchRow[i];
        bool hover = row.has(s.mouseX, s.mouseY);
        bool sel = s.searchSelected == i;
        if (hover || sel) fillRound(g, row, 10, p.dark ? Color(22, 255, 255, 255) : Color(240, 246, 254));
        mini(g, MI_PIN, { row.x + 10, row.cy() - 9, 18, 18 }, sel ? p.accent : p.sub, 1.5f);
        text(g, widen(s.searchResults[i].name), font(13, true), { row.x + 38, row.y + 6, row.w - 100, 21 }, p.text);
        std::wstring sub = widen(s.searchResults[i].admin);
        if (!sub.empty() && !s.searchResults[i].country.empty()) sub += L" · ";
        sub += widen(s.searchResults[i].country);
        text(g, sub, font(10), { row.x + 38, row.y + 28, row.w - 100, 16 }, p.faint);
        text(g, L"添加", font(10, true), { row.right() - 58, row.y, 44, row.h }, sel ? p.accent : p.faint, 2, 1);
    }
}

static void drawToast(Graphics& g, const AppState& s, const Pal& p) {
    long long now = (long long)GetTickCount64();
    if (s.toast.empty() || now >= s.toastUntil) return;
    long long left = s.toastUntil - now;
    int alpha = left < 350 ? (int)(220 * left / 350) : 220;
    float w = std::min((float)s.W - 260, std::max(280.0f, tw(g, s.toast, font(11.5f, true)) + 92));
    Rect r = { ((float)s.W - w) * 0.5f, (float)s.H - 86, w, 44 };
    fillRound(g, r, 22, Color((BYTE)alpha, p.dark ? 22 : 32, p.dark ? 35 : 45, p.dark ? 52 : 62));
    strokeRound(g, r, 22, Color((BYTE)alpha, 255, 255, 255));
    mini(g, MI_CHECK, { r.x + 16, r.cy() - 8, 16, 16 }, Color((BYTE)alpha, 255, 255, 255), 1.6f);
    text(g, s.toast, font(11.5f, true), { r.x + 42, r.y, r.w - 56, r.h }, Color((BYTE)alpha, 255, 255, 255), 0, 1);
}

static void drawSunrisePage(Graphics& g, AppState& s, const Pal& p, const Layout& L) {
    Rect r = L.content;
    panel(g, s, p, r, 22, 0);
    text(g, L"今日太阳轨迹", font(16, true), { r.x + 24, r.y + 14, 220, 24 }, p.text);
    text(g, L"未来 10 天日出与日落", font(10.5f), { r.right() - 220, r.y + 18, 196, 18 }, p.faint, 2);
    if (!s.hasWeather || s.weather.days.empty()) {
        text(g, L"正在获取日出日落数据…", font(12), { r.x, r.cy(), r.w, 24 }, p.sub, 1, 1);
        return;
    }
    Rect arcR = { r.cx() - 330, r.y + 58, 660, 190 };
    drawSunArc(g, s, p, arcR);
    float headY = r.y + 300;
    float dateW = r.w * 0.42f, riseW = r.w * 0.29f;
    text(g, L"日期", font(10.5f, true), { r.x + 40, headY, dateW, 18 }, p.sub);
    text(g, L"日出", font(10.5f, true), { r.x + 40 + dateW, headY, riseW, 18 }, p.sub);
    text(g, L"日落", font(10.5f, true), { r.x + 40 + dateW + riseW, headY, riseW, 18 }, p.sub);
    line(g, r.x + 32, headY + 22, r.right() - 32, headY + 22, p.border, 1.0f);
    int n = std::min(10, (int)s.weather.days.size());
    float rowH = (r.bottom() - headY - 34) / 10.0f;
    for (int i = 0; i < n; i++) {
        const DayItem& d = s.weather.days[i];
        float y = headY + 30 + i * rowH;
        CivilTime ct = civilLocal(d.utcMidnight, locationOffset(s));
        std::wstring label = i == 0 ? L"今天" : (i == 1 ? L"明天" : weekdayCN(ct.wd));
        text(g, label, font(11.5f, i == 0), { r.x + 40, y, 90, rowH }, p.text, 0, 1);
        std::wstring date = wfmt(L"%02d/%02d", d.m, d.d);
        text(g, date, font(10.5f), { r.x + 132, y, 80, rowH }, p.faint, 0, 1);
        text(g, widen(d.sunrise.empty() ? "—" : d.sunrise), font(12, true),
             { r.x + 40 + dateW, y, riseW, rowH }, p.text, 0, 1);
        text(g, widen(d.sunset.empty() ? "—" : d.sunset), font(12, true),
             { r.x + 40 + dateW + riseW, y, riseW, rowH }, p.text, 0, 1);
        if (i < n - 1) line(g, r.x + 32, y + rowH, r.right() - 32, y + rowH,
                            p.dark ? Color(18, 255, 255, 255) : Color(234, 240, 248), 1.0f);
    }
}

static void drawBigMoon(Graphics& g, Rect r, int idx, const Pal& p) {
    float R = std::min(r.w, r.h) * 0.46f;
    float cx = r.cx(), cy = r.cy();
    LinearGradientBrush base(PointF(cx - R, cy - R), PointF(cx + R, cy + R),
                             Color(255, 255, 252, 231), Color(255, 205, 218, 239));
    g.FillEllipse(&base, cx - R, cy - R, R * 2, R * 2);
    Color crater(90, 145, 164, 195);
    ellipse(g, cx - R * 0.52f, cy - R * 0.42f, R * 0.20f, R * 0.15f, crater);
    ellipse(g, cx + R * 0.16f, cy + R * 0.10f, R * 0.26f, R * 0.20f, crater);
    ellipse(g, cx - R * 0.18f, cy + R * 0.44f, R * 0.15f, R * 0.12f, crater);
    ellipse(g, cx + R * 0.42f, cy - R * 0.34f, R * 0.10f, R * 0.10f, crater);
    static const float dx[8] = { 0.0f, -0.55f, -1.0f, -1.55f, 0.0f, 1.55f, 1.0f, 0.55f };
    if (idx == 0) {
        SolidBrush dark(Color(255, 18, 28, 42));
        g.FillEllipse(&dark, cx - R, cy - R, R * 2, R * 2);
    } else if (idx != 4) {
        GraphicsPath clip;
        clip.AddEllipse(cx - R, cy - R, R * 2, R * 2);
        g.SetClip(&clip, CombineModeReplace);
        SolidBrush shadow(Color(245, 17, 27, 41));
        float off = dx[idx] * R;
        g.FillEllipse(&shadow, cx - R + off, cy - R, R * 2, R * 2);
        g.ResetClip();
    }
    Pen pen(withAlpha(p.faint, 120), 1.2f);
    g.DrawEllipse(&pen, cx - R, cy - R, R * 2, R * 2);
}

static void drawMoonPage(Graphics& g, AppState& s, const Pal& p, const Layout& L) {
    Rect r = L.content;
    panel(g, s, p, r, 22, 0);
    text(g, L"当前月相", font(16, true), { r.x + 24, r.y + 14, 220, 24 }, p.text);
    if (!s.hasWeather || s.weather.days.empty()) {
        text(g, L"正在获取月相数据…", font(12), { r.x, r.cy(), r.w, 24 }, p.sub, 1, 1);
        return;
    }
    int idx = s.weather.moonPhaseIndex;
    float size = std::min(r.h * 0.54f, 330.0f);
    float groupH = size + 112;
    float top = r.y + (r.h - groupH) * 0.5f;
    Rect moon = { r.cx() - size * 0.5f, top, size, size };
    drawBigMoon(g, moon, idx, p);
    static const wchar_t* names[8] = { L"新月", L"娥眉月", L"上弦月", L"盈凸月",
                                       L"满月", L"亏凸月", L"下弦月", L"残月" };
    text(g, std::wstring(L"当前月相：") + names[idx & 7], font(22, true),
         { r.cx() - 250, moon.bottom() + 22, 500, 34 }, p.text, 1);
    text(g, L"月相数据由 MET Norway 提供 · 北半球视角", font(10.5f),
         { r.cx() - 250, moon.bottom() + 64, 500, 20 }, p.faint, 1);
}

static void drawAdvice(Graphics& g, AppState& s, const Pal& p, const Layout& L) {
    if (!s.hasWeather || s.weather.advices.empty()) return;
    std::string joined;
    for (size_t i = 0; i < s.weather.advices.size(); i++) {
        if (i) joined += " | ";
        joined += s.weather.advices[i];
    }
    fillRound(g, { L.advice.x - 14, L.advice.y - 4, L.advice.w + 28, 27 }, 13,
              p.dark ? Color(255, 18, 29, 43) : Color(255, 255, 255, 255));
    strokeRound(g, { L.advice.x - 14, L.advice.y - 4, L.advice.w + 28, 27 }, 13, p.border);
    text(g, widen(joined), font(11.0f), { L.advice.x, L.advice.y, L.advice.w, L.advice.h }, p.text, 0, 1);
}
static void drawAll(Graphics& g, AppState& s) {
    Pal p = palette(s);
    ensureStaticBackground(s, p, g_backW, g_backH);
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.DrawImage(g_bgCache, RectF(0, 0, (REAL)s.W, (REAL)s.H));
    g.SetCompositingMode(CompositingModeSourceOver);
    drawSidebar(g, s, p, layout(s));
    Layout L = layout(s);
    drawTopBar(g, s, p, L);
    drawTabs(g, s, p, L);
    if (s.page == 0) drawOverview(g, s, p, L);
    else if (s.page == 1) drawHourlyPage(g, s, p, L);
    else if (s.page == 2) drawTenDay(g, s, p, L);
    else if (s.page == 3) drawSunrisePage(g, s, p, L);
    else drawMoonPage(g, s, p, L);
    drawAdvice(g, s, p, L);
    if (s.searchOpen) drawSearch(g, s, p, L);
    drawToast(g, s, p);
    if (s.settingsOpen) drawSettings(g, s, p, L);
}

static int getEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    std::vector<BYTE> mem(size);
    ImageCodecInfo* codecs = (ImageCodecInfo*)mem.data();
    GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(codecs[i].MimeType, format) == 0) { *pClsid = codecs[i].Clsid; return (int)i; }
    }
    return -1;
}


// A compact hash of what the pointer is currently hovering. Moving inside the
// same card must not trigger a whole-window repaint; only crossing into another
// interactive target does.
static int hoverSignature(const AppState& s) {
    float x = s.mouseX, y = s.mouseY;
    if (x < 0 || y < 0) return -1;
    Layout L = layout(s);
    if (s.settingsOpen) {
        if (!L.dlg.has(x, y)) return 1;
        if (L.closeDlg.has(x, y)) return 2;
        if (L.themeDark.has(x, y)) return 3;
        if (L.themeLight.has(x, y)) return 4;
        if (L.unitC.has(x, y)) return 5;
        if (L.unitF.has(x, y)) return 6;
        for (int i = 0; i < 5; i++) if (L.toggles[i].has(x, y)) return 10 + i;
        if (L.clearBtn.has(x, y)) return 20;
        if (L.updateBtn.has(x, y)) return 21;
        return 0;
    }
    if (s.searchOpen && !L.searchCard.empty() && L.searchCard.has(x, y)) {
        for (int i = 0; i < L.searchRowCount; i++) if (L.searchRow[i].has(x, y)) return 30 + i;
        return 29;
    }
    if (L.winMin.has(x, y)) return 40;
    if (L.winMax.has(x, y)) return 41;
    if (L.winClose.has(x, y)) return 42;
    if (L.themeBtn.has(x, y)) return 43;
    if (L.settingsBtn.has(x, y)) return 44;
    if (L.search.has(x, y)) return 45;
    for (int i = 0; i < 5; i++) if (L.tabs[i].has(x, y)) return 50 + i;
    if (L.autoItem.has(x, y)) return 60;
    int start = std::max(0, std::min((int)s.settings.saved.size() - L.cityCount, s.sidebarScroll));
    for (int i = 0; i < L.cityCount; i++) {
        if (L.city[i].has(x, y)) {
            bool active = s.settings.hasCurrent && !s.settings.current.autoLoc &&
                          start + i < (int)s.settings.saved.size() &&
                          s.settings.saved[start + i].key() == s.settings.current.key();
            return (active && x > L.city[i].right() - 28) ? 80 + i : 70 + i;
        }
    }
    if (L.addCity.has(x, y)) return 90;
    if (L.bottomTheme.has(x, y)) return 91;
    if (L.bottomSettings.has(x, y)) return 92;
    if (s.page == 0) {
        if (!s.hasWeather && heroRetryRect(L).has(x, y)) return 100;
        if (L.hourly.has(x, y)) return 101;
        if (L.weekly.has(x, y)) return 102;
        for (int i = 0; i < 7; i++) if (L.weeklyCol[i].has(x, y)) return 110 + i;
    } else if (s.page == 1) {
        if (L.chart.has(x, y)) {
            int n = std::min(48, (int)s.weather.hours.size());
            if (n > 1 && y > L.chart.y + 58 && y < L.chart.bottom() - 18) {
                float left = L.chart.x + 28, right = L.chart.right() - 54;
                float step = (right - left) / (n - 1);
                int idx = (int)std::lround((x - left) / std::max(1.0f, step));
                if (idx >= 0 && idx < n) return 200 + idx;
            }
            return 199;
        }
        if (L.details.has(x, y)) return 150;
    } else if (s.page == 2) {
        for (int i = 0; i < 10; i++) if (L.tenRow[i].has(x, y)) return 300 + i;
    }
    return 0;
}

} // namespace

// ================= public API =================
namespace UI {

void init(HWND hwnd) {
    g_hwnd = hwnd;
    GdiplusStartupInput gi;
    ULONG_PTR token = 0;
    GdiplusStartup(&token, &gi, nullptr);
}

void shutdown() {
    for (auto& kv : g_fonts) delete kv.second;
    g_fonts.clear();
    for (auto& kv : g_tiles) delete kv.second;
    g_tiles.clear();
    for (auto& kv : g_iconCache) delete kv.second;
    g_iconCache.clear();
    for (auto& kv : g_panelCache) delete kv.second;
    g_panelCache.clear();
    delete g_back; g_back = nullptr;
    clearBackgroundCache();
}

void render(HDC hdc, AppState& s) {
    if (!g_hwnd || s.W <= 0 || s.H <= 0) return;
    float dpi = s.dpi > 0 ? s.dpi : 1.0f;
    int pw = (int)std::lround(s.W * dpi), ph = (int)std::lround(s.H * dpi);
    if (pw <= 0 || ph <= 0) return;
    ensureBack(pw, ph);
    {
        Graphics g(g_back);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.SetCompositingQuality(CompositingQualityHighQuality);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.ScaleTransform(dpi, dpi);
        float mx = s.mouseX, my = s.mouseY;
        s.mouseX = -1;
        s.mouseY = -1;
        drawAll(g, s);
        s.mouseX = mx;
        s.mouseY = my;
        g.Flush();
    }
    Graphics screen(hdc);
    screen.SetCompositingMode(CompositingModeSourceCopy);
    screen.DrawImage(g_back, 0, 0);
}

bool saveScreenshot(AppState& s, const std::wstring& path) {
    float dpi = s.dpi > 0 ? s.dpi : 1.0f;
    int pw = (int)std::lround(s.W * dpi), ph = (int)std::lround(s.H * dpi);
    ensureBack(pw, ph);
    {
        Graphics g(g_back);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.SetCompositingQuality(CompositingQualityHighQuality);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.ScaleTransform(dpi, dpi);
        float mx = s.mouseX, my = s.mouseY;
        s.mouseX = -1;
        s.mouseY = -1;
        drawAll(g, s);
        s.mouseX = mx;
        s.mouseY = my;
        g.Flush();
    }
    CLSID png;
    if (getEncoderClsid(L"image/png", &png) < 0) return false;
    return g_back->Save(path.c_str(), &png, nullptr) == Ok;
}

void layoutSearchEdit(AppState& s) {
    if (!s.hSearch) return;
    Layout L = layout(s);
    float rx = L.search.x + 38, ry = L.search.y + 6;
    float rw = L.search.w - 52, rh = L.search.h - 12;
    int x = (int)std::lround(rx * s.dpi), y = (int)std::lround(ry * s.dpi);
    int w = (int)std::lround(rw * s.dpi), h = (int)std::lround(rh * s.dpi);
    SetWindowPos(s.hSearch, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void clearTiles() {
    for (auto& kv : g_tiles) delete kv.second;
    g_tiles.clear();
    g_tilePending.clear();
    g_tileFailed.clear();
}

void onTile(const TileMsg& msg) {
    g_tilePending.erase(msg.key);
    if (msg.status == 200 && !msg.bytes.empty()) {
        IStream* st = SHCreateMemStream((const BYTE*)msg.bytes.data(), (UINT)msg.bytes.size());
        if (st) {
            {
                Bitmap tmp(st, FALSE);
                if (tmp.GetLastStatus() == Ok && tmp.GetWidth() > 0) {
                    Bitmap* bmp = tmp.Clone(0, 0, tmp.GetWidth(), tmp.GetHeight(), PixelFormat32bppPARGB);
                    if (bmp && bmp->GetLastStatus() == Ok) {
                        auto old = g_tiles.find(msg.key);
                        if (old != g_tiles.end()) { delete old->second; g_tiles.erase(old); }
                        g_tiles[msg.key] = bmp;
                    } else delete bmp;
                }
            }
            st->Release();
        }
    } else {
        g_tileFailed[msg.key] = (DWORD)GetTickCount64();
    }
    while (g_tiles.size() > 120) {
        auto it = g_tiles.begin();
        delete it->second;
        g_tiles.erase(it);
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

bool mouseMove(AppState& s, float x, float y) {
    s.mouseX = x;
    s.mouseY = y;
    if (s.draggingWindow) {
        if (IsZoomed(s.hwnd)) return true;
        POINT p;
        GetCursorPos(&p);
        int dx = p.x - s.dragCursor.x, dy = p.y - s.dragCursor.y;
        SetWindowPos(s.hwnd, nullptr, s.dragRect.left + dx, s.dragRect.top + dy, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return true;
    }
    return true;
}

bool mouseDown(AppState& s, float x, float y) {
    s.mouseX = x; s.mouseY = y;
    s.mouseDown = true;
    Layout L = layout(s);

    if (s.settingsOpen) {
        if (L.closeDlg.has(x, y)) { s.settingsOpen = false; InvalidateRect(s.hwnd, nullptr, FALSE); return true; }
        if (!L.dlg.has(x, y)) { s.settingsOpen = false; InvalidateRect(s.hwnd, nullptr, FALSE); return true; }
        if (L.themeDark.has(x, y)) { if (!s.settings.dark) appToggleTheme(s); return true; }
        if (L.themeLight.has(x, y)) { if (s.settings.dark) appToggleTheme(s); return true; }
        if (L.unitC.has(x, y)) { s.settings.celsius = true; appSaveSettings(s); InvalidateRect(s.hwnd, nullptr, FALSE); return true; }
        if (L.unitF.has(x, y)) { s.settings.celsius = false; appSaveSettings(s); InvalidateRect(s.hwnd, nullptr, FALSE); return true; }
        for (int i = 0; i < 4; i++) {
            if (L.toggles[i].has(x, y)) {
                if (i == 0) { s.settings.startup = !s.settings.startup; setStartupEnabled(s.settings.startup); }
                else if (i == 1) s.settings.trayClose = !s.settings.trayClose;
                else if (i == 2) s.settings.notify = !s.settings.notify;
                else if (i == 3) s.settings.autoRefresh = !s.settings.autoRefresh;

                appSaveSettings(s);
                InvalidateRect(s.hwnd, nullptr, FALSE);
                return true;
            }
        }
        if (L.clearBtn.has(x, y)) { appClearCache(s); return true; }
        if (L.updateBtn.has(x, y)) { checkForUpdates(); return true; }
        return true;
    }

    if (s.searchOpen && !L.searchCard.empty() && L.searchCard.has(x, y)) {
        for (int i = 0; i < L.searchRowCount; i++) {
            if (L.searchRow[i].has(x, y)) { appSelectSearchResult(s, i); return true; }
        }
        return true;
    }
    if (s.searchOpen && !L.search.has(x, y)) {
        s.searchOpen = false;
        InvalidateRect(s.hwnd, nullptr, FALSE);
    }

    if (L.winClose.has(x, y)) { PostMessageW(s.hwnd, WM_CLOSE, 0, 0); return true; }
    if (L.winMax.has(x, y)) { ShowWindow(s.hwnd, IsZoomed(s.hwnd) ? SW_RESTORE : SW_MAXIMIZE); return true; }
    if (L.winMin.has(x, y)) { PostMessageW(s.hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0); return true; }
    if (L.themeBtn.has(x, y)) { appToggleTheme(s); return true; }
    if (L.settingsBtn.has(x, y)) { s.settingsOpen = true; s.searchOpen = false; InvalidateRect(s.hwnd, nullptr, FALSE); return true; }
    if (L.search.has(x, y)) {
        SetFocus(s.hSearch);
        s.searchOpen = !s.searchResults.empty() || s.searchLoading;
        InvalidateRect(s.hwnd, nullptr, FALSE);
        return true;
    }
    for (int i = 0; i < 5; i++) {
        if (L.tabs[i].has(x, y)) {
            if (s.page != i) { s.page = i; InvalidateRect(s.hwnd, nullptr, FALSE); }
            return true;
        }
    }
    if (L.autoItem.has(x, y)) { appLocate(s); return true; }
    int start = std::max(0, std::min((int)s.settings.saved.size() - L.cityCount, s.sidebarScroll));
    for (int i = 0; i < L.cityCount; i++) {
        if (L.city[i].has(x, y)) {
            int idx = start + i;
            if (idx < 0 || idx >= (int)s.settings.saved.size()) continue;
            bool active = s.settings.hasCurrent && !s.settings.current.autoLoc &&
                          s.settings.saved[idx].key() == s.settings.current.key();
            if (active && x > L.city[i].right() - 28) {
                Location removed = s.settings.saved[idx];
                s.settings.saved.erase(s.settings.saved.begin() + idx);
                if (s.settings.hasCurrent && removed.key() == s.settings.current.key()) {
                    s.settings.hasCurrent = false;
                    appLocate(s);
                }
                appSaveSettings(s);
                InvalidateRect(s.hwnd, nullptr, FALSE);
                return true;
            }
            appSelectLocation(s, s.settings.saved[idx], false);
            return true;
        }
    }
    if (L.addCity.has(x, y)) { appFocusSearch(s); return true; }
    if (L.bottomTheme.has(x, y)) { appToggleTheme(s); return true; }
    if (L.bottomSettings.has(x, y)) { s.settingsOpen = true; InvalidateRect(s.hwnd, nullptr, FALSE); return true; }

    if (s.page == 0) {
        if (L.hero.has(x, y) && !s.hasWeather && !s.locating && !s.updating && heroRetryRect(L).has(x, y)) {
            appRequestWeather(s); return true;
        }
        if (L.hourly.has(x, y)) { s.page = 1; InvalidateRect(s.hwnd, nullptr, FALSE); return true; }
        if (s.hasWeather && L.weekly.has(x, y)) {
            if (L.showTen.has(x, y) || y > L.weekly.y + 32) { s.page = 2; InvalidateRect(s.hwnd, nullptr, FALSE); return true; }
        }
    }

    if (y < TOPBAR_H && x > SIDEBAR_W) {
        if (IsZoomed(s.hwnd)) return true;
        s.draggingWindow = true;
        GetWindowRect(s.hwnd, &s.dragRect);
        GetCursorPos(&s.dragCursor);
        SetCapture(s.hwnd);
        return true;
    }
    return false;
}

bool mouseUp(AppState& s, float x, float y) {
    s.mouseX = x; s.mouseY = y;
    bool was = s.draggingWindow;
    s.draggingWindow = false;
    s.mouseDown = false;
    if (GetCapture() == s.hwnd) ReleaseCapture();
    if (was) InvalidateRect(s.hwnd, nullptr, FALSE);
    return was;
}

bool mouseDoubleClick(AppState& s, float x, float y) {
    (void)s;
    if (y < TOPBAR_H && x > SIDEBAR_W) {
        ShowWindow(s.hwnd, IsZoomed(s.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        return true;
    }
    return false;
}

bool mouseWheel(AppState& s, float x, float y, int delta) {
    Layout L = layout(s);
    if (x < SIDEBAR_W) {
        int maxVisible = std::max(3, std::min(8, (int)((s.H - 340) / 60)));
        int maxScroll = std::max(0, (int)s.settings.saved.size() - maxVisible);
        s.sidebarScroll = std::max(0, std::min(maxScroll, s.sidebarScroll + (delta > 0 ? -1 : 1)));
        InvalidateRect(s.hwnd, nullptr, FALSE);
        return true;
    }
    return false;
}

bool keyDown(AppState& s, WPARAM key) {
    if (key == VK_ESCAPE) {
        if (s.settingsOpen) { s.settingsOpen = false; InvalidateRect(s.hwnd, nullptr, FALSE); return true; }
        if (s.searchOpen) { s.searchOpen = false; InvalidateRect(s.hwnd, nullptr, FALSE); return true; }
    }
    if (key == VK_F5) { appRequestWeather(s); return true; }
    if ((GetKeyState(VK_CONTROL) & 0x8000) && (key == 'F')) { appFocusSearch(s); return true; }
    if (key >= '1' && key <= '5') {
        s.page = (int)(key - '1');
        InvalidateRect(s.hwnd, nullptr, FALSE);
        return true;
    }
    return false;
}

bool interactive(const AppState& s, float x, float y) {
    Layout L = layout(s);
    if (y < TOPBAR_H) {
        return L.search.has(x, y) || L.themeBtn.has(x, y) || L.settingsBtn.has(x, y) ||
               L.winMin.has(x, y) || L.winMax.has(x, y) || L.winClose.has(x, y);
    }
    return x < SIDEBAR_W || y > TABS_Y;
}

COLORREF searchBg(const AppState& s) {
    return s.settings.dark ? RGB(17, 36, 55) : RGB(255, 255, 255);
}
COLORREF searchFg(const AppState& s) {
    return s.settings.dark ? RGB(242, 248, 255) : RGB(25, 39, 58);
}

} // namespace UI























