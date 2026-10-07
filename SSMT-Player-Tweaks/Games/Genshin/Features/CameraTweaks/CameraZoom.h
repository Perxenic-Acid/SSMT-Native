#pragma once

#include "Core/PatternScanner.h"
#include <array>
#include <cstdint>
#include <ostream>
#include <string_view>

namespace SSMT::Tweaks::Genshin { struct GenshinCameraBridge; }

namespace SSMT::Tweaks::Genshin::CameraTweaks
{
    enum class ZoomFunction { InputTick, InputAdjust, UpdateManualRatio, ScriptedManualRatio, DistanceLimit, RadiusUpdate, RadiusSmoothDamp, CollectAvatarState };

    struct ZoomClampSite
    {
        ZoomFunction function;
        const char *name;
        std::size_t offset;
        std::size_t size;
        std::uint8_t destinationXmm;
        std::uint8_t sourceXmm;
        std::string_view pattern;
        bool loadsOne;
        std::size_t guardOffset;
        bool clampCall = false;
        bool skipRatioReset = false;
    };

    // CN 7.1 的函数内偏移。只用于已通过入口 AOB 和局部语义验证的函数，不能作为 RVA 回退。
    inline constexpr std::array<ZoomClampSite, 13> ZoomClampSites{{
        {ZoomFunction::InputTick, "InitialRatio", 0x4D7, 0x16, 3, 0,
            "F2 0F 10 1D ?? ?? ?? ?? 66 0F 2E C3 77 08 66 0F 57 DB F2 0F 5F D8", true, 0x4CA},
        {ZoomFunction::InputTick, "InitialRadius", 0x551, 0x0E, 6, 1,
            "66 0F 2E CE 77 08 66 0F 28 F7 F2 0F 5F F1", false, 0x544},
        {ZoomFunction::InputTick, "InputRatio", 0x5F8, 0x16, 3, 1,
            "F2 0F 10 1D ?? ?? ?? ?? 66 0F 2E CB 77 08 66 0F 57 DB F2 0F 5F D9", true, 0x5EB},
        {ZoomFunction::InputTick, "InputRadius", 0x6F5, 0x17, 7, 1,
            "F2 0F 10 3D ?? ?? ?? ?? 66 0F 2E CF 77 09 66 41 0F 28 F9 F2 0F 5F F9", true, 0x6E8},
        {ZoomFunction::InputTick, "AdjustedRadius", 0x72A, 0x11, 8, 7,
            "66 41 0F 2E F8 77 0A F2 44 0F 5F CF 66 45 0F 28 C1", false, 0x71D},
        {ZoomFunction::InputAdjust, "ElevationRadius", 0xF5, 0x10, 6, 7,
            "66 0F 2E FE 77 0A F2 44 0F 5F C7 66 41 0F 28 F0", false, 0xE8},
        {ZoomFunction::UpdateManualRatio, "RecomputedRatio", 0x2A, 0x16, 1, 0,
            "F2 0F 10 0D ?? ?? ?? ?? 66 0F 2E C1 77 08 66 0F 57 C9 F2 0F 5F C8", true, 0x21},
        {ZoomFunction::ScriptedManualRatio, "ScriptedRatio", 0x56, 0x16, 0, 6,
            "F2 0F 10 05 ?? ?? ?? ?? 66 0F 2E F0 77 08 66 0F 57 C0 F2 0F 5F C6", true, 0x4D},
        // 输入模块已经写入半径后，下游仍会按 minRadius / maxRadius 重新限制并写回。
        {ZoomFunction::DistanceLimit, "FinalDistance", 0xA5, 0x0E, 0, 6,
            "66 0F 2E F0 77 08 F2 0F 5F D6 66 0F 28 C2", false, 0x9C},
        {ZoomFunction::RadiusUpdate, "SettledDistance", 0x142, 5, 0, 0,
            "E8 ?? ?? ?? ??", false, 0x22, true},
        {ZoomFunction::RadiusUpdate, "MovingDistance", 0x24F, 5, 0, 0,
            "E8 ?? ?? ?? ??", false, 0x22, true},
        {ZoomFunction::RadiusSmoothDamp, "SmoothDistanceDelta", 0x5F, 0x0E, 0, 3,
            "66 0F 2E D8 77 08 F2 0F 5F D3 66 0F 28 C2", false, 0x52},
        // 聚焦镜头拥有临时距离；进入时不应清掉退出后继续使用的手动缩放比例。
        {ZoomFunction::CollectAvatarState, "PreserveManualRatio", 0x1D2, 7, 0, 0,
            "48 89 86 48 04 00 00", false, 0x13, false, true},
    }};

    bool ValidateZoomFunction(const PatternScanner &scanner, std::uintptr_t address, ZoomFunction function);
    std::array<std::uint8_t, 19> MakeZoomClampDetour(const ZoomClampSite &site, std::uintptr_t continuation);
    std::array<std::uint8_t, 64> MakeScopedZoomClampDetour(const ZoomClampSite &site,
        std::uintptr_t continuation, std::uintptr_t callerReturn, std::uintptr_t original);
    bool InstallCameraZoomHooks(const PatternScanner &scanner, const GenshinCameraBridge &bridge, std::ostream &log);
}
