#pragma once

#include "Core/CameraPolicy.h"
#include "Features/FpsUnlock/FpsUnlockConfig.h"

#include <Windows.h>
#include <cstdint>
#include <ostream>

namespace SSMT::Tweaks::Genshin
{

    struct GenshinConfig
    {
        bool enabled = true;

        FpsUnlockConfig fpsUnlock;
        CameraPolicyConfig camera;
    };

    GenshinConfig LoadGenshinConfig(HMODULE pluginModule, std::ostream &log);

} // namespace SSMT::Tweaks::Genshin
