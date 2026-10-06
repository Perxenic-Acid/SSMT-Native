#pragma once

#include "Core/PatternScanner.h"
#include "Core/CameraPolicy.h"

#include <ostream>

namespace SSMT::Tweaks::Genshin::CameraTweaks
{
    void Initialize(PatternScanner &scanner, const CameraPolicyConfig &config,
        std::ostream &log);
}
