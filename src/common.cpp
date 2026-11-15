#include "common.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <map>

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') o.push_back((char)c);
        else { o.push_back('%'); o.push_back(hex[c >> 4]); o.push_back(hex[c & 15]); }
    }
    return o;
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else o.push_back((char)c);
        }
    }
    return o;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    return s.substr(a, b - a);
}

std::string lowerAscii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

std::vector<std::string> splitString(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t a = 0;
    while (true) {
        size_t b = s.find(sep, a);
        if (b == std::string::npos) { out.push_back(s.substr(a)); break; }
        out.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return out;
}

std::string Location::key() const {
    char b[64];
    snprintf(b, sizeof b, "%.4f,%.4f", lat, lon);
    return b;
}

long long daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (long long)era * 146097LL + (long long)doe - 719468LL;
}

void civilFromDays(long long z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)(yoe + era * 400);
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
}

long long parseIsoUtc(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 5) {
        if (sscanf(s.c_str(), "%d-%d-%d", &y, &mo, &d) == 3)
            return daysFromCivil(y, mo, d) * 86400LL;
        return 0;
    }
    long long t = daysFromCivil(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL + sec;
    // explicit offsets (rare for MET, but support them)
    size_t zpos = s.find_last_of("+-");
    if (zpos != std::string::npos && zpos > 10 && s.back() != 'Z') {
        int oh = 0, om = 0;
        if (sscanf(s.c_str() + zpos + 1, "%d:%d", &oh, &om) >= 1) {
            int sign = s[zpos] == '-' ? -1 : 1;
            t -= sign * (oh * 3600 + om * 60);
        }
    }
    return t;
}

std::string isoDate(long long utc) {
    long long days = utc / 86400;
    int y; unsigned m, d;
    civilFromDays(days, y, m, d);
    char b[32];
    snprintf(b, sizeof b, "%04d-%02u-%02u", y, m, d);
    return b;
}

std::string isoDateFromCivil(int y, int m, int d) {
    char b[32];
    snprintf(b, sizeof b, "%04d-%02d-%02d", y, m, d);
    return b;
}

CivilTime civilLocal(long long utc, int offsetSec) {
    long long t = utc + offsetSec;
    long long days = t / 86400;
    long long rem = t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    CivilTime c;
    unsigned mm = 1, dd = 1;
    civilFromDays(days, c.y, mm, dd);
    c.mo = (int)mm;
    c.d = (int)dd;
    c.h = (int)(rem / 3600);
    c.mi = (int)((rem % 3600) / 60);
    c.s = (int)(rem % 60);
    c.wd = (int)((days + 4) % 7);
    if (c.wd < 0) c.wd += 7;
    return c;
}

long long localMidnightUtc(int y, int m, int d, int offsetSec) {
    return daysFromCivil(y, (unsigned)m, (unsigned)d) * 86400LL - offsetSec;
}

std::wstring twoDigits(int v) {
    wchar_t b[8];
    swprintf(b, 8, L"%02d", v);
    return b;
}

