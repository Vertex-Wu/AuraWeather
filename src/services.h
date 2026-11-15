#pragma once

#include "common.h"
#include <string>
#include <vector>

bool fetchWeather(const Location& loc, WeatherData& out, std::string& err);
bool loadCachedWeather(const Location& loc, WeatherData& out, std::string& err);
bool geocodeCity(const std::string& query, std::vector<SearchResult>& out, std::string& err);
bool locateByIp(Location& out, std::string& err);
bool fetchLatestRelease(const std::string& url, std::string& tag, std::string& page, std::string& err);

std::wstring appDataDirW();
std::string appDataDirA();
std::wstring cacheDirW();
void ensureDirectory(const std::wstring& path);
std::string loadWeatherCache(const Location& loc);
void saveWeatherCache(const Location& loc, const std::string& body);
void clearCacheFiles();
std::string formatDurationAgo(long long utc);
std::wstring solarTimeText(int minutes, bool polarDay, bool polarNight);

