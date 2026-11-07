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
        auto checkCanEnter = ResolveSymbol(patternScanner,
            Patterns::CheckCanEnter, ResolverKind::Direct);
        // Do not guess between the two generic legacy-pattern matches.
        // The extended new signature must independently resolve uniquely.
        if (!checkCanEnter)
        {
            auto alternative = ResolveSymbol(patternScanner,
                Patterns::CheckCanEnterNov2026, ResolverKind::Direct);
            log << "FastTeamPage CheckCanEnter: legacy candidates=" << checkCanEnter.candidates
                << ", nov2026 candidates=" << alternative.candidates
                << ", result=" << alternative.validation << '\n';
            checkCanEnter = alternative;
        }
        const auto openTeamPage = ResolveSymbol(patternScanner,
            Patterns::OpenTeamPage, ResolverKind::Direct);
        auto openTeam = ResolveSymbol(patternScanner,
            Patterns::OpenTeam, ResolverKind::Direct);
        // Old short conditional jump changed to a near conditional jump.
        if (!openTeam && openTeam.candidates == 0)
        {
            auto alternative = ResolveSymbol(patternScanner,
                Patterns::OpenTeamNov2026, ResolverKind::Direct);
            log << "FastTeamPage OpenTeam: legacy candidates=" << openTeam.candidates
                << ", nov2026 candidates=" << alternative.candidates
                << ", result=" << alternative.validation << '\n';
            openTeam = alternative;
        }
        log << "FastTeamPage OpenTeamPage: candidates=" << openTeamPage.candidates
            << ", result=" << openTeamPage.validation << '\n';
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
