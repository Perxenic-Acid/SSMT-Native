#include "FastTeamPage.h"

#include "Core/PatternScanner.h"
#include "Core/HookManager.h"
#include "Core/SymbolResolver.h"
#include "Games/Genshin/GenshinPatterns.h"
#include <stdexcept>

namespace SSMT::Tweaks::Genshin::FastTeamPage
{
    namespace
    {
        using CheckCanEnterFn = bool (*)();
        using OpenTeamPageFn = void (*)(bool);
        using OpenTeamFn = void (*)();

        CheckCanEnterFn g_checkCanEnter = nullptr;
        OpenTeamPageFn g_openTeamPage = nullptr;
        OpenTeamFn g_originalOpenTeam = nullptr;
    }

    void Initialize(PatternScanner &patternScanner, std::ostream &log)
    {
        const auto checkCanEnter = ResolveSymbol(patternScanner,
            Patterns::CheckCanEnter, ResolverKind::Direct);
        const auto openTeamPage = ResolveSymbol(patternScanner,
            Patterns::OpenTeamPage, ResolverKind::Direct);
        const auto openTeam = ResolveSymbol(patternScanner,
            Patterns::OpenTeam, ResolverKind::Direct);
        if (!checkCanEnter || !openTeamPage || !openTeam)
            throw std::runtime_error("FastTeamPage symbol unavailable: " +
                checkCanEnter.validation + ", " + openTeamPage.validation + ", " + openTeam.validation);

        g_checkCanEnter = reinterpret_cast<CheckCanEnterFn>(checkCanEnter.address);
        g_openTeamPage = reinterpret_cast<OpenTeamPageFn>(openTeamPage.address);
        HookManager::Create(openTeam.address,
            reinterpret_cast<void *>(&HookedOpenTeam),
            reinterpret_cast<void **>(&g_originalOpenTeam));
        log << "FastTeamPage enabled.\n";
    }

    void HookedOpenTeam()
    {
        if (
            g_checkCanEnter != nullptr &&
            g_openTeamPage != nullptr &&
            g_checkCanEnter())
        {
            g_openTeamPage(false);
            return;
        }

        if (g_originalOpenTeam != nullptr)
            g_originalOpenTeam();
    }
} // namespace SSMT::Tweaks::Genshin::FastTeamPage
