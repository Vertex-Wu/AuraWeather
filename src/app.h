#pragma once

#include "state.h"
#include <string>

void appRequestWeather(AppState& s);
void appLocate(AppState& s);
void appSelectLocation(AppState& s, const Location& loc, bool addToSaved);
void appSelectSearchResult(AppState& s, int index);
void appStartSearch(AppState& s);
void appEnqueueTile(AppState& s, const std::string& key, const std::string& url);
void appSaveSettings(AppState& s);
void appToggleTheme(AppState& s);
void appFocusSearch(AppState& s);
void appClearCache(AppState& s);
void appRefreshStartup(AppState& s);

