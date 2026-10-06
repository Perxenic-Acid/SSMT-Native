#include "GenshinRunTime.h"

#include "Core/PatternScanner.h"
#include "Games/Genshin/Features/CameraTweaks/CameraTweaks.h"
#include "Games/Genshin/Features/FpsUnlock/FpsUnlock.h"
#include "Games/Genshin/Features/FastTeamPage/FastTeamPage.h"

#include "Games/Genshin/GenshinConfig.h"

#include <ostream>
#include <exception>

namespace SSMT::Tweaks::Genshin
{
    void Initialize(
        PatternScanner &patternScanner,
        std::ostream &log,
        HMODULE pluginModule)
    {
        const GenshinConfig config = LoadGenshinConfig(pluginModule, log);

        try { CameraTweaks::Initialize(patternScanner, config.camera, log); }
        catch (const std::exception &error) { log << "Camera unavailable: " << error.what() << '\n'; }

        if (config.fpsUnlock.enabled)
        {
            try { FpsUnlock::Initialize(patternScanner, config.fpsUnlock, log); }
            catch (const std::exception &error) { log << "FpsUnlock unavailable: " << error.what() << '\n'; }
        }
        else log << "FpsUnlock disabled by game settings.\n";

        if (config.fastTeamPage)
        {
            try { FastTeamPage::Initialize(patternScanner, log); }
            catch (const std::exception &error) { log << "FastTeamPage unavailable: " << error.what() << '\n'; }
        }
        else log << "FastTeamPage disabled by game settings.\n";
    }
} // namespace SSMT::Tweaks::Genshin
