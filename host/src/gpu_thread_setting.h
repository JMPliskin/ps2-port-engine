#ifndef PS2HOST_GPU_THREAD_SETTING_H
#define PS2HOST_GPU_THREAD_SETTING_H
#include "ps2host/settings.h"

namespace ps2host::detail {
// Preserve legacy launchers. Explicit CLI wins over environment, then INI,
// then the engine's existing default ON. Only called before renderer creation.
inline bool GpuThreadAtStartup(const char* environment) {
    if (environment && SettingOverride("gpu_thread").empty())
        OverrideSetting("gpu_thread", std::string(environment) == "0" ? "off" : "on", "env");
    return SettingOn("gpu_thread", true);
}
} // namespace ps2host::detail
#endif
