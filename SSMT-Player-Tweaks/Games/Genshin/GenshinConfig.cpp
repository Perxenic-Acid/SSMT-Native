#include "GenshinConfig.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace SSMT::Tweaks::Genshin
{
    namespace
    {
        std::filesystem::path GetConfigPath(HMODULE pluginModule)
        {
            std::vector<wchar_t> path(MAX_PATH);
            for (;;)
            {
                const DWORD length = GetModuleFileNameW(pluginModule, path.data(),
                    static_cast<DWORD>(path.size()));
                if (!length) return {};
                if (length < path.size() - 1)
                    return std::filesystem::path(path.data()).parent_path() /
                        L"SSMT-Player-Tweaks.ini";
                if (path.size() >= 32768) return {};
                path.resize(path.size() * 2);
            }
        }

        bool ReadBool(const wchar_t *path, const wchar_t *name, bool fallback)
        {
            return GetPrivateProfileIntW(L"Camera", name, fallback ? 1 : 0, path) != 0;
        }

        float ReadFloat(const wchar_t *path, const wchar_t *name,
            float fallback, float minimum, float maximum)
        {
            wchar_t value[64]{};
            GetPrivateProfileStringW(L"Camera", name, L"", value,
                static_cast<DWORD>(std::size(value)), path);
            if (!value[0]) return fallback;
            wchar_t *end = nullptr;
            const float parsed = std::wcstof(value, &end);
            return end != value && *end == L'\0' && std::isfinite(parsed) &&
                parsed >= minimum && parsed <= maximum ? parsed : fallback;
        }
    }

    GenshinConfig LoadGenshinConfig(HMODULE pluginModule, std::ostream &log)
    {
        GenshinConfig config{};
        const auto path = GetConfigPath(pluginModule);
        std::error_code fileError;
        if (path.empty() || !std::filesystem::is_regular_file(path, fileError))
        {
            log << "Player Tweaks config missing; using defaults.\n";
            return config;
        }

        const auto *file = path.c_str();
        auto &camera = config.camera;
        camera.customFov = ReadBool(file, L"CustomFov", camera.customFov);
        camera.fov = ReadFloat(file, L"Fov", camera.fov, 30.0f, 120.0f);
        camera.preserveAimingFov = ReadBool(file, L"PreserveAimingFov", camera.preserveAimingFov);
        camera.preserveCutsceneFov = ReadBool(file, L"PreserveCutsceneFov", camera.preserveCutsceneFov);
        camera.restoreInUi = ReadBool(file, L"RestoreInUi", camera.restoreInUi);
        camera.transitionSpeed = ReadFloat(file, L"TransitionSpeed", camera.transitionSpeed, 0.0f, 30.0f);
        camera.disableInputSmoothing = ReadBool(file, L"DisableInputSmoothing", camera.disableInputSmoothing);
        camera.disableTransitionBlend = ReadBool(file, L"DisableTransitionBlend", camera.disableTransitionBlend);
        camera.disableCharacterFade = ReadBool(file, L"DisableCharacterFade", camera.disableCharacterFade);
        camera.disableEventCameraMovement = ReadBool(file, L"DisableEventCameraMovement", camera.disableEventCameraMovement);
        config.fpsUnlock.enabled = GetPrivateProfileIntW(L"Gameplay", L"FpsUnlock", config.fpsUnlock.enabled ? 1 : 0, file) != 0;
        const int targetFps = GetPrivateProfileIntW(L"Gameplay", L"TargetFps", config.fpsUnlock.targetFps, file);
        if (targetFps >= 30 && targetFps <= 240) config.fpsUnlock.targetFps = targetFps;
        config.fastTeamPage = GetPrivateProfileIntW(L"Gameplay", L"FastTeamPage", config.fastTeamPage ? 1 : 0, file) != 0;
        log << "Player Tweaks config loaded from plugin directory.\n";
        return config;
    }
}
