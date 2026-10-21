#include "CharacterVisibility.h"
#include "Core/HookManager.h"

#include <cstring>
#include <exception>
#include <stdexcept>

namespace SSMT::Tweaks::Genshin::CameraTweaks
{
    namespace
    {
        bool Match(const PatternScanner &scanner, std::uintptr_t address,
            std::string_view pattern)
        {
            const auto size = (pattern.size() + 1) / 3;
            if (!scanner.IsReadableAddress(address, size)) return false;
            const auto *bytes = reinterpret_cast<const std::uint8_t *>(address);
            for (std::size_t i = 0; i < size; ++i)
                if (pattern[i * 3] != '?' && bytes[i] !=
                    ((PatternScanner::HexDigit(pattern[i * 3]) << 4) |
                        PatternScanner::HexDigit(pattern[i * 3 + 1]))) return false;
            return true;
        }

        bool InactiveHotfix(const PatternScanner &scanner, std::uintptr_t guard,
            std::uintptr_t expectedBranch)
        {
            if (!Match(scanner, guard, "80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ??")) return false;
            std::int32_t displacement;
            std::memcpy(&displacement, reinterpret_cast<const void *>(guard + 2), sizeof(displacement));
            const auto flag = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(guard + 7) + displacement);
            std::memcpy(&displacement, reinterpret_cast<const void *>(guard + 9), sizeof(displacement));
            const auto branch = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(guard + 13) + displacement);
            return scanner.IsReadableAddress(flag) && *reinterpret_cast<const std::uint8_t *>(flag) == 0 &&
                branch == expectedBranch && scanner.IsExecutableAddress(branch);
        }

        bool FloatConstant(const PatternScanner &scanner, std::uintptr_t instruction,
            std::size_t displacementOffset, std::size_t instructionSize, std::uint32_t expected)
        {
            std::int32_t displacement;
            std::memcpy(&displacement, reinterpret_cast<const void *>(instruction + displacementOffset), sizeof(displacement));
            const auto constant = static_cast<std::uintptr_t>(
                static_cast<std::intptr_t>(instruction + instructionSize) + displacement);
            std::uint32_t bits = 0;
            if (!scanner.IsReadableAddress(constant, sizeof(bits))) return false;
            std::memcpy(&bits, reinterpret_cast<const void *>(constant), sizeof(bits));
            return bits == expected;
        }
    }

    bool ValidateSceneCameraFade(const PatternScanner &scanner, std::uintptr_t address)
    {
        // 7.1 CN/Bilibili：角色展示场景从镜头到模型骨骼的距离算出淡出值，直接写入
        // 渲染组件，绕过 PlayerPerspective。只核对这个路径，不能替换通用显隐 setter。
        return InactiveHotfix(scanner, address + 0x8C, address + 0x3FF) &&
            InactiveHotfix(scanner, address + 0x351, address + 0x456) &&
            Match(scanner, address + 0x99,
                "F3 44 0F 10 35 ?? ?? ?? ?? 80 BF 56 01 00 00 00") &&
            FloatConstant(scanner, address + 0x99, 5, 9, 0xBF800000) &&
            Match(scanner, address + 0x13C,
                "F3 44 0F 5C 4C 24 34 F3 44 0F 5E 4E 28") &&
            Match(scanner, address + 0x297,
                "0F 59 D2 F3 0F 58 CA F3 0F 16 C2 F3 0F 58 C1 41 0F 2E C4") &&
            Match(scanner, address + 0x30E,
                "4C 8B 05 ?? ?? ?? ?? BA 01 70 01 00 E8 ?? ?? ?? ?? 48 85 C0 74 68") &&
            Match(scanner, address + 0x327,
                "F3 0F 10 46 18 F3 0F 5C F0 F3 44 0F 5C C0 F3 41 0F 5E F0 "
                "F3 0F 10 05 ?? ?? ?? ?? F3 0F 5D C6 0F 57 C9 F3 0F C2 F1 01 0F 55 F0") &&
            FloatConstant(scanner, address + 0x33A, 4, 8, 0x3F800000) &&
            Match(scanner, address + SceneCameraFadeWriteOffset,
                "0F 2E B7 6C 01 00 00 0F 9A C0 0F 95 C1 08 C1 08 8F 95 01 00 00 "
                "F3 0F 11 B7 6C 01 00 00 48 89 F9 E8 ?? ?? ?? ?? F3 44 0F 10 B7 6C 01 00 00") &&
            scanner.ResolveRelativeBranch(address + 0x37E) != 0 &&
            Match(scanner, address + 0x38C,
                "41 0F 28 C6 0F 28 74 24 60 0F 28 7C 24 70") &&
            Match(scanner, address + 0x3D9,
                "48 81 C4 F8 00 00 00 5B 5D 5F 5E 41 5E 41 5F C3");
    }

    std::array<std::uint8_t, 26> MakeSceneCameraFadeDetour(std::uintptr_t original)
    {
        // 仅覆盖即将提交的镜头淡出标量；保留所有通用寄存器、标志和原生脏标记/应用逻辑。
        std::array<std::uint8_t, 26> code{
            0xF3, 0x0F, 0x10, 0x35, 0x0E, 0x00, 0x00, 0x00, // movss xmm6, [rip + 14]
            0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,             // jmp [rip]
            0, 0, 0, 0, 0, 0, 0, 0,
            0x00, 0x00, 0x80, 0x3F,
        };
        std::memcpy(code.data() + 14, &original, sizeof(original));
        return code;
    }

    bool InstallSceneCameraFadeHook(const PatternScanner &scanner,
        const ResolvedSymbol &symbol, std::ostream &log)
    {
        if (!symbol || !ValidateSceneCameraFade(scanner, symbol.address))
        {
            log << "Camera.SceneCharacterFade disabled: "
                << (symbol ? "camera fade layout or IFix state changed" : symbol.validation) << '\n';
            return false;
        }
        const auto target = symbol.address + SceneCameraFadeWriteOffset;
        void *code = nullptr;
        bool created = false;
        try
        {
            code = VirtualAlloc(nullptr, 26, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (!code) throw std::runtime_error("VirtualAlloc failed");
            void *original = nullptr;
            HookManager::CreateDisabled(target, code, &original);
            created = true;
            const auto bytes = MakeSceneCameraFadeDetour(reinterpret_cast<std::uintptr_t>(original));
            std::memcpy(code, bytes.data(), bytes.size());
            DWORD protection;
            if (!VirtualProtect(code, bytes.size(), PAGE_EXECUTE_READ, &protection) ||
                !FlushInstructionCache(GetCurrentProcess(), code, bytes.size()))
                throw std::runtime_error("Cannot prepare camera fade detour");
            const std::array targets{target};
            HookManager::Enable(targets);
            // 与现有缩放 Hook 相同，成功安装的跳板随插件和游戏进程常驻。
            log << "Camera.SceneCharacterFade enabled (native camera opacity only).\n";
            return true;
        }
        catch (const std::exception &error)
        {
            if (created)
            {
                try { HookManager::Remove(target); }
                catch (const std::exception &rollback)
                {
                    // 不能确认 Hook 已移除时保留代码，避免原生线程跳入已释放的页。
                    log << "Camera.SceneCharacterFade rollback failed: " << rollback.what() << '\n';
                    code = nullptr;
                }
            }
            if (code) VirtualFree(code, 0, MEM_RELEASE);
            log << "Camera.SceneCharacterFade disabled: " << error.what() << '\n';
            return false;
        }
    }
}
