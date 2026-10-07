#include "CameraZoom.h"
#include "Core/HookManager.h"
#include "Games/Genshin/GenshinCameraBridge.h"

#include <cstring>
#include <exception>
#include <span>
#include <stdexcept>

namespace SSMT::Tweaks::Genshin::CameraTweaks
{
    namespace
    {
        bool MatchPattern(const PatternScanner &scanner, std::uintptr_t address,
            std::string_view pattern, std::size_t size)
        {
            if (!scanner.IsReadableAddress(address, size)) return false;
            const auto *bytes = reinterpret_cast<const std::uint8_t *>(address);
            std::size_t offset = 0;
            for (std::size_t i = 0; i < pattern.size();)
            {
                if (pattern[i] == ' ') { ++i; continue; }
                if (i + 1 >= pattern.size() || offset >= size) return false;
                if (pattern[i] != '?' && bytes[offset] !=
                    ((PatternScanner::HexDigit(pattern[i]) << 4) | PatternScanner::HexDigit(pattern[i + 1])))
                    return false;
                ++offset;
                i += 2;
            }
            return offset == size;
        }

        std::uintptr_t RipTarget(std::uintptr_t address, std::size_t displacementOffset, std::size_t instructionSize)
        {
            std::int32_t displacement;
            std::memcpy(&displacement, reinterpret_cast<const void *>(address + displacementOffset), sizeof(displacement));
            return static_cast<std::uintptr_t>(static_cast<std::intptr_t>(address + instructionSize) + displacement);
        }

        bool ValidateHotfixGuard(const PatternScanner &scanner, std::uintptr_t address, bool shortBranch)
        {
            if (!MatchPattern(scanner, address, "80 3D ?? ?? ?? ?? 00", 7) ||
                !MatchPattern(scanner, address + 7, shortBranch ? "75 ??" : "0F 85 ?? ?? ?? ??", shortBranch ? 2 : 6))
                return false;
            const auto flag = RipTarget(address, 2, 7);
            // IFix 激活时会走未验证的托管钳制分支。保留该分支，拒绝启用此功能。
            return scanner.IsReadableAddress(flag) && *reinterpret_cast<const std::uint8_t *>(flag) == 0;
        }

        const ResolvedSymbol &Symbol(const GenshinCameraBridge &bridge, ZoomFunction function)
        {
            switch (function)
            {
            case ZoomFunction::InputTick: return bridge.inputZoomTick;
            case ZoomFunction::InputAdjust: return bridge.inputZoomAdjust;
            case ZoomFunction::UpdateManualRatio: return bridge.updateManualLocateRatio;
            case ZoomFunction::ScriptedManualRatio: return bridge.scriptedManualLocateRatio;
            case ZoomFunction::DistanceLimit: return bridge.zoomDistanceLimit;
            case ZoomFunction::RadiusUpdate: return bridge.zoomRadiusUpdate;
            case ZoomFunction::RadiusSmoothDamp: return bridge.zoomRadiusSmoothDamp;
            default: return bridge.zoomCollectAvatarState;
            }
        }
    }

