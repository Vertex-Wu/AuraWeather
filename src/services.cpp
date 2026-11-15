#include "services.h"
#include "http.h"
#include "json.h"

#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cmath>
#include <map>
#include <set>
#include <algorithm>
#include <cstdio>

namespace fs = std::filesystem;

std::wstring appDataDirW() {
    wchar_t exe[MAX_PATH * 2] = {};
    if (GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(wchar_t)))) {
        fs::path dir = fs::path(exe).parent_path();
        if (fs::exists(dir / L"portable.flag")) {
            std::error_code ec;
            fs::create_directories(dir / L"data", ec);
            return (dir / L"data" / L"AuraWeather").wstring();
        }
    }
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)(sizeof(buf) / sizeof(wchar_t)));
    if (n == 0 || n >= sizeof(buf) / sizeof(wchar_t)) {
        wchar_t p[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p))) {
            wcscpy(buf, p);
        } else {
            wcscpy(buf, L".");
        }
    }
    return std::wstring(buf) + L"\\AuraWeather";
}

std::string appDataDirA() { return narrow(appDataDirW()); }

std::wstring cacheDirW() { return appDataDirW() + L"\\cache"; }

void ensureDirectory(const std::wstring& path) {
    std::error_code ec;
    fs::create_directories(fs::path(path), ec);
}

static std::wstring cachePathW(const Location& loc) {
    wchar_t b[64];
    swprintf(b, 64, L"\\weather_%.4f_%.4f.json", loc.lat, loc.lon);
    return cacheDirW() + b;
}

