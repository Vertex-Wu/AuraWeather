#pragma once

#include "common.h"
#include <string>
#include <vector>

struct AdviceItem {
    std::wstring icon;   // Segoe MDL2 Assets glyph
    std::wstring text;   // short Chinese advice
};

struct AppState {
    HINSTANCE hInst = nullptr;
    HWND hwnd = nullptr;
    HWND hSearch = nullptr;

    Settings settings;
    WeatherData weather;
    Location pendingLocation;
    bool hasWeather = false;
    bool updating = false;
    bool locating = false;
    int weatherGen = 0;
    int ipGen = 0;
    long long lastAttempt = 0;
    long long nextRefresh = 0;
    std::wstring errorText;
    std::vector<AdviceItem> advice;
    std::wstring noticeText;
    long long noticeUntil = 0;

    int page = 0; // 0 overview, 1 hourly, 2 ten-day, 3 satellite
    float mouseX = -1, mouseY = -1;
    int hoverSig = -777777;
    int drawnHoverSig = -777777;
    long long hoverDue = 0;
    bool mouseDown = false;
    float downX = 0, downY = 0;
    bool draggingWindow = false;
    RECT dragRect = {};
    POINT dragCursor = {};

    // search
    std::wstring searchQuery;
    std::vector<SearchResult> searchResults;
    bool searchOpen = false;
    bool searchLoading = false;
    bool searchNoResult = false;
    int searchGen = 0;
    int searchSelected = 0;
    long long searchDebounce = 0;
    std::string lastGeoQuery;

    // settings overlay
    bool settingsOpen = false;

    // map
    double mapLat = 39.9042, mapLon = 116.4074;
    int mapZoom = 4;
    int mapDay = 1;
    bool mapDragging = false;
    DWORD lastMapPaint = 0;
    float mapDragX = 0, mapDragY = 0;
    double mapDragLat = 0, mapDragLon = 0;
    bool mapUserMoved = false;

    // sidebar
    int hoverSidebar = -1;
    int sidebarScroll = 0;

    // animation / timers
    double animTime = 0;
    DWORD lastAnimTick = 0;
    long long lastClockTick = 0;

    // feedback
    std::wstring toast;
    long long toastUntil = 0;

    // window size in logical pixels
    int W = 1280, H = 860;
    float dpi = 1.0f;

    // screenshot / test mode
    bool captureMode = false;
    std::wstring capturePath;
    DWORD captureAt = 0;
    int capturePage = -1;
    int captureTheme = -1;
    int autoPick = -1;

    bool trayHintShown = false;
    bool firstLoad = true;
    bool hasNotifiedRain = false;
    bool startedInBackground = false;

    void toastMsg(const std::wstring& s, int ms = 3200) {
        toast = s;
        toastUntil = (long long)GetTickCount64() + ms;
    }
    bool isAutoCurrent() const {
        return settings.hasCurrent && settings.current.autoLoc;
    }
    int activeLocationIndex() const {
        if (!settings.hasCurrent) return -1;
        if (settings.current.autoLoc) return 0;
        for (size_t i = 0; i < settings.saved.size(); i++) {
            if (settings.saved[i].key() == settings.current.key()) return (int)i + 1;
        }
        return -1;
    }
};

