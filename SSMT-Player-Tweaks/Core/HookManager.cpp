#pragma once
#include "HookManager.h"
#include <cstdint>
#include <MinHook.h>
#include <stdexcept>
#include <string>

namespace SSMT::Tweaks
{
    void HookManager::Initialize()
    {
        const MH_STATUS status = MH_Initialize();

        if (status == MH_OK)
            return;

        if (status == MH_ERROR_ALREADY_INITIALIZED)
            return;

        throw std::runtime_error(
            std::string("HookManager: MH_Initialize failed: ") + MH_StatusToString(status));
    }
    void HookManager::Uninitialize()
    {
        const MH_STATUS status = MH_Uninitialize();

        if (status == MH_OK)
            return;

        if (status == MH_ERROR_NOT_INITIALIZED)
            return;

        throw std::runtime_error(
            std::string("HookManager: MH_Uninitialize failed: ") + MH_StatusToString(status));
    }
    void HookManager::Create(std::uintptr_t target, void *detour, void **original)
    {
        CreateDisabled(target, detour, original);
        try
        {
            const auto status = MH_EnableHook(reinterpret_cast<void *>(target));
            if (status != MH_OK)
                throw std::runtime_error(std::string("HookManager: MH_EnableHook failed: ") + MH_StatusToString(status));
        }
        catch (...)
        {
            Remove(target);
            throw;
        }
    }
    void HookManager::CreateDisabled(std::uintptr_t target, void *detour, void **original)
    {
        const MH_STATUS createStatus =
            MH_CreateHook(
                reinterpret_cast<void *>(target),
                detour,
                original);

        if (createStatus != MH_OK)
        {
            throw std::runtime_error(
                std::string("HookManager: MH_CreateHook failed: ") + MH_StatusToString(createStatus));
        }
    }
    void HookManager::Enable(std::span<const std::uintptr_t> targets)
    {
        // 在同一次线程暂停中启用关联的限制点，避免逐个安装产生半启用状态。
        for (const auto target : targets)
        {
            const auto status = MH_QueueEnableHook(reinterpret_cast<void *>(target));
            if (status != MH_OK)
                throw std::runtime_error(std::string("HookManager: MH_QueueEnableHook failed: ") + MH_StatusToString(status));
        }
        const auto status = MH_ApplyQueued();
        if (status != MH_OK)
            throw std::runtime_error(std::string("HookManager: MH_ApplyQueued failed: ") + MH_StatusToString(status));
    }
    void HookManager::Remove(std::uintptr_t target)
    {
        const auto status = MH_RemoveHook(reinterpret_cast<void *>(target));
        if (status != MH_OK)
            throw std::runtime_error(std::string("HookManager: MH_RemoveHook failed: ") + MH_StatusToString(status));
    }
}