std::string loadWeatherCache(const Location& loc) {
    std::ifstream f(fs::path(cachePathW(loc)), std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void saveWeatherCache(const Location& loc, const std::string& body) {
    if (body.size() < 100) return;
    ensureDirectory(cacheDirW());
    fs::path p(cachePathW(loc));
    fs::path tmp = p;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f.write(body.data(), (std::streamsize)body.size());
        if (!f.good()) return;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
}

void clearCacheFiles() {
    std::error_code ec;
    fs::remove_all(fs::path(cacheDirW()), ec);
    ensureDirectory(cacheDirW());
}

static double jnum(const JsonValue* o, const char* key, double def = 0.0) {
    if (!o) return def;
    const JsonValue* v = o->get(key);
    return v ? v->number(def) : def;
}
static std::string jstr(const JsonValue* o, const char* key, const std::string& def = "") {
    if (!o) return def;
    const JsonValue* v = o->get(key);
    return v ? v->string(def) : def;
}

static int offsetFor(const Location& l, long long utc) {
    if (!l.timezone.empty() && l.timezone != "auto" && l.timezone.rfind("UTC", 0) != 0)
        return ianaOffset(l.timezone, utc);
    if (!l.timezone.empty() && l.timezone.rfind("UTC", 0) == 0)
        return ianaOffset(l.timezone, utc);
    return l.offset;
}

std::wstring solarTimeText(int minutes, bool polarDay, bool polarNight) {
    if (polarDay) return L"极昼";
    if (polarNight) return L"极夜";
    if (minutes < 0) return L"--:--";
    wchar_t b[16];
    swprintf(b, 16, L"%02d:%02d", minutes / 60 % 24, minutes % 60);
    return b;
}

std::string formatDurationAgo(long long utc) {
    if (utc <= 0) return "never";
    long long d = (long long)time(nullptr) - utc;
    if (d < 0) d = 0;
    if (d < 60) return "just now";
    if (d < 3600) return std::to_string(d / 60) + " min ago";
    if (d < 86400) return std::to_string(d / 3600) + " h ago";
    return std::to_string(d / 86400) + " d ago";
}

struct RawPoint {
    long long utc = 0;
    double temp = 0, feels = 0, humidity = 0, pressure = 0, cloud = 0;
    double wind = 0, windDir = 0, dew = 0, uv = 0;
    double precip1 = 0, precip6 = 0;
    double tmax6 = 0, tmin6 = 0;
    bool hasT6 = false;
    std::string sym1, sym6;
};

static double interpolate(double a, double b, double t) { return a + (b - a) * t; }

struct DayBuild {
    DayItem item;
    bool init = false;
    double bestHourDist = 999;
    std::map<std::string, bool> seen;
};

static WeatherData parseMetJson(const std::string& body, const Location& loc, bool& ok, std::string& err) {
    WeatherData w;
    w.loc = loc;
    ok = false;
    JsonValue root = JsonParser::parse(body, &err);
    if (!root.isObj()) { err = "天气数据格式无效"; return w; }
    const JsonValue* props = root.get("properties");
    if (!props) { err = "天气数据缺少 properties"; return w; }
    const JsonValue* ts = props->get("timeseries");
    if (!ts || !ts->isArr() || ts->arr.empty()) { err = "天气数据为空"; return w; }

    std::vector<RawPoint> raw;
    raw.reserve(ts->arr.size());
    for (const auto& e : ts->arr) {
        if (!e.isObj()) continue;
        RawPoint p;
        p.utc = parseIsoUtc(jstr(&e, "time"));
        if (!p.utc) continue;
        const JsonValue* data = e.get("data");
        const JsonValue* inst = data ? data->get("instant") : nullptr;
        const JsonValue* det = inst ? inst->get("details") : nullptr;
        if (!det) continue;
        p.temp = jnum(det, "air_temperature");
        p.feels = jnum(det, "apparent_air_temperature", p.temp);
        p.humidity = jnum(det, "relative_humidity");
        p.pressure = jnum(det, "air_pressure_at_sea_level");
        p.cloud = jnum(det, "cloud_area_fraction");
        p.wind = jnum(det, "wind_speed");
        p.windDir = jnum(det, "wind_from_direction");
        p.dew = jnum(det, "dew_point_temperature");
        p.uv = jnum(det, "ultraviolet_index_clear_sky");
        const JsonValue* n1 = data->get("next_1_hours");
        const JsonValue* n6 = data->get("next_6_hours");
        if (n1) {
            const JsonValue* sum = n1->get("summary");
            p.sym1 = jstr(sum, "symbol_code");
            const JsonValue* d1 = n1->get("details");
            p.precip1 = jnum(d1, "precipitation_amount");
        }
        if (n6) {
            const JsonValue* sum = n6->get("summary");
            p.sym6 = jstr(sum, "symbol_code");
            const JsonValue* d6 = n6->get("details");
            p.precip6 = jnum(d6, "precipitation_amount");
            p.tmax6 = jnum(d6, "air_temperature_max");
            p.tmin6 = jnum(d6, "air_temperature_min");
            p.hasT6 = d6 && d6->get("air_temperature_max") != nullptr;
        }
        if (p.sym1.empty()) p.sym1 = p.sym6;
        if (p.sym6.empty()) p.sym6 = p.sym1;
        raw.push_back(p);
    }
    if (raw.empty()) { err = "天气数据为空"; return w; }
    std::sort(raw.begin(), raw.end(), [](const RawPoint& a, const RawPoint& b) { return a.utc < b.utc; });

    long long now = (long long)time(nullptr);
    int off = offsetFor(loc, now);
    w.loc.offset = off;

    const RawPoint* cur = &raw.front();
    long long best = std::llabs(raw.front().utc - now);
    for (const auto& p : raw) {
        long long d = std::llabs(p.utc - now);
        if (d < best) { best = d; cur = &p; }
    }

    auto daySolar = [&](long long utc, int& sr, int& ss, bool& pd, bool& pn) {
        int o = offsetFor(loc, utc);
        CivilTime c = civilLocal(utc, o);
        long long mid = localMidnightUtc(c.y, c.mo, c.d, o);
        return solarTimes(mid, loc.lat, loc.lon, o, sr, ss, pd, pn);
    };

    int sr = 0, ss = 0; bool pd = false, pn = false;
    daySolar(now, sr, ss, pd, pn);
    CivilTime nc = civilLocal(now, off);
    int nowMin = nc.h * 60 + nc.mi;
    bool isDay = true;
    if (pd) isDay = true;
    else if (pn) isDay = false;
    else isDay = nowMin >= sr && nowMin < ss;
    if (symbolIsNight(cur->sym1)) isDay = false;

    w.now.temp = cur->temp;
    w.now.feels = cur->feels;
    w.now.humidity = cur->humidity;
    w.now.pressure = cur->pressure;
    w.now.cloud = cur->cloud;
    w.now.wind = cur->wind;
    w.now.windDir = cur->windDir;
    w.now.dew = cur->dew;
    w.now.uv = cur->uv;
    w.now.precip = cur->precip1;
    w.now.cond = condFromSymbol(cur->sym1);
    w.now.symbol = cur->sym1;
    w.now.isDay = isDay;
    w.updatedUtc = (long long)time(nullptr);

    // -------- hourly (72 h, interpolated to exact hours) --------
    long long startHour = (now / 3600) * 3600;
    std::map<std::string, std::pair<int, int>> solarCache;
    auto solarFor = [&](long long utc) -> std::pair<int, int> {
        int o = offsetFor(loc, utc);
        CivilTime c = civilLocal(utc, o);
        char key[32];
        snprintf(key, sizeof key, "%04d-%02d-%02d", c.y, c.mo, c.d);
        auto it = solarCache.find(key);
        if (it != solarCache.end()) return it->second;
        long long mid = localMidnightUtc(c.y, c.mo, c.d, o);
        int a = 0, b = 0; bool p1 = false, p2 = false;
        solarTimes(mid, loc.lat, loc.lon, o, a, b, p1, p2);
        auto val = std::make_pair(a, b);
        solarCache[key] = val;
        return val;
    };

    for (int h = 0; h < 72; h++) {
        long long t = startHour + h * 3600LL;
        size_t i = 0;
        while (i + 1 < raw.size() && raw[i + 1].utc <= t) i++;
        if (t < raw.front().utc) i = 0;
        size_t j = std::min(i + 1, raw.size() - 1);
        double frac = 0;
        if (raw[j].utc > raw[i].utc && t > raw[i].utc)
            frac = std::min(1.0, std::max(0.0, (double)(t - raw[i].utc) / (double)(raw[j].utc - raw[i].utc)));
        HourItem hi;
        hi.utc = t;
        hi.temp = interpolate(raw[i].temp, raw[j].temp, frac);
        hi.feels = interpolate(raw[i].feels, raw[j].feels, frac);
        hi.humidity = interpolate(raw[i].humidity, raw[j].humidity, frac);
        hi.wind = interpolate(raw[i].wind, raw[j].wind, frac);
        hi.windDir = raw[i].windDir;
        hi.cloud = interpolate(raw[i].cloud, raw[j].cloud, frac);
        hi.uv = interpolate(raw[i].uv, raw[j].uv, frac);
        hi.precip = raw[i].precip1;
        if (raw[i].precip1 <= 0 && raw[i].precip6 > 0) hi.precip = raw[i].precip6 / 6.0;
        hi.cond = condFromSymbol(raw[i].sym1);
        hi.symbol = raw[i].sym1;
        int o = offsetFor(loc, t);
        CivilTime c = civilLocal(t, o);
        int sr2 = 0, ss2 = 0;
        auto sp = solarFor(t);
        sr2 = sp.first; ss2 = sp.second;
        int mins = c.h * 60 + c.mi;
        hi.isDay = (sr2 == ss2) ? !symbolIsNight(hi.symbol) : (mins >= sr2 && mins < ss2);
        if (symbolIsNight(hi.symbol)) hi.isDay = false;
        w.hours.push_back(hi);
    }

    // -------- daily (up to 10 days) --------
    CivilTime todayC = civilLocal(now, off);
    std::map<std::string, DayBuild> dayMap;
    for (size_t i = 0; i < raw.size(); i++) {
        long long o = offsetFor(loc, raw[i].utc);
        CivilTime c = civilLocal(raw[i].utc, o);
        char key[32];
        snprintf(key, sizeof key, "%04d-%02d-%02d", c.y, c.mo, c.d);
        auto& b = dayMap[key];
        if (!b.init) {
            b.init = true;
            b.item.y = c.y; b.item.m = c.mo; b.item.d = c.d;
            b.item.utcMidnight = localMidnightUtc(c.y, c.mo, c.d, o);
            b.item.tmax = raw[i].temp;
            b.item.tmin = raw[i].temp;
        }
        b.item.tmax = std::max(b.item.tmax, raw[i].temp);
        b.item.tmin = std::min(b.item.tmin, raw[i].temp);
        if (raw[i].hasT6) {
            b.item.tmax = std::max(b.item.tmax, raw[i].tmax6);
            b.item.tmin = std::min(b.item.tmin, raw[i].tmin6);
        }
        b.item.windMax = std::max(b.item.windMax, raw[i].wind);
        b.item.uvMax = std::max(b.item.uvMax, raw[i].uv);
        // precipitation: each point represents the following interval
        long long next = (i + 1 < raw.size()) ? raw[i + 1].utc : raw[i].utc + 3600;
        long long gap = std::max(3600LL, next - raw[i].utc);
        if (gap <= 5400) b.item.precip += raw[i].precip1;
        else b.item.precip += (raw[i].precip6 > 0 ? raw[i].precip6 : raw[i].precip1);
        int hourDist = std::abs(c.h - 12);
        if (hourDist < b.bestHourDist) {
            b.bestHourDist = hourDist;
            b.item.symbol = raw[i].sym1.empty() ? raw[i].sym6 : raw[i].sym1;
        }
    }

    for (int k = 0; k < 10; k++) {
        long long mid = localMidnightUtc(todayC.y, todayC.mo, todayC.d, off) + k * 86400LL;
        CivilTime c = civilLocal(mid, offsetFor(loc, mid));
        char key[32];
        snprintf(key, sizeof key, "%04d-%02d-%02d", c.y, c.mo, c.d);
        auto it = dayMap.find(key);
        if (it == dayMap.end()) continue;
        DayItem d = it->second.item;
        int o = offsetFor(loc, d.utcMidnight + 12 * 3600);
        int a = 0, bb = 0; bool pday = false, pnight = false;
        solarTimes(d.utcMidnight, loc.lat, loc.lon, o, a, bb, pday, pnight);
        d.sunrise = narrow(solarTimeText(a, pday, pnight));
        d.sunset = narrow(solarTimeText(bb, pday, pnight));
        d.cond = condFromSymbol(d.symbol);
        d.moonPhase = moonPhaseFraction(d.utcMidnight + 12 * 3600);
        w.days.push_back(d);
    }

    ok = true;
    return w;
}

static double fetchAqi(const Location& loc) {
    char url[384];
    snprintf(url, sizeof url,
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f&current=us_aqi",
             loc.lat, loc.lon);
    HttpResult r = Http::get(url, 6500);
    if (!r.success() || r.body.empty()) return 0;
    std::string e;
    JsonValue root = JsonParser::parse(r.body, &e);
    const JsonValue* cur = root.get("current");
    double v = jnum(cur, "us_aqi", 0);
    return (v >= 0 && v < 1000) ? v : 0;
}

static void buildInsights(WeatherData& w) {
    w.alertText.clear();
    w.advices.clear();
    double tmax = w.now.temp, tmin = w.now.temp, windMax = w.now.wind;
    double rain = w.now.precip, uvMax = w.now.uv, humidityMin = w.now.humidity;
    bool thunder = w.now.cond == Cond::Thunder;
    bool snow = w.now.cond == Cond::Snow || w.now.cond == Cond::Sleet;
    for (size_t i = 0; i < w.hours.size() && i < 3; i++) {
        const HourItem& h = w.hours[i];
        tmax = std::max(tmax, h.temp); tmin = std::min(tmin, h.temp);
        windMax = std::max(windMax, h.wind);
        rain += std::max(0.0, h.precip);
        uvMax = std::max(uvMax, h.uv);
        humidityMin = std::min(humidityMin, h.humidity);
        thunder = thunder || h.cond == Cond::Thunder;
        snow = snow || h.cond == Cond::Snow || h.cond == Cond::Sleet;
    }
    double swing = (!w.days.empty()) ? w.days[0].tmax - w.days[0].tmin : 0;
    std::vector<std::string> alerts;
    auto addAlert = [&](const char* s) {
        if (std::find(alerts.begin(), alerts.end(), s) == alerts.end()) alerts.push_back(s);
    };
    if (thunder) addAlert("【雷暴预警】强对流天气，注意防雷防触电！");
    if (tmax >= 37) addAlert("【高温预警】极端炎热，避免室外作业！");
    if (tmin <= -10) addAlert("【寒潮预警】气温极低，注意水管防冻！");
    if (windMax > 24.5) addAlert("【狂风预警】极度危险，请留在室内！");
    else if (windMax > 17.2) addAlert("【大风预警】风力强劲，注意高空坠物！");
    if (rain > 16) addAlert("【暴雨预警】短时强降水，极易引起积水！");
    if (tmin < 0 && rain > 5) addAlert("【暴雪预警】道路结冰注意防滑！");
    if (w.visibilityKm < 0.5) addAlert("【大雾预警】能见度极低，驾车需开雾灯！");
    if (w.aqi > 200) addAlert("【重度污染预警】空气极差，请紧闭门窗！");
    if (humidityMin < 20) addAlert("【极度干燥预警】静电高发，注意补水与火险！");
    for (size_t i = 0; i < alerts.size(); i++) {
        if (i) w.alertText += "; ";
        w.alertText += alerts[i];
    }
    auto addAdvice = [&](const char* s) {
        if (std::find(w.advices.begin(), w.advices.end(), s) == w.advices.end()) w.advices.push_back(s);
    };
    bool rainy = rain > 0.05 || snow;
    if (rain > 0.05) addAdvice("出门记得带伞");
    if (tmin < 10) addAdvice("建议穿厚外套");
    else if (tmax > 28) addAdvice("天气炎热防暑");
    if (swing > 10) addAdvice("早晚温差大");
    if (uvMax > 5) addAdvice("紫外线较强，注意防晒");
    if (w.aqi > 100) addAdvice("空气质量差，建议戴口罩");
    if (w.aqi <= 100 && !rainy) addAdvice("适宜户外运动");
    if (!rainy) addAdvice("适宜洗车");
}

bool fetchLatestRelease(const std::string& url, std::string& tag, std::string& page, std::string& err) {
    tag.clear();
    page.clear();
    std::wstring wurl(url.begin(), url.end());
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { err = "bad url"; return false; }
    HINTERNET session = WinHttpOpen(L"AuraWeather", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { err = "winhttp open"; return false; }
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 12000);
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) { WinHttpCloseHandle(session); err = "connect"; return false; }
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); err = "request"; return false; }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    WinHttpAddRequestHeaders(request, L"User-Agent: AuraWeather", (DWORD)-1L,
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0, sz = sizeof(status);
    if (ok) WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    std::string body;
    if (ok && status == 200) {
        DWORD avail = 0;
        do {
            avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail)) break;
            if (!avail) break;
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), avail, &read) || !read) break;
            chunk.resize(read);
            body += chunk;
        } while (avail > 0);
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (status != 200 || body.empty()) {
        err = "http " + std::to_string(status);
        return false;
    }
    std::string perr;
    JsonValue root = JsonParser::parse(body, &perr);
    const JsonValue* t = root.get("tag_name");
    const JsonValue* h = root.get("html_url");
    if (t) tag = t->string("");
    if (h) page = h->string("");
    while (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V' || tag.front() == ' ')) tag.erase(tag.begin());
    if (tag.empty()) { err = "no tag"; return false; }
    return true;
}

