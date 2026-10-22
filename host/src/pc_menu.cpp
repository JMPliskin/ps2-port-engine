/*
 * The PC settings panel: F9 (or L3 + R3 on a controller) over any game.
 *
 * A port can put its PC settings in the game's own menus (Burnout 3's main
 * menu has PC SETTINGS); this panel is the engine's, for every port: the
 * settings the host itself applies, changed while the game runs and saved
 * with the PC settings (settings.h). While it is open the game gets no input.
 *
 *   Up / Down          choose
 *   Left / Right       change (Enter or Cross too)
 *   F9, Esc, Circle    close
 */

#include <array>
#include <string>
#include <vector>

#include "ps2host/run.h"
#include "ps2host/settings.h"
#include "ps2host/texture_pack.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace ps2host {

namespace {

bool g_open = false;
bool g_releasing = false;                // closed, the game's input held back until the keys are let go
int g_row = 0;

struct Choice {
    const char* key;
    const char* label;
    std::vector<std::string> values;        // as the setting keeps them
    std::vector<std::string> shown;         // as the panel says them
    std::string (*get)();
    void (*set)(const std::string&);
    const char* game_label;                 // short and in capitals, for a game's own menu (PcSettings)
    std::vector<std::string> game_values;
    const char* help;
};

std::string OnOff(bool on) { return on ? "on" : "off"; }

const std::vector<Choice>& Choices() {
    static const std::vector<Choice> choices = {
        {"resolution", "Resolution", {"original", "2x", "3x", "4x", "display"},
         {"Original (640x448)", "2x", "3x", "4x", "The screen's"},
         [] { return Setting("resolution", "original"); },
         [](const std::string& v) { SetSetting("resolution", v); },
         "RESOLUTION", {"1X", "2X", "3X", "4X", "AUTO"}, "The resolution the game is drawn at."},
        {"aspect", "Aspect ratio", {"4:3", "16:9", "auto"}, {"4:3", "16:9", "Fill the window"},
         [] { return Setting("aspect", "4:3"); },
         [](const std::string& v) { SetWindowAspect(v); },
         "ASPECT", {"4:3", "16:9", "FILL"}, "The picture's shape: 4:3, 16:9 or the window's."},
        {"fullscreen", "Fullscreen", {"off", "on"}, {"Off", "On"},
         [] { return OnOff(WindowFullscreen()); },
         [](const std::string& v) { SetWindowFullscreen(v == "on"); },
         "FULLSCREEN", {"OFF", "ON"}, "Play in a window or on the whole screen."},
        {"vsync", "V-sync", {"off", "on"}, {"Off", "On"},
         [] { return OnOff(WindowVsync()); },
         [](const std::string& v) { SetWindowVsync(v == "on"); },
         "V-SYNC", {"OFF", "ON"}, "Wait for the screen's refresh: no tearing."},
        {"frame_pacing", "Frame pacing", {"on", "off"}, {"On", "Off"},
         [] { return OnOff(SettingOn("frame_pacing", FramePacingDefault())); },
         [](const std::string& v) { SetSettingOn("frame_pacing", v == "on"); },
         "PACING", {"ON", "OFF"}, "Frame pacing: pictures shown at an even pace."},
        {"fps_counter", "FPS counter (F1)", {"off", "on"}, {"Off", "On"},
         [] { return OnOff(SettingOn("fps_counter", false)); },
         [](const std::string& v) { SetSettingOn("fps_counter", v == "on"); },
         "FPS COUNTER", {"OFF", "ON"}, "Show presentation FPS. F1 also toggles it."},
        {"gpu_thread", "GPU Thread", {"off", "on"}, {"Off", "On"},
         [] { return OnOff(SettingOn("gpu_thread", true)); },
         [](const std::string& v) { SetSetting("gpu_thread", v); },
         "GPU THREAD", {"OFF", "ON"}, "Changes require restarting the game."},
        {"texture_pack", "Texture pack", {"on", "off"}, {"On", "Off"},
         [] { return OnOff(texture_pack::Enabled()); },
         [](const std::string& v) {
             SetSettingOn("texture_pack", v == "on");
             texture_pack::SetEnabled(SettingOn("texture_pack", texture_pack::Enabled()));
         },
         "TEXTURES", {"ON", "OFF"}, "The texture pack's textures, or the game's own."},
    };
    return choices;
}

// The rows shown: a texture pack's only when one is loaded; then Close.
std::vector<int> Rows() {
    std::vector<int> rows;
    const auto& c = Choices();
    for (int i = 0; i < int(c.size()); ++i)
        if (std::string(c[i].label) != "Texture pack" || texture_pack::Count()) rows.push_back(i);
    rows.push_back(-1);
    return rows;
}

std::string Value(const Choice& c, bool game = false) {
    const auto show = [&](const std::string& value) {
        for (std::size_t i = 0; i < c.values.size(); ++i)
            if (c.values[i] == value) return game ? c.game_values[i] : c.shown[i];
        return value;
    };
    const std::string requested = c.get();
    std::string value = show(requested);
    if (std::string(c.key) == "gpu_thread") {
        const std::string active = EffectiveSetting(c.key, requested);
        if (active != requested) value = show(active) + " -> " + value + " (restart)";
    }
    const std::string source = SettingOverride(c.key);
    if (!source.empty()) value += " [" + source + "]";
    return value;
}

void Draw(bool force = false) {
    std::vector<std::string> lines = {"PC SETTINGS   (F9 closes)"};
    const std::vector<int> rows = Rows();
    for (int r = 0; r < int(rows.size()); ++r) {
        std::string text;
        if (rows[r] < 0) {
            text = "Close";
        } else {
            const Choice& c = Choices()[rows[r]];
            const std::string shown = Value(c);
            text = std::string(c.label) + std::string(16 - std::min<std::size_t>(15, std::string(c.label).size()), ' ') +
                   "< " + shown + " >";
        }
        lines.push_back((r == g_row ? "> " : "  ") + text);
    }
    lines.push_back("");
    lines.push_back("Up/Down choose, Left/Right change");
    if (g_row >= 0 && g_row < int(rows.size()) && rows[g_row] >= 0)
        lines.push_back(Choices()[rows[g_row]].help);
    lines.push_back("[CLI]/[env]: fixed for this run");
    const std::string error = SettingsError();
    if (!error.empty()) lines.push_back(error + " - preference unchanged");
    static std::vector<std::string> shown;
    if (force || lines != shown) SetWindowOverlay(lines);     // drawn again only when it changes
    shown = lines;
}

void Change(int step) {
    const std::vector<int> rows = Rows();
    const int which = rows[std::size_t(g_row)];
    if (which < 0) {
        g_open = false;
        SetWindowOverlay({});
        return;
    }
    const Choice& c = Choices()[which];
    if (!SettingOverride(c.key).empty()) return;
    const std::string now = c.get();
    int at = 0;
    for (int i = 0; i < int(c.values.size()); ++i)
        if (c.values[i] == now) at = i;
    const int count = int(c.values.size());
    c.set(c.values[std::size_t(((at + step) % count + count) % count)]);
}

// Keys and buttons, each acting when it goes down.
enum Input { kToggle, kUp, kDown, kLeft, kRight, kAccept, kBack, kInputs };

std::array<bool, kInputs> Held() {
    std::array<bool, kInputs> held{};
    const bool focused = WindowFocused();
#ifdef _WIN32
    auto key = [&](int vk) { return focused && HostKeyDown(vk); };
    held[kToggle] = key(VK_F9);
    held[kUp] = key(VK_UP);
    held[kDown] = key(VK_DOWN);
    held[kLeft] = key(VK_LEFT);
    held[kRight] = key(VK_RIGHT);
    held[kAccept] = key(VK_RETURN);
    held[kBack] = key(VK_ESCAPE);
#endif
    HostPadState pad;
    if (focused && HostPad(&pad)) {
        // DualShock 2 order: 1 L3, 2 R3, 4 up, 5 right, 6 down, 7 left, 13 circle, 14 cross.
        auto button = [&](int bit) { return (pad.buttons >> bit) & 1; };
        held[kToggle] = held[kToggle] || (button(1) && button(2));
        held[kUp] = held[kUp] || button(4);
        held[kDown] = held[kDown] || button(6);
        held[kLeft] = held[kLeft] || button(7);
        held[kRight] = held[kRight] || button(5);
        held[kAccept] = held[kAccept] || button(14);
        held[kBack] = held[kBack] || button(13);
    }
    return held;
}

}  // namespace