    bool ValidateZoomFunction(const PatternScanner &scanner, std::uintptr_t address, ZoomFunction function)
    {
        const auto entryGuard = function == ZoomFunction::InputTick ? 0x57 :
            function == ZoomFunction::InputAdjust ? 0x1E : function == ZoomFunction::UpdateManualRatio ? 0x08 :
            function == ZoomFunction::ScriptedManualRatio ? 0x18 : function == ZoomFunction::DistanceLimit ? 0x12 :
            function == ZoomFunction::RadiusUpdate ? 0x22 : function == ZoomFunction::RadiusSmoothDamp ? 0x40 : 0x13;
        if (!ValidateHotfixGuard(scanner, address + entryGuard,
            function == ZoomFunction::UpdateManualRatio || function == ZoomFunction::ScriptedManualRatio)) return false;

        for (const auto &site : ZoomClampSites)
        {
            if (site.function != function) continue;
            const auto target = address + site.offset;
            if (!scanner.IsExecutableAddress(target) || !scanner.IsExecutableAddress(target + site.size) ||
                !MatchPattern(scanner, target, site.pattern, site.size) ||
                !ValidateHotfixGuard(scanner, address + site.guardOffset,
                    function == ZoomFunction::UpdateManualRatio || function == ZoomFunction::ScriptedManualRatio ||
                    function == ZoomFunction::DistanceLimit)) return false;
            if (site.clampCall)
            {
                const auto helper = scanner.ResolveRelativeBranch(target);
                if (!helper || !MatchPattern(scanner, helper,
                    "48 83 EC 28 66 0F 28 DA 66 0F 28 D1", 12) ||
                    !ValidateHotfixGuard(scanner, helper + 0xC, true) ||
                    !MatchPattern(scanner, helper + 0x15,
                        "66 0F 2E C3 77 08 F2 0F 5F D0 66 0F 28 DA 66 0F 28 C3 48 83 C4 28 C3", 23)) return false;
            }
            if (site.loadsOne)
            {
                const auto constant = RipTarget(target, 4, 8);
                std::uint64_t bits = 0;
                if (!scanner.IsReadableAddress(constant, sizeof(bits))) return false;
                std::memcpy(&bits, reinterpret_cast<const void *>(constant), sizeof(bits));
                if (bits != 0x3FF0000000000000ULL) return false;
            }
        }
        // 输入仍由原生速度 * deltaTime 积分，末尾仍使用原生标尺并写入半径。
        if (function == ZoomFunction::InputTick)
            return MatchPattern(scanner, address + 0x5CA,
                "F2 0F 10 8E 28 05 00 00 F2 0F 59 CE F2 0F 58 8E 48 04 00 00", 20) &&
                MatchPattern(scanner, address + 0x73B,
                    "F2 44 0F 59 86 98 04 00 00 F2 44 0F 11 86 10 02 00 00", 18);
        if (function == ZoomFunction::UpdateManualRatio)
            return MatchPattern(scanner, address + 0x40, "F2 0F 11 8E 48 04 00 00 48 83 C4 20 5E C3", 14);
        if (function == ZoomFunction::DistanceLimit)
        {
            // 验证当前半径与上下界的数据流和 double 写回，不匹配其它用途的 Clamp。
            if (!MatchPattern(scanner, address + 0x71, "80 7F 63 00 75 ?? 80 BE FA 04 00 00 00 75 ??", 15) ||
                !MatchPattern(scanner, address + 0x80,
                    "F2 0F 10 B6 10 02 00 00 48 89 F1 E8 ?? ?? ?? ?? 66 0F 28 D0 F2 0F 10 86 98 04 00 00", 28) ||
                !MatchPattern(scanner, address + 0xB3,
                    "F2 0F 11 86 10 02 00 00 0F 28 74 24 40 48 83 C4 58 5F 5E C3", 20)) return false;
            const auto minimumGetter = scanner.ResolveRelativeBranch(address + 0x8B);
            return minimumGetter && MatchPattern(scanner, minimumGetter,
                "56 48 83 EC 50 0F 29 7C 24 40 66 0F 29 74 24 30 48 89 CE", 19) &&
                ValidateHotfixGuard(scanner, minimumGetter + 0x13, false) &&
                MatchPattern(scanner, minimumGetter + 0x11A,
                    "F2 0F 10 BE 90 04 00 00 F2 0F 58 BE 38 04 00 00", 16);
        }
        if (function == ZoomFunction::RadiusUpdate)
        {
            return MatchPattern(scanner, address + 0x122,
                "F2 0F 11 BE 10 02 00 00 48 89 F1 E8 ?? ?? ?? ?? 66 0F 28 C8 "
                "F2 0F 10 96 98 04 00 00 66 0F 28 C7", 32) &&
                MatchPattern(scanner, address + 0x22F,
                    "F2 0F 11 86 10 02 00 00 48 89 F1 E8 ?? ?? ?? ?? 66 0F 28 C8 "
                    "F2 0F 10 96 98 04 00 00 66 0F 28 C6", 32) &&
                MatchPattern(scanner, address + 0x254, "F2 0F 11 86 10 02 00 00", 8) &&
                MatchPattern(scanner, address + 0x2F7,
                    "F2 0F 5C C2 F2 0F 10 A6 98 04 00 00 F2 0F 5C E2", 16) &&
                MatchPattern(scanner, address + 0x31A,
                    "F2 0F 11 64 24 28 F2 0F 11 44 24 20 66 0F 28 C3 66 0F 28 DE E8 ?? ?? ?? ??", 25) &&
                scanner.ResolveRelativeBranch(address + 0x32E) != 0;
        }
        if (function == ZoomFunction::RadiusSmoothDamp)
            return MatchPattern(scanner, address + 0x4D, "F2 41 0F 5C D8", 5) &&
                MatchPattern(scanner, address + 0xD9,
                    "F2 0F 11 16 F2 0F 58 C8 F2 0F 59 CC F2 41 0F 58 C8 66 0F 28 C1", 21);
        if (function == ZoomFunction::CollectAvatarState)
            // 同时确认上一帧/当前聚焦状态、配置门控及精确的 1.0 重置。
            // 保留整段原生聚焦逻辑，仅跳过最后的手动比例写入。
            return ValidateHotfixGuard(scanner, address + 0xA4, false) &&
                ValidateHotfixGuard(scanner, address + 0xB1, false) &&
                MatchPattern(scanner, address + 0x47,
                    "0F B6 86 C0 04 00 00 88 86 C1 04 00 00", 13) &&
                MatchPattern(scanner, address + 0x5D,
                    "48 8B 43 20 45 31 F6 48 85 C0 74 09 0F B6 80 40 05 00 00 EB 02 31 C0 88 86 C0 04 00 00", 29) &&
                MatchPattern(scanner, address + 0x166, "80 BE C1 04 00 00 00 75 6A EB 33", 11) &&
                MatchPattern(scanner, address + 0x19B, "80 BE C1 04 00 00 00 75 35", 9) &&
                MatchPattern(scanner, address + 0x1A4,
                    "80 BE C0 04 00 00 00 74 2C 48 85 C0 0F 84 ?? ?? ?? ?? "
                    "80 B8 42 05 00 00 00 74 1A 80 B8 43 05 00 00 00 75 11 "
                    "48 B8 00 00 00 00 00 00 F0 3F", 46) &&
                MatchPattern(scanner, address + 0x1D9,
                    "8B 86 D8 04 00 00 48 8B 8E D0 04 00 00", 13);
        return true;
    }