bool fetchWeather(const Location& loc, WeatherData& out, std::string& err) {
    char url[512];
    snprintf(url, sizeof url,
             "https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=%.4f&lon=%.4f",
             loc.lat, loc.lon);
    HttpResult r = Http::get(url, 15000);
    if (!r.success()) {
        snprintf(url, sizeof url,
                 "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=%.4f&lon=%.4f",
                 loc.lat, loc.lon);
        r = Http::get(url, 15000);
    }
    bool ok = false;
    std::string perr;
    if (r.success() && !r.body.empty()) {
        WeatherData w = parseMetJson(r.body, loc, ok, perr);
        if (ok) {
            saveWeatherCache(loc, r.body);
            w.aqi = fetchAqi(loc);
            w.visibilityKm = w.now.cond == Cond::Fog ? 0.4 : 10.0;
            if (!w.days.empty()) {
                int idx = (int)std::floor(w.days[0].moonPhase * 8.0 + 0.5);
                w.moonPhaseIndex = ((idx % 8) + 8) % 8;
            }
            buildInsights(w);
            out = std::move(w);
            err.clear();
            return true;
        }
    }
    std::string cached = loadWeatherCache(loc);
    if (!cached.empty()) {
        WeatherData w = parseMetJson(cached, loc, ok, perr);
        if (ok) {
            w.stale = true;
            w.provider = "MET Norway · 离线缓存";
            w.visibilityKm = w.now.cond == Cond::Fog ? 0.4 : 10.0;
            if (!w.days.empty()) {
                int idx = (int)std::floor(w.days[0].moonPhase * 8.0 + 0.5);
                w.moonPhaseIndex = ((idx % 8) + 8) % 8;
            }
            buildInsights(w);
            out = std::move(w);
            err = r.error.empty() ? "网络不可用，显示上次缓存" : ("网络不可用（" + r.error + "），显示上次缓存");
            return true;
        }
    }
    err = r.error.empty() ? "天气服务暂时不可用" : r.error;
    if (r.status) err += " (HTTP " + std::to_string(r.status) + ")";
    return false;
}

