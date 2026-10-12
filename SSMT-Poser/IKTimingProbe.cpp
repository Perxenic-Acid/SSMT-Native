#include "IKTimingProbe.h"

#include <MinHook.h>
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <ostream>

namespace poser {
namespace {

using OnAnimatorIK = void (*)(uintptr_t, int, uintptr_t);

constexpr unsigned MaxSamples = 64;

struct Sample {
    DWORD thread = 0;
    uint64_t tick = 0;
    uintptr_t helper = 0;
    uintptr_t selectedAnimator = 0;
    int layer = 0;
    bool mainThread = false;
};

NativeMethod method{};
uintptr_t hookAddress = 0;
OnAnimatorIK original = nullptr;
uintptr_t selectedAnimator = 0;
uintptr_t selectedNative = 0;
DWORD unityThread = 0;
std::atomic<bool> installed{false};
std::atomic<unsigned> inFlight{0};
std::atomic<uint64_t> calls{0};
std::atomic<uint64_t> offMain{0};
std::atomic<uint64_t> faults{0};
std::atomic<unsigned> sampleCount{0};
Sample samples[MaxSamples]{};

template<class T>
bool Read(uintptr_t address, T& value) {
    SIZE_T count = 0;
    return address && address <= UINTPTR_MAX - sizeof(T) &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
            &value, sizeof(T), &count) && count == sizeof(T);
}

void OnAnimatorIKHook(uintptr_t helper, int layer, uintptr_t methodInfo) {
    inFlight.fetch_add(1, std::memory_order_acq_rel);
    __try {
        const bool active = installed.load(std::memory_order_acquire);
        if (active) {
            calls.fetch_add(1, std::memory_order_relaxed);
            const auto thread = GetCurrentThreadId();
            const bool main = !unityThread || thread == unityThread;
            if (!main) offMain.fetch_add(1, std::memory_order_relaxed);
            const auto index = sampleCount.fetch_add(1, std::memory_order_relaxed);
            if (index < MaxSamples) {
                samples[index] = Sample{thread, GetTickCount64(), helper,
                    selectedAnimator, layer, main};
            }
        }
        if (original) original(helper, layer, methodInfo);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        faults.fetch_add(1, std::memory_order_relaxed);
    }
    inFlight.fetch_sub(1, std::memory_order_acq_rel);
}

void Reset() {
    method = {};
    hookAddress = 0;
    original = nullptr;
    selectedAnimator = 0;
    selectedNative = 0;
    unityThread = 0;
    calls = 0;
    offMain = 0;
    faults = 0;
    sampleCount = 0;
    for (auto& sample : samples) sample = {};
}

}