    std::array<std::uint8_t, 19> MakeZoomClampDetour(const ZoomClampSite &site, std::uintptr_t continuation)
    {
        static_assert(sizeof(std::uintptr_t) == 8, "Camera zoom detours require Windows x64");
        std::array<std::uint8_t, 19> code{};
        code.fill(0x90);
        std::size_t i = 0;
        // 限制点只传递游戏计算的 double；比例重置点只跳过存储。
        // 两者均不改通用寄存器、栈、标志或主动写入相机数据字段。
        if (!site.skipRatioReset)
        {
            code[i++] = 0xF2;
            if (site.destinationXmm >= 8) code[i++] = 0x44;
            code[i++] = 0x0F;
            code[i++] = 0x10;
            code[i++] = static_cast<std::uint8_t>(0xC0 | ((site.destinationXmm & 7) << 3) | site.sourceXmm);
        }
        code[i++] = 0xFF;
        code[i++] = 0x25;
        for (int j = 0; j < 4; ++j) code[i++] = 0;
        std::memcpy(code.data() + i, &continuation, sizeof(continuation));
        return code;
    }

    std::array<std::uint8_t, 64> MakeScopedZoomClampDetour(const ZoomClampSite &site,
        std::uintptr_t continuation, std::uintptr_t callerReturn, std::uintptr_t original)
    {
        std::array<std::uint8_t, 64> code{};
        code.fill(0x90);
        // 共享平滑函数只在指定相机调用点解除边界。其栈帧为 push RSI + sub RSP,0x70；
        // 再保存 RAX/标志后，原调用者返回地址位于 [RSP+0x88]。所有分支均恢复二者。
        const std::uint8_t guard[] = {0x50, 0x9C, 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,
            0x48, 0x39, 0x84, 0x24, 0x88, 0, 0, 0, 0x75, 0x1A, 0x9D, 0x58};
        std::memcpy(code.data(), guard, sizeof(guard));
        std::memcpy(code.data() + 4, &callerReturn, sizeof(callerReturn));
        const auto bypass = MakeZoomClampDetour(site, continuation);
        std::memcpy(code.data() + 24, bypass.data(), bypass.size());
        const std::uint8_t fallback[] = {0x9D, 0x58, 0xFF, 0x25, 0, 0, 0, 0};
        std::memcpy(code.data() + 48, fallback, sizeof(fallback));
        std::memcpy(code.data() + 56, &original, sizeof(original));
        return code;
    }

