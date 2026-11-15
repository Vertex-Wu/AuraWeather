#include "app.h"
#include "ui.h"
#include "services.h"
#include "settings.h"
#include "http.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <thread>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

#define IDI_APP 101
#define IDR_CA 200
#define ID_EDIT_SEARCH 1001
#define WM_APP_TRAY (WM_APP + 20)

static AppState* g_s = nullptr;
static HINSTANCE g_inst = nullptr;
static HICON g_iconLarge = nullptr;
static HICON g_iconSmall = nullptr;
static NOTIFYICONDATAW g_nid = {};
static HBRUSH g_editBrush = nullptr;
static COLORREF g_editBrushColor = CLR_INVALID;
static HFONT g_editFont = nullptr;
static bool g_captureExiting = false;
static UINT g_taskbarCreated = 0;

static void logLine(const std::string& s) {
    OutputDebugStringA(("AuraWeather: " + s + "\n").c_str());
    std::wstring dir = appDataDirW();
    ensureDirectory(dir);
    std::ofstream f(narrow(dir + L"\\aura.log"), std::ios::app);
    if (!f) return;
    time_t t = time(nullptr);
    tm tmv{};
    localtime_s(&tmv, &t);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%H:%M:%S", &tmv);
    f << stamp << "  " << s << "\n";
}

static std::wstring getWindowTextStr(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize((size_t)n);
    return s;
}

static void setEditFont(HWND hwnd, float dpi) {
    if (g_editFont) DeleteObject(g_editFont);
    int height = -(int)std::lround(12.0f * dpi);
    g_editFont = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_editFont, TRUE);
    UI::layoutSearchEdit(*g_s);
}

static void addTrayIcon(HWND hwnd) {
    ZeroMemory(&g_nid, sizeof g_nid);
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = g_iconSmall ? g_iconSmall : g_iconLarge;
    wcscpy_s(g_nid.szTip, L"AURA 天气 · 每 10 分钟更新");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

static void removeTrayIcon() {
    if (g_nid.hWnd) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_nid.hWnd = nullptr;
}

static void balloon(const std::wstring& title, const std::wstring& body, DWORD flags = NIIF_INFO) {
    if (!g_nid.hWnd) return;
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = flags;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, body.c_str(), _TRUNCATE);
    n.uTimeout = 6000;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void applyRoundedRegion(HWND hwnd) {
    if (IsZoomed(hwnd)) {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }
    RECT rc;
    GetClientRect(hwnd, &rc);
    float dpi = g_s && g_s->dpi > 0 ? g_s->dpi : 1.0f;
    int rad = (int)std::lround(18.0f * dpi);
    HRGN rgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, rad * 2, rad * 2);
    SetWindowRgn(hwnd, rgn, TRUE);
}

static void updateMetricsFromClient(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    float dpi = g_s->dpi > 0 ? g_s->dpi : 1.0f;
    g_s->W = std::max(1, (int)std::lround((rc.right - rc.left) / dpi));
    g_s->H = std::max(1, (int)std::lround((rc.bottom - rc.top) / dpi));
    if (!IsZoomed(hwnd)) {
        g_s->settings.winW = g_s->W;
        g_s->settings.winH = g_s->H;
    }
    UI::layoutSearchEdit(*g_s);
}