bool loadCachedWeather(const Location& loc, WeatherData& out, std::string& err) {
    std::string cached = loadWeatherCache(loc);
    if (cached.empty()) { err = "没有本地缓存"; return false; }
    bool ok = false;
    WeatherData w = parseMetJson(cached, loc, ok, err);
    if (!ok) return false;
    w.stale = true;
    w.provider = "MET Norway · 离线缓存";
    w.visibilityKm = w.now.cond == Cond::Fog ? 0.4 : 10.0;
    if (!w.days.empty()) {
        int idx = (int)std::floor(w.days[0].moonPhase * 8.0 + 0.5);
        w.moonPhaseIndex = ((idx % 8) + 8) % 8;
    }
    buildInsights(w);
    out = std::move(w);
    return true;
}

static bool parseCoordQuery(const std::string& q, SearchResult& out) {
    auto parts = splitString(q, ',');
    if (parts.size() != 2) return false;
    try {
        double lat = std::stod(trim(parts[0]));
        double lon = std::stod(trim(parts[1]));
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
        out.name = "自定义位置";
        out.admin = "";
        out.country = "";
        out.lat = lat; out.lon = lon;
        out.timezone = guessTimezone(lat, lon);
        return true;
    } catch (...) { return false; }
}

static bool geocodeOpenMeteo(const std::string& query, std::vector<SearchResult>& out, std::string& err) {
    std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(query) +
                      "&count=8&language=zh&format=json";
    HttpResult r = Http::get(url, 22000);
    if (!r.success()) { err = r.error.empty() ? "搜索服务不可用" : r.error; return false; }
    JsonValue root = JsonParser::parse(r.body, &err);
    if (!root.isObj()) return false;
    const JsonValue* results = root.get("results");
    if (!results || !results->isArr()) { err = "未找到城市"; return false; }
    for (const auto& e : results->arr) {
        SearchResult s;
        s.name = jstr(&e, "name");
        s.admin = jstr(&e, "admin1");
        s.country = jstr(&e, "country");
        s.timezone = jstr(&e, "timezone");
        s.lat = jnum(&e, "latitude");
        s.lon = jnum(&e, "longitude");
        s.population = (long long)jnum(&e, "population");
        if (s.name.empty()) continue;
        if (s.timezone.empty()) s.timezone = guessTimezone(s.lat, s.lon);
        bool dup = false;
        for (const auto& x : out)
            if (x.name == s.name && std::abs(x.lat - s.lat) < 0.01 && std::abs(x.lon - s.lon) < 0.01) dup = true;
        if (!dup) out.push_back(s);
        if (out.size() >= 8) break;
    }
    if (out.empty()) { err = "未找到城市"; return false; }
    return true;
}

