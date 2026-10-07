#pragma once

#include <cstdint>

namespace SSMT::Tweaks
{
    struct CameraPolicyConfig
    {
        bool customFov = false;
        float fov = 60.0f;
        bool preserveAimingFov = true;
        bool preserveCutsceneFov = true;
        bool restoreInUi = true;
        float transitionSpeed = 8.0f;
        bool disableInputSmoothing = false;
        bool disableTransitionBlend = false;
        bool disableCharacterFade = true;
        bool disableEventCameraMovement = false;
        bool cameraZoom = false;
    };

    struct CameraContext
    {
        bool aiming = false;
        bool cutscene = false;
        bool ui = false;
    };

    struct FovTransitionState
    {
        const void *camera = nullptr;
        float output = 0.0f;
        std::uint64_t lastTickMs = 0;
        bool initialized = false;
    };

    float ResolveCameraFov(
        float incoming,
        const void *camera,
        const CameraContext &context,
        const CameraPolicyConfig &config,
        std::uint64_t nowMs,
        FovTransitionState &state);
}
