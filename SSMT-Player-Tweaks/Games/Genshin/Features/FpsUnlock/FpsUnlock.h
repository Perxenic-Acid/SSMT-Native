#pragma once

#include <iosfwd>
#include "FpsUnlockConfig.h"

namespace SSMT::Tweaks
{
    class PatternScanner;
} // namespace SSMT::Tweaks::Genshin

namespace SSMT::Tweaks::Genshin::FpsUnlock
{
    void Initialize(
        PatternScanner &patternScanner,
        const FpsUnlockConfig &config,
        std::ostream &log
    );
} // namespace SSMT::tweaks::Genshin::FpsUnlock
