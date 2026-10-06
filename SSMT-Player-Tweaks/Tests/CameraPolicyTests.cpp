#include "Core/CameraPolicy.h"

#include <cmath>
#include <iostream>

using namespace SSMT::Tweaks;

int main()
{
    CameraPolicyConfig config{};
    config.customFov = true;
    config.fov = 90.0f;
    config.transitionSpeed = 0.0f;
    FovTransitionState state{};
    const void *camera = reinterpret_cast<const void *>(0x1000);
    const auto check = [](float actual, float expected, const char *caseName)
    {
        if (std::fabs(actual - expected) < 0.01f) return true;
        std::cerr << caseName << ": got " << actual << ", expected " << expected << '\n';
        return false;
    };

    bool passed = true;
    passed &= check(ResolveCameraFov(60, camera, {}, config, 100, state), 90, "override");
    passed &= check(ResolveCameraFov(20, camera, {.aiming = true}, config, 110, state), 20, "aiming");
    passed &= check(ResolveCameraFov(45, camera, {.cutscene = true}, config, 120, state), 45, "cutscene");
    passed &= check(ResolveCameraFov(60, camera, {.ui = true}, config, 130, state), 60, "ui");
    passed &= check(ResolveCameraFov(60, camera, {}, config, 140, state), 90, "resume");
    passed &= check(ResolveCameraFov(0, camera, {}, config, 150, state), 0, "invalid input");
    config.preserveAimingFov = false;
    config.preserveCutsceneFov = false;
    config.restoreInUi = false;
    passed &= check(ResolveCameraFov(20, camera, {.aiming = true}, config, 160, state), 90, "aiming override");
    passed &= check(ResolveCameraFov(45, camera, {.cutscene = true}, config, 170, state), 90, "cutscene override");
    passed &= check(ResolveCameraFov(60, camera, {.ui = true}, config, 180, state), 90, "ui override");

    config.transitionSpeed = 8.0f;
    state = {};
    passed &= check(ResolveCameraFov(60, camera, {}, config, 100, state), 60, "transition start");
    const float intermediate = ResolveCameraFov(60, camera, {}, config, 200, state);
    if (!(intermediate > 60 && intermediate < 90))
    {
        std::cerr << "transition progress: " << intermediate << '\n';
        passed = false;
    }
    passed &= check(ResolveCameraFov(60, reinterpret_cast<const void *>(0x2000), {}, config, 210, state),
        60, "camera switch");
    config.customFov = false;
    passed &= check(ResolveCameraFov(45, camera, {}, config, 220, state), 45, "disabled plugin policy");
    return passed ? 0 : 1;
}
