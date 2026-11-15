#include "settings.h"
#include "services.h"
#include "json.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdio>

namespace fs = std::filesystem;

std::wstring executablePath() {
    wchar_t buf[32768] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(wchar_t)));
    if (n == 0) return L"";
    return std::wstring(buf, n);
}

std::wstring settingsPathW() {
    return appDataDirW() + L"\\settings.json";
}

static void writeLocation(std::ostringstream& o, const Location& l) {
    o << "{";
    o << "\"name\":\"" << jsonEscape(l.name) << "\",";
    o << "\"admin\":\"" << jsonEscape(l.admin) << "\",";
    o << "\"country\":\"" << jsonEscape(l.country) << "\",";
    o << "\"timezone\":\"" << jsonEscape(l.timezone) << "\",";
    o << "\"lat\":" << l.lat << ",";
    o << "\"lon\":" << l.lon << ",";
    o << "\"offset\":" << l.offset << ",";
    o << "\"auto\":" << (l.autoLoc ? "true" : "false");
    o << "}";
}

bool saveSettings(const Settings& s) {
    ensureDirectory(appDataDirW());
    std::ostringstream o;
    o << "{\n";
    o << "  \"version\":1,\n";
    o << "  \"dark\":" << (s.dark ? "true" : "false") << ",\n";
    o << "  \"celsius\":" << (s.celsius ? "true" : "false") << ",\n";
    o << "  \"startup\":" << (s.startup ? "true" : "false") << ",\n";
    o << "  \"notify\":" << (s.notify ? "true" : "false") << ",\n";
    o << "  \"trayClose\":" << (s.trayClose ? "true" : "false") << ",\n";
    o << "  \"autoRefresh\":" << (s.autoRefresh ? "true" : "false") << ",\n";
    o << "  \"animations\":" << (s.animations ? "true" : "false") << ",\n";
    o << "  \"refreshMinutes\":" << s.refreshMinutes << ",\n";
    o << "  \"window\":{\"x\":" << s.winX << ",\"y\":" << s.winY << ",\"w\":" << s.winW
      << ",\"h\":" << s.winH << ",\"max\":" << (s.maximized ? "true" : "false") << "},\n";
    o << "  \"current\":";
    if (s.hasCurrent) writeLocation(o, s.current);
    else o << "null";
    o << ",\n";
    o << "  \"currentAuto\":" << (s.currentAuto ? "true" : "false") << ",\n";
    o << "  \"saved\":[";
    for (size_t i = 0; i < s.saved.size(); i++) {
        if (i) o << ",";
        o << "\n    ";
        writeLocation(o, s.saved[i]);
    }
    if (!s.saved.empty()) o << "\n  ";
    o << "]\n}\n";

    fs::path p(settingsPathW());
    fs::path tmp = p;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        std::string text = o.str();
        f.write(text.data(), (std::streamsize)text.size());
        if (!f.good()) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
    }
    return !ec;
}

static Location readLocation(const JsonValue* o) {
    Location l;
    if (!o || !o->isObj()) return l;
    const JsonValue* v;
    if ((v = o->get("name"))) l.name = v->string();
    if ((v = o->get("admin"))) l.admin = v->string();
    if ((v = o->get("country"))) l.country = v->string();
    if ((v = o->get("timezone"))) l.timezone = v->string();
    if ((v = o->get("lat"))) l.lat = v->number();
    if ((v = o->get("lon"))) l.lon = v->number();
    if ((v = o->get("offset"))) l.offset = (int)v->number();
    if ((v = o->get("auto"))) l.autoLoc = v->boolean();
    return l;
}

bool loadSettings(Settings& s, std::string& err) {
    std::ifstream f(fs::path(settingsPathW()), std::ios::binary);
    if (!f) { err = "首次运行"; return false; }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    if (text.empty()) { err = "设置文件为空"; return false; }
    JsonValue root = JsonParser::parse(text, &err);
    if (!root.isObj()) return false;
    const JsonValue* v;
    if ((v = root.get("dark"))) s.dark = v->boolean(true);
    else if ((v = root.get("glass"))) s.dark = v->boolean(true);
    if ((v = root.get("celsius"))) s.celsius = v->boolean(true);
    if ((v = root.get("startup"))) s.startup = v->boolean(false);
    if ((v = root.get("notify"))) s.notify = v->boolean(true);
    if ((v = root.get("trayClose"))) s.trayClose = v->boolean(true);
    if ((v = root.get("autoRefresh"))) s.autoRefresh = v->boolean(true);
    if ((v = root.get("animations"))) s.animations = v->boolean(false);
    if ((v = root.get("refreshMinutes"))) s.refreshMinutes = (int)v->number(10);
    if ((v = root.get("currentAuto"))) s.currentAuto = v->boolean(true);
    if ((v = root.get("window"))) {
        const JsonValue* w = v;
        const JsonValue* x;
        if ((x = w->get("x"))) s.winX = (int)x->number(-1);
        if ((x = w->get("y"))) s.winY = (int)x->number(-1);
        if ((x = w->get("w"))) s.winW = (int)x->number(1280);
        if ((x = w->get("h"))) s.winH = (int)x->number(840);
        if ((x = w->get("max"))) s.maximized = x->boolean(false);
    }
    if ((v = root.get("current")) && v->isObj()) {
        s.current = readLocation(v);
        s.hasCurrent = !s.current.name.empty();
    }
    if ((v = root.get("saved")) && v->isArr()) {
        s.saved.clear();
        for (const auto& e : v->arr) {
            Location l = readLocation(&e);
            if (!l.name.empty()) s.saved.push_back(l);
            if (s.saved.size() >= 12) break;
        }
    }
    if (s.refreshMinutes < 5) s.refreshMinutes = 10;
    if (s.refreshMinutes > 120) s.refreshMinutes = 10;
    if (s.winW < 900) s.winW = 1280;
    if (s.winH < 740) s.winH = 840;
    return true;
}

bool isStartupEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    wchar_t buf[32768] = {};
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    LSTATUS st = RegQueryValueExW(key, L"AuraWeather", nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(key);
    return st == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

bool setStartupEnabled(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    bool ok = false;
    if (enabled) {
        std::wstring cmd = L"\"" + executablePath() + L"\" --background";
        LSTATUS st = RegSetValueExW(key, L"AuraWeather", 0, REG_SZ, (const BYTE*)cmd.c_str(),
                                    (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
        ok = (st == ERROR_SUCCESS);
    } else {
        LSTATUS st = RegDeleteValueW(key, L"AuraWeather");
        ok = (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(key);
    return ok;
}


