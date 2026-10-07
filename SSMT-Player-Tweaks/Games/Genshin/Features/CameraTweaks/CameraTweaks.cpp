#include "CameraTweaks.h"
#include "CameraZoom.h"

#include "Core/HookManager.h"
#include "Games/Genshin/GenshinCameraBridge.h"

#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>

namespace SSMT::Tweaks::Genshin::CameraTweaks
{
    namespace
    {
        using SetFovFn = std::int32_t (*)(void *, float);
        using UpdateViewFn = void (*)(void *);
        using BlenderTickFn = void (*)(void *, float);
        using PlayerPerspectiveFn = void *(*)(void *, float, void *);
        using EventCameraFn = bool (*)(void *, void *);

        SetFovFn originalSetFov = nullptr;
        UpdateViewFn originalUpdateView = nullptr;
        BlenderTickFn originalBlenderTick = nullptr;
        PlayerPerspectiveFn originalPlayerPerspective = nullptr;
        EventCameraFn originalEventCamera = nullptr;
        CameraPolicyConfig cameraConfig{};
        GenshinCameraBridge cameraBridge{};

        bool UiRequested()
        {
            const HWND foreground = GetForegroundWindow();
            DWORD foregroundProcess = 0;
            if (foreground) GetWindowThreadProcessId(foreground, &foregroundProcess);
            if (foregroundProcess && foregroundProcess != GetCurrentProcessId()) return true;

            CURSORINFO cursor{};
            cursor.cbSize = sizeof(cursor);
            return GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) != 0;
        }

        std::int32_t HookedSetFov(void *camera, float incoming)
        {
            thread_local std::array<FovTransitionState, 4> transitions{};
            thread_local std::uint64_t lastCutsceneCheck = 0;
            thread_local bool cutsceneActive = true;
            const auto now = GetTickCount64();
            if (cameraConfig.preserveCutsceneFov && cameraBridge.SupportsCutsceneState() &&
                (lastCutsceneCheck == 0 || now - lastCutsceneCheck >= 250))
            {
                cutsceneActive = cameraBridge.IsCutsceneActive().value_or(true);
                lastCutsceneCheck = now;
            }
            FovTransitionState *transition = nullptr;
            for (auto &candidate : transitions)
            {
                if (candidate.initialized && candidate.camera == camera)
                {
                    transition = &candidate;
                    break;
                }
            }
            if (!transition)
            {
                transition = &transitions[0];
                for (auto &candidate : transitions)
                {
                    if (!candidate.initialized) { transition = &candidate; break; }
                    if (candidate.lastTickMs < transition->lastTickMs) transition = &candidate;
                }
            }
            const CameraContext context{
                .aiming = incoming <= 30.0f,
                .cutscene = cutsceneActive,
                .ui = cameraConfig.restoreInUi && UiRequested(),
            };
            const float output = ResolveCameraFov(incoming, camera, context,
                cameraConfig, now, *transition);
            return originalSetFov ? originalSetFov(camera, output) : 0;
        }

        void HookedUpdateView(void *state)
        {
            if (state)
            {
                __try
                {
                    auto *values = static_cast<float *>(state);
                    if (std::isfinite(values[28]) && std::isfinite(values[29]))
                    {
                        values[30] = values[28];
                        values[31] = values[29];
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (originalUpdateView) originalUpdateView(state);
        }

        void HookedBlenderTick(void *state, float deltaTime)
        {
            if (state && deltaTime >= 0.0f)
            {
                __try
                {
                    auto *values = static_cast<float *>(state);
                    const float duration = values[24];
                    if (std::isfinite(duration) && duration > 0.0f)
                        values[25] = duration;
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (originalBlenderTick) originalBlenderTick(state, deltaTime);
        }

        void *HookedPlayerPerspective(void *object, float value, void *context)
        {
            return originalPlayerPerspective
                ? originalPlayerPerspective(object, 1.0f, context) : nullptr;
        }

        bool HookedEventCamera(void *object, void *context)
        {
            // 游戏把 true 作为已处理；仅在用户启用此项时安装 Hook。
            return true;
        }

        void TryInstall(std::ostream &log, const char *name,
            const ResolvedSymbol &symbol, void *detour, void **original)
        {
            if (!symbol)
            {
                log << "Camera." << name << " disabled: " << symbol.validation << '\n';
                return;
            }
            try
            {
                HookManager::Create(symbol.address, detour, original);
                log << "Camera." << name << " enabled.\n";
            }
            catch (const std::exception &error)
            {
                log << "Camera." << name << " disabled: " << error.what() << '\n';
            }
        }
    }

    void Initialize(PatternScanner &scanner, const CameraPolicyConfig &config,
        std::ostream &log)
    {
        cameraConfig = config;
        cameraBridge = ResolveCameraBridge(scanner, config, log);

        if (config.cameraZoom)
            InstallCameraZoomHooks(scanner, cameraBridge, log);

        if (config.customFov)
        {
            if (config.preserveCutsceneFov && !cameraBridge.SupportsCutsceneState())
            {
                log << "Camera.SetFov disabled: cutscene state resolver unavailable.\n";
            }
            else
            {
                TryInstall(log, "SetFov", cameraBridge.setFov,
                    reinterpret_cast<void *>(&HookedSetFov),
                    reinterpret_cast<void **>(&originalSetFov));
            }
        }
        if (config.disableInputSmoothing)
            TryInstall(log, "InputSmoothing", cameraBridge.updateView,
                reinterpret_cast<void *>(&HookedUpdateView),
                reinterpret_cast<void **>(&originalUpdateView));
        if (config.disableTransitionBlend)
            TryInstall(log, "TransitionBlend", cameraBridge.blenderTick,
                reinterpret_cast<void *>(&HookedBlenderTick),
                reinterpret_cast<void **>(&originalBlenderTick));
        if (config.disableCharacterFade)
            TryInstall(log, "CharacterFade", cameraBridge.playerPerspective,
                reinterpret_cast<void *>(&HookedPlayerPerspective),
                reinterpret_cast<void **>(&originalPlayerPerspective));
        if (config.disableEventCameraMovement)
            TryInstall(log, "EventCamera", cameraBridge.eventCamera,
                reinterpret_cast<void *>(&HookedEventCamera),
                reinterpret_cast<void **>(&originalEventCamera));
    }
}
