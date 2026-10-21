#include "Games/Genshin/Features/CameraTweaks/CharacterVisibility.h"
#include "Games/Genshin/GenshinCameraBridge.h"
#include "Core/HookManager.h"

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
    void Check(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    std::vector<std::uint8_t> ReadFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        Check(input && input.tellg() > 0, "Cannot read evidence");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(input.tellg()));
        input.seekg(0);
        Check(static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()), bytes.size())), "Incomplete evidence");
        return bytes;
    }

    struct Image
    {
        std::uint8_t *base;
        explicit Image(std::size_t size) : base(static_cast<std::uint8_t *>(VirtualAlloc(
            nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)))
        { Check(base != nullptr, "VirtualAlloc failed"); }
        ~Image() { VirtualFree(base, 0, MEM_RELEASE); }
        Image(const Image &) = delete;
        Image &operator=(const Image &) = delete;
    };

    void Relocate(std::uint8_t *instruction, std::size_t offset, std::size_t length, const void *target)
    {
        const auto delta = reinterpret_cast<std::intptr_t>(target) - reinterpret_cast<std::intptr_t>(instruction + length);
        Check(delta >= INT32_MIN && delta <= INT32_MAX, "Test relocation overflow");
        const auto value = static_cast<std::int32_t>(delta);
        std::memcpy(instruction + offset, &value, sizeof(value));
    }

    void TestLayout(const std::vector<std::uint8_t> &evidence)
    {
        Check(evidence.size() == 0x48B, "Unexpected camera fade evidence size");
        Image image(0x6000);
        auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(image.base);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x80;
        auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(image.base + 0x80);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.NumberOfSections = 1;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt->OptionalHeader.SizeOfImage = 0x6000;
        auto *section = IMAGE_FIRST_SECTION(nt);
        section->VirtualAddress = 0x1000;
        section->Misc.VirtualSize = 0x4000;
        section->Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE;
        const float one = 1;
        std::memcpy(image.base + 0x5004, &one, sizeof(one));
        const float noChange = -1;
        std::memcpy(image.base + 0x5008, &noChange, sizeof(noChange));
        auto prepare = [&](std::size_t offset)
        {
            auto *code = image.base + offset;
            std::memcpy(code, evidence.data(), evidence.size());
            Relocate(code + 0x8C, 2, 7, image.base + 0x5000);
            Relocate(code + 0x351, 2, 7, image.base + 0x5001);
            Relocate(code + 0x37E, 1, 5, image.base + 0x4000);
            Relocate(code + 0x99, 5, 9, image.base + 0x5008);
            Relocate(code + 0x33A, 4, 8, image.base + 0x5004);
        };
        prepare(0x1000);
        PatternScanner scanner(reinterpret_cast<HMODULE>(image.base));
        CameraPolicyConfig config{};
        std::ostringstream log;
        auto resolve = [&]() { return ResolveCameraBridge(scanner, config, log).sceneCameraFade; };
        auto symbol = resolve();
        Check(symbol && symbol.candidates == 1 && symbol.address == scanner.BaseAddress() + 0x1000,
            "Camera-specific scene path must resolve uniquely");
        config.disableCharacterFade = false;
        Check(!resolve() && resolve().candidates == 0, "Disabled feature must perform no discovery");
        config.disableCharacterFade = true;
        for (const auto offset : {0x99, 0x13C, 0x297, 0x30E, 0x327, 0x35E, 0x37D, 0x38C, 0x3D9})
        {
            image.base[0x1000 + offset] ^= 1;
            Check(!resolve(), "Changed distance, renderer write or return layout must fail closed");
            Check(!InstallSceneCameraFadeHook(scanner, symbol, log), "Revalidate stale symbols before installing");
            image.base[0x1000 + offset] ^= 1;
        }
        for (const auto offset : {0x5000, 0x5001})
        {
            image.base[offset] = 1;
            Check(!resolve(), "Active IFix must fail closed");
            image.base[offset] = 0;
        }
        image.base[0x1000 + 0x8C + 9] ^= 1;
        Check(!resolve(), "Changed IFix branch target must fail closed");
        image.base[0x1000 + 0x8C + 9] ^= 1;
        image.base[0x5004] ^= 1;
        Check(!resolve(), "Changed opaque scalar must fail closed");
        image.base[0x5004] ^= 1;
        image.base[0x5008] ^= 1;
        Check(!resolve(), "Changed no-op return sentinel must fail closed");
        image.base[0x5008] ^= 1;
        prepare(0x2000);
        Check(!resolve() && resolve().candidates == 2, "Ambiguous functions must fail closed");
        std::memset(image.base + 0x2000, 0, evidence.size());

        Check(InstallSceneCameraFadeHook(scanner, symbol, log), "Verified scene hook must install");
        Check(!InstallSceneCameraFadeHook(scanner, symbol, log), "Repeated installation must leave existing hook intact");
        HookManager::Remove(symbol.address + SceneCameraFadeWriteOffset);
        Check(ValidateSceneCameraFade(scanner, symbol.address), "Removal must restore original commit instructions");
        auto *code = image.base + 0x4000;
        code[0] = 0xC3;
        void *original = nullptr;
        HookManager::CreateDisabled(symbol.address + SceneCameraFadeWriteOffset, code, &original);
        Check(!InstallSceneCameraFadeHook(scanner, symbol, log), "An existing foreign hook must not be replaced");
        HookManager::Enable(std::array{symbol.address + SceneCameraFadeWriteOffset});
        HookManager::Remove(symbol.address + SceneCameraFadeWriteOffset);
        Check(ValidateSceneCameraFade(scanner, symbol.address), "Installation conflict must preserve the foreign hook");
    }

    void TestNativeCommit(const std::vector<std::uint8_t> &evidence)
    {
        Image image(0x4000);
        auto *entry = image.base + 0x1000;
        auto *commit = image.base + 0x2000;
        auto *apply = image.base + 0x3000;
        // 只执行已复制的通用 SSE 提交块；不执行任何完整游戏函数或访问游戏对象。
        const std::uint8_t prologue[]{0x57, 0x48, 0x83, 0xEC, 0x30,
            0x0F, 0x29, 0x74, 0x24, 0x20, 0x48, 0x89, 0xCF, 0x0F, 0x28, 0xF1, 0xE9, 0, 0, 0, 0};
        std::memcpy(entry, prologue, sizeof(prologue));
        Relocate(entry + 16, 1, 5, commit);
        constexpr std::size_t commitSize = 0x383 - SceneCameraFadeWriteOffset;
        std::memcpy(commit, evidence.data() + SceneCameraFadeWriteOffset, commitSize);
        Relocate(commit + 0x37E - SceneCameraFadeWriteOffset, 1, 5, apply);
        const std::uint8_t epilogue[]{0xF3, 0x0F, 0x10, 0x87, 0x6C, 0x01, 0, 0,
            0x0F, 0x28, 0x74, 0x24, 0x20, 0x48, 0x83, 0xC4, 0x30, 0x5F, 0xC3};
        std::memcpy(commit + commitSize, epilogue, sizeof(epilogue));
        const std::uint8_t applyBody[]{0xFF, 0x81, 0x98, 0x01, 0, 0, 0xC3}; // 记录应用次数
        std::memcpy(apply, applyBody, sizeof(applyBody));
        using Run = float (*)(void *, float);
        const auto run = reinterpret_cast<Run>(entry);
        std::array<std::uint8_t, 0x200> state{};
        auto reset = [&](float before)
        {
            state.fill(0);
            std::memcpy(state.data() + 0x16C, &before, sizeof(before));
        };
        reset(1);
        Check(run(state.data(), 0.25f) == 0.25f && state[0x195] == 1 && state[0x198] == 1,
            "Original block must submit camera fade and apply it");
        void *original = nullptr;
        HookManager::CreateDisabled(reinterpret_cast<std::uintptr_t>(commit), image.base, &original);
        const auto bytes = MakeSceneCameraFadeDetour(reinterpret_cast<std::uintptr_t>(original));
        std::memcpy(image.base, bytes.data(), bytes.size());
        FlushInstructionCache(GetCurrentProcess(), image.base, 0x4000);
        HookManager::Enable(std::array{reinterpret_cast<std::uintptr_t>(commit)});
        for (const float incoming : {0.0f, 0.25f, 0.99f, 1.0f, std::numeric_limits<float>::quiet_NaN()})
        {
            reset(0.2f);
            Check(run(state.data(), incoming) == 1 && state[0x195] == 1 && state[0x198] == 1,
                "Camera fade must become opaque through the original dirty and apply path");
            Check(state[0x174] == 0 && state[0x175] == 0 && state[0x184] == 0 && state[0x18C] == 0,
                "Unrelated hide and transition channels must remain untouched");
        }
        reset(1);
        Check(run(state.data(), 0) == 1 && state[0x195] == 0 && state[0x198] == 1,
            "Already opaque model must not get a spurious dirty flag");
        HookManager::Remove(reinterpret_cast<std::uintptr_t>(commit));
        reset(1);
        Check(run(state.data(), 0) == 0, "Removing the hook must restore camera fade");
    }

    void TestExecutable(const std::filesystem::path &path)
    {
        const auto bytes = ReadFile(path);
        Check(bytes.size() >= sizeof(IMAGE_DOS_HEADER), "Invalid PE");
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(bytes.data());
        Check(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew >= 0 &&
            static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= bytes.size(), "Invalid PE header");
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(bytes.data() + dos->e_lfanew);
        Check(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
            nt->OptionalHeader.SizeOfHeaders <= bytes.size() &&
            nt->OptionalHeader.SizeOfHeaders <= nt->OptionalHeader.SizeOfImage, "Invalid PE image");
        Image image(nt->OptionalHeader.SizeOfImage);
        std::memcpy(image.base, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
        const auto *sections = IMAGE_FIRST_SECTION(nt);
        Check(reinterpret_cast<const std::uint8_t *>(sections) - bytes.data() +
            nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) <= bytes.size(), "Invalid PE sections");
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const auto &s = sections[i];
            Check(static_cast<std::size_t>(s.PointerToRawData) + s.SizeOfRawData <= bytes.size() &&
                static_cast<std::size_t>(s.VirtualAddress) + s.SizeOfRawData <= nt->OptionalHeader.SizeOfImage &&
                static_cast<std::size_t>(s.VirtualAddress) + s.Misc.VirtualSize <= nt->OptionalHeader.SizeOfImage,
                "Invalid PE section range");
            std::memcpy(image.base + s.VirtualAddress, bytes.data() + s.PointerToRawData, s.SizeOfRawData);
        }
        PatternScanner scanner(reinterpret_cast<HMODULE>(image.base));
        std::ostringstream log;
        const auto bridge = ResolveCameraBridge(scanner, CameraPolicyConfig{}, log);
        std::cout << log.str();
        Check(bridge.sceneCameraFade && bridge.sceneCameraFade.candidates == 1 &&
            bridge.sceneCameraFade.address - scanner.BaseAddress() == 0x0CBBE480,
            "Recorded executable must resolve the camera-only scene fade uniquely");
        Check(static_cast<bool>(bridge.playerPerspective), "Existing world fade path must remain available");
    }
}

int main(int argc, char **argv)
{
    try
    {
        Check(argc >= 2, "Usage: CharacterVisibilityTests <camera fade evidence bin> [recorded YuanShen.exe]");
        const auto evidence = ReadFile(std::filesystem::u8path(argv[1]));
        HookManager::Initialize();
        TestLayout(evidence);
        TestNativeCommit(evidence);
        HookManager::Uninitialize();
        if (argc > 2) TestExecutable(std::filesystem::u8path(argv[2]));
        std::cout << "CharacterVisibilityTests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