    bool InstallCameraZoomHooks(const PatternScanner &scanner, const GenshinCameraBridge &bridge, std::ostream &log)
    {
        for (const auto function : {ZoomFunction::InputTick, ZoomFunction::InputAdjust,
            ZoomFunction::UpdateManualRatio, ZoomFunction::ScriptedManualRatio, ZoomFunction::DistanceLimit,
            ZoomFunction::RadiusUpdate, ZoomFunction::RadiusSmoothDamp, ZoomFunction::CollectAvatarState})
        {
            const auto &symbol = Symbol(bridge, function);
            if (!symbol || !ValidateZoomFunction(scanner, symbol.address, function))
            {
                log << "Camera.ZoomLimits disabled: incomplete or unsupported native clamp layout.\n";
                return false;
            }
        }
        if (scanner.ResolveRelativeBranch(bridge.zoomRadiusUpdate.address + 0x32E) != bridge.zoomRadiusSmoothDamp.address)
        {
            log << "Camera.ZoomLimits disabled: radius smoothing call relationship changed.\n";
            return false;
        }

        constexpr std::size_t stride = 64;
        constexpr std::size_t codeSize = stride * ZoomClampSites.size();
        auto *code = static_cast<std::uint8_t *>(VirtualAlloc(nullptr, codeSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!code)
        {
            log << "Camera.ZoomLimits disabled: detour allocation failed.\n";
            return false;
        }
        std::array<std::uintptr_t, ZoomClampSites.size()> targets{};
        std::size_t created = 0;
        try
        {
            for (std::size_t i = 0; i < ZoomClampSites.size(); ++i)
            {
                const auto &site = ZoomClampSites[i];
                targets[i] = Symbol(bridge, site.function).address + site.offset;
                if (site.function == ZoomFunction::RadiusSmoothDamp)
                {
                    const auto stub = MakeScopedZoomClampDetour(site, targets[i] + site.size,
                        bridge.zoomRadiusUpdate.address + 0x333, 0);
                    std::memcpy(code + i * stride, stub.data(), stub.size());
                }
                else
                {
                    const auto stub = MakeZoomClampDetour(site, targets[i] + site.size);
                    std::memcpy(code + i * stride, stub.data(), stub.size());
                }
            }
            DWORD previous;
            if (!VirtualProtect(code, codeSize, PAGE_EXECUTE_READ, &previous) ||
                !FlushInstructionCache(GetCurrentProcess(), code, codeSize))
                throw std::runtime_error("detour protection/cache update failed");
            for (std::size_t i = 0; i < targets.size(); ++i)
            {
                void *original = nullptr;
                HookManager::CreateDisabled(targets[i], code + i * stride, &original);
                ++created;
                if (ZoomClampSites[i].function == ZoomFunction::RadiusSmoothDamp)
                {
                    if (!VirtualProtect(code, codeSize, PAGE_READWRITE, &previous))
                        throw std::runtime_error("scoped detour write protection failed");
                    const auto pointer = reinterpret_cast<std::uintptr_t>(original);
                    std::memcpy(code + i * stride + 56, &pointer, sizeof(pointer));
                    if (!VirtualProtect(code, codeSize, PAGE_EXECUTE_READ, &previous) ||
                        !FlushInstructionCache(GetCurrentProcess(), code, codeSize))
                        throw std::runtime_error("scoped detour protection/cache update failed");
                }
            }
            HookManager::Enable(targets);
            // 与现有 Hook 一样常驻进程；已启用的跳转必须始终拥有有效的代码存储。
            log << "Camera.ZoomLimits enabled: " << ZoomClampSites.size()
                << " native limit/reset sites bypassed; final distance and manual ratio preserved.\n";
            return true;
        }
        catch (const std::exception &error)
        {
            bool removed = true;
            while (created)
            {
                try { HookManager::Remove(targets[--created]); }
                catch (const std::exception &cleanup)
                {
                    removed = false;
                    log << "Camera.ZoomLimits cleanup failed: " << cleanup.what() << '\n';
                }
            }
            if (removed) VirtualFree(code, 0, MEM_RELEASE);
            log << "Camera.ZoomLimits installation failed: " << error.what() << '\n';
            return false;
        }
    }
}
