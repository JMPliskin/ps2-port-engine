// Real settings and menu code; synthetic window/input endpoints, no game or GPU.
#define NOMINMAX
#include "ps2host/run.h"
#include "ps2host/settings.h"
#include "ps2host/texture_pack.h"
#include "../src/gpu_thread_setting.h"
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace {
int failures = 0, checks = 0;
std::set<int> keys;
ps2host::HostPadState pad;
bool focused = true, pack = false;
std::size_t pack_count = 0;
std::vector<std::string> overlay;
void Check(bool ok, const char* what) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what); }
}
std::string Read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void File(const std::filesystem::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary); out << text;
}
int Row(const std::string& name) {
    const auto rows = ps2host::PcSettings();
    for (std::size_t n = 0; n < rows.size(); ++n) if (rows[n].label == name) return int(n);
    Check(false, "required menu row exists"); return 0;
}
std::string Value(const std::string& name) { return ps2host::PcSettings()[Row(name)].value; }
void Key(int k) {
    keys = {k}; ps2host::UpdatePcMenu(); keys.clear(); ps2host::UpdatePcMenu();
}
void Pad(unsigned buttons) {
    pad.buttons = u16(buttons); ps2host::UpdatePcMenu();
    pad.buttons = 0; ps2host::UpdatePcMenu();
}
}

namespace ps2host {
bool WindowFocused() { return focused; }
bool HostKeyDown(int k) { return keys.count(k) != 0; }
bool HostPad(HostPadState* out) { *out = pad; return true; }
void SetWindowOverlay(const std::vector<std::string>& lines) { overlay = lines; }
bool FramePacingDefault() { return true; }
bool WindowFullscreen() { return SettingOn("fullscreen", false); }
bool WindowVsync() { return SettingOn("vsync", false); }
void SetWindowFullscreen(bool v) { SetSettingOn("fullscreen", v); }
void SetWindowVsync(bool v) { SetSettingOn("vsync", v); }
void SetWindowAspect(const std::string& v) { SetSetting("aspect", v); }
namespace texture_pack {
std::size_t Count() { return pack_count; }
bool Enabled() { return pack; }
void SetEnabled(bool v) { pack = v; }
}
}

