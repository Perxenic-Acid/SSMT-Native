#pragma once
#include <cstdint>
#include <span>

namespace SSMT::Tweaks
{
    class HookManager
    {
    public:
        static void Initialize();
        static void Uninitialize();

        static void Create(
            std::uintptr_t target,
            void* detour,
            void** original
        );
        static void CreateDisabled(std::uintptr_t target, void *detour, void **original);
        static void Enable(std::span<const std::uintptr_t> targets);
        static void Remove(std::uintptr_t target);
    };
}
