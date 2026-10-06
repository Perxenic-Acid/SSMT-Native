#include "GenshinCameraBridge.h"

#include "GenshinPatterns.h"

namespace SSMT::Tweaks::Genshin
{
    namespace
    {
        bool ValidateUpdateView(const PatternScanner &scanner, std::uintptr_t address)
        {
            // 同一 AOB 还会命中内联 Update；输入专用函数拥有这个独立的尾部。
            static constexpr std::uint8_t tail[] = {
                0xF3, 0x0F, 0x11, 0x4E, 0x28, 0x0F, 0x28, 0x74, 0x24, 0x20,
                0x0F, 0x28, 0x7C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x40, 0x5E, 0xC3,
            };
            return scanner.MatchBytes(address + 0xC6, tail, sizeof(tail));
        }

        bool ValidateBlender(const PatternScanner &scanner, std::uintptr_t address)
        {
            // 时长和已用时间必须仍在 +0x60/+0x64；布局变化时停止此能力。
            static constexpr std::uint8_t timers[] = {
                0xF3, 0x0F, 0x10, 0x46, 0x60, 0xF3, 0x0F, 0x10, 0x76, 0x64,
                0x0F, 0x28, 0xCE, 0xF3, 0x41, 0x0F, 0x58, 0xC8,
                0xF3, 0x0F, 0x11, 0x4E, 0x64,
            };
            return scanner.MatchBytes(address + 0x4F, timers, sizeof(timers));
        }

        void LogSymbol(std::ostream &log, const char *name,
            const ResolvedSymbol &symbol, const PatternScanner &scanner)
        {
            log << "Camera." << name << " candidates=" << symbol.candidates
                << " validation=" << symbol.validation;
            if (symbol) log << " RVA=0x" << std::hex
                << symbol.address - scanner.BaseAddress() << std::dec;
            log << '\n';
        }
    }

    bool GenshinCameraBridge::SupportsCutsceneState() const
    {
        return findString && findGameObject && getActive;
    }

    std::optional<bool> GenshinCameraBridge::IsCutsceneActive() const
    {
        if (!SupportsCutsceneState()) return std::nullopt;
        using FindStringFn = void *(*)(const char *);
        using FindObjectFn = void *(*)(void *);
        using GetActiveFn = bool (*)(void *);
        const auto makeString = reinterpret_cast<FindStringFn>(findString.address);
        const auto findObject = reinterpret_cast<FindObjectFn>(findGameObject.address);
        const auto isActive = reinterpret_cast<GetActiveFn>(getActive.address);

        static constexpr const char *pages[] = {
            "TalkDialog", "TalkDialogV1", "InLevelCutScenePage",
        };
        __try
        {
            for (const char *name : pages)
            {
                void *text = makeString(name);
                void *object = text ? findObject(text) : nullptr;
                if (object && isActive(object)) return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return std::nullopt;
        }
        return false;
    }

    GenshinCameraBridge ResolveCameraBridge(
        const PatternScanner &scanner,
        const CameraPolicyConfig &config,
        std::ostream &log)
    {
        GenshinCameraBridge bridge{};
        if (config.customFov)
        {
            bridge.setFov = ResolveSymbol(scanner, Patterns::ChangeFov, ResolverKind::Direct);
            LogSymbol(log, "SetFov", bridge.setFov, scanner);
            if (config.preserveCutsceneFov)
            {
                bridge.findString = ResolveSymbol(scanner, Patterns::FindString, ResolverKind::Direct);
                bridge.findGameObject = ResolveSymbol(scanner, Patterns::FindGameObject, ResolverKind::Direct);
                bridge.getActive = ResolveSymbol(scanner, Patterns::GetActive, ResolverKind::RelativeBranch);
                LogSymbol(log, "FindString", bridge.findString, scanner);
                LogSymbol(log, "FindGameObject", bridge.findGameObject, scanner);
                LogSymbol(log, "GetActive", bridge.getActive, scanner);
            }
        }
        if (config.disableInputSmoothing)
        {
            bridge.updateView = ResolveSymbol(scanner, Patterns::CameraUpdateView,
                ResolverKind::Direct, ValidateUpdateView);
            LogSymbol(log, "UpdateView", bridge.updateView, scanner);
        }
        if (config.disableTransitionBlend)
        {
            bridge.blenderTick = ResolveSymbol(scanner, Patterns::CameraStateBlenderTick,
                ResolverKind::Direct, ValidateBlender);
            LogSymbol(log, "BlenderTick", bridge.blenderTick, scanner);
        }
        if (config.disableCharacterFade)
        {
            bridge.playerPerspective = ResolveSymbol(scanner, Patterns::PlayerPerspective,
                ResolverKind::RelativeBranch);
            LogSymbol(log, "PlayerPerspective", bridge.playerPerspective, scanner);
        }
        if (config.disableEventCameraMovement)
        {
            bridge.eventCamera = ResolveSymbol(scanner, Patterns::EventCamera, ResolverKind::Direct);
            LogSymbol(log, "EventCamera", bridge.eventCamera, scanner);
        }
        return bridge;
    }
}
