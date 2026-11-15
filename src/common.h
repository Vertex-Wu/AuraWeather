#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum class Cond {
    Clear, Fair, Partly, Cloudy, Fog, Drizzle, Rain, HeavyRain,
    Showers, Snow, Sleet, Thunder
};

struct Location {
    std::string name;
    std::string admin;
    std::string country;
    std::string timezone;
    double lat = 0.0;
    double lon = 0.0;
    int offset = 0;          // fallback UTC offset (seconds)
    bool autoLoc = false;

    std::string key() const;
};

struct WeatherNow {
    double temp = 0, feels = 0, humidity = 0, pressure = 0, cloud = 0;
    double wind = 0, windDir = 0, dew = 0, uv = 0, precip = 0;
    Cond cond = Cond::Cloudy;
    bool isDay = true;
    std::string symbol;
};

struct HourItem {
    long long utc = 0;
    double temp = 0, feels = 0, precip = 0, humidity = 0, wind = 0;
    double windDir = 0, cloud = 0, uv = 0;
    Cond cond = Cond::Cloudy;
    bool isDay = true;
    std::string symbol;
};

struct DayItem {
    long long utcMidnight = 0;
    int y = 1970, m = 1, d = 1;
    double tmax = 0, tmin = 0, precip = 0, windMax = 0, uvMax = 0;
    Cond cond = Cond::Cloudy;
    std::string symbol;
    std::string sunrise;
    std::string sunset;
    double moonPhase = 0;
};

struct WeatherData {
    Location loc;
    WeatherNow now;
    std::vector<HourItem> hours;
    std::vector<DayItem> days;
    long long updatedUtc = 0;
    bool stale = false;
    double aqi = 0;          // US AQI, 0 = unavailable
    double visibilityKm = 10;
    int moonPhaseIndex = 0;  // 0 new, 1 waxing crescent ... 4 full ... 7 waning crescent
    std::string provider = "MET Norway";
};

struct SearchResult {
    std::string name;
    std::string admin;
    std::string country;
    std::string timezone;
    double lat = 0, lon = 0;
    long long population = 0;
};

struct Settings {
    bool dark = true;
    bool celsius = true;
    bool startup = false;
    bool notify = true;
    bool trayClose = true;
    bool autoRefresh = true;
    bool animations = false;
    int refreshMinutes = 10;
    std::vector<Location> saved;
    Location current;
    bool hasCurrent = false;
    bool currentAuto = true;
    int winX = -1, winY = -1, winW = 1280, winH = 840;
    bool maximized = false;
};

struct WeatherMsg {
    int gen = 0;
    bool ok = false;
    WeatherData data;
    std::string error;
};

struct GeoMsg {
    int gen = 0;
    bool ok = false;
    std::string query;
    std::vector<SearchResult> results;
    std::string error;
};

struct IpMsg {
    bool ok = false;
    Location loc;
    std::string error;
};

struct TileMsg {
    std::string key;
    int status = 0;
    std::string bytes;
};

#define WM_APP_WEATHER (WM_APP + 11)
#define WM_APP_GEO     (WM_APP + 12)
#define WM_APP_TILE    (WM_APP + 13)
#define WM_APP_IP      (WM_APP + 14)

#define TIMER_ANIM     1
#define TIMER_REFRESH  2
#define TIMER_CLOCK    3
#define TIMER_DEBOUNCE 4

// ---------- UTF-8 / UTF-16 ----------
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
std::string urlEncode(const std::string& s);
std::string jsonEscape(const std::string& s);
std::string trim(const std::string& s);
std::string lowerAscii(std::string s);
std::vector<std::string> splitString(const std::string& s, char sep);

// ---------- time ----------
long long daysFromCivil(int y, unsigned m, unsigned d);
void civilFromDays(long long z, int& y, unsigned& m, unsigned& d);
long long parseIsoUtc(const std::string& s);
std::string isoDate(long long utc);
std::string isoDateFromCivil(int y, int m, int d);
struct CivilTime { int y = 1970, mo = 1, d = 1, h = 0, mi = 0, s = 0; int wd = 0; };
CivilTime civilLocal(long long utc, int offsetSec);
long long localMidnightUtc(int y, int m, int d, int offsetSec);
std::wstring twoDigits(int v);
std::wstring weekdayCN(int wd);
std::wstring formatHM(long long utc, int offsetSec);
std::wstring formatDateCN(long long utc, int offsetSec, bool withWeekday = true);
std::wstring relativeUpdate(long long updatedUtc);
int ianaOffset(const std::string& zone, long long utc);
std::string guessTimezone(double lat, double lon);

// ---------- weather helpers ----------
Cond condFromSymbol(const std::string& symbol);
std::wstring condName(Cond c);
std::wstring windDirName(double deg);
bool symbolIsNight(const std::string& s);
double cToF(double c);
std::wstring tempText(double celsius, bool useC, bool withUnit = true);

// solar
bool solarTimes(long long localMidnightUtc, double lat, double lon, int offsetSec,
                int& sunriseMin, int& sunsetMin, bool& polarDay, bool& polarNight);
std::wstring moonPhaseName(double phase);
double moonPhaseFraction(long long utc);