static void showMainWindow(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void hideToTray(HWND hwnd) {
    logLine("hideToTray");
    ShowWindow(hwnd, SW_HIDE);
    
    if (g_s && !g_s->trayHintShown) {
        g_s->trayHintShown = true;
        balloon(L"AURA 天气仍在运行", L"已最小化到系统托盘，每 10 分钟自动更新天气。右键托盘图标可退出。");
    }
}

static bool rainSoon(const WeatherData& w) {
    int n = std::min(6, (int)w.hours.size());
    for (int i = 0; i < n; i++) if (w.hours[i].precip > 0.05) return true;
    return false;
}

// ---------------- async requests ----------------
void appRequestWeather(AppState& s) {
    if (!s.settings.hasCurrent) return;
    s.updating = true;
    s.errorText.clear();
    int gen = ++s.weatherGen;
    Location loc = s.settings.current;
    if (!s.hasWeather) {
        WeatherData cached;
        std::string cerr;
        if (loadCachedWeather(loc, cached, cerr)) {
            s.weather = std::move(cached);
            s.hasWeather = true;
            if (!s.mapUserMoved) {
                s.mapLat = s.weather.loc.lat;
                s.mapLon = s.weather.loc.lon;
            }
            InvalidateRect(s.hwnd, nullptr, FALSE);
        }
    }
    HWND hwnd = s.hwnd;
    std::thread([hwnd, gen, loc]() {
        WeatherData w;
        std::string err;
        bool ok = false;
        try { ok = fetchWeather(loc, w, err); }
        catch (const std::exception& e) { err = std::string("weather exception: ") + e.what(); }
        catch (...) { err = "weather unknown exception"; }
        auto* m = new WeatherMsg{};
        m->gen = gen;
        m->ok = ok;
        m->data = std::move(w);
        m->error = err;
        PostMessageW(hwnd, WM_APP_WEATHER, 0, (LPARAM)m);
    }).detach();
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

void appLocate(AppState& s) {
    if (s.locating) return;
    s.locating = true;
    int gen = ++s.ipGen;
    HWND hwnd = s.hwnd;
    std::thread([hwnd, gen]() {
        Location loc;
        std::string err;
        bool ok = locateByIp(loc, err);
        auto* m = new IpMsg{};
        m->ok = ok;
        m->loc = loc;
        m->error = err;
        PostMessageW(hwnd, WM_APP_IP, (WPARAM)gen, (LPARAM)m);
    }).detach();
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

void appStartSearch(AppState& s) {
    std::string q = trim(narrow(s.searchQuery));
    if (q.empty()) return;
    if (q == s.lastGeoQuery && (s.searchLoading || !s.searchResults.empty())) {
        s.searchOpen = true;
        InvalidateRect(s.hwnd, nullptr, FALSE);
        return;
    }
    s.lastGeoQuery = q;
    s.searchLoading = true;
    s.searchNoResult = false;
    s.searchOpen = true;
    int gen = ++s.searchGen;
    HWND hwnd = s.hwnd;
    std::thread([hwnd, gen, q]() {
        std::vector<SearchResult> results;
        std::string err;
        bool ok = geocodeCity(q, results, err);
        auto* m = new GeoMsg{};
        m->gen = gen;
        m->ok = ok;
        m->query = q;
        m->results = std::move(results);
        m->error = err;
        PostMessageW(hwnd, WM_APP_GEO, 0, (LPARAM)m);
    }).detach();
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

void appSelectSearchResult(AppState& s, int index) {
    if (index < 0 || index >= (int)s.searchResults.size()) return;
    SearchResult r = s.searchResults[index];
    Location loc;
    loc.name = r.name;
    loc.admin = r.admin;
    loc.country = r.country;
    loc.timezone = r.timezone;
    loc.lat = r.lat;
    loc.lon = r.lon;
    loc.offset = ianaOffset(loc.timezone, (long long)time(nullptr));
    loc.autoLoc = false;
    appSelectLocation(s, loc, true);
    SetWindowTextW(s.hSearch, L"");
    s.searchQuery.clear();
    s.searchResults.clear();
    s.searchOpen = false;
    s.searchLoading = false;
    s.searchNoResult = false;
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

void appSelectLocation(AppState& s, const Location& loc, bool addToSaved) {
    s.settings.current = loc;
    s.settings.hasCurrent = true;
    s.settings.currentAuto = loc.autoLoc;
    s.hasWeather = false;
    s.weather = WeatherData{};
    s.mapUserMoved = false;
    s.mapLat = loc.lat;
    s.mapLon = loc.lon;
    s.mapZoom = 4;
    if (addToSaved && !loc.autoLoc) {
        bool found = false;
        for (auto& x : s.settings.saved) {
            if (x.key() == loc.key()) { x = loc; found = true; break; }
        }
        if (!found) s.settings.saved.push_back(loc);
        if (s.settings.saved.size() > 12) s.settings.saved.erase(s.settings.saved.begin());
        s.sidebarScroll = std::max(0, (int)s.settings.saved.size() - 7);
    }
    appSaveSettings(s);
    s.searchOpen = false;
    appRequestWeather(s);
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

void appEnqueueTile(AppState& s, const std::string& key, const std::string& url) {
    HWND hwnd = s.hwnd;
    std::thread([hwnd, key, url]() {
        HttpResult r = Http::get(url, 26000);
        auto* m = new TileMsg{};
        m->key = key;
        m->status = r.status;
        m->bytes = std::move(r.body);
        PostMessageW(hwnd, WM_APP_TILE, 0, (LPARAM)m);
    }).detach();
}

void appSaveSettings(AppState& s) {
    s.settings.startup = isStartupEnabled();
    saveSettings(s.settings);
}

static void applyDwmTheme(HWND hwnd, bool dark) {
    if (!hwnd) return;
    BOOL value = dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1+); 19 works on older builds.
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value))))
        DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));
}

