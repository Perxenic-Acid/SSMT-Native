#pragma once

#include <iosfwd>
#include <Windows.h>

namespace SSMT::Tweaks
{
    class PatternScanner;

    namespace Genshin
    {
        void Initialize(
            PatternScanner &patternScanner,
            std::ostream &log,
            HMODULE pluginModule
        );
    } // namespace Genshin
    
} // namespace SSMT::Tweaks