std::wstring weekdayCN(int wd) {
    static const wchar_t* names[] = { L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六" };
    if (wd < 0 || wd > 6) wd = 0;
    return names[wd];
}

std::wstring formatHM(long long utc, int offsetSec) {
    CivilTime c = civilLocal(utc, offsetSec);
    return twoDigits(c.h) + L":" + twoDigits(c.mi);
}

std::wstring formatDateCN(long long utc, int offsetSec, bool withWeekday) {
    CivilTime c = civilLocal(utc, offsetSec);
    std::wstring s = std::to_wstring(c.mo) + L"月" + std::to_wstring(c.d) + L"日";
    if (withWeekday) s += L" " + weekdayCN(c.wd);
    return s;
}

std::wstring relativeUpdate(long long updatedUtc) {
    if (updatedUtc <= 0) return L"尚未更新";
    long long now = (long long)time(nullptr);
    long long diff = now - updatedUtc;
    if (diff < 0) diff = 0;
    if (diff < 60) return L"刚刚更新";
    if (diff < 3600) return std::to_wstring(diff / 60) + L" 分钟前更新";
    if (diff < 86400) return std::to_wstring(diff / 3600) + L" 小时前更新";
    return std::to_wstring(diff / 86400) + L" 天前更新";
}

// ---------------- time zones ----------------
struct ZoneDef { const char* name; int stdMin; int rule; };
// rule: 0 none, 1 EU, 2 US, 3 AU, 4 NZ, 5 Chile, 6 LordHowe(simplified AU)
static const ZoneDef kZones[] = {
    {"Asia/Shanghai",480,0},{"Asia/Chongqing",480,0},{"Asia/Harbin",480,0},{"Asia/Urumqi",360,0},
    {"Asia/Tokyo",540,0},{"Asia/Seoul",540,0},{"Asia/Pyongyang",540,0},{"Asia/Hong_Kong",480,0},
    {"Asia/Macau",480,0},{"Asia/Taipei",480,0},{"Asia/Singapore",480,0},{"Asia/Kuala_Lumpur",480,0},
    {"Asia/Bangkok",420,0},{"Asia/Jakarta",420,0},{"Asia/Manila",480,0},{"Asia/Ho_Chi_Minh",420,0},
    {"Asia/Yangon",390,0},{"Asia/Dhaka",360,0},{"Asia/Kolkata",330,0},{"Asia/Calcutta",330,0},
    {"Asia/Kathmandu",345,0},{"Asia/Karachi",300,0},{"Asia/Kabul",270,0},{"Asia/Dubai",240,0},
    {"Asia/Muscat",240,0},{"Asia/Riyadh",180,0},{"Asia/Tehran",210,0},{"Asia/Baghdad",180,0},
    {"Asia/Jerusalem",120,0},{"Asia/Beirut",120,0},{"Asia/Amman",180,0},{"Asia/Damascus",180,0},
    {"Asia/Istanbul",180,0},{"Europe/Istanbul",180,0},{"Europe/London",0,1},{"Europe/Dublin",0,1},
    {"Europe/Lisbon",0,1},{"Europe/Paris",60,1},{"Europe/Berlin",60,1},{"Europe/Madrid",60,1},
    {"Europe/Rome",60,1},{"Europe/Amsterdam",60,1},{"Europe/Brussels",60,1},{"Europe/Vienna",60,1},
    {"Europe/Zurich",60,1},{"Europe/Stockholm",60,1},{"Europe/Oslo",60,1},{"Europe/Copenhagen",60,1},
    {"Europe/Helsinki",120,1},{"Europe/Warsaw",60,1},{"Europe/Prague",60,1},{"Europe/Budapest",60,1},
    {"Europe/Athens",120,1},{"Europe/Bucharest",120,1},{"Europe/Kiev",120,1},{"Europe/Kyiv",120,1},
    {"Europe/Moscow",180,0},{"Europe/Minsk",180,0},{"Europe/Riga",120,1},{"Europe/Vilnius",120,1},
    {"Europe/Tallinn",120,1},{"Europe/Belgrade",60,1},{"Europe/Sofia",120,1},{"Europe/Zagreb",60,1},
    {"Europe/Ljubljana",60,1},{"Europe/Sarajevo",60,1},{"Europe/Skopje",60,1},{"Europe/Tirane",60,1},
    {"Europe/Valletta",60,1},{"Europe/Luxembourg",60,1},{"Europe/Monaco",60,1},{"Europe/Andorra",60,1},
    {"Europe/Reykjavik",0,0},{"Atlantic/Reykjavik",0,0},{"Africa/Cairo",120,0},
    {"Africa/Johannesburg",120,0},{"Africa/Lagos",60,0},{"Africa/Nairobi",180,0},
    {"Africa/Addis_Ababa",180,0},{"Africa/Casablanca",60,0},{"Africa/Algiers",60,0},
    {"Africa/Tunis",60,0},{"Africa/Tripoli",120,0},{"Africa/Accra",0,0},{"Africa/Dakar",0,0},
    {"America/New_York",-300,2},{"America/Detroit",-300,2},{"America/Toronto",-300,2},
    {"America/Montreal",-300,2},{"America/Chicago",-360,2},{"America/Winnipeg",-360,2},
    {"America/Denver",-420,2},{"America/Edmonton",-420,2},{"America/Phoenix",-420,0},
    {"America/Los_Angeles",-480,2},{"America/Vancouver",-480,2},{"America/Anchorage",-540,2},
    {"America/Halifax",-240,2},{"America/St_Johns",-210,2},{"America/Mexico_City",-360,0},
    {"America/Bogota",-300,0},{"America/Lima",-300,0},{"America/Santiago",-240,5},
    {"America/Sao_Paulo",-180,0},{"America/Argentina/Buenos_Aires",-180,0},
    {"America/Montevideo",-180,0},{"America/Caracas",-240,0},{"America/La_Paz",-240,0},
    {"America/Asuncion",-240,0},{"America/Panama",-300,0},{"America/Costa_Rica",-360,0},
    {"America/Havana",-300,2},{"America/Santo_Domingo",-240,0},{"America/Puerto_Rico",-240,0},
    {"Pacific/Honolulu",-600,0},{"Pacific/Auckland",720,4},{"Pacific/Fiji",720,0},
    {"Pacific/Guam",600,0},{"Pacific/Port_Moresby",600,0},{"Australia/Sydney",600,3},
    {"Australia/Melbourne",600,3},{"Australia/Brisbane",600,0},{"Australia/Perth",480,0},
    {"Australia/Adelaide",570,3},{"Australia/Darwin",570,0},{"Australia/Hobart",600,3},
    {"Australia/Canberra",600,3},{"Indian/Maldives",300,0},{"Indian/Mauritius",240,0},
    {"Indian/Reunion",240,0},{"UTC",0,0},{"Etc/UTC",0,0},{"GMT",0,0}
};

static int lastSunday(int y, int m) {
    int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    int d = dim[m - 1] + (m == 2 && leap ? 1 : 0);
    long long days = daysFromCivil(y, (unsigned)m, (unsigned)d);
    int wd = (int)((days + 4) % 7);
    if (wd < 0) wd += 7;
    return d - wd;
}
static int nthSunday(int y, int m, int n) {
    long long days = daysFromCivil(y, (unsigned)m, 1);
    int wd = (int)((days + 4) % 7);
    if (wd < 0) wd += 7;
    int first = 1 + ((7 - wd) % 7);
    return first + (n - 1) * 7;
}

static bool inPeriod(long long utc, long long a, long long b) {
    if (a <= b) return utc >= a && utc < b;
    return utc >= a || utc < b;
}

static long long utcAtLocalDate(int y, int m, int d, int hour, int baseOffsetMin) {
    return daysFromCivil(y, (unsigned)m, (unsigned)d) * 86400LL + hour * 3600LL - baseOffsetMin * 60LL;
}

int ianaOffset(const std::string& zone, long long utc) {
    if (zone.empty()) return 0;
    const ZoneDef* z = nullptr;
    for (const auto& d : kZones) if (zone == d.name) { z = &d; break; }
    if (!z) {
        if (zone.find("Shanghai") != std::string::npos || zone.find("Beijing") != std::string::npos) return 28800;
        if (zone.find("Tokyo") != std::string::npos) return 32400;
        if (zone.rfind("UTC", 0) == 0 || zone.rfind("GMT", 0) == 0) {
            const char* p = zone.c_str() + 3;
            if (*p == '+' || *p == '-') {
                int sign = (*p == '-') ? -1 : 1;
                double h = atof(p + 1);
                return (int)std::lround(sign * h * 3600.0);
            }
            return 0;
        }
        return 0;
    }
    CivilTime c = civilLocal(utc, z->stdMin * 60);
    int y = c.y;
    if (z->rule == 0) return z->stdMin * 60;

    long long start = 0, end = 0;
    if (z->rule == 1) { // EU
        int sm = 3, em = 10;
        int sd = lastSunday(y, sm), ed = lastSunday(y, em);
        start = utcAtLocalDate(y, sm, sd, 1, 0); // 01:00 UTC
        end = utcAtLocalDate(y, em, ed, 1, 0);
    } else if (z->rule == 2) { // US/Canada approximations
        int sd = nthSunday(y, 3, 2), ed = nthSunday(y, 11, 1);
        int sm = 3, em = 11;
        start = utcAtLocalDate(y, sm, sd, 7, 0); // 2am EST = 07:00Z
        end = utcAtLocalDate(y, em, ed, 6, 0);   // 2am EDT = 06:00Z
    } else if (z->rule == 3) { // AU eastern
        int sd = nthSunday(y, 10, 1), ed = nthSunday(y, 4, 1);
        start = utcAtLocalDate(y, 10, sd, 16, 0); // 2am AEST = 16:00Z prev day
        end = utcAtLocalDate(y, 4, ed, 16, 0);    // 3am AEDT = 16:00Z prev day
    } else if (z->rule == 4) { // NZ
        int sd = lastSunday(y, 9), ed = nthSunday(y, 4, 1);
        start = utcAtLocalDate(y, 9, sd, 14, 0);
        end = utcAtLocalDate(y, 4, ed, 14, 0);
    } else if (z->rule == 5) { // Chile
        int sd = nthSunday(y, 9, 1), ed = nthSunday(y, 4, 1);
        start = utcAtLocalDate(y, 9, sd, 4, 0);
        end = utcAtLocalDate(y, 4, ed, 3, 0);
    }
    if (start && end && inPeriod(utc, start, end)) return (z->stdMin + 60) * 60;
    return z->stdMin * 60;
}

std::string guessTimezone(double lat, double lon) {
    (void)lat;
    double h = lon / 15.0;
    int off = (int)std::lround(h);
    if (off < -12) off = -12;
    if (off > 14) off = 14;
    char b[32];
    snprintf(b, sizeof b, "UTC%+d", off);
    return b;
}

// ---------------- weather text ----------------
Cond condFromSymbol(const std::string& symbol) {
    std::string s = lowerAscii(symbol);
    if (s.find("thunder") != std::string::npos) return Cond::Thunder;
    if (s.find("snow") != std::string::npos) return Cond::Snow;
    if (s.find("sleet") != std::string::npos) return Cond::Sleet;
    if (s.find("rainshowers") != std::string::npos || s.find("showers") != std::string::npos) return Cond::Showers;
    if (s.find("heavyrain") != std::string::npos) return Cond::HeavyRain;
    if (s.find("lightrain") != std::string::npos || s.find("drizzle") != std::string::npos) return Cond::Drizzle;
    if (s.find("rain") != std::string::npos) return Cond::Rain;
    if (s.find("fog") != std::string::npos) return Cond::Fog;
    if (s.find("partlycloudy") != std::string::npos) return Cond::Partly;
    if (s.find("cloudy") != std::string::npos) return Cond::Cloudy;
    if (s.find("fair") != std::string::npos) return Cond::Fair;
    if (s.find("clearsky") != std::string::npos || s.find("clear") != std::string::npos) return Cond::Clear;
    return Cond::Cloudy;
}

std::wstring condName(Cond c) {
    switch (c) {
    case Cond::Clear: return L"晴朗";
    case Cond::Fair: return L"晴间多云";
    case Cond::Partly: return L"局部多云";
    case Cond::Cloudy: return L"多云";
    case Cond::Fog: return L"雾";
    case Cond::Drizzle: return L"小雨";
    case Cond::Rain: return L"中雨";
    case Cond::HeavyRain: return L"大雨";
    case Cond::Showers: return L"阵雨";
    case Cond::Snow: return L"降雪";
    case Cond::Sleet: return L"雨夹雪";
    case Cond::Thunder: return L"雷阵雨";
    }
    return L"多云";
}

std::wstring windDirName(double deg) {
    static const wchar_t* names[] = { L"北", L"东北", L"东", L"东南", L"南", L"西南", L"西", L"西北" };
    int idx = (int)std::lround(deg / 45.0) % 8;
    if (idx < 0) idx += 8;
    return names[idx];
}

bool symbolIsNight(const std::string& s) {
    return s.find("_night") != std::string::npos;
}

double cToF(double c) { return c * 9.0 / 5.0 + 32.0; }

std::wstring tempText(double c, bool useC, bool withUnit) {
    int v = (int)std::lround(useC ? c : cToF(c));
    std::wstring s = std::to_wstring(v) + L"°";
    if (withUnit) s += useC ? L"C" : L"F";
    return s;
}

bool solarTimes(long long localMidnightUtc, double lat, double lon, int offsetSec,
                int& sunriseMin, int& sunsetMin, bool& polarDay, bool& polarNight) {
    polarDay = polarNight = false;
    CivilTime c = civilLocal(localMidnightUtc + 12 * 3600, offsetSec);
    int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = (c.y % 4 == 0 && (c.y % 100 != 0 || c.y % 400 == 0));
    int doy = 0;
    for (int i = 0; i < c.mo - 1; i++) doy += dim[i] + (i == 1 && leap ? 1 : 0);
    doy += c.d;
    double gamma = 2.0 * M_PI / 365.0 * (doy - 1 + 0.5);
    double eqtime = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma)
                    - 0.014615 * cos(2 * gamma) - 0.040849 * sin(2 * gamma));
    double decl = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma)
                - 0.006758 * cos(2 * gamma) + 0.000907 * sin(2 * gamma)
                - 0.002697 * cos(3 * gamma) + 0.00148 * sin(3 * gamma);
    double latr = lat * M_PI / 180.0;
    double cosH = cos(90.833 * M_PI / 180.0) / (cos(latr) * cos(decl)) - tan(latr) * tan(decl);
    if (cosH > 1.0) { polarNight = true; sunriseMin = sunsetMin = 0; return false; }
    if (cosH < -1.0) { polarDay = true; sunriseMin = sunsetMin = 0; return false; }
    double ha = acos(cosH) * 180.0 / M_PI;
    double sr = 720.0 - 4.0 * (lon + ha) - eqtime;
    double ss = 720.0 - 4.0 * (lon - ha) - eqtime;
    double localOff = offsetSec / 60.0;
    sr += localOff; ss += localOff;
    while (sr < 0) sr += 1440;
    while (sr >= 1440) sr -= 1440;
    while (ss < 0) ss += 1440;
    while (ss >= 1440) ss -= 1440;
    sunriseMin = (int)std::lround(sr);
    sunsetMin = (int)std::lround(ss);
    return true;
}

double moonPhaseFraction(long long utc) {
    const double synodic = 29.530588853 * 86400.0;
    const double ref = 947182440.0; // 2000-01-06 18:14 UTC new moon
    double p = fmod((double)(utc - (long long)ref) / synodic, 1.0);
    if (p < 0) p += 1.0;
    return p;
}

std::wstring moonPhaseName(double phase) {
    if (phase < 0.03 || phase >= 0.97) return L"新月";
    if (phase < 0.22) return L"娥眉月";
    if (phase < 0.28) return L"上弦月";
    if (phase < 0.47) return L"盈凸月";
    if (phase < 0.53) return L"满月";
    if (phase < 0.72) return L"亏凸月";
    if (phase < 0.78) return L"下弦月";
    return L"残月";
}

