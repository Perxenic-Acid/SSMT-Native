#include "Games/Genshin/Features/CameraTweaks/CameraZoom.h"
#include "Games/Genshin/GenshinCameraBridge.h"
#include "Core/HookManager.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace SSMT::Tweaks;
using namespace SSMT::Tweaks::Genshin;
using namespace SSMT::Tweaks::Genshin::CameraTweaks;

namespace
{
    bool passed = true;
    void Check(bool condition, const char *name)
    {
        if (condition) return;
        passed = false;
        std::cerr << "FAIL: " << name << '\n';
    }

    std::vector<std::uint8_t> ReadFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() <= 0) throw std::runtime_error("Cannot read test input");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(input.tellg()));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
            throw std::runtime_error("Incomplete test input");
        return bytes;
    }

    struct TestImage
    {
        std::uint8_t *base;
        explicit TestImage(std::size_t size)
            : base(static_cast<std::uint8_t *>(VirtualAlloc(nullptr, size,
                MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)))
        {
            if (!base) throw std::runtime_error("VirtualAlloc failed");
        }
        ~TestImage() { VirtualFree(base, 0, MEM_RELEASE); }
        TestImage(const TestImage &) = delete;
        TestImage &operator=(const TestImage &) = delete;
    };

    CameraPolicyConfig ZoomOnlyConfig()
    {
        CameraPolicyConfig config{};
        config.disableCharacterFade = false;
        config.cameraZoom = true;
        return config;
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

    constexpr std::array<ZoomFunction, 8> Functions{ZoomFunction::InputTick, ZoomFunction::InputAdjust,
        ZoomFunction::UpdateManualRatio, ZoomFunction::ScriptedManualRatio, ZoomFunction::DistanceLimit,
        ZoomFunction::RadiusUpdate, ZoomFunction::RadiusSmoothDamp, ZoomFunction::CollectAvatarState};
    constexpr std::array<std::size_t, 8> Offsets{0x1000, 0x3000, 0x4000, 0x5000, 0x8000, 0x9000, 0xA000, 0xD000};
    constexpr std::array<const char *, 8> Fixtures{"input_zoom_tick.bin", "input_zoom_adjust.bin",
        "update_manual_ratio.bin", "scripted_manual_ratio.bin", "distance_limit.bin",
        "radius_update.bin", "radius_smooth_damp.bin", "collect_avatar_state.bin"};
    constexpr std::array<std::size_t, 8> EntryGuards{0x57, 0x1E, 0x08, 0x18, 0x12, 0x22, 0x40, 0x13};

    void Relocate(std::uint8_t *instruction, std::size_t displacementOffset,
        std::size_t instructionSize, const void *target)
    {
        const auto displacement = reinterpret_cast<const std::uint8_t *>(target) - (instruction + instructionSize);
        if (displacement < INT32_MIN || displacement > INT32_MAX) throw std::runtime_error("Test relocation overflow");
        const auto value = static_cast<std::int32_t>(displacement);
        std::memcpy(instruction + displacementOffset, &value, sizeof(value));
    }

    void TestEvidence(const std::filesystem::path &directory)
    {
        // 游戏函数仅作为扫描证据；改写引用到测试映像的常量/标志，绝不执行完整函数。
        TestImage image(0x10000);
        auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(image.base);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x80;
        auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(image.base + 0x80);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.NumberOfSections = 1;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt->OptionalHeader.SizeOfImage = 0x10000;
        auto *section = IMAGE_FIRST_SECTION(nt);
        std::memcpy(section->Name, ".text", 5);
        section->VirtualAddress = 0x1000;
        section->Misc.VirtualSize = 0xD000;
        section->Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE;
        const double one = 1;
        std::memcpy(image.base + 0xE000, &one, sizeof(one));
        auto relocateFunction = [&](std::size_t index, std::size_t offset)
        {
            auto *function = image.base + offset;
            Relocate(function + EntryGuards[index], 2, 7, image.base + 0xE100);
            for (const auto &site : ZoomClampSites)
            {
                if (site.function != Functions[index]) continue;
                // RadiusUpdate 的两个 CALL 共用入口 IFix 检查。
                Relocate(function + site.guardOffset, 2, 7,
                    image.base + (site.guardOffset == EntryGuards[index] ? 0xE100 : 0xE101));
                if (site.loadsOne) Relocate(function + site.offset, 4, 8, image.base + 0xE000);
                if (site.clampCall) Relocate(function + site.offset, 1, 5, image.base + 0x7800);
            }
            if (Functions[index] == ZoomFunction::DistanceLimit)
                Relocate(function + 0x8B, 1, 5, image.base + 0xB000);
            if (Functions[index] == ZoomFunction::RadiusUpdate)
                Relocate(function + 0x32E, 1, 5, image.base + 0xA000);
            if (Functions[index] == ZoomFunction::CollectAvatarState)
                for (const auto guard : {0xA4, 0xB1})
                    Relocate(function + guard, 2, 7, image.base + 0xE101);
        };
        std::array<std::vector<std::uint8_t>, 8> fixtures;
        for (std::size_t i = 0; i < fixtures.size(); ++i)
        {
            fixtures[i] = ReadFile(directory / Fixtures[i]);
            if (fixtures[i].size() > (i == 0 ? 0x2000 : 0x1000)) throw std::runtime_error("Unexpected fixture size");
            std::memcpy(image.base + Offsets[i], fixtures[i].data(), fixtures[i].size());
            relocateFunction(i, Offsets[i]);
        }
        const auto minimumGetter = ReadFile(directory / "minimum_radius_rva124460A0.bin");
        if (minimumGetter.size() > 0x1000) throw std::runtime_error("Unexpected getter size");
        std::memcpy(image.base + 0xB000, minimumGetter.data(), minimumGetter.size());
        Relocate(image.base + 0xB000 + 0x13, 2, 7, image.base + 0xE102);
        const auto scalarClamp = ReadFile(directory / "scalar_distance_clamp.bin");
        if (scalarClamp.size() > 0x800) throw std::runtime_error("Unexpected clamp size");
        std::memcpy(image.base + 0x7800, scalarClamp.data(), scalarClamp.size());
        Relocate(image.base + 0x7800 + 0xC, 2, 7, image.base + 0xE103);
        PatternScanner scanner(reinterpret_cast<HMODULE>(image.base));
        std::ostringstream log;
        auto config = ZoomOnlyConfig();
        const auto resolve = [&]() { return ResolveCameraBridge(scanner, config, log); };
        auto bridge = resolve();
        for (std::size_t i = 0; i < Functions.size(); ++i)
            Check(Symbol(bridge, Functions[i]) && Symbol(bridge, Functions[i]).candidates == 1 &&
                Symbol(bridge, Functions[i]).address == scanner.BaseAddress() + Offsets[i], "unique verified native clamp function");
        config.cameraZoom = false;
        bridge = resolve();
        for (auto function : Functions)
            Check(!Symbol(bridge, function) && Symbol(bridge, function).candidates == 0, "switch off performs no zoom discovery");
        config.cameraZoom = true;

        for (const auto &site : ZoomClampSites)
        {
            const auto index = static_cast<std::size_t>(site.function);
            const auto offset = Offsets[index] + site.offset + site.size - 1;
            image.base[offset] ^= 1;
            bridge = resolve();
            Check(!Symbol(bridge, site.function), "changed clamp layout rejected despite matching entry");
            Check(!InstallCameraZoomHooks(scanner, bridge, log), "incomplete layout installs no hooks");
            image.base[offset] ^= 1;
        }
        for (const auto offset : {0xE100, 0xE101})
        {
            image.base[offset] = 1;
            bridge = resolve();
            for (auto function : Functions)
                if (offset == 0xE100 || function != ZoomFunction::RadiusUpdate)
                    Check(!Symbol(bridge, function), "active IFix branch rejected");
            image.base[offset] = 0;
        }
        image.base[0xE000 + 6] ^= 1;
        bridge = resolve();
        Check(!bridge.inputZoomTick && !bridge.updateManualLocateRatio && !bridge.scriptedManualLocateRatio,
            "wrong upper-bound constant rejected");
        image.base[0xE000 + 6] ^= 1;
        image.base[0x1000 + 0x5CA] ^= 1;
        Check(!resolve().inputZoomTick, "changed input integration rejected");
        image.base[0x1000 + 0x5CA] ^= 1;
        image.base[0x1000 + 0x73B] ^= 1;
        Check(!resolve().inputZoomTick, "changed native distance scale rejected");
        image.base[0x1000 + 0x73B] ^= 1;

        for (const auto offset : {0x84, 0x98, 0xB7, 0x8C})
        {
            image.base[0x8000 + offset] ^= 1;
            bridge = resolve();
            Check(!bridge.zoomDistanceLimit && bridge.zoomDistanceLimit.candidates == 1,
                "changed distance load, bounds, writeback or getter call rejected");
            Check(!InstallCameraZoomHooks(scanner, bridge, log), "missing final clamp rejects complete zoom group");
            image.base[0x8000 + offset] ^= 1;
        }
        image.base[0xE102] = 1;
        Check(!resolve().zoomDistanceLimit, "active minimum getter hotfix rejected");
        image.base[0xE102] = 0;
        image.base[0xE103] = 1;
        Check(!resolve().zoomRadiusUpdate, "active shared clamp hotfix rejected");
        image.base[0xE103] = 0;
        for (const auto offset : {0x4A, 0x6C, 0x169, 0x19E, 0x1B8, 0x1C2, 0x1D0, 0x1DB})
        {
            image.base[0xD000 + offset] ^= 1;
            bridge = resolve();
            Check(!bridge.zoomCollectAvatarState && bridge.zoomCollectAvatarState.candidates == 1,
                "changed focus state, gate, reset constant or continuation rejects ratio preservation");
            Check(!InstallCameraZoomHooks(scanner, bridge, log), "missing focus preservation rejects complete zoom group");
            image.base[0xD000 + offset] ^= 1;
        }
        image.base[0x9000 + 0x32F] ^= 1;
        bridge = resolve();
        Check(bridge.zoomRadiusUpdate && bridge.zoomRadiusSmoothDamp,
            "individual functions can validate with a changed caller relationship");
        Check(!InstallCameraZoomHooks(scanner, bridge, log), "changed smoothing caller rejects complete hook group");
        image.base[0x9000 + 0x32F] ^= 1;

        std::memcpy(image.base + 0x6000, fixtures[0].data(), fixtures[0].size());
        relocateFunction(0, 0x6000);
        bridge = resolve();
        Check(!bridge.inputZoomTick && bridge.inputZoomTick.candidates == 2, "ambiguous valid targets rejected");
        image.base[0x6000 + 0x73B] ^= 1;
        Check(static_cast<bool>(resolve().inputZoomTick), "only semantically valid candidate selected");
        std::memset(image.base + 0x6000, 0, fixtures[0].size());

        // 验证中途创建失败会删除本功能已经创建的 Hook，保留已有 Hook。
        bridge = resolve();
        const auto conflictTarget = bridge.inputZoomTick.address + ZoomClampSites[1].offset;
        TestImage conflictCode(0x1000);
        const auto conflictStub = MakeZoomClampDetour(ZoomClampSites[1], conflictTarget + ZoomClampSites[1].size);
        std::memcpy(conflictCode.base, conflictStub.data(), conflictStub.size());
        HookManager::CreateDisabled(conflictTarget, conflictCode.base, nullptr);
        Check(!InstallCameraZoomHooks(scanner, bridge, log), "hook creation conflict fails and rolls back");
        HookManager::Remove(conflictTarget);
        Check(InstallCameraZoomHooks(scanner, bridge, log), "all thirteen limit/reset sites install together after rollback");
        for (const auto &site : ZoomClampSites)
            HookManager::Remove(Symbol(bridge, site.function).address + site.offset);
        Check(static_cast<bool>(resolve().inputZoomTick), "removal restores native layout");

        config.customFov = true;
        config.disableInputSmoothing = true;
        Check(static_cast<bool>(resolve().inputZoomTick), "zoom resolves with several camera features");
        config.disableTransitionBlend = true;
        config.disableCharacterFade = true;
        config.disableEventCameraMovement = true;
        Check(static_cast<bool>(resolve().inputZoomTick), "zoom resolves with all camera features");
        image.base[0x1000] ^= 1;
        bridge = resolve();
        Check(!bridge.inputZoomTick && bridge.inputZoomTick.candidates == 0, "missing input entry rejected");
    }

    void TestDetours(const std::filesystem::path &directory)
    {
        for (const auto &site : ZoomClampSites)
        {
            if (site.skipRatioReset) continue;
            const auto fixture = ReadFile(directory / Fixtures[static_cast<std::size_t>(site.function)]);
            if (site.offset + site.size > fixture.size()) throw std::runtime_error("Short clamp fixture");
            // 独立 ABI 测试函数：保存非易失寄存器，装入输入及上下界，执行一段通用 SSE 钳制。
            TestImage image(0x1000);
            const bool scoped = site.function == ZoomFunction::RadiusSmoothDamp;
            const bool physicalRadius = site.function == ZoomFunction::DistanceLimit || site.clampCall;
            const auto frameSize = static_cast<std::uint8_t>(scoped ? 0x70 : 0x40);
            std::array<std::uint8_t, 0x550> cameraData;
            cameraData.fill(0xA5);
            std::vector<std::uint8_t> code{
                0x56, 0x48, 0x83, 0xEC, frameSize,
                0x0F, 0x29, 0x34, 0x24, 0x0F, 0x29, 0x7C, 0x24, 0x10,
                0x44, 0x0F, 0x29, 0x44, 0x24, 0x20, 0x44, 0x0F, 0x29, 0x4C, 0x24, 0x30,
                0xF2, 0x0F, 0x10, 0xE8,
                0x66, 0x0F, 0x57, 0xC9,
                0x66, 0x0F, 0x57, 0xD2,
                0x66, 0x0F, 0x57, 0xF6, 0x66, 0x0F, 0x57, 0xFF,
                0x66, 0x45, 0x0F, 0x57, 0xC0, 0x66, 0x45, 0x0F, 0x57, 0xC9,
                0xF2};
            if (site.destinationXmm >= 8) code.push_back(0x44);
            code.insert(code.end(), {0x0F, 0x10, static_cast<std::uint8_t>(0x05 | ((site.destinationXmm & 7) << 3)), 0, 0, 0, 0});
            const auto upperLoad = code.size() - 4;
            std::size_t callUpperLoad = 0;
            if (site.clampCall)
            {
                code.insert(code.end(), {0xF2, 0x0F, 0x10, 0x15, 0, 0, 0, 0});
                callUpperLoad = code.size() - 4;
            }
            code.push_back(0xF2);
            code.insert(code.end(), {0x0F, 0x10, static_cast<std::uint8_t>(0xC5 | (site.sourceXmm << 3))});
            if (physicalRadius)
            {
                code.insert(code.end(), {0x48, 0xBE});
                const auto pointer = reinterpret_cast<std::uintptr_t>(cameraData.data());
                const auto *pointerBytes = reinterpret_cast<const std::uint8_t *>(&pointer);
                code.insert(code.end(), pointerBytes, pointerBytes + sizeof(pointer));
            }
            const auto blockOffset = code.size();
            code.insert(code.end(), fixture.begin() + site.offset, fixture.begin() + site.offset + site.size);
            const auto tail = code.size();
            // 在限制块后执行样本的原生半径写回，验证距离本身不会再次饱和。
            if (physicalRadius)
            {
                const auto writebackFixture = ReadFile(directory / "distance_limit.bin");
                code.insert(code.end(), writebackFixture.begin() + 0xB3, writebackFixture.begin() + 0xBB);
            }
            code.push_back(0xF2);
            if (site.destinationXmm >= 8) code.push_back(0x41);
            code.insert(code.end(), {0x0F, 0x10, static_cast<std::uint8_t>(0xC0 | (site.destinationXmm & 7)),
                0x0F, 0x28, 0x34, 0x24, 0x0F, 0x28, 0x7C, 0x24, 0x10,
                0x44, 0x0F, 0x28, 0x44, 0x24, 0x20, 0x44, 0x0F, 0x28, 0x4C, 0x24, 0x30,
                0x48, 0x83, 0xC4, frameSize, 0x5E, 0xC3});
            std::memcpy(image.base, code.data(), code.size());
            const double one = 1;
            std::memcpy(image.base + 0xF00, &one, sizeof(one));
            Relocate(image.base + upperLoad, 0, 4, image.base + 0xF00);
            if (site.clampCall)
            {
                Relocate(image.base + callUpperLoad, 0, 4, image.base + 0xF00);
                // 从通用 Clamp 样本提取纯 SSE 运算，移除 IFix 与栈帧依赖后单独执行。
                const auto scalarClamp = ReadFile(directory / "scalar_distance_clamp.bin");
                std::memcpy(image.base + 0x200, scalarClamp.data() + 4, 8);
                std::memcpy(image.base + 0x208, scalarClamp.data() + 0x15, 18);
                image.base[0x21A] = 0xC3;
                Relocate(image.base + blockOffset, 1, 5, image.base + 0x200);
            }
            if (site.loadsOne) Relocate(image.base + blockOffset, 4, 8, image.base + 0xF00);
            const auto continuation = reinterpret_cast<std::uintptr_t>(image.base + tail);
            const auto stub = MakeZoomClampDetour(site, continuation);
            std::memcpy(image.base + 0x800, stub.data(), stub.size());
            if (scoped)
            {
                // 模拟指定相机调用点和直接调用者，共享完全相同的钳制函数。
                const std::uint8_t caller[] = {0x48, 0x83, 0xEC, 0x28, 0xE8, 0, 0, 0, 0,
                    0x48, 0x83, 0xC4, 0x28, 0xC3};
                std::memcpy(image.base + 0x400, caller, sizeof(caller));
                Relocate(image.base + 0x404, 1, 5, image.base);
            }
            FlushInstructionCache(GetCurrentProcess(), image.base, 0x1000);
            const auto run = reinterpret_cast<double (*)(double)>(image.base + (scoped ? 0x400 : 0));
            const auto otherCaller = reinterpret_cast<double (*)(double)>(image.base);
            Check(run(-2) == 0 && run(0.5) == 0.5 && run(2) == 1, "original synthetic clamp enforces both bounds");
            if (physicalRadius)
            {
                double radius;
                std::memcpy(&radius, cameraData.data() + 0x210, sizeof(radius));
                Check(radius == 1, "original downstream writeback saturates actual radius despite larger input");
            }
            const auto target = reinterpret_cast<std::uintptr_t>(image.base + blockOffset);
            if (scoped)
            {
                void *original = nullptr;
                HookManager::CreateDisabled(target, image.base + 0x800, &original);
                const auto scopedStub = MakeScopedZoomClampDetour(site, continuation,
                    reinterpret_cast<std::uintptr_t>(image.base + 0x409), reinterpret_cast<std::uintptr_t>(original));
                std::memcpy(image.base + 0x800, scopedStub.data(), scopedStub.size());
                FlushInstructionCache(GetCurrentProcess(), image.base + 0x800, scopedStub.size());
                const std::array targets{target};
                HookManager::Enable(targets);
            }
            else HookManager::Create(target, image.base + 0x800, nullptr);
            for (const double value : {-1000.0, -2.0, 0.0, 0.125, 0.5, 1.0, 2.0, 1000.0})
            {
                Check(run(value) == value, "real MinHook detour passes original value without imposing bounds");
                if (scoped)
                    Check(otherCaller(value) == (value < 0 ? 0 : value > 1 ? 1 : value),
                        "shared smooth clamp keeps both bounds for unrelated callers");
                if (physicalRadius)
                {
                    double radius;
                    std::memcpy(&radius, cameraData.data() + 0x210, sizeof(radius));
                    Check(radius == value, "native camera radius writeback preserves out-of-range distance");
                    std::array<std::uint8_t, 0x550> expected;
                    expected.fill(0xA5);
                    std::memcpy(expected.data() + 0x210, &value, sizeof(value));
                    Check(cameraData == expected, "distance unlock leaves all other camera fields unchanged");
                }
            }
            HookManager::Remove(target);
            Check(run(-2) == 0 && run(2) == 1, "removing detour restores native bounds");
        }
    }

    void TestFocusRatioPreservation(const std::filesystem::path &directory)
    {
        // 只执行样本中不带调用的聚焦门控/重置片段；模拟临时镜头覆盖实际半径。
        const auto fixture = ReadFile(directory / "collect_avatar_state.bin");
        const auto &site = ZoomClampSites.back();
        TestImage image(0x1000);
        std::vector<std::uint8_t> code{0x56, 0x48, 0x89, 0xCE, 0x48, 0x89, 0xD0};
        const auto blockOffset = code.size();
        code.insert(code.end(), fixture.begin() + 0x19B, fixture.begin() + 0x1D9);
        const auto tail = code.size();
        code.insert(code.end(), {0xF2, 0x0F, 0x10, 0x86, 0x48, 0x04, 0, 0, 0x5E, 0xC3});
        std::memcpy(image.base, code.data(), code.size());
        // 原空配置分支会进入游戏异常处理；在独立测试函数中跳到返回点。
        Relocate(image.base + blockOffset + 0x15, 2, 6, image.base + tail);
        const auto resetOffset = blockOffset + site.offset - 0x19B;
        const auto target = reinterpret_cast<std::uintptr_t>(image.base + resetOffset);
        const auto stub = MakeZoomClampDetour(site, reinterpret_cast<std::uintptr_t>(image.base + tail));
        std::memcpy(image.base + 0x800, stub.data(), stub.size());
        FlushInstructionCache(GetCurrentProcess(), image.base, 0x1000);
        const auto run = reinterpret_cast<double (*)(void *, void *)>(image.base);
        std::array<std::uint8_t, 0x550> cameraData;
        std::array<std::uint8_t, 0x550> focusConfig{};
        focusConfig[0x542] = 1;
        auto prepare = [&](double ratio, bool focused, bool wasFocused)
        {
            cameraData.fill(0xA5);
            cameraData[0x4C0] = focused;
            cameraData[0x4C1] = wasFocused;
            std::memcpy(cameraData.data() + 0x448, &ratio, sizeof(ratio));
        };
        prepare(12.5, true, false);
        Check(run(cameraData.data(), focusConfig.data()) == 1,
            "native focus entry discards the previous long-distance manual ratio");
        HookManager::Create(target, image.base + 0x800, nullptr);
        for (double ratio : {-2.0, 0.125, 1.0, 12.5, 1000.0})
        {
            for (const auto states : {std::array{false, false}, std::array{true, false},
                std::array{true, true}, std::array{false, true}})
            {
                prepare(ratio, states[0], states[1]);
                const auto expected = cameraData;
                Check(run(cameraData.data(), focusConfig.data()) == ratio,
                    "focus entry, held aim, exit and normal state preserve the same manual ratio");
                Check(cameraData == expected, "ratio preservation changes no camera fields");
            }
            // 临时镜头可改变实际半径；退出后沿用先前的手动比例进行原生距离换算。
            prepare(ratio, true, false);
            run(cameraData.data(), focusConfig.data());
            const double aimedRadius = 2;
            std::memcpy(cameraData.data() + 0x210, &aimedRadius, sizeof(aimedRadius));
            cameraData[0x4C0] = 0;
            cameraData[0x4C1] = 1;
            const double restoredRatio = run(cameraData.data(), focusConfig.data());
            Check(restoredRatio == ratio, "temporary aiming radius does not destroy the return ratio");
        }
        // 两个相机数据对象之间没有插件缓存，不能把一个对象的缩放带到另一个对象。
        std::array<std::uint8_t, 0x550> secondCamera{};
        const double secondRatio = 0.75;
        std::memcpy(secondCamera.data() + 0x448, &secondRatio, sizeof(secondRatio));
        secondCamera[0x4C0] = 1;
        Check(run(secondCamera.data(), focusConfig.data()) == secondRatio, "different camera objects retain independent ratios");
        HookManager::Remove(target);
        prepare(12.5, true, false);
        Check(run(cameraData.data(), focusConfig.data()) == 1, "removing hook restores the native focus reset");
    }

    void TestExecutable(const std::filesystem::path &path)
    {
        const auto bytes = ReadFile(path);
        if (bytes.size() < sizeof(IMAGE_DOS_HEADER)) throw std::runtime_error("Invalid test PE");
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(bytes.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > bytes.size())
            throw std::runtime_error("Invalid test PE header");
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(bytes.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            !nt->OptionalHeader.SizeOfImage || nt->OptionalHeader.SizeOfHeaders > bytes.size() ||
            nt->OptionalHeader.SizeOfHeaders > nt->OptionalHeader.SizeOfImage)
            throw std::runtime_error("Invalid test PE image");
        TestImage image(nt->OptionalHeader.SizeOfImage);
        std::memcpy(image.base, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
        const auto *sections = IMAGE_FIRST_SECTION(nt);
        const auto sectionOffset = reinterpret_cast<const std::uint8_t *>(sections) - bytes.data();
        if (sectionOffset + nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > bytes.size())
            throw std::runtime_error("Invalid test PE sections");
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const auto &section = sections[i];
            if (static_cast<std::size_t>(section.PointerToRawData) + section.SizeOfRawData > bytes.size() ||
                static_cast<std::size_t>(section.VirtualAddress) + section.SizeOfRawData > nt->OptionalHeader.SizeOfImage ||
                static_cast<std::size_t>(section.VirtualAddress) + section.Misc.VirtualSize > nt->OptionalHeader.SizeOfImage)
                throw std::runtime_error("Invalid test PE section range");
            std::memcpy(image.base + section.VirtualAddress,
                bytes.data() + section.PointerToRawData, section.SizeOfRawData);
        }
        PatternScanner scanner(reinterpret_cast<HMODULE>(image.base));
        std::ostringstream log;
        const auto bridge = ResolveCameraBridge(scanner, ZoomOnlyConfig(), log);
        std::cout << log.str();
        // 仅对命令行提供的已核对 SHA-256 样本断言 RVA；生产解析器没有 RVA 回退。
        constexpr std::array<std::uintptr_t, 8> rvas{0x0BAD61E0, 0x0BAD74D0, 0x12446560, 0x12448000,
            0x0EF742F0, 0x0EF73930, 0x070E5150, 0x07645AC0};
        for (std::size_t i = 0; i < Functions.size(); ++i)
        {
            const auto &symbol = Symbol(bridge, Functions[i]);
            Check(symbol && symbol.candidates == 1 && symbol.address - scanner.BaseAddress() == rvas[i],
                "full recorded executable resolves each function uniquely");
        }
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc < 2) throw std::runtime_error("Usage: CameraZoomTests <zoom limits evidence dir> [recorded YuanShen.exe]");
        HookManager::Initialize();
        TestDetours(std::filesystem::u8path(argv[1]));
        TestFocusRatioPreservation(std::filesystem::u8path(argv[1]));
        TestEvidence(std::filesystem::u8path(argv[1]));
        HookManager::Uninitialize();
        if (argc >= 3) TestExecutable(std::filesystem::u8path(argv[2]));
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    if (passed) std::cout << "CameraZoomTests passed\n";
    return passed ? 0 : 1;
}
