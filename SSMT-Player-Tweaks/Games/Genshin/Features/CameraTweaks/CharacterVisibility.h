#pragma once

#include "Core/PatternScanner.h"
#include "Core/SymbolResolver.h"
#include <array>
#include <ostream>

namespace SSMT::Tweaks::Genshin::CameraTweaks
{
    inline constexpr std::size_t SceneCameraFadeWriteOffset = 0x35E;

    bool ValidateSceneCameraFade(const PatternScanner &scanner, std::uintptr_t address);
    std::array<std::uint8_t, 26> MakeSceneCameraFadeDetour(std::uintptr_t original);
    bool InstallSceneCameraFadeHook(const PatternScanner &scanner,
        const ResolvedSymbol &symbol, std::ostream &log);
}