int main(int argc, char** argv) {
    using namespace ps2host;
    if (argc != 3) return 2;
    const auto file = std::filesystem::u8path(argv[2]);
    if (std::string(argv[1]) == "--reopen") {
        LoadSettings(file.u8string());
        Check(SettingOn("gpu_thread"), "saved GPU preference survives a new process");
        const bool actual = detail::GpuThreadAtStartup(nullptr);
        SetEffectiveSetting("gpu_thread", actual ? "on" : "off");
        Check(actual && Value("GPU THREAD") == "ON", "next startup has no pending restart");
        Check(Setting("custom_legacy") == "keep", "unrelated old preference preserved");
        std::printf("Settings restart process: %s\n", failures ? "FAIL" : "PASS");
        return failures ? 1 : 0;
    }
    if (!std::filesystem::create_directories(file.parent_path())) return 3;
    const auto unicode = file.parent_path() / std::filesystem::u8path("ajustes-\xC3\xB1.ini");
    LoadSettings(unicode.u8string()); SetSetting("unicode", "saved");
    Check(Read(unicode).find("unicode=saved") != std::string::npos, "UTF-8 settings path persists");
    File(file, "\xEF\xBB\xBF# old settings\r\n custom_legacy = keep\r\nresolution=3x\r\n"
               "aspect=4:3\r\nframe_pacing=off\r\ngpu_thread=off\r\nvsync=off\r\n"
               "fps_counter=off\r\nfullscreen=off\r\ntexture_pack=off\r\n");
    const auto sentinel = file.parent_path() / "synthetic-card";
    File(sentinel, "not a game save");
    LoadSettings(file.u8string());
    Check(Setting("resolution") == "3x" && Setting("custom_legacy") == "keep", "legacy CRLF/BOM/whitespace");
    const auto before_open = Read(file);
    Key(VK_F9); Check(PcMenuOpen(), "F9 opens");
    Key(VK_F9); Check(!PcMenuOpen() && overlay.empty(), "F9 closes and releases input");
    Check(Read(file) == before_open, "opening/closing does not rewrite preferences");
    Check(Row("FPS COUNTER") >= 0 && Row("GPU THREAD") >= 0, "new functional options exposed");
    Check(PcSettings().size() == 7, "no empty texture pack row");
    pack_count = 1;
    Check(PcSettings().size() == 8, "existing loaded texture pack remains available");
    StepPcSetting(Row("TEXTURES"), 1); Check(pack && SettingOn("texture_pack"), "texture pack setter persists");
    pack_count = 0;
    Key(VK_F9); Key(VK_RIGHT);
    Check(Setting("resolution") == "4x", "keyboard changes selected row");
    Key(VK_DOWN); Key(VK_RETURN);
    Check(Setting("aspect") == "16:9", "keyboard navigation and accept");
    Key(VK_ESCAPE); Check(!PcMenuOpen(), "escape closes");
    Pad((1u << 1) | (1u << 2)); Check(PcMenuOpen(), "L3+R3 opens");
    Pad(1u << 6); Pad(1u << 14);
    Check(Setting("aspect") == "auto", "controller down and cross change");
    Pad(1u << 13); Check(!PcMenuOpen(), "controller circle closes");
    focused = false; Key(VK_F9); Check(!PcMenuOpen(), "unfocused input ignored"); focused = true;
    Key(VK_F9);
    for (std::size_t n = 0; n < PcSettings().size(); ++n) Key(VK_DOWN);
    Key(VK_RETURN); Check(!PcMenuOpen(), "close row works");
    StepPcSetting(Row("FPS COUNTER"), 1); Check(SettingOn("fps_counter"), "FPS row persists");

    RunSetting("resolution", "2x"); RunSetting("frame_pacing", "on");
    StepPcSetting(Row("RESOLUTION"), 1);
    Check(Setting("resolution") == "2x" && Value("RESOLUTION").find("[CLI]") != std::string::npos,
          "CLI effective value is locked and labelled");
    SetSetting("resolution", "original"); SetSetting("custom_legacy", "keep");
    SetSetting("new_preference", "saved");
    Check(SavedSetting("resolution") == "4x" && Read(file).find("resolution=4x") != std::string::npos,
          "other writes do not persist a CLI override");
    Check(Read(file).find("frame_pacing=off") != std::string::npos, "CLI pacing does not leak into INI");
    LoadSettings(file.u8string());
    Check(Setting("resolution") == "4x" && SettingOverride("resolution").empty(), "reload drops runtime overrides");

    Check(!detail::GpuThreadAtStartup(nullptr), "saved GPU OFF selected at startup");
    SetEffectiveSetting("gpu_thread", "off");
    StepPcSetting(Row("GPU THREAD"), 1);
    Check(SettingOn("gpu_thread") && EffectiveSetting("gpu_thread") == "off", "GPU preference changes without hot toggle");
    Check(Value("GPU THREAD").find("OFF -> ON (restart)") != std::string::npos, "active and requested values are distinct");
    StepPcSetting(Row("GPU THREAD"), -1);
    Check(Value("GPU THREAD") == "OFF", "reverting preference cancels pending restart");
    Check(!detail::GpuThreadAtStartup("0") && SettingOverride("gpu_thread") == "env", "legacy environment selection");
    StepPcSetting(Row("GPU THREAD"), 1);
    Check(!SettingOn("gpu_thread") && Value("GPU THREAD").find("[env]") != std::string::npos, "environment override locked and labelled");
    RunSetting("gpu_thread", "on");
    Check(detail::GpuThreadAtStartup("0") && SettingOverride("gpu_thread") == "CLI", "CLI wins over environment");
    Check(SavedSetting("gpu_thread") == "off", "startup overrides never replace saved preference");

    LoadSettings(file.u8string());
    const auto before_fail = Read(file);
    HANDLE locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "hold original INI against replacement");
    SetSetting("resolution", "original");
    Check(!SettingsError().empty() && Read(file) == before_fail && Setting("resolution") == "4x",
          "failed atomic save preserves disk and memory");
    OpenPcMenu(); UpdatePcMenu();
    bool error_shown = false; for (const auto& line : overlay) error_shown |= line.find("Cannot save") != std::string::npos;
    Check(error_shown, "save failure shown in F9"); Key(VK_ESCAPE);
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
    SetSetting("resolution", "2x"); Check(SettingsError().empty(), "successful save clears failure");
    SetSetting("gpu_thread", "on");
    Check(Read(sentinel) == "not a game save", "unrelated synthetic card preserved");
    std::printf("PC settings/menu: %s (%d checks; real INI, synthetic input/window endpoints)\n",
                failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
