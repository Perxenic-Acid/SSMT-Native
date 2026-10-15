#include "IKTimingProbe.h"

#include <MinHook.h>
#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <ostream>

namespace poser {
namespace {

using OnAnimatorIK = void (*)(uintptr_t, int, uintptr_t);
using AnimatorIKPositionGetter = Vector3* (*)(Vector3*, uintptr_t, int, uintptr_t);
using AnimatorIKPositionSetter = void (*)(uintptr_t, int, const Vector3*, uintptr_t);
using AnimatorIKWeightGetter = float (*)(uintptr_t, int, uintptr_t);
using AnimatorIKWeightSetter = void (*)(uintptr_t, int, float, uintptr_t);

constexpr unsigned MaxSamples = 128;
// Threaded IK callbacks can fill the general sample ring before the sparse
// main-thread callback arrives. Keep an independent small capture so identity
// evidence is never lost to callback ordering.
constexpr unsigned MaxMainSamples = 16;
constexpr unsigned MaxHooks = 4;
// Unity's AvatarIKGoal enum is LeftHand=0, RightHand=1, LeftFoot=2,
// RightFoot=3.  The native writer is a foot writer; using 0/1 would silently
// move the hands while appearing to pass a Vector3 canary.
constexpr int LeftFootGoal = 2;
constexpr int RightFootGoal = 3;

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
    uintptr_t helperTransform = 0;
    uintptr_t animatorTransform = 0;
    int layer = 0;
    uint8_t hook = 0;
    uint8_t identityStatus = 0;
    uint8_t hierarchyStatus = 0;
    uint8_t writerStatus = 0;
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
uintptr_t transformGetter = 0;
uintptr_t transformDescriptor = 0;
uintptr_t parentGetter = 0;
uintptr_t parentDescriptor = 0;
uintptr_t transformClass = 0;
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

enum class WriterState : uint8_t { Unavailable, AwaitingHierarchy, Canary, Enabled, Failed };
std::atomic<WriterState> writerState{WriterState::Unavailable};
NativeMethod positionGetterMethod{}, positionSetterMethod{}, weightGetterMethod{}, weightSetterMethod{};
AnimatorIKPositionGetter positionGetter = nullptr;
AnimatorIKPositionSetter positionSetter = nullptr;
AnimatorIKWeightGetter weightGetter = nullptr;
AnimatorIKWeightSetter weightSetter = nullptr;
std::atomic<uint64_t> writerCalls{0};
std::atomic<uint64_t> writerApplied{0};
std::atomic<uint64_t> writerRejected{0};
std::atomic<uint64_t> canaryAttempts{0};
std::atomic<uint64_t> canaryPassed{0};
std::atomic<uint64_t> canaryFailed{0};
std::atomic<uint64_t> hierarchyPassed{0};
std::atomic<uint64_t> hierarchyFailed{0};
uintptr_t verifiedHelper = 0;
uintptr_t verifiedHelperNative = 0;
uintptr_t verifiedAnimatorNative = 0;

// Both publication and consumption are constrained to Unity's main thread;
// the inactive slot is therefore never copied concurrently by this plugin.
AnimatorIKTargetSnapshot targetSlots[2]{};
std::atomic<unsigned> targetSlot{0};
std::atomic<uint64_t> targetGeneration{0};

using Getter = uintptr_t (*)(uintptr_t, uintptr_t);

template<class T>
bool Read(uintptr_t address, T& value) {
    SIZE_T count = 0;
    return address && address <= UINTPTR_MAX - sizeof(T) &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
            &value, sizeof(T), &count) && count == sizeof(T);
}

bool BytesMatch(uintptr_t address, const std::initializer_list<int>& bytes) {
    if (!address) return false;
    size_t index = 0;
    for (const auto expected : bytes) {
        uint8_t actual = 0;
        if (!Read(address + index, actual) || (expected >= 0 && actual != uint8_t(expected))) return false;
        ++index;
    }
    return true;
}

bool HasBytes(uintptr_t address, size_t span, const std::initializer_list<int>& bytes) {
    if (!address || bytes.size() > span) return false;
    for (size_t start = 0; start + bytes.size() <= span; ++start) {
        bool match = true;
        size_t offset = 0;
        for (const auto expected : bytes) {
            uint8_t actual = 0;
            if (!Read(address + start + offset, actual) || (expected >= 0 && actual != uint8_t(expected))) {
                match = false;
                break;
            }
            ++offset;
        }
        if (match) return true;
    }
    return false;
}

bool Finite(Vector3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool Near(Vector3 a, Vector3 b) {
    return std::fabs(a.x - b.x) < 0.0001f && std::fabs(a.y - b.y) < 0.0001f &&
        std::fabs(a.z - b.z) < 0.0001f;
}

bool Near(float a, float b) {
    return std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) < 0.0001f;
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

bool GetTransform(uintptr_t component, uintptr_t& transform) {
    transform = 0;
    if (!component || !transformGetter || !transformDescriptor) return false;
    transform = reinterpret_cast<Getter>(transformGetter)(component, transformDescriptor);
    uintptr_t native = 0;
    return ObjectIdentity(transform, transformClass, native);
}

bool SameTransform(uintptr_t left, uintptr_t right) {
    if (!left || !right) return false;
    uintptr_t leftNative = 0, rightNative = 0;
    return ObjectIdentity(left, transformClass, leftNative) && ObjectIdentity(right, transformClass, rightNative) &&
        (left == right || leftNative == rightNative);
}

bool IsAncestor(uintptr_t ancestor, uintptr_t descendant) {
    uintptr_t current = descendant;
    for (unsigned depth = 0; current && depth != 96; ++depth) {
        if (SameTransform(ancestor, current)) return true;
        if (!parentGetter || !parentDescriptor) return false;
        current = reinterpret_cast<Getter>(parentGetter)(current, parentDescriptor);
    }
    return false;
}

bool VerifyHierarchy(uintptr_t helper, Sample* sample) {
    __try {
        uintptr_t helperNative = 0, currentAnimatorNative = 0;
        if (!ObjectIdentity(helper, hooks[0].expectedClass, helperNative) ||
            !ObjectIdentity(selectedAnimator, animatorClass, currentAnimatorNative) ||
            currentAnimatorNative != selectedNative) {
            if (sample) sample->hierarchyStatus = 1;
            return false;
        }
        if (helper == verifiedHelper && helperNative == verifiedHelperNative &&
            currentAnimatorNative == verifiedAnimatorNative) {
            if (sample) sample->hierarchyStatus = 2;
            return true;
        }
        uintptr_t helperTransform = 0, animatorTransform = 0;
        if (!GetTransform(helper, helperTransform) || !GetTransform(selectedAnimator, animatorTransform)) {
            if (sample) sample->hierarchyStatus = 1;
            return false;
        }
        if (sample) {
            sample->helperTransform = helperTransform;
            sample->animatorTransform = animatorTransform;
        }
        const bool related = IsAncestor(helperTransform, animatorTransform);
        if (sample) sample->hierarchyStatus = related ? 2 : 3;
        if (related) {
            verifiedHelper = helper;
            verifiedHelperNative = helperNative;
            verifiedAnimatorNative = currentAnimatorNative;
        }
        return related;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (sample) sample->hierarchyStatus = 4;
        return false;
    }
}

bool ReadPosition(int goal, Vector3& value) {
    if (!positionGetter) return false;
    struct Guarded { uint32_t first; Vector3 value; uint32_t last; } buffer{0xD17A6C3B, {}, 0xD17A6C3B};
    const auto returned = positionGetter(&buffer.value, selectedAnimator, goal, positionGetterMethod.descriptor);
    if (returned != &buffer.value || buffer.first != 0xD17A6C3B || buffer.last != 0xD17A6C3B || !Finite(buffer.value)) return false;
    value = buffer.value;
    return true;
}

bool ReadWeight(int goal, float& value) {
    if (!weightGetter) return false;
    const auto result = weightGetter(selectedAnimator, goal, weightGetterMethod.descriptor);
    if (!std::isfinite(result) || result < -0.001f || result > 1.001f) return false;
    value = result;
    return true;
}

bool RunWriterCanary() {
    canaryAttempts.fetch_add(1, std::memory_order_relaxed);
    Vector3 left{}, right{};
    __try {
        if (!ReadPosition(LeftFootGoal, left) || !ReadPosition(RightFootGoal, right)) {
            canaryFailed.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        // 先写回原值，成功时不改变当前画面；权重 setter 尚未通过独立
        // ABI 证明，因此本阶段只接管位置，保留游戏现有权重。
        positionSetter(selectedAnimator, LeftFootGoal, &left, positionSetterMethod.descriptor);
        positionSetter(selectedAnimator, RightFootGoal, &right, positionSetterMethod.descriptor);
        Vector3 leftAfter{}, rightAfter{};
        const bool passed = ReadPosition(LeftFootGoal, leftAfter) && ReadPosition(RightFootGoal, rightAfter) &&
            Near(left, leftAfter) && Near(right, rightAfter);
        if (passed) canaryPassed.fetch_add(1, std::memory_order_relaxed);
        else canaryFailed.fetch_add(1, std::memory_order_relaxed);
        return passed;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        canaryFailed.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

bool ReadTarget(AnimatorIKTargetSnapshot& snapshot) {
    const auto index = targetSlot.load(std::memory_order_acquire);
    snapshot = targetSlots[index & 1u];
    return snapshot.valid && snapshot.generation && snapshot.animator == selectedAnimator &&
        snapshot.animatorNative == selectedNative && Finite(snapshot.leftFoot) && Finite(snapshot.rightFoot) &&
        std::isfinite(snapshot.leftWeight) && std::isfinite(snapshot.rightWeight) &&
        snapshot.leftWeight >= 0.0f && snapshot.leftWeight <= 1.0f &&
        snapshot.rightWeight >= 0.0f && snapshot.rightWeight <= 1.0f;
}

void ApplyWriter(unsigned hookIndex, uintptr_t instance, Sample* sample) {
    if (hookIndex != 0) return;
    writerCalls.fetch_add(1, std::memory_order_relaxed);
    if (writerState.load(std::memory_order_acquire) == WriterState::Unavailable ||
        writerState.load(std::memory_order_acquire) == WriterState::Failed) return;
    // A missing sample must never become a write permission. The sample also
    // proves the helper instance's class/native identity before the ancestry
    // check accepts a different GameObject under the same actor hierarchy.
    if (!VerifyHierarchy(instance, sample)) {
        hierarchyFailed.fetch_add(1, std::memory_order_relaxed);
        if (sample) sample->writerStatus = 1;
        return;
    }
    hierarchyPassed.fetch_add(1, std::memory_order_relaxed);
    auto state = writerState.load(std::memory_order_acquire);
    if (state == WriterState::AwaitingHierarchy) {
        writerState.store(WriterState::Canary, std::memory_order_release);
        state = WriterState::Canary;
    }
    if (state == WriterState::Canary) {
        if (!RunWriterCanary()) {
            writerState.store(WriterState::Failed, std::memory_order_release);
            if (sample) sample->writerStatus = 2;
            return;
        }
        writerState.store(WriterState::Enabled, std::memory_order_release);
    }
    if (writerState.load(std::memory_order_acquire) != WriterState::Enabled) return;
    AnimatorIKTargetSnapshot snapshot{};
    if (!ReadTarget(snapshot)) {
        writerRejected.fetch_add(1, std::memory_order_relaxed);
        if (sample) sample->writerStatus = 3;
        return;
    }
    __try {
        if (!snapshot.enabled) return;
        positionSetter(selectedAnimator, LeftFootGoal, &snapshot.leftFoot, positionSetterMethod.descriptor);
        positionSetter(selectedAnimator, RightFootGoal, &snapshot.rightFoot, positionSetterMethod.descriptor);
        writerApplied.fetch_add(1, std::memory_order_relaxed);
        if (sample) sample->writerStatus = 4;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        writerRejected.fetch_add(1, std::memory_order_relaxed);
        writerState.store(WriterState::Failed, std::memory_order_release);
        if (sample) sample->writerStatus = 5;
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

Sample* Record(unsigned index, uintptr_t instance, int layer) {
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
    if (!sample) return nullptr;
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
    return sample;
}

void Invoke(unsigned index, uintptr_t instance, int layer, uintptr_t methodInfo) {
    inFlight.fetch_add(1, std::memory_order_acq_rel);
    __try {
        Sample* sample = nullptr;
        if (installed.load(std::memory_order_acquire)) sample = Record(index, instance, layer);
        const auto original = index < hooks.size() ? hooks[index].original : nullptr;
        if (original) original(instance, layer, methodInfo);
        if (installed.load(std::memory_order_acquire) && (!unityThread || GetCurrentThreadId() == unityThread))
            ApplyWriter(index, instance, sample);
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
    writerCalls = 0;
    writerApplied = 0;
    writerRejected = 0;
    canaryAttempts = 0;
    canaryPassed = 0;
    canaryFailed = 0;
    hierarchyPassed = 0;
    hierarchyFailed = 0;
    writerState.store(WriterState::Unavailable, std::memory_order_release);
    targetSlots[0] = {};
    targetSlots[1] = {};
    targetSlot.store(0, std::memory_order_release);
    targetGeneration.store(0, std::memory_order_release);
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

void ResetWriter() {
    writerState.store(WriterState::Unavailable, std::memory_order_release);
    verifiedHelper = 0;
    verifiedHelperNative = 0;
    verifiedAnimatorNative = 0;
    positionGetterMethod = {};
    positionSetterMethod = {};
    weightGetterMethod = {};
    weightSetterMethod = {};
    positionGetter = nullptr;
    positionSetter = nullptr;
    weightGetter = nullptr;
    weightSetter = nullptr;
    transformGetter = 0;
    transformDescriptor = 0;
    parentGetter = 0;
    parentDescriptor = 0;
    transformClass = 0;
}

bool ResolveWriterMethods(GenshinNativeRuntime& runtime, std::ostream& report) {
    NativeClass animatorNativeClass, componentNativeClass, transformNativeClass;
    if (!runtime.FindClass("UnityEngine", "Animator", animatorNativeClass) ||
        !runtime.FindClass("UnityEngine", "Component", componentNativeClass) ||
        !runtime.FindClass("UnityEngine", "Transform", transformNativeClass)) {
        report << "[ANIMATOR_IK_WRITER] status=BLOCKED reason=class_unresolved\n";
        return false;
    }
    NativeMethod getPosition, setPosition, getWeight, setWeight, getTransform, getParent;
    const auto findAny = [&](const NativeClass& klass, const char* const* names, size_t count,
        uint8_t parameters, NativeMethod& result) {
        for (size_t i = 0; i < count; ++i)
            if (runtime.FindMethod(klass, names[i], parameters, result) && result.entry && result.descriptor) return true;
        return false;
    };
    constexpr const char* positionGetNames[] = {"GetIKPositionInternal", "GetIKPositionNoThreadCheck", "GetIKPosition"};
    constexpr const char* positionSetNames[] = {"SetIKPositionNoThreadCheck", "SetIKPositionInternal", "SetIKPosition"};
    constexpr const char* weightGetNames[] = {"GetIKPositionWeightNoThreadCheck", "GetIKPositionWeight"};
    constexpr const char* weightSetNames[] = {"SetIKPositionWeightNoThreadCheck", "SetIKPositionWeightInternal", "SetIKPositionWeight"};
    const bool getPositionReady = findAny(animatorNativeClass, positionGetNames, std::size(positionGetNames), 1, getPosition);
    const bool setPositionReady = findAny(animatorNativeClass, positionSetNames, std::size(positionSetNames), 2, setPosition);
    const bool getWeightReady = findAny(animatorNativeClass, weightGetNames, std::size(weightGetNames), 1, getWeight);
    const bool setWeightReady = findAny(animatorNativeClass, weightSetNames, std::size(weightSetNames), 2, setWeight);
    const bool transformReady = runtime.FindMethod(componentNativeClass, "get_transform", 0, getTransform);
    const bool parentReady = runtime.FindMethod(transformNativeClass, "get_parent", 0, getParent);
    const bool getterBytes = getPositionReady &&
        (BytesMatch(getPosition.entry, {0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xCE}) ||
         BytesMatch(getPosition.entry, {0x48, 0x89, 0xCE}));
    const bool setterBytes = setPositionReady && BytesMatch(setPosition.entry,
        {0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x4C, 0x89, 0xC3, 0x89, 0xD6, 0x48, 0x89, 0xCF});
    // Position and weight are independent gates. Unity does not expose a
    // weight getter on this profile, so a missing/uncertain weight ABI cannot
    // block the position path or justify an unverified float call.
    const bool weightSetterBytes = setWeightReady &&
        HasBytes(setWeight.entry, 128, {0x48, 0x89, 0xCF}) &&
        HasBytes(setWeight.entry, 128, {0x89, 0xD6}) &&
        (HasBytes(setWeight.entry, 128, {0xF3, 0x0F, 0x10}) ||
         HasBytes(setWeight.entry, 128, {0xF3, 0x0F, 0x11}) ||
         HasBytes(setWeight.entry, 128, {0xF3, 0x0F, 0x59}) ||
         HasBytes(setWeight.entry, 128, {0xF3, 0x0F, 0x5E}));
    report << "[ANIMATOR_IK_WRITER_METHODS] get_position=" << (getPositionReady ? getPosition.name : "UNRESOLVED")
        << " setter_position=" << (setPositionReady ? setPosition.name : "UNRESOLVED")
        << " get_weight=" << (getWeightReady ? getWeight.name : "UNRESOLVED")
        << " setter_weight=" << (setWeightReady ? setWeight.name : "UNRESOLVED")
        << " get_transform=" << (transformReady ? "VERIFIED" : "UNRESOLVED")
        << " get_parent=" << (parentReady ? "VERIFIED" : "UNRESOLVED")
        << " getter_bytes=" << (getterBytes ? "VERIFIED" : "REJECTED")
        << " setter_bytes=" << (setterBytes ? "VERIFIED" : "REJECTED")
        << " weight_setter_bytes=" << (weightSetterBytes ? "VERIFIED" : "REJECTED") << '\n';
    if (!getPositionReady || !setPositionReady || !transformReady || !parentReady ||
        !getterBytes || !setterBytes) {
        report << "[ANIMATOR_IK_WRITER] status=BLOCKED reason=POSITION_ABI_OR_HIERARCHY_METHOD_GATE\n";
        return false;
    }
    positionGetterMethod = getPosition;
    positionSetterMethod = setPosition;
    weightGetterMethod = getWeight;
    weightSetterMethod = setWeight;
    positionGetter = reinterpret_cast<AnimatorIKPositionGetter>(getPosition.entry);
    positionSetter = reinterpret_cast<AnimatorIKPositionSetter>(setPosition.entry);
    weightGetter = getWeightReady ? reinterpret_cast<AnimatorIKWeightGetter>(getWeight.entry) : nullptr;
    weightSetter = weightSetterBytes ? reinterpret_cast<AnimatorIKWeightSetter>(setWeight.entry) : nullptr;
    transformGetter = getTransform.entry;
    transformDescriptor = getTransform.descriptor;
    parentGetter = getParent.entry;
    parentDescriptor = getParent.descriptor;
    transformClass = transformNativeClass.address;
    writerState.store(WriterState::AwaitingHierarchy, std::memory_order_release);
    report << "[ANIMATOR_IK_WRITER] status=ARMED canary=ZERO_VISUAL_CHANGE main_thread_only=1 position=ENABLED weight="
        << (weightSetterBytes ? "ABI_PROVEN_BUT_NOT_CALLED" : "BLOCKED_ABI") << '\n';
    return true;
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
    report << "animator_ik_writer=POSITION_CANARY_GATED; weight_setter=NOT_CALLED\n";
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
    const bool writerReady = ResolveWriterMethods(runtime, report);

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
    report << "ik_timing_semantics=READ_ONLY; setters=" << (writerReady ? "CANARY_GATED" : "BLOCKED")
        << "; callback_identity=RECORDED; motion_gate=NON_BLOCKING\n";
    report << "ik_writer_semantics=" << (writerReady ? "ARMED_MAIN_THREAD_HIERARCHY_AND_CANARY_GATED" : "BLOCKED_ABI_GATE")
        << "; motion_targets=EXTERNAL_SNAPSHOT; analytic_solver=NOT_USED\n";
    report.flush();
    if (failed || !created) {
        const bool removed=RemoveIKTimingProbe(report);
        if (removed&&ownsMinHook) MH_Uninitialize();
        if (removed) ResetDescriptors();
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
    ClearAnimatorIKTargets();
    if (passed) for (auto& hook : hooks) if (hook.created) {
        const auto status = MH_RemoveHook(reinterpret_cast<void*>(hook.address));
        passed = passed && status == MH_OK;
        report << "ik_timing_hook_remove method=" << hook.name << " status=" << MH_StatusToString(status) << '\n';
        if(status==MH_OK)hook.created = false;
    }
    if (!passed && activeHooks) {
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&AnimationHelperHook), &pinned);
        report << "ik_timing_hook_cleanup=FAILED_MODULE_PINNED\n";
    }
    report << "ik_timing_hook_inflight=" << inFlight.load() << '\n';
    ResetWriter();
    return passed;
}

bool IKTimingProbeInstalled() { return installed.load(std::memory_order_acquire); }
bool IKTimingHooksReleased() {for(const auto& hook:hooks)if(hook.created)return false;return !inFlight.load();}

bool PublishAnimatorIKTargets(const AnimatorIKTargetSnapshot& snapshot) {
    if (!IKTimingProbeInstalled() || GetCurrentThreadId() != unityThread || !snapshot.valid ||
        snapshot.animator != selectedAnimator || !snapshot.animatorNative ||
        snapshot.animatorNative != selectedNative || !snapshot.generation ||
        !Finite(snapshot.leftFoot) || !Finite(snapshot.rightFoot) ||
        !std::isfinite(snapshot.leftWeight) || !std::isfinite(snapshot.rightWeight) ||
        snapshot.leftWeight < 0.0f || snapshot.leftWeight > 1.0f ||
        snapshot.rightWeight < 0.0f || snapshot.rightWeight > 1.0f) return false;
    // Publishing and consumption are both restricted to Unity's main thread;
    // the inactive slot therefore cannot be observed while it is copied.
    const auto inactive = (targetSlot.load(std::memory_order_relaxed) ^ 1u) & 1u;
    targetSlots[inactive] = snapshot;
    targetSlot.store(inactive, std::memory_order_release);
    targetGeneration.store(snapshot.generation, std::memory_order_release);
    return true;
}

bool ApplyAnimatorIKTargets() {
    if (!IKTimingProbeInstalled() || GetCurrentThreadId() != unityThread) return false;
    uintptr_t native = 0;
    if (!ObjectIdentity(selectedAnimator, animatorClass, native) || native != selectedNative) {
        writerState.store(WriterState::Failed, std::memory_order_release);
        ClearAnimatorIKTargets();
        return false;
    }
    auto state = writerState.load(std::memory_order_acquire);
    if (state == WriterState::Unavailable || state == WriterState::Failed) return false;
    if (state == WriterState::AwaitingHierarchy) {
        writerState.store(WriterState::Canary, std::memory_order_release);
        state = WriterState::Canary;
    }
    if (state == WriterState::Canary) {
        if (!RunWriterCanary()) {
            writerState.store(WriterState::Failed, std::memory_order_release);
            return false;
        }
        writerState.store(WriterState::Enabled, std::memory_order_release);
    }
    AnimatorIKTargetSnapshot snapshot{};
    if (!ReadTarget(snapshot)) {
        writerRejected.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!snapshot.enabled) return true;
    __try {
        positionSetter(selectedAnimator, 0, &snapshot.leftFoot, positionSetterMethod.descriptor);
        positionSetter(selectedAnimator, 1, &snapshot.rightFoot, positionSetterMethod.descriptor);
        writerApplied.fetch_add(1, std::memory_order_relaxed);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        writerRejected.fetch_add(1, std::memory_order_relaxed);
        writerState.store(WriterState::Failed, std::memory_order_release);
        return false;
    }
}

void ClearAnimatorIKTargets() {
    targetSlots[0] = {};
    targetSlots[1] = {};
    targetSlot.store(0, std::memory_order_release);
    targetGeneration.fetch_add(1, std::memory_order_acq_rel);
}

void ReportIKTimingProbe(std::ostream& report) {
    const auto count = sampleCount.load(std::memory_order_acquire);
    report << "[IK_TIMING_SUMMARY] installed=" << (IKTimingProbeInstalled() ? 1 : 0)
        << " calls=" << calls.load() << " sampled=" << (count < MaxSamples ? count : MaxSamples)
        << " off_main=" << offMain.load() << " fault=" << faults.load()
        << " identity_attempted=" << identityProbeCount.load()
        << " identity_fault=" << identityFaults.load()
        << " writer_state=" << unsigned(writerState.load(std::memory_order_acquire))
        << " writer_calls=" << writerCalls.load() << " writer_applied=" << writerApplied.load()
        << " writer_rejected=" << writerRejected.load()
        << " canary_attempts=" << canaryAttempts.load() << " canary_passed=" << canaryPassed.load()
        << " canary_failed=" << canaryFailed.load()
        << " hierarchy_passed=" << hierarchyPassed.load() << " hierarchy_failed=" << hierarchyFailed.load()
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
            << " helper_transform=0x" << sample.helperTransform << " animator_transform=0x" << sample.animatorTransform
            << std::dec << " identity=" << IdentityName(sample.identityStatus)
            << " hierarchy=" << unsigned(sample.hierarchyStatus) << " writer=" << unsigned(sample.writerStatus)
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
            << " helper_transform=0x" << sample.helperTransform << " animator_transform=0x" << sample.animatorTransform
            << std::dec << " identity=" << IdentityName(sample.identityStatus)
            << " hierarchy=" << unsigned(sample.hierarchyStatus) << " writer=" << unsigned(sample.writerStatus)
            << " main_thread=1\n";
    }
}

}
