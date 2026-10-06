#include "FpsUnlock.h"

#include "Core/PatternScanner.h"
#include "Core/HookManager.h"
#include "Core/SymbolResolver.h"
#include "Games/Genshin/GenshinPatterns.h"

#include <cstdint>
#include <ostream>
#include <stdexcept>

#include <wInDoWs.h>
#include <atomic>
#include <filesystem>
#include <fstream>

namespace SSMT::Tweaks::Genshin::FpsUnlock
{
    namespace
    {
        std::ofstream get_log()
        {
            wchar_t localAppData[MAX_PATH]{};
            const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
            if (length == 0 || length >= MAX_PATH)
                return {};
            const std::filesystem::path logPath = std::filesystem::path(localAppData) / L"SSMT4CachedFolder" / L"Logs" / L"SSMT-Player-Tweaks.log";
            return std::ofstream(logPath, std::ios::app);
        }

        std::atomic<ULONGLONG>
            g_lastLogTime{0};
        std::int32_t g_targetFrameRate = 120;

        using GetFrameCountFn = std::int32_t (*)();

        using SetFrameCountFn = void (*)(std::int32_t);

        GetFrameCountFn g_originalGetFrameCount = nullptr;

        SetFrameCountFn g_setFrameCount = nullptr;

        using SetSyncCountFn = void (*)(std::int32_t);

        SetSyncCountFn g_originalSetSyncCount = nullptr;

        std::atomic_bool
            g_syncInitialized{false};

        void HookedSetSyncCount(
            std::int32_t /* syncCount */)
        {
            g_originalSetSyncCount(0);
        }
        // SetSyncCountFn g_setSyncCount = nullptr;

        std::int32_t HookedGetFrameCount()
        {

            if (g_originalGetFrameCount == nullptr)
                return 60;

            std::int32_t frameCount =
                g_originalGetFrameCount();

            // 程序启动时 frameCount = -1 (ロゴ画面). 修正するとクラッシュする.
            if (frameCount < 1)
            {
                return frameCount;
            }

            if (
                g_originalSetSyncCount != nullptr &&
                !g_syncInitialized.exchange(true))
            {
                g_originalSetSyncCount(0);
            }

            const std::int32_t oldFrameCount = frameCount;

            if (
                g_setFrameCount != nullptr &&
                frameCount != g_targetFrameRate)
            {
                g_setFrameCount(g_targetFrameRate);

                frameCount = g_originalGetFrameCount();

                auto currTime = GetTickCount64();

                auto previousLog = g_lastLogTime.load(std::memory_order_relaxed);
                if (currTime - previousLog >= 5000 &&
                    g_lastLogTime.compare_exchange_strong(previousLog, currTime,
                        std::memory_order_relaxed))
                {
                    auto log = get_log();
                    if (log.is_open())
                        log << currTime << "[Fps Unlock] " << oldFrameCount
                            << " -> " << frameCount << '\n';
                }
            }

            if (frameCount >= 60)
                return 60;

            if (frameCount >= 45)
                return 45;

            if (frameCount >= 30)
                return 30;

            return frameCount;
        }
    }

    void Initialize(
        PatternScanner &patternScanner,
        const FpsUnlockConfig &config,
        std::ostream &log)
    {
        const auto getFrameCount = ResolveSymbol(patternScanner,
            Patterns::GetFrameCount, ResolverKind::RelativeBranch);
        const auto setFrameCount = ResolveSymbol(patternScanner,
            Patterns::SetFrameCount, ResolverKind::RelativeBranch);
        if (!getFrameCount || !setFrameCount)
            throw std::runtime_error("FPS getter/setter symbol unavailable: " +
                getFrameCount.validation + ", " + setFrameCount.validation);

        g_targetFrameRate = config.targetFps;
        g_setFrameCount = reinterpret_cast<SetFrameCountFn>(setFrameCount.address);
        HookManager::Create(getFrameCount.address,
            reinterpret_cast<void *>(&HookedGetFrameCount),
            reinterpret_cast<void **>(&g_originalGetFrameCount));
        log << "FpsUnlock enabled: target=" << g_targetFrameRate << ".\n";

        const auto setSyncCount = ResolveSymbol(patternScanner,
            Patterns::SetSyncCount, ResolverKind::RelativeBranch);
        if (!setSyncCount)
        {
            log << "FpsUnlock VSync hook unavailable: " << setSyncCount.validation << '\n';
            return;
        }
        try
        {
            HookManager::Create(setSyncCount.address,
                reinterpret_cast<void *>(&HookedSetSyncCount),
                reinterpret_cast<void **>(&g_originalSetSyncCount));
            log << "FpsUnlock VSync hook enabled.\n";
        }
        catch (const std::exception &error)
        {
            log << "FpsUnlock VSync hook unavailable: " << error.what() << '\n';
        }
    }
} // namespace SSMT::Tweaks::Genshin::FpsUnlock
