/*
 * The port's settings (settings.h): a map of keys to values, read from and
 * written to one text file. Lines are `key=value`; anything else (comments,
 * blank lines) is left out when the file is written again.
 */

#include "ps2host/settings.h"

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <map>
#include <mutex>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace ps2host {

namespace {

std::mutex g_lock;
std::map<std::string, std::string> g_values;
std::map<std::string, std::pair<std::string, std::string>> g_overrides;
std::map<std::string, std::string> g_effective;
std::string g_path;
std::string g_error;

std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

bool Write() {
    if (g_path.empty()) return true;
    const auto path = std::filesystem::u8path(g_path);
    auto temp = path;
#ifdef _WIN32
    temp += ".tmp." + std::to_string(GetCurrentProcessId());
#else
    temp += ".tmp";
#endif
    bool done = false;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << "# The PC port's settings (F9).\n";
        for (const auto& [key, value] : g_values) out << key << '=' << value << '\n';
        out.flush();
        done = bool(out);
        out.close();
        done = done && !out.fail();
    }
    if (done) {
#ifdef _WIN32
        done = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code ec;
        std::filesystem::rename(temp, path, ec);
        done = !ec;
#endif
    }
    if (!done) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        g_error = "Cannot save PC settings";
        std::fprintf(stderr, "[settings] cannot save %s\n", g_path.c_str());
    } else g_error.clear();
    return done;
}

}  // namespace

void LoadSettings(const std::string& path) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_path = path;
    g_values.clear();
    g_overrides.clear();
    g_effective.clear();
    g_error.clear();
    std::ifstream in(std::filesystem::u8path(path));
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        line = Trim(line);
        const auto equals = line.find('=');
        if (line.empty() || line[0] == '#' || equals == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, equals));
        if (!key.empty()) g_values[key] = Trim(line.substr(equals + 1));
    }
}

const std::string& SettingsPath() { return g_path; }

std::string Setting(const std::string& key, const std::string& fallback) {
    std::lock_guard<std::mutex> hold(g_lock);
    const auto override = g_overrides.find(key);
    if (override != g_overrides.end()) return override->second.first;
    const auto found = g_values.find(key);
    return found == g_values.end() ? fallback : found->second;
}

bool SettingOn(const std::string& key, bool fallback) {
    const std::string value = Setting(key, fallback ? "on" : "off");
    return value == "on" || value == "1" || value == "true" || value == "yes";
}

void SetSetting(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_overrides.count(key)) return;
    if (g_values.count(key) && g_values[key] == value) return;
    const auto found = g_values.find(key);
    const bool existed = found != g_values.end();
    const std::string old = existed ? found->second : "";
    g_values[key] = value;
    if (!Write()) {
        if (existed) g_values[key] = old;
        else g_values.erase(key);
    }
}

void SetSettingOn(const std::string& key, bool on) { SetSetting(key, on ? "on" : "off"); }

void RunSetting(const std::string& key, const std::string& value) {
    OverrideSetting(key, value, "CLI");
}

void OverrideSetting(const std::string& key, const std::string& value, const std::string& source) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_overrides[key] = {value, source};
}

std::string SettingOverride(const std::string& key) {
    std::lock_guard<std::mutex> hold(g_lock);
    const auto found = g_overrides.find(key);
    return found == g_overrides.end() ? "" : found->second.second;
}

std::string SavedSetting(const std::string& key, const std::string& fallback) {
    std::lock_guard<std::mutex> hold(g_lock);
    const auto found = g_values.find(key);
    return found == g_values.end() ? fallback : found->second;
}

void SetEffectiveSetting(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> hold(g_lock);
    g_effective[key] = value;
}

std::string EffectiveSetting(const std::string& key, const std::string& fallback) {
    std::lock_guard<std::mutex> hold(g_lock);
    const auto found = g_effective.find(key);
    return found == g_effective.end() ? fallback : found->second;
}

std::string SettingsError() {
    std::lock_guard<std::mutex> hold(g_lock);
    return g_error;
}

}  // namespace ps2host
