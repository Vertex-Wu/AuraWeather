#pragma once

#include "common.h"
#include <string>

std::wstring settingsPathW();
bool loadSettings(Settings& s, std::string& err);
bool saveSettings(const Settings& s);
bool isStartupEnabled();
bool setStartupEnabled(bool enabled);
std::wstring executablePath();