static bool geocodeWttr(const std::string& query, std::vector<SearchResult>& out) {
    std::string url = "https://wttr.in/" + urlEncode(query) + "?format=j1";
    HttpResult r = Http::get(url, 22000);
    if (!r.success()) return false;
    std::string err;
    JsonValue root = JsonParser::parse(r.body, &err);
    if (!root.isObj()) return false;
    const JsonValue* na = root.get("nearest_area");
    if (!na || !na->isArr() || na->arr.empty()) return false;
    const JsonValue& a = na->arr[0];
    auto firstVal = [&](const char* key) -> std::string {
        const JsonValue* arr = a.get(key);
        if (!arr || !arr->isArr() || arr->arr.empty()) return "";
        const JsonValue* v = arr->arr[0].get("value");
        return v ? v->string() : "";
    };
    SearchResult s;
    s.name = firstVal("areaName");
    s.admin = firstVal("region");
    s.country = firstVal("country");
    s.lat = jnum(&a, "latitude");
    s.lon = jnum(&a, "longitude");
    s.timezone = guessTimezone(s.lat, s.lon);
    if (s.name.empty()) return false;
    out.push_back(s);
    return true;
}

bool geocodeCity(const std::string& query, std::vector<SearchResult>& out, std::string& err) {
    out.clear();
    std::string q = trim(query);
    if (q.empty()) { err = "请输入城市名称"; return false; }
    SearchResult coord;
    if (parseCoordQuery(q, coord)) { out.push_back(coord); return true; }
    std::string e1;
    if (geocodeOpenMeteo(q, out, e1)) return true;
    out.clear();
    if (geocodeWttr(q, out)) return true;
    err = e1.empty() ? "未找到匹配的城市" : e1;
    return false;
}

