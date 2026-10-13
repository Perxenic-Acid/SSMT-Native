#include "IKTimingProbe.h"

#include <MinHook.h>
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>

namespace poser {
namespace {

using OnAnimatorIK = void (*)(uintptr_t, int, uintptr_t);

constexpr unsigned MaxSamples = 128;
// Threaded IK callbacks can fill the general sample ring before the sparse
// main-thread callback arrives. Keep an independent small capture so identity
// evidence is never lost to callback ordering.
constexpr unsigned MaxMainSamples = 16;
constexpr unsigned MaxHooks = 4;

struct Sample {
    DWORD thread = 0;
    uint64_t tick = 0;
    uintptr_t instance = 0;
    uintptr_t instanceClass = 0;
    uintptr_t instanceNative = 0;
    uintptr_t selectedAnimator = 0;
    uintptr_t helperGameObject = 0;
    uintptr_t helperGameObjectNative = 0;
    uintptr_t animatorGameObject = 0;
    uintptr_t animatorGameObjectNative = 0;
    int layer = 0;
    uint8_t hook = 0;
    uint8_t identityStatus = 0;
    bool mainThread = false;
};

struct HookSlot {
    const char* namespaze = "";
    const char* klass = "";
    const char* name = "";
    NativeMethod method{};
    uintptr_t address = 0;
    uintptr_t expectedClass = 0;
    OnAnimatorIK original = nullptr;
    bool created = false;
};

std::array<HookSlot, MaxHooks> hooks{{
    {"MoleMole", "MonoAnimationIKEventHelper", "OnAnimatorIK"},
    {"MoleMole", "MonoAnimationIKEventHelper", "OnAnimatorIKThreaded"},
    {"MoleMole", "AnimageIKRig", "OnAnimatorIK"},
    {"MoleMole", "AnimageIKRig", "OnAnimatorIKThreaded"},
}};
uintptr_t selectedAnimator = 0;
uintptr_t selectedNative = 0;
uintptr_t animatorClass = 0;
uintptr_t gameObjectClass = 0;
uintptr_t gameObjectGetter = 0;
uintptr_t gameObjectDescriptor = 0;
DWORD unityThread = 0;
std::atomic<bool> installed{false};
std::atomic<unsigned> inFlight{0};
std::atomic<uint64_t> calls{0};
std::array<std::atomic<uint64_t>, MaxHooks> hookCalls{};
std::atomic<uint64_t> offMain{0};
std::atomic<uint64_t> faults{0};
std::atomic<uint64_t> identityFaults{0};
std::atomic<unsigned> sampleCount{0};
std::atomic<unsigned> identityProbeCount{0};
Sample samples[MaxSamples]{};
Sample mainSamples[MaxMainSamples]{};

using Getter = uintptr_t (*)(uintptr_t, uintptr_t);

template<class T>
bool Read(uintptr_t address, T& value) {
    SIZE_T count = 0;
    return address && address <= UINTPTR_MAX - sizeof(T) &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
            &value, sizeof(T), &count) && count == sizeof(T);
}

bool ObjectIdentity(uintptr_t object, uintptr_t expectedClass, uintptr_t& native) {
    uintptr_t actualClass = 0;
    return Read(object, actualClass) && actualClass == expectedClass &&
        Read(object + 0x10, native) && native;
}

DWORD CaptureIdentity(unsigned index, uintptr_t instance, Sample& sample) {
    __try {
        if (index >= hooks.size() || !hooks[index].expectedClass || !gameObjectGetter ||
            !animatorClass || !gameObjectClass) {
            sample.identityStatus = 1;
            return 0;
        }
        if (!ObjectIdentity(instance, hooks[index].expectedClass, sample.instanceNative)) {
            sample.identityStatus = 2;
            return 0;
        }
        uintptr_t selectedNativeNow = 0;
        if (!ObjectIdentity(selectedAnimator, animatorClass, selectedNativeNow) ||
            selectedNativeNow != selectedNative) {
            sample.identityStatus = 3;
            return 0;
        }
        sample.helperGameObject = reinterpret_cast<Getter>(gameObjectGetter)(instance, gameObjectDescriptor);
        sample.animatorGameObject = reinterpret_cast<Getter>(gameObjectGetter)(selectedAnimator, gameObjectDescriptor);
        if (!ObjectIdentity(sample.helperGameObject, gameObjectClass, sample.helperGameObjectNative) ||
            !ObjectIdentity(sample.animatorGameObject, gameObjectClass, sample.animatorGameObjectNative)) {
            sample.identityStatus = 4;
            return 0;
        }
        sample.identityStatus = sample.helperGameObject == sample.animatorGameObject &&
            sample.helperGameObjectNative == sample.animatorGameObjectNative ? 5 : 6;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sample.identityStatus = 7;
        return GetExceptionCode();
    }
}

const char* IdentityName(uint8_t status) {
    switch (status) {
    case 1: return "METHOD_UNAVAILABLE";
    case 2: return "HELPER_CLASS_OR_NATIVE_MISMATCH";
    case 3: return "SELECTED_ANIMATOR_IDENTITY_MISMATCH";
    case 4: return "GAMEOBJECT_IDENTITY_MISMATCH";
    case 5: return "SAME_GAMEOBJECT";
    case 6: return "DIFFERENT_GAMEOBJECT";
    case 7: return "SEH_FAULT";
    default: return "NOT_SAMPLED";
    }
}

void Record(unsigned index, uintptr_t instance, int layer) {
    calls.fetch_add(1, std::memory_order_relaxed);
    if (index < hookCalls.size()) hookCalls[index].fetch_add(1, std::memory_order_relaxed);
    const auto thread = GetCurrentThreadId();
    const bool main = !unityThread || thread == unityThread;
    if (!main) offMain.fetch_add(1, std::memory_order_relaxed);
    const auto sampleIndex = sampleCount.fetch_add(1, std::memory_order_relaxed);
    Sample* sample = nullptr;
    if (main) {
        const auto mainIndex = identityProbeCount.fetch_add(1, std::memory_order_relaxed);
        if (mainIndex < MaxMainSamples) sample = &mainSamples[mainIndex];
    } else if (sampleIndex < MaxSamples) {
        sample = &samples[sampleIndex];
    }
    if (!sample) return;
    *sample = Sample{};
    sample->thread = thread;
    sample->tick = GetTickCount64();
    sample->instance = instance;
    Read(instance, sample->instanceClass);
    sample->selectedAnimator = selectedAnimator;
    sample->layer = layer;
    sample->hook = static_cast<uint8_t>(index);
    sample->mainThread = main;
    if (main) {
        const auto fault = CaptureIdentity(index, instance, *sample);
        if (fault) identityFaults.fetch_add(1, std::memory_order_relaxed);
    }
}

void Invoke(unsigned index, uintptr_t instance, int layer, uintptr_t methodInfo) {
    inFlight.fetch_add(1, std::memory_order_acq_rel);
    __try {
        if (installed.load(std::memory_order_acquire)) Record(index, instance, layer);
        const auto original = index < hooks.size() ? hooks[index].original : nullptr;
        if (original) original(instance, layer, methodInfo);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        faults.fetch_add(1, std::memory_order_relaxed);
    }
    inFlight.fetch_sub(1, std::memory_order_acq_rel);
}

void AnimationHelperHook(uintptr_t instance, int layer, uintptr_t methodInfo) {
    Invoke(0, instance, layer, methodInfo);
}
void AnimationHelperThreadedHook(uintptr_t instance, int layer, uintptr_t methodInfo) {
    Invoke(1, instance, layer, methodInfo);
}
void AnimageRigHook(uintptr_t instance, int layer, uintptr_t methodInfo) {
    Invoke(2, instance, layer, methodInfo);
}
void AnimageRigThreadedHook(uintptr_t instance, int layer, uintptr_t methodInfo) {
    Invoke(3, instance, layer, methodInfo);
}

void* Callback(unsigned index) {
    switch (index) {
    case 0: return reinterpret_cast<void*>(&AnimationHelperHook);
    case 1: return reinterpret_cast<void*>(&AnimationHelperThreadedHook);
    case 2: return reinterpret_cast<void*>(&AnimageRigHook);
    case 3: return reinterpret_cast<void*>(&AnimageRigThreadedHook);
    default: return nullptr;
    }
}

void ResetCounters() {
    calls = 0;
    offMain = 0;
    faults = 0;
    identityFaults = 0;
    sampleCount = 0;
    identityProbeCount = 0;
    for (auto& count : hookCalls) count = 0;
    for (auto& sample : samples) sample = {};
    for (auto& sample : mainSamples) sample = {};
}

void ResetDescriptors() {
    for (auto& hook : hooks) {
        hook.method = {};
        hook.address = 0;
        hook.expectedClass = 0;
        hook.original = nullptr;
        hook.created = false;
    }
}

void ReportStandardAnimatorIK(GenshinNativeRuntime& runtime, std::ostream& report) {
    NativeClass animator;
    if (!runtime.FindClass("UnityEngine", "Animator", animator)) {
        report << "[ANIMATOR_IK_ABI] class=UnityEngine.Animator status=UNRESOLVED\n";
        return;
    }
    struct Candidate { const char* name; uint8_t parameters; };
    constexpr Candidate candidates[] = {
        {"GetIKPosition", 1}, {"SetIKPosition", 2},
        {"GetIKRotation", 1}, {"SetIKRotation", 2},
        {"SetIKPositionWeight", 2}, {"SetIKRotationWeight", 2},
        {"SetIKHintPosition", 2}, {"SetIKHintPositionWeight", 2},
    };
    for (const auto& candidate : candidates) {
        NativeMethod method;
        const bool ready = runtime.FindMethod(animator, candidate.name, candidate.parameters, method) &&
            method.descriptor && method.entry && method.target;
        report << "[ANIMATOR_IK_ABI] method=" << candidate.name
            << " parameters=" << unsigned(candidate.parameters)
            << " status=" << (ready ? "DESCRIPTOR_VERIFIED" : "UNRESOLVED");
        if (ready) {
            report << " id=" << method.metadataId << " entry=0x" << std::hex << method.entry
                << " target=0x" << method.target << " descriptor=0x" << method.descriptor << std::dec
                << " callable=DEFERRED_VECTOR_CANARY";
        }
        report << '\n';
    }
    report << "animator_ik_writer=BLOCKED; reason=CALLBACK_IDENTITY_AND_VECTOR_ABI_REQUIRED\n";
}

}