bool InstallIKTimingProbe(GenshinNativeRuntime& runtime, DWORD mainThread,
    uintptr_t animator, std::ostream& report) {
    if (installed.load(std::memory_order_acquire) || hookAddress) {
        report << "ik_timing_install=ALREADY_INSTALLED\n";
        return false;
    }
    if (!animator) {
        report << "ik_timing_blocker=selected_animator_missing\n";
        return false;
    }
    NativeClass klass;
    if (!runtime.FindClass("MoleMole", "MonoAnimationIKEventHelper", klass) ||
        !runtime.FindMethod(klass, "OnAnimatorIK", 1, method) ||
        !method.entry || !method.target || !method.descriptor) {
        report << "ik_timing_blocker=OnAnimatorIK_descriptor\n";
        method = {};
        return false;
    }
    selectedAnimator = animator;
    Read(animator + 0x10, selectedNative);
    unityThread = mainThread;
    hookAddress = method.entry;

    const auto initialize = MH_Initialize();
    const bool ownsMinHook = initialize == MH_OK;
    if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
        report << "ik_timing_hook_initialize=" << MH_StatusToString(initialize) << '\n';
        Reset();
        return false;
    }
    bool created = false;
    auto status = MH_CreateHook(reinterpret_cast<void*>(hookAddress),
        reinterpret_cast<void*>(&OnAnimatorIKHook), reinterpret_cast<void**>(&original));
    if (status == MH_OK) {
        created = true;
        status = MH_EnableHook(reinterpret_cast<void*>(hookAddress));
    }
    report << "[IK_TIMING_METHOD] namespace=MoleMole class=MonoAnimationIKEventHelper method=OnAnimatorIK"
        << " id=" << method.metadataId << " parameters=" << unsigned(method.parameterCount)
        << " entry=0x" << std::hex << method.entry << " target=0x" << method.target
        << " descriptor=0x" << method.descriptor << std::dec << '\n';
    report << "ik_timing_hook_install=" << MH_StatusToString(status)
        << " minhook_owner=" << (ownsMinHook ? 1 : 0)
        << " selected_animator=0x" << std::hex << selectedAnimator
        << " selected_native=0x" << selectedNative << std::dec
        << " main_tid=" << unityThread << '\n';
    report << "ik_timing_semantics=READ_ONLY; setters=NOT_CALLED; motion_gate=NON_BLOCKING\n";
    report.flush();
    if (status != MH_OK) {
        if (created) MH_RemoveHook(reinterpret_cast<void*>(hookAddress));
        if (ownsMinHook) MH_Uninitialize();
        Reset();
        return false;
    }
    calls = 0;
    offMain = 0;
    faults = 0;
    sampleCount = 0;
    installed.store(true, std::memory_order_release);
    return true;
}

bool RemoveIKTimingProbe(std::ostream& report) {
    installed.store(false, std::memory_order_release);
    bool passed = true;
    if (hookAddress) {
        auto status = MH_DisableHook(reinterpret_cast<void*>(hookAddress));
        passed = status == MH_OK || status == MH_ERROR_DISABLED;
        report << "ik_timing_hook_disable=" << MH_StatusToString(status) << '\n';
        const auto deadline = GetTickCount64() + 3000;
        while (inFlight.load(std::memory_order_acquire) && GetTickCount64() < deadline) Sleep(1);
        passed = passed && !inFlight.load(std::memory_order_acquire);
        if (passed) {
            status = MH_RemoveHook(reinterpret_cast<void*>(hookAddress));
            passed = status == MH_OK;
            report << "ik_timing_hook_remove=" << MH_StatusToString(status) << '\n';
        }
        if (!passed) {
            HMODULE pinned = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&OnAnimatorIKHook), &pinned);
            report << "ik_timing_hook_cleanup=FAILED_MODULE_PINNED\n";
        } else {
            // Keep the bounded counters/samples for the session report. The
            // next install resets them after its hook has been created.
            hookAddress = 0;
            original = nullptr;
            method = {};
        }
    }
    report << "ik_timing_hook_inflight=" << inFlight.load() << '\n';
    return passed;
}

bool IKTimingProbeInstalled() { return installed.load(std::memory_order_acquire); }

void ReportIKTimingProbe(std::ostream& report) {
    const auto count = sampleCount.load(std::memory_order_acquire);
    report << "[IK_TIMING_SUMMARY] installed=" << (IKTimingProbeInstalled() ? 1 : 0)
        << " calls=" << calls.load() << " sampled=" << (count < MaxSamples ? count : MaxSamples)
        << " off_main=" << offMain.load() << " fault=" << faults.load()
        << " selected_animator=0x" << std::hex << selectedAnimator
        << " selected_native=0x" << selectedNative << std::dec
        << " main_tid=" << unityThread << '\n';
    const auto limit = count < MaxSamples ? count : MaxSamples;
    for (unsigned i = 0; i < limit; ++i) {
        const auto& sample = samples[i];
        report << "[IK_TIMING_CALLBACK] index=" << i << " tid=" << sample.thread
            << " tick=" << sample.tick << " layer=" << sample.layer
            << " helper=0x" << std::hex << sample.helper
            << " selected_animator=0x" << sample.selectedAnimator << std::dec
            << " main_thread=" << (sample.mainThread ? 1 : 0) << '\n';
    }
}

}