static bool localizeWithBigDataCloud(Location& out) {
    char url[512];
    snprintf(url, sizeof url,
             "https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=%.5f&longitude=%.5f&localityLanguage=zh",
             out.lat, out.lon);
    HttpResult r = Http::get(url, 6500);
    if (!r.success()) return false;
    std::string e;
    JsonValue root = JsonParser::parse(r.body, &e);
    if (!root.isObj()) return false;
    std::string city = jstr(&root, "city");
    std::string admin = jstr(&root, "principalSubdivision");
    std::string country = jstr(&root, "countryName");
    if (city.size() > 3 && city.compare(city.size() - 3, 3, "\xE5\xB8\x82") == 0)
        city.resize(city.size() - 3);
    if (!city.empty()) out.name = city;
    if (!admin.empty()) out.admin = admin;
    if (!country.empty()) out.country = (country == "中华人民共和国") ? "中国" : country;
    if (out.country.empty()) out.country = "中国";
    return true;
}

bool locateByIp(Location& out, std::string& err) {
    HttpResult r = Http::get("https://ipwho.is/", 7000);
    if (r.success()) {
        std::string e;
        JsonValue root = JsonParser::parse(r.body, &e);
        if (root.isObj() && root.get("success") && root.get("success")->boolean()) {
            out.name = jstr(&root, "city");
            out.admin = jstr(&root, "region");
            out.country = jstr(&root, "country");
            out.lat = jnum(&root, "latitude");
            out.lon = jnum(&root, "longitude");
            const JsonValue* tz = root.get("timezone");
            out.timezone = jstr(tz, "id");
            int off = (int)jnum(tz, "offset", 0);
            out.offset = off ? off : ianaOffset(out.timezone, (long long)time(nullptr));
            out.autoLoc = true;
            if (out.name.empty()) out.name = "当前位置";
            if (std::abs(out.lat) < 90 && std::abs(out.lon) <= 180) {
                localizeWithBigDataCloud(out);
                return true;
            }
        }
    }
    HttpResult r2 = Http::get("http://ip-api.com/json/?lang=zh-CN&fields=status,message,country,regionName,city,lat,lon,timezone", 5000);
    if (r2.success()) {
        std::string e;
        JsonValue root = JsonParser::parse(r2.body, &e);
        if (root.isObj() && jstr(&root, "status") == "success") {
            out.name = jstr(&root, "city");
            out.admin = jstr(&root, "regionName");
            out.country = jstr(&root, "country");
            out.timezone = jstr(&root, "timezone");
            out.lat = jnum(&root, "lat");
            out.lon = jnum(&root, "lon");
            out.autoLoc = true;
            if (out.name.empty()) out.name = "当前位置";
            out.offset = ianaOffset(out.timezone, (long long)time(nullptr));
            if (std::abs(out.lat) < 90 && std::abs(out.lon) <= 180) return true;
        }
    }
    err = r.error.empty() ? "定位服务不可用" : r.error;
    return false;
}