bool InstallIKTimingProbe(GenshinNativeRuntime& runtime, DWORD mainThread,
    uintptr_t animator, uintptr_t expectedAnimatorClass, uintptr_t expectedGameObjectClass,
    uintptr_t componentGetGameObjectEntry, uintptr_t componentGetGameObjectDescriptor,
    std::ostream& report) {
    if (installed.load(std::memory_order_acquire)) {
        report << "ik_timing_install=ALREADY_INSTALLED\n";
        return false;
    }
    if (!animator) {
        report << "ik_timing_blocker=selected_animator_missing\n";
        return false;
    }
    selectedAnimator = animator;
    Read(animator + 0x10, selectedNative);
    animatorClass = expectedAnimatorClass;
    gameObjectClass = expectedGameObjectClass;
    gameObjectGetter = componentGetGameObjectEntry;
    gameObjectDescriptor = componentGetGameObjectDescriptor;
    unityThread = mainThread;
    ResetDescriptors();
    ResetCounters();
    ReportStandardAnimatorIK(runtime, report);

    const auto initialize = MH_Initialize();
    const bool ownsMinHook = initialize == MH_OK;
    if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) {
        report << "ik_timing_hook_initialize=" << MH_StatusToString(initialize) << '\n';
        return false;
    }
    unsigned created = 0;
    bool failed = false;
    for (unsigned i = 0; i < hooks.size(); ++i) {
        auto& hook = hooks[i];
        NativeClass klass;
        const bool descriptor = runtime.FindClass(hook.namespaze, hook.klass, klass) &&
            runtime.FindMethod(klass, hook.name, 1, hook.method) && hook.method.entry &&
            hook.method.target && hook.method.descriptor;
        if (!descriptor) {
            report << "[IK_TIMING_METHOD] namespace=" << hook.namespaze << " class=" << hook.klass
                << " method=" << hook.name << " status=UNRESOLVED\n";
            continue;
        }
        hook.expectedClass = klass.address;
        hook.address = hook.method.entry;
        report << "[IK_TIMING_METHOD] namespace=" << hook.namespaze << " class=" << hook.klass
            << " method=" << hook.name << " id=" << hook.method.metadataId
            << " parameters=" << unsigned(hook.method.parameterCount)
            << " entry=0x" << std::hex << hook.method.entry << " target=0x" << hook.method.target
            << " descriptor=0x" << hook.method.descriptor << std::dec;
        bool duplicate = false;
        for (unsigned j = 0; j < i; ++j) duplicate |= hooks[j].created && hooks[j].address == hook.address;
        if (duplicate) {
            report << " status=DUPLICATE_ENTRY\n";
            continue;
        }
        const auto status = MH_CreateHook(reinterpret_cast<void*>(hook.address), Callback(i),
            reinterpret_cast<void**>(&hook.original));
        if (status == MH_OK) {
            hook.created = true;
            ++created;
            const auto enable = MH_EnableHook(reinterpret_cast<void*>(hook.address));
            if (enable != MH_OK) {
                report << " status=" << MH_StatusToString(enable) << '\n';
                failed = true;
                break;
            }
            report << " status=MH_OK\n";
        } else {
            report << " status=" << MH_StatusToString(status) << '\n';
            if (status != MH_ERROR_ALREADY_CREATED) failed = true;
        }
    }
    report << "ik_timing_hook_install=" << (failed || !created ? "FAILED" : "MH_OK")
        << " hooks=" << created << " minhook_owner=" << (ownsMinHook ? 1 : 0)
        << " selected_animator=0x" << std::hex << selectedAnimator
        << " selected_native=0x" << selectedNative << std::dec
        << " main_tid=" << unityThread << '\n';
    report << "ik_timing_semantics=READ_ONLY; setters=NOT_CALLED; callback_identity=RECORDED; motion_gate=NON_BLOCKING\n";
    report.flush();
    if (failed || !created) {
        for (auto& hook : hooks) if (hook.created) {
            MH_DisableHook(reinterpret_cast<void*>(hook.address));
            MH_RemoveHook(reinterpret_cast<void*>(hook.address));
        }
        if (ownsMinHook) MH_Uninitialize();
        ResetDescriptors();
        return false;
    }
    installed.store(true, std::memory_order_release);
    return true;
}

