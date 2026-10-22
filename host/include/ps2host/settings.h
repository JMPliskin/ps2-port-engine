/*
 * The port's own settings: the PC enhancements a player chooses (fullscreen,
 * ...), kept between runs in a small text file of `key=value` lines and
 * written whenever one changes. A game's hooks offer them in its own menus
 * (Burnout 3: port/overrides.cpp, "PC SETTINGS"); the host applies them.
 */

#ifndef PS2HOST_SETTINGS_H
#define PS2HOST_SETTINGS_H

#include <string>

namespace ps2host {

/* Reads the settings from `path` (none yet is no error) and keeps it as the
   file later changes go to. */
void LoadSettings(const std::string& path);
/* Where they are kept ("" before LoadSettings). */
const std::string& SettingsPath();

/* A setting's value, or `fallback` when it has none. */
std::string Setting(const std::string& key, const std::string& fallback = "");
bool SettingOn(const std::string& key, bool fallback = false);
/* Sets one and writes the file. */
void SetSetting(const std::string& key, const std::string& value);
void SetSettingOn(const std::string& key, bool on);
/* Runtime overrides take precedence over saved values, are locked against UI
   changes, and are never copied to disk by another setting's write. */
void RunSetting(const std::string& key, const std::string& value);
void OverrideSetting(const std::string& key, const std::string& value, const std::string& source);
std::string SettingOverride(const std::string& key); // "CLI", "env", or empty
std::string SavedSetting(const std::string& key, const std::string& fallback = "");
/* A startup-only consumer records what it actually applied. F9 may change the
   saved/requested value without changing this effective value. */
void SetEffectiveSetting(const std::string& key, const std::string& value);
std::string EffectiveSetting(const std::string& key, const std::string& fallback = "");
/* Last persistence failure, cleared by a successful write/load. Failed writes
   leave both the original file and the in-memory preference unchanged. */
std::string SettingsError();

}  // namespace ps2host

#endif  /* PS2HOST_SETTINGS_H */