void appToggleTheme(AppState& s) {
    s.settings.dark = !s.settings.dark;
    appSaveSettings(s);
    applyDwmTheme(s.hwnd, s.settings.dark);
    s.toastMsg(s.settings.dark ? L"已切换到暗色模式" : L"已切换到亮色模式");
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

void appFocusSearch(AppState& s) {
    if (!IsWindowVisible(s.hwnd)) showMainWindow(s.hwnd);
    SetForegroundWindow(s.hwnd);
    SetFocus(s.hSearch);
    SetWindowTextW(s.hSearch, L"");
    s.searchQuery.clear();
    s.searchOpen = false;
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

void appClearCache(AppState& s) {
    clearCacheFiles();
    UI::clearTiles();
    s.toastMsg(L"天气与云图缓存已清除");
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

void appRefreshStartup(AppState& s) {
    s.settings.startup = isStartupEnabled();
}

// ---------------- edit subclass ----------------
static LRESULT CALLBACK EditSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR, DWORD_PTR) {
    AppState* s = g_s;
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            if (!s->searchResults.empty()) {
                int idx = std::max(0, std::min((int)s->searchResults.size() - 1, s->searchSelected));
                appSelectSearchResult(*s, idx);
            } else {
                appStartSearch(*s);
            }
            return 0;
        }
        if (wp == VK_ESCAPE) {
            s->searchOpen = false;
            SetFocus(s->hwnd);
            InvalidateRect(s->hwnd, nullptr, FALSE);
            return 0;
        }
        if (wp == VK_DOWN || wp == VK_UP) {
            if (!s->searchResults.empty()) {
                int n = (int)s->searchResults.size();
                s->searchSelected = (s->searchSelected + (wp == VK_DOWN ? 1 : n - 1)) % n;
                s->searchOpen = true;
                InvalidateRect(s->hwnd, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_SETFOCUS:
        if (!s->searchResults.empty()) s->searchOpen = true;
        InvalidateRect(s->hwnd, nullptr, FALSE);
        break;
    case WM_KILLFOCUS:
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, EditSubclass, 1);
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ---------------- window proc ----------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppState& s = *g_s;
    if (g_taskbarCreated && msg == g_taskbarCreated) {
        addTrayIcon(hwnd);
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_NCCALCSIZE:
        if (wp) return 0;
        break;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        float dpi = s.dpi > 0 ? s.dpi : 1.0f;
        mm->ptMinTrackSize.x = (LONG)std::lround(1040 * dpi);
        mm->ptMinTrackSize.y = (LONG)std::lround(760 * dpi);
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(mon, &mi)) {
            mm->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mm->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mm->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mm->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        }
        return 0;
    }
    case WM_NCHITTEST: {
        if (IsZoomed(hwnd)) return HTCLIENT;
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc;
        GetWindowRect(hwnd, &rc);
        int b = (int)std::lround(6 * s.dpi);
        bool left = p.x < rc.left + b, right = p.x >= rc.right - b;
        bool top = p.y < rc.top + b, bottom = p.y >= rc.bottom - b;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        return HTCLIENT;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        ULONGLONG t0 = GetTickCount64();
        UI::render(hdc, s);
        ULONGLONG dt = GetTickCount64() - t0;
        if (dt >= 30) logLine("render " + std::to_string(dt) + " ms");
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            if (s.settings.trayClose && !s.captureMode) hideToTray(hwnd);
            return 0;
        }
        updateMetricsFromClient(hwnd);
        applyRoundedRegion(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        s.dpi = (float)HIWORD(wp) / 96.0f;
        RECT* nr = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, nr->left, nr->top, nr->right - nr->left, nr->bottom - nr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        updateMetricsFromClient(hwnd);
        applyRoundedRegion(hwnd);
        setEditFont(s.hSearch, s.dpi);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (s.captureMode) return 0;
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        UI::mouseMove(s, (float)GET_X_LPARAM(lp) / s.dpi, (float)GET_Y_LPARAM(lp) / s.dpi);
        return 0;
    }
    case WM_MOUSELEAVE:
        s.mouseX = -1;
        s.mouseY = -1;
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        UI::mouseDown(s, (float)GET_X_LPARAM(lp) / s.dpi, (float)GET_Y_LPARAM(lp) / s.dpi);
        return 0;
    case WM_LBUTTONUP:
        UI::mouseUp(s, (float)GET_X_LPARAM(lp) / s.dpi, (float)GET_Y_LPARAM(lp) / s.dpi);
        return 0;
    case WM_LBUTTONDBLCLK:
        UI::mouseDoubleClick(s, (float)GET_X_LPARAM(lp) / s.dpi, (float)GET_Y_LPARAM(lp) / s.dpi);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        UI::mouseWheel(s, (float)p.x / s.dpi, (float)p.y / s.dpi, GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            float lx = (float)p.x / s.dpi, ly = (float)p.y / s.dpi;
            if (UI::interactive(s, lx, ly)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;
    case WM_KEYDOWN:
        if (UI::keyDown(s, wp)) return 0;
        break;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if (id == ID_EDIT_SEARCH) {
            if (code == EN_CHANGE) {
                s.searchQuery = getWindowTextStr(s.hSearch);
                if (s.searchQuery.empty()) {
                    s.searchOpen = false;
                    s.searchResults.clear();
                    s.searchLoading = false;
                    s.searchNoResult = false;
                } else {
                    s.searchOpen = true;
                    s.searchDebounce = (long long)GetTickCount64() + 380;
                    s.searchLoading = false;
                    s.searchNoResult = false;
                    s.searchResults.clear();
                    s.lastGeoQuery.clear();
                    s.searchGen++;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (code == EN_SETFOCUS) {
                if (!s.searchResults.empty()) s.searchOpen = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        COLORREF bg = UI::searchBg(s);
        SetTextColor(hdc, UI::searchFg(s));
        SetBkColor(hdc, bg);
        if (!g_editBrush || g_editBrushColor != bg) {
            if (g_editBrush) DeleteObject(g_editBrush);
            g_editBrush = CreateSolidBrush(bg);
            g_editBrushColor = bg;
        }
        return (LRESULT)g_editBrush;
    }
    case WM_TIMER:
        if (wp == TIMER_CLOCK) {
            long long now = (long long)GetTickCount64();
            if (s.searchDebounce > 0 && now >= s.searchDebounce && !s.searchLoading) {
                s.searchDebounce = 0;
                appStartSearch(s);
            }
            if (s.captureMode && !g_captureExiting && now >= (long long)s.captureAt) {
                g_captureExiting = true;
                logLine("screenshot begin");
                bool ok = UI::saveScreenshot(s, s.capturePath);
                logLine(ok ? "screenshot saved" : "screenshot failed");
                DestroyWindow(hwnd);
                return 0;
            }
            long long sec = (long long)time(nullptr);
            if (s.settings.autoRefresh && !s.updating && s.settings.hasCurrent && sec >= s.nextRefresh) {
                s.nextRefresh = sec + (s.hasWeather ? s.settings.refreshMinutes * 60 : 120);
                appRequestWeather(s);
            }
            if ((now % 15000 < 600 && s.hasWeather) ||
                (s.toastUntil > 0 && now < s.toastUntil + 700))
                InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    case WM_APP_WEATHER: {
        WeatherMsg* m = (WeatherMsg*)lp;
        if (m->gen == s.weatherGen) {
            s.updating = false;
            bool oldRain = s.hasWeather && rainSoon(s.weather);
            if (m->ok) {
                s.weather = std::move(m->data);
                s.hasWeather = true;
                logLine("weather success: " + s.weather.now.symbol);
                s.settings.hasCurrent = true;
                s.settings.current = s.weather.loc;
                s.settings.current.offset = s.weather.loc.offset;
                if (!s.settings.current.autoLoc) {
                    for (auto& x : s.settings.saved)
                        if (x.key() == s.settings.current.key()) { x.offset = s.weather.loc.offset; break; }
                }
                if (!s.mapUserMoved) { s.mapLat = s.weather.loc.lat; s.mapLon = s.weather.loc.lon; }
                if (m->data.stale) {
                    s.errorText = widen(m->error);
                    s.toastMsg(L"网络不可用，正在显示上次缓存");
                } else {
                    s.errorText.clear();
                }
                bool newRain = rainSoon(s.weather);
                if (s.settings.notify && !s.firstLoad && newRain && !oldRain && !s.hasNotifiedRain) {
                    s.hasNotifiedRain = true;
                    if (IsWindowVisible(hwnd)) s.toastMsg(L"未来 6 小时可能有降水，记得带伞");
                    else balloon(L"天气提醒", L"未来 6 小时可能有降水，出门记得带伞。", NIIF_WARNING);
                }
                if (!newRain) s.hasNotifiedRain = false;
                s.firstLoad = false;
                s.nextRefresh = (long long)time(nullptr) + s.settings.refreshMinutes * 60;
                appSaveSettings(s);
            } else {
                s.errorText = widen(m->error.empty() ? "天气服务暂时不可用" : m->error);
                if (!s.hasWeather) s.toastMsg(L"无法获取天气，请检查网络");
                s.nextRefresh = (long long)time(nullptr) + 120;
                logLine("weather error: " + m->error);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        delete m;
        return 0;
    }
    case WM_APP_GEO: {
        GeoMsg* m = (GeoMsg*)lp;
        if (m->gen == s.searchGen) {
            s.searchLoading = false;
            s.searchOpen = true;
            s.searchResults = std::move(m->results);
            s.searchNoResult = s.searchResults.empty();
            s.searchSelected = 0;
            if (s.searchNoResult) logLine("geocode: " + m->error);
            if (s.autoPick >= 0 && !s.searchResults.empty()) {
                int pick = std::max(0, std::min((int)s.searchResults.size() - 1, s.autoPick));
                s.autoPick = -1;
                appSelectSearchResult(s, pick);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        delete m;
        return 0;
    }
    case WM_APP_IP: {
        int gen = (int)wp;
        IpMsg* m = (IpMsg*)lp;
        s.locating = false;
        if (gen == s.ipGen) {
            if (m->ok) {
                if (!s.settings.hasCurrent || s.settings.currentAuto) {
                    double dlat = s.settings.hasCurrent ? std::abs(s.settings.current.lat - m->loc.lat) : 999;
                    double dlon = s.settings.hasCurrent ? std::abs(s.settings.current.lon - m->loc.lon) : 999;
                    bool moved = dlat > 0.02 || dlon > 0.02;
                    s.settings.current = m->loc;
                    s.settings.current.autoLoc = true;
                    s.settings.hasCurrent = true;
                    s.settings.currentAuto = true;
                    if (moved) {
                        s.mapUserMoved = false;
                        s.mapLat = m->loc.lat;
                        s.mapLon = m->loc.lon;
                        s.hasWeather = false;
                        s.weather = WeatherData{};
                    }
                    appSaveSettings(s);
                    if (moved || !s.hasWeather) appRequestWeather(s);
                }
            } else {
                logLine("ip locate error: " + m->error);
                if (!s.settings.hasCurrent) {
                    Location bj;
                    bj.name = "北京";
                    bj.admin = "北京市";
                    bj.country = "中国";
                    bj.timezone = "Asia/Shanghai";
                    bj.lat = 39.9042;
                    bj.lon = 116.4074;
                    bj.offset = 28800;
                    bj.autoLoc = false;
                    s.settings.current = bj;
                    s.settings.hasCurrent = true;
                    s.settings.currentAuto = false;
                    s.errorText = L"自动定位失败，已显示北京";
                    s.toastMsg(L"自动定位失败，已显示北京");
                    appRequestWeather(s);
                } else {
                    appRequestWeather(s);
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        delete m;
        return 0;
    }
    case WM_APP_TILE: {
        TileMsg* m = (TileMsg*)lp;
        UI::onTile(*m);
        delete m;
        return 0;
    }
    case WM_APP_TRAY: {
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK) {
            showMainWindow(hwnd);
            return 0;
        }
        if (LOWORD(lp) == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 40001, L"显示 AURA 天气");
            AppendMenuW(menu, MF_STRING, 40002, L"立即刷新");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING | (isStartupEnabled() ? MF_CHECKED : 0), 40003, L"开机启动");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 40004, L"退出");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            PostMessageW(hwnd, WM_NULL, 0, 0);
            if (cmd == 40001) showMainWindow(hwnd);
            else if (cmd == 40002) appRequestWeather(s);
            else if (cmd == 40003) {
                bool en = !isStartupEnabled();
                setStartupEnabled(en);
                s.settings.startup = en;
                appSaveSettings(s);
            } else if (cmd == 40004) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        logLine("WM_CLOSE");
        if (s.settings.trayClose && !s.captureMode) {
            hideToTray(hwnd);
            appSaveSettings(s);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        logLine("WM_DESTROY");
        removeTrayIcon();
        
        KillTimer(hwnd, TIMER_CLOCK);
        appSaveSettings(s);
        UI::shutdown();
        PostQuitMessage(0);
        return 0;
    case WM_ENDSESSION:
        if (wp) appSaveSettings(s);
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) appRequestWeather(s);
        return TRUE;
    case WM_SYSCOMMAND:
        if (wp == SC_MINIMIZE && s.settings.trayClose && !s.captureMode) {
            hideToTray(hwnd);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) {
            s.lastAnimTick = (DWORD)GetTickCount64();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool loadCaBundle(std::string& pem) {
    HRSRC res = FindResourceW(g_inst, MAKEINTRESOURCEW(IDR_CA), RT_RCDATA);
    if (!res) return false;
    HGLOBAL mem = LoadResource(g_inst, res);
    if (!mem) return false;
    DWORD size = SizeofResource(g_inst, res);
    const void* p = LockResource(mem);
    if (!p || size == 0) return false;
    pem.assign((const char*)p, size);
    return true;
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    char buf[256];
    snprintf(buf, sizeof buf, "UNHANDLED EXCEPTION code=0x%08lX addr=%p",
             ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0,
             ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr);
    logLine(buf);
    return EXCEPTION_EXECUTE_HANDLER;
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    g_inst = hInst;
    SetUnhandledExceptionFilter(CrashFilter);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    logLine("startup");

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    auto* s = new AppState();
    g_s = s;
    s->hInst = hInst;

    HANDLE singleMutex = CreateMutexW(nullptr, TRUE, L"Local\\AuraWeather.SingleInstance");
    if (singleMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"AuraWeatherMain", nullptr);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(singleMutex);
        delete s;
        g_s = nullptr;
        return 0;
    }

    std::string loadErr;
    loadSettings(s->settings, loadErr);
    s->settings.startup = isStartupEnabled();

    bool background = false;
    int startupFlag = 0; // -1 off, 1 on, hidden test switch
    int waitMs = 10000;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--background") background = true;
        else if (a == L"--debug-startup=on") startupFlag = 1;
        else if (a == L"--debug-startup=off") startupFlag = -1;
        else if (a == L"--light") { s->settings.dark = false; s->captureTheme = 0; }
        else if (a == L"--dark") { s->settings.dark = true; s->captureTheme = 1; }
        else if (a.rfind(L"--debug-screenshot=", 0) == 0) { s->captureMode = true; s->capturePath = a.substr(19); }
        else if (a == L"--debug-screenshot" && i + 1 < argc) { s->captureMode = true; s->capturePath = argv[++i]; }
        else if (a.rfind(L"--wait=", 0) == 0) waitMs = _wtoi(a.c_str() + 7);
        else if (a.rfind(L"--page=", 0) == 0) s->capturePage = _wtoi(a.c_str() + 7);
        else if (a == L"--debug-settings") s->settingsOpen = true;
        else if (a.rfind(L"--search=", 0) == 0) { s->searchQuery = a.substr(9); s->searchOpen = true; }
        else if (a.rfind(L"--debug-pick=", 0) == 0) s->autoPick = _wtoi(a.c_str() + 13);
        else if (a.rfind(L"--theme=", 0) == 0) {
            std::wstring t = a.substr(8);
            s->captureTheme = (t == L"light" || t == L"flat") ? 0 : 1;
            s->settings.dark = s->captureTheme == 1;
        }
    }
    LocalFree(argv);
    if (startupFlag) {
        bool on = startupFlag > 0;
        if (setStartupEnabled(on)) s->settings.startup = on;
        appSaveSettings(*s);
    }
    if (s->captureMode) {
        s->captureAt = (DWORD)GetTickCount64() + (DWORD)std::max(1500, waitMs);
        if (s->capturePage >= 0 && s->capturePage <= 4) s->page = s->capturePage;
        s->settings.animations = false;
        nCmdShow = SW_SHOW;
    }

    HDC screenDc = GetDC(nullptr);
    int sysDpi = GetDeviceCaps(screenDc, LOGPIXELSX);
    ReleaseDC(nullptr, screenDc);
    if (sysDpi <= 0) sysDpi = 96;
    s->dpi = sysDpi / 96.0f;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(5, 10, 20));
    wc.lpszClassName = L"AuraWeatherMain";
    g_iconLarge = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    g_iconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hIcon = g_iconLarge;
    wc.hIconSm = g_iconSmall;
    RegisterClassExW(&wc);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int ww = s->settings.winW > 900 ? s->settings.winW : 1280;
    int wh = s->settings.winH > 680 ? s->settings.winH : 840;
    ww = std::min(ww, (int)((work.right - work.left) / s->dpi) - 8);
    wh = std::min(wh, (int)((work.bottom - work.top) / s->dpi) - 8);
    int wx = s->settings.winX;
    int wy = s->settings.winY;
    if (wx < work.left - 100 || wx > work.right - 200 || wy < work.top - 100 || wy > work.bottom - 200) {
        wx = work.left + (work.right - work.left - (int)(ww * s->dpi)) / 2;
        wy = work.top + (work.bottom - work.top - (int)(wh * s->dpi)) / 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"AuraWeatherMain", L"AURA 天气 2.0.5",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                wx, wy, (int)std::lround(ww * s->dpi), (int)std::lround(wh * s->dpi),
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"无法创建应用窗口。", L"AURA 天气", MB_ICONERROR);
        if (singleMutex) CloseHandle(singleMutex);
        return 1;
    }
    s->hwnd = hwnd;
    applyDwmTheme(hwnd, s->settings.dark);
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_iconLarge);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_iconSmall);

    s->hSearch = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
                                 0, 0, 10, 10, hwnd, (HMENU)ID_EDIT_SEARCH, hInst, nullptr);
    SendMessageW(s->hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索城市，或输入 纬度,经度");
    SetWindowSubclass(s->hSearch, EditSubclass, 1, 0);
    setEditFont(s->hSearch, s->dpi);
    if (!s->searchQuery.empty()) {
        SetWindowTextW(s->hSearch, s->searchQuery.c_str());
        s->searchDebounce = (long long)GetTickCount64() + 900;
    }

    UI::init(hwnd);
    updateMetricsFromClient(hwnd);
    applyRoundedRegion(hwnd);

    std::string ca;
    if (!loadCaBundle(ca)) {
        logLine("CA bundle resource missing");
        s->errorText = L"安全证书资源缺失";
    } else {
        std::string err;
        if (!Http::init(ca, err)) {
            logLine("HTTP init failed: " + err);
            s->errorText = L"网络组件初始化失败";
        }
    }

    logLine("window created");
    addTrayIcon(hwnd);
    
    SetTimer(hwnd, TIMER_CLOCK, 500, nullptr);

    if (s->settings.hasCurrent && !s->settings.current.name.empty()) {
        appRequestWeather(*s);
        if (s->settings.current.autoLoc) appLocate(*s);
    } else {
        appLocate(*s);
    }

    ShowWindow(hwnd, (background && !s->captureMode) ? SW_HIDE : nCmdShow);
    UpdateWindow(hwnd);
    if (background && !s->captureMode) {
        
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    logLine("message loop end");
    delete s;
    if (singleMutex) {
        ReleaseMutex(singleMutex);
        CloseHandle(singleMutex);
    }
    CoUninitialize();
    return (int)msg.wParam;
}