bool RemoveIKTimingProbe(std::ostream& report) {
    installed.store(false, std::memory_order_release);
    bool passed = true;
    unsigned activeHooks = 0;
    for (const auto& hook : hooks) if (hook.created) ++activeHooks;
    for (auto& hook : hooks) if (hook.created) {
        const auto status = MH_DisableHook(reinterpret_cast<void*>(hook.address));
        passed = passed && (status == MH_OK || status == MH_ERROR_DISABLED);
        report << "ik_timing_hook_disable method=" << hook.name << " status=" << MH_StatusToString(status) << '\n';
    }
    const auto deadline = GetTickCount64() + 3000;
    while (inFlight.load(std::memory_order_acquire) && GetTickCount64() < deadline) Sleep(1);
    passed = passed && !inFlight.load(std::memory_order_acquire);
    if (passed) for (auto& hook : hooks) if (hook.created) {
        const auto status = MH_RemoveHook(reinterpret_cast<void*>(hook.address));
        passed = passed && status == MH_OK;
        report << "ik_timing_hook_remove method=" << hook.name << " status=" << MH_StatusToString(status) << '\n';
        hook.created = false;
    }
    if (!passed && activeHooks) {
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&AnimationHelperHook), &pinned);
        report << "ik_timing_hook_cleanup=FAILED_MODULE_PINNED\n";
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
        << " identity_attempted=" << identityProbeCount.load()
        << " identity_fault=" << identityFaults.load()
        << " selected_animator=0x" << std::hex << selectedAnimator
        << " selected_native=0x" << selectedNative << std::dec
        << " main_tid=" << unityThread << '\n';
    for (unsigned i = 0; i < hooks.size(); ++i)
        report << "[IK_TIMING_HOOK] index=" << i << " class=" << hooks[i].klass
            << " method=" << hooks[i].name << " calls=" << hookCalls[i].load()
            << " installed=" << hooks[i].created << " expected_class=0x" << std::hex
            << hooks[i].expectedClass << std::dec << '\n';
    const auto limit = count < MaxSamples ? count : MaxSamples;
    for (unsigned i = 0; i < limit; ++i) {
        const auto& sample = samples[i];
        report << "[IK_TIMING_CALLBACK] index=" << i << " hook=" << unsigned(sample.hook)
            << " tid=" << sample.thread << " tick=" << sample.tick << " layer=" << sample.layer
            << " instance=0x" << std::hex << sample.instance << " instance_class=0x" << sample.instanceClass
            << " instance_native=0x" << sample.instanceNative
            << " selected_animator=0x" << sample.selectedAnimator
            << " helper_go=0x" << sample.helperGameObject << " helper_go_native=0x" << sample.helperGameObjectNative
            << " animator_go=0x" << sample.animatorGameObject << " animator_go_native=0x" << sample.animatorGameObjectNative
            << std::dec << " identity=" << IdentityName(sample.identityStatus)
            << " main_thread=" << (sample.mainThread ? 1 : 0) << '\n';
    }
    const auto mainLimit = identityProbeCount.load(std::memory_order_acquire) < MaxMainSamples
        ? identityProbeCount.load(std::memory_order_acquire) : MaxMainSamples;
    for (unsigned i = 0; i < mainLimit; ++i) {
        const auto& sample = mainSamples[i];
        report << "[IK_TIMING_MAIN_CALLBACK] index=" << i << " hook=" << unsigned(sample.hook)
            << " tid=" << sample.thread << " tick=" << sample.tick << " layer=" << sample.layer
            << " instance=0x" << std::hex << sample.instance << " instance_class=0x" << sample.instanceClass
            << " instance_native=0x" << sample.instanceNative
            << " selected_animator=0x" << sample.selectedAnimator
            << " helper_go=0x" << sample.helperGameObject << " helper_go_native=0x" << sample.helperGameObjectNative
            << " animator_go=0x" << sample.animatorGameObject << " animator_go_native=0x" << sample.animatorGameObjectNative
            << std::dec << " identity=" << IdentityName(sample.identityStatus)
            << " main_thread=1\n";
    }
}

}
