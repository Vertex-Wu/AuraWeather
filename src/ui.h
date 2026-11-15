#pragma once

#include "state.h"
#include <windows.h>
#include <string>

namespace UI {
void init(HWND hwnd);
void shutdown();
void render(HDC hdc, AppState& s);
bool saveScreenshot(AppState& s, const std::wstring& path);
void layoutSearchEdit(AppState& s);
void onTile(const TileMsg& msg);
void clearTiles();
bool mouseMove(AppState& s, float x, float y);
bool mouseDown(AppState& s, float x, float y);
bool mouseUp(AppState& s, float x, float y);
bool mouseDoubleClick(AppState& s, float x, float y);
bool mouseWheel(AppState& s, float x, float y, int delta);
bool keyDown(AppState& s, WPARAM key);
bool interactive(const AppState& s, float x, float y);
COLORREF searchBg(const AppState& s);
COLORREF searchFg(const AppState& s);
}