std::vector<PcSetting> PcSettings() {
    std::vector<PcSetting> settings;
    for (int i : Rows()) {
        if (i < 0) continue;
        const Choice& c = Choices()[std::size_t(i)];
        std::string help = c.help;
        if (!SettingOverride(c.key).empty()) help += " Fixed for this run.";
        settings.push_back({c.game_label, Value(c, true), help});
    }
    return settings;
}

void StepPcSetting(int index, int step) {
    int n = 0;
    for (int i : Rows()) {
        if (i < 0) continue;
        if (n++ != index) continue;
        const Choice& c = Choices()[std::size_t(i)];
        if (!SettingOverride(c.key).empty()) return;
        const std::string now = c.get();
        int at = 0;
        for (int v = 0; v < int(c.values.size()); ++v)
            if (c.values[std::size_t(v)] == now) at = v;
        const int count = int(c.values.size());
        c.set(c.values[std::size_t(((at + step) % count + count) % count)]);
        return;
    }
}

bool PcMenuOpen() { return g_open || g_releasing; }

void OpenPcMenu() {
    if (g_open) return;
    g_open = true;
    g_row = 0;
    Draw(true);
}

void UpdatePcMenu() {
    static std::array<bool, kInputs> before{};
    const std::array<bool, kInputs> now = Held();
    const bool was_open = g_open;
    auto went_down = [&](Input i) { return now[i] && !before[i]; };
    if (went_down(kToggle) || (g_open && went_down(kBack))) {
        g_open = !g_open;
        g_row = 0;
        if (!g_open) SetWindowOverlay({});
        else Draw(true);
    } else if (g_open) {
        const int rows = int(Rows().size());
        if (went_down(kUp)) g_row = (g_row + rows - 1) % rows;
        if (went_down(kDown)) g_row = (g_row + 1) % rows;
        if (went_down(kLeft)) Change(-1);
        if (went_down(kRight) || went_down(kAccept)) Change(+1);
    }
    before = now;
    if (!g_open && was_open) g_releasing = true;
    if (g_releasing) {
        bool any = false;
        for (bool held : now) any = any || held;
        g_releasing = any;
    }
    if (g_open) Draw();
}

}  // namespace ps2host
