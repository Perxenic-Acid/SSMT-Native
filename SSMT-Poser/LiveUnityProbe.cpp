#include "LiveUnityProbe.h"
#include "RigProbe.h"
#include "BoneWriteProbe.h"
#include "IKTimingProbe.h"
#include "MotionPlayback.h"
#include "FacePlayback.h"
#include "NativePhysicsCapture.h"
#include "UiBridge.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>

namespace poser {
namespace live_detail { bool ValidateArray(uintptr_t array, uintptr_t elementClass, uint64_t& length); }
namespace {
template<class T> bool Read(uintptr_t address, T& value) {
    SIZE_T count = 0;
    return address && address <= UINTPTR_MAX - sizeof(T) &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(T), &count) && count == sizeof(T);
}
bool Name(uintptr_t klass, const char* expected) {
    uintptr_t p = 0;
    if (!Read(klass + 0x28, p)) return false;
    for (size_t i = 0; i <= std::strlen(expected); ++i) {
        char c = 0;
        if (!Read(p + i, c) || c != expected[i]) return false;
    }
    return true;
}
uintptr_t Relative(uintptr_t instruction, size_t displacement, size_t length) {
    int32_t d = 0;
    return Read(instruction + displacement, d) ? native_detail::RelativeTarget(instruction, d, length) : 0;
}
bool Pattern(uintptr_t address, size_t size, const char* signature) {
    std::vector<uint8_t> bytes(size);
    SIZE_T count = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.data(), size, &count) || count != size) return false;
    return native_detail::FindPattern(bytes, signature).size() == 1;
}
bool HasPattern(uintptr_t address, size_t size, const char* signature) {
    std::vector<uint8_t> bytes(size);
    SIZE_T count = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.data(), size, &count) || count != size) return false;
    return !native_detail::FindPattern(bytes, signature).empty();
}
bool Method(GenshinNativeRuntime& runtime, const char* owner, const char* name,
    uint8_t parameters, NativeMethod& result);
std::string CodeBytes(uintptr_t address, size_t size = 64) {
    if (!address || size == 0) return {};
    std::vector<uint8_t> bytes(size);
    SIZE_T count = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.data(), size, &count) ||
        count != size) return {};
    static constexpr char Digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 3 - 1);
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i) result.push_back(' ');
        result.push_back(Digits[bytes[i] >> 4]);
        result.push_back(Digits[bytes[i] & 0x0f]);
    }
    return result;
}
const char* Vector3AbiHint(const NativeMethod& method) {
    if (!method.entry) return "UNRESOLVED";
    // This is evidence only. The call gate remains disabled until a live
    // canary proves both the hidden return buffer and the argument order.
    if (Pattern(method.entry, 3, "48 89 CE")) return "SRET_PREFIX_48_89_CE";
    if (Pattern(method.entry, 3, "48 89 C8")) return "SRET_PREFIX_48_89_C8";
    if (Pattern(method.entry, 4, "48 83 EC 28")) return "STACK_WRAPPER_PREFIX";
    return "PREFIX_UNCLASSIFIED";
}
const char* AnimatorIKSetterAbiHint(const NativeMethod& method) {
    if (!method.entry || method.parameterCount != 2) return "UNRESOLVED_OR_PARAMETER_COUNT";

    // These are deliberately conservative data-flow observations, not an ABI
    // proof. The current Unity wrapper copies RCX (Animator) to RDI and RDX
    // (AvatarIKGoal) to ESI. SetIKPositionNoThreadCheck additionally copies
    // R8 (the hidden Vector3 pointer argument) to a callee-saved register.
    // A future writer must still perform a same-value getter/setter canary on
    // the main Animator IK callback before invoking either setter.
    const bool thisMove = HasPattern(method.entry, 96, "48 89 CF");
    const bool goalMove = HasPattern(method.entry, 96, "89 D6");
    const bool vectorMove = HasPattern(method.entry, 96, "4C 89 C3");
    const bool scalarFloatOp =
        HasPattern(method.entry, 96, "F3 0F 10") ||
        HasPattern(method.entry, 96, "F3 0F 11") ||
        HasPattern(method.entry, 96, "F3 0F 59") ||
        HasPattern(method.entry, 96, "F3 0F 5E");
    const bool position = method.name.find("SetIKPosition") != std::string::npos &&
        method.name.find("Weight") == std::string::npos;
    const bool weight = method.name.find("SetIKPositionWeight") != std::string::npos;
    if (position && thisMove && goalMove && vectorMove) return "POSITION_RCX_RDX_R8_POINTER_OBSERVED";
    if (weight && thisMove && goalMove && scalarFloatOp) return "WEIGHT_RCX_RDX_XMM_SCALAR_OBSERVED";
    if (weight && thisMove && goalMove) return "WEIGHT_RCX_RDX_OBSERVED_XMM_UNCLASSIFIED";
    if (thisMove && goalMove) return "RCX_RDX_FORWARDING_OBSERVED_ARGUMENT_CHANNEL_UNCLASSIFIED";
    return "ENTRY_DATAFLOW_UNCLASSIFIED";
}
const char* AnimatorIKSetterCanary(const NativeMethod& method) {
    const bool position = method.name.find("SetIKPosition") != std::string::npos &&
        method.name.find("Weight") == std::string::npos;
    const bool weight = method.name.find("SetIKPositionWeight") != std::string::npos;
    if (position) {
        return "MAIN_THREAD_ONLY;GET_POSITION(goal)->SET_SAME_POSITION(goal,Vector3*)->GET_POSITION(goal);"
               "require_exact_readback_and_guard_canary;disable_on_fault_or_identity_change";
    }
    if (weight) {
        return "MAIN_THREAD_ONLY;GET_POSITION_WEIGHT(goal)->SET_SAME_WEIGHT(goal,float)->GET_POSITION_WEIGHT(goal);"
               "weight_getter_ABI_must_be_validated_first;disable_on_fault_or_identity_change";
    }
    return "NONE";
}
void ReportAnimatorIKVariant(GenshinNativeRuntime& runtime, const char* name,
    uint8_t parameters, std::ostream& report) {
    NativeMethod method;
    const bool resolved = Method(runtime, "Animator", name, parameters, method);
    report << "[ANIMATOR_IK_VARIANT] method=" << name
        << " parameters=" << unsigned(parameters)
        << " resolved=" << (resolved ? 1 : 0);
    if (resolved) {
        report << " id=" << method.metadataId << " entry=0x" << std::hex << method.entry
            << " target=0x" << method.target << " descriptor=0x" << method.descriptor
            << std::dec << " entry_hint=" << Vector3AbiHint(method)
            << " setter_hint=" << AnimatorIKSetterAbiHint(method)
            << " setter_canary=\"" << AnimatorIKSetterCanary(method) << "\""
            << " entry_bytes=\"" << CodeBytes(method.entry)
            << "\" target_bytes=\"" << CodeBytes(method.target) << "\"";
    }
    // Variant descriptors are census evidence only. No candidate here is
    // invoked until its ABI is independently validated with a canary.
    report << " invocation=NONE\n";
}
void ReportAnimatorIKVariants(GenshinNativeRuntime& runtime, std::ostream& report) {
    struct Candidate { const char* name; uint8_t parameters; };
    constexpr Candidate candidates[] = {
        {"GetIKPosition", 1}, {"GetIKPositionInternal", 1},
        {"INTERNAL_CALL_GetIKPositionInternal", 3},
        {"GetIKPositionNoThreadCheck", 1},
        {"SetIKPosition", 2}, {"SetIKPositionInternal", 2},
        {"INTERNAL_CALL_SetIKPositionInternal", 3},
        {"SetIKPositionNoThreadCheck", 2},
        {"SetIKPositionWeight", 2}, {"SetIKPositionWeightInternal", 2},
        {"SetIKPositionWeightNoThreadCheck", 2},
        {"GetIKRotation", 1}, {"GetIKRotationInternal", 1},
        {"INTERNAL_CALL_GetIKRotationInternal", 3},
        {"GetIKRotationNoThreadCheck", 1},
    };
    report << "[ANIMATOR_IK_VARIANTS] invocation=NONE candidates="
        << (sizeof(candidates) / sizeof(candidates[0])) << '\n';
    for (const auto& candidate : candidates)
        ReportAnimatorIKVariant(runtime, candidate.name, candidate.parameters, report);
}
struct Bone {
    uintptr_t object = 0, klass = 0, native = 0;
    int nameLength = 0;
    wchar_t name[128]{};
};
struct Item {
    uintptr_t klass = 0, descriptor = 0, reflection = 0, reflectionDescriptor = 0, repeat = 0, array = 0, arrayClass = 0;
    uintptr_t object = 0, objectClass = 0, nativeObject = 0, transform = 0, gameObject = 0, nameObject = 0;
    uint32_t metadataId = 0, handles[7]{};
    unsigned rooted = 0, released = 0;
    unsigned skippedTransforms = 0;
    uint64_t length = 0;
    int childCount = -1, nativeChildCount = -1, nameLength = 0;
    wchar_t name[128]{};
    bool arrayValid = false, liveValid = false;
    uintptr_t rootBone = 0, bonesArray = 0, sharedMesh = 0, meshClass = 0;
    uint64_t bonesLength = 0;
    int nativeBonesLength = -1, meshNameLength = 0;
    wchar_t meshName[128]{};
    bool isHuman = false, humanRead = false, bonesValid = false;
    std::array<Bone, 512> bones;
};
struct GroundCandidate {
    const char* namespaze = "";
    const char* name = "";
    uintptr_t klass = 0, descriptor = 0;
    uintptr_t reflection = 0, array = 0, arrayClass = 0, elementDescriptor = 0;
    uint64_t count = 0;
    uint64_t bounds = UINT64_MAX, length = 0;
    uint32_t elementMetadataId = UINT32_MAX, expectedMetadataId = UINT32_MAX;
    uint32_t handles[2]{};
    uint8_t arrayType = 0;
    int stage = 0;
    bool resolved = false, read = false;
    bool hierarchyValid = false, derivesObject = false, derivesComponent = false;
    std::string baseChain;
    unsigned sampleCount = 0;
    std::array<uintptr_t, 8> sampleObjects{};
    std::array<std::string, 8> sampleNames{};
    unsigned hierarchyMatches = 0, hierarchySampleCount = 0;
    std::array<uintptr_t, 8> hierarchyObjects{};
    std::array<unsigned, 8> hierarchyDepth{};
    std::array<std::string, 8> hierarchyNames{};
    unsigned subtreeMatches = 0, subtreeSampleCount = 0;
    std::array<uintptr_t, 8> subtreeObjects{};
    std::array<std::string, 8> subtreeNames{};
};
struct ComponentLayer {
    uintptr_t transform = 0, gameObject = 0;
    uint64_t componentCount = 0;
    unsigned depth = 0;
    int stage = 0;
    int nameLength = 0;
    wchar_t name[128]{};
};
struct ComponentClassCount {
    uintptr_t klass = 0;
    uint32_t count = 0;
};
// Secondary-motion discovery is deliberately read-only.  The game owns these
// components and their simulation state; the probe only classifies the type
// and the nearby bone names so passthrough coverage can be checked per actor.
enum class SecondaryKind : uint8_t {
    None,
    DynamicBone,
    Verlet,
    SpringBone,
    Cloth,
    TransformChange,
    Rigidbody,
    Collider,
};
constexpr size_t SecondaryKindCount = static_cast<size_t>(SecondaryKind::Collider) + 1;
const char* SecondaryKindName(SecondaryKind kind) {
    switch (kind) {
    case SecondaryKind::DynamicBone: return "DYNAMIC_BONE";
    case SecondaryKind::Verlet: return "VERLET";
    case SecondaryKind::SpringBone: return "SPRING_BONE";
    case SecondaryKind::Cloth: return "CLOTH";
    case SecondaryKind::TransformChange: return "TRANSFORM_CHANGE";
    case SecondaryKind::Rigidbody: return "RIGIDBODY";
    case SecondaryKind::Collider: return "COLLIDER";
    default: return "NONE";
    }
}
SecondaryKind ClassifySecondaryType(std::string_view name) {
    if (name.find("DynamicBone") != std::string_view::npos) return SecondaryKind::DynamicBone;
    if (name.find("Verlet") != std::string_view::npos) return SecondaryKind::Verlet;
    if (name.find("SpringBone") != std::string_view::npos || name.find("Spring") != std::string_view::npos)
        return SecondaryKind::SpringBone;
    if (name.find("Cloth") != std::string_view::npos) return SecondaryKind::Cloth;
    if (name.find("TransformChange") != std::string_view::npos ||
        name.find("FlyAttachment") != std::string_view::npos)
        return SecondaryKind::TransformChange;
    if (name.find("Rigidbody") != std::string_view::npos) return SecondaryKind::Rigidbody;
    if (name.find("Collider") != std::string_view::npos) return SecondaryKind::Collider;
    return SecondaryKind::None;
}
SecondaryKind ClassifySecondaryBone(std::string_view name) {
    // Bone names are only evidence for chain discovery, never a write target.
    if (name.find("Hair") != std::string_view::npos || name.find("Pony") != std::string_view::npos ||
        name.find("Tail") != std::string_view::npos || name.find("Ribbon") != std::string_view::npos)
        return SecondaryKind::DynamicBone;
    if (name.find("Skirt") != std::string_view::npos || name.find("Dress") != std::string_view::npos ||
        name.find("Cloth") != std::string_view::npos || name.find("Coat") != std::string_view::npos)
        return SecondaryKind::Cloth;
    return SecondaryKind::None;
}
std::string Utf8(const wchar_t* text, int length);
bool ReadName(uintptr_t object, wchar_t (&name)[128], int& length);
bool Alive(uintptr_t object, uintptr_t expected, uintptr_t& native);
struct Job {
    NativeCallContext context;
    uintptr_t find = 0, findMethod = 0, child = 0, childMethod = 0;
    uintptr_t transform = 0, transformMethod = 0, gameObject = 0, gameObjectMethod = 0, name = 0, nameMethod = 0;
    uintptr_t transformClass = 0, gameObjectClass = 0, componentClass = 0, objectClass = 0, reflectionClass = 0;
    NativeMethod parent, components;
    uint32_t componentTypeHandle = 0, componentScanHandles[4]{};
    uintptr_t componentScanAnimator = 0;
    unsigned componentLayerCount = 0;
    std::array<ComponentLayer, 24> componentLayers{};
    bool componentScanRead = false;
    uintptr_t componentSubtreeAnimator = 0, componentSubtreeRoot = 0;
    uint64_t componentSubtreeComponentCount = 0;
    unsigned componentSubtreeTypeCount = 0;
    std::array<ComponentClassCount, 256> componentSubtreeTypes{};
    std::string componentSubtreeRootName;
    bool componentSubtreeRead = false, componentSubtreeFresh = false, componentSubtreeTruncated = false;
    bool classParentLayoutValidated = false;
    NativeMethod human, rootBone, bones, mesh;
    NativeMethod fbikEffectorCount, fbikChainCount, fbikCheckPass;
    NativeMethod animatorLayerCount, animatorLayerEnable;
    NativeMethod animatorIkGetPosition;
    NativeMethod fbikChainElementCount;
    NativeMethod groundHeight;
    bool fbikMethodsReady = false;
    bool fbikChainElementMethodReady = false;
    bool animatorLayerMethodsReady = false;
    bool animatorIkPositionMethodReady = false;
    bool animatorIkPositionAbiReady = false;
    bool fbikProbeEnabled = false;
    bool fbikRead = false, fbikPass = false;
    unsigned fbikReadFailure = 0;
    DWORD fbikReadFault = 0;
    int fbikEffectors = -1, fbikChains = -1;
    std::array<int, 64> fbikElementCounts{};
    unsigned fbikElementRead = 0;
    bool animatorLayerRead = false;
    int animatorLayerCountValue = -1;
    std::array<int8_t, 8> animatorLayerEnabled{};
    unsigned animatorLayerReadCount = 0;
    DWORD animatorLayerReadFault = 0;
    bool animatorIkPositionRead = false;
    unsigned animatorIkPositionSampleCount = 0;
    DWORD animatorIkPositionReadFault = 0;
    std::array<Vector3, 4> animatorIkPositions{};
    bool groundHeightReady = false, groundHeightRead = false;
    std::array<float, 2> groundHeights{};
    std::array<float, 2> footGroundDeltas{};
    std::array<uint8_t, 2> groundHeightValid{};
    DWORD groundHeightFault = 0;
    DWORD mainThread = 0, actualThread = 0, fault = 0;
    uintptr_t runtimeThread = 0;
    unsigned step = 0;
    unsigned mode = 0;
    bool complete = false;
    std::array<Item, 3> items;
    std::array<GroundCandidate, 14> groundCandidates{{
        {"MoleMole", "FootLockDragonIK"},
        {"MoleMole", "AnimageIKRig"},
        {"MoleMole", "EntityActor"},
        {"MoleMole", "VCAnimatorMove"},
        {"MoleMole", "IdleRig"},
        {"", "QuadrupedsGoundIK"},
        {"MoleMole", "MonoAnimationIKEventHelper"},
        {"VerletEngine", "DynamicBoneArray"},
        {"", "HeightfieldDeformation"},
        {"MoleMole", "MonoTransformChangeHelper"},
        {"MoleMole", "MonoFlyAttachmentHelper"},
        {"UnityEngine", "CapsuleCollider"},
        {"UnityEngine", "MeshCollider"},
        {"UnityEngine", "Rigidbody"},
    }};
};
// 任务数据常驻插件；各次 callback 只执行有界只读任务，不跨帧保留 managed 引用。
Job job;
std::atomic<unsigned> state{0}, active{0};
UINT dispatchMessage = 0;
using Getter = uintptr_t (*)(uintptr_t, uintptr_t);
using IntGetter = int (*)(uintptr_t, uintptr_t);
using IndexedIntGetter = int (*)(uintptr_t, int, uintptr_t);
using IndexedBoolGetter = bool (*)(uintptr_t, int, uintptr_t);
using Factory = uintptr_t (*)(uintptr_t);
using GroundHeightGetter = float (*)(float, float, uintptr_t);
using NewHandle = uint32_t (*)(uintptr_t, bool);
using TargetHandle = uintptr_t (*)(uint32_t);
using FreeHandle = void (*)(uint32_t);

bool ReadFBIKCounts(uintptr_t animator) {
    if (!animator || !job.fbikMethodsReady || !job.fbikProbeEnabled) return false;
    const auto count = reinterpret_cast<IntGetter>(job.fbikEffectorCount.entry)(animator, job.fbikEffectorCount.descriptor);
    const auto chains = reinterpret_cast<IntGetter>(job.fbikChainCount.entry)(animator, job.fbikChainCount.descriptor);
    const auto pass = reinterpret_cast<bool (*)(uintptr_t, uintptr_t)>(job.fbikCheckPass.entry)(animator, job.fbikCheckPass.descriptor);
    if (count < 0 || count > 128 || chains < 0 || chains > 64) return false;
    job.fbikEffectors = count;
    job.fbikChains = chains;
    job.fbikPass = pass;
    job.fbikElementCounts.fill(-1);
    job.fbikElementRead = 0;
    if (job.fbikChainElementMethodReady && chains > 0) {
        const auto elementCount = reinterpret_cast<IndexedIntGetter>(job.fbikChainElementCount.entry);
        for (int chain = 0; chain < chains && chain < static_cast<int>(job.fbikElementCounts.size()); ++chain) {
            // The managed signature is GetFBIKChainElementCount(int chain).
            // Il2Cpp's generated wrapper keeps the instance first, then the
            // user argument, and the MethodInfo descriptor last. Passing the
            // descriptor as the chain index can return plausible zero values
            // while corrupting the probe's read-only evidence.
            const auto elementCountValue = elementCount(animator, chain, job.fbikChainElementCount.descriptor);
            if (elementCountValue < 0 || elementCountValue > 128) return false;
            job.fbikElementCounts[chain] = elementCountValue;
            ++job.fbikElementRead;
        }
    }
    job.fbikRead = true;
    return true;
}
DWORD ReadFBIKCountsGuarded(uintptr_t animator, bool& read) {
    __try {
        read = ReadFBIKCounts(animator);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
        return GetExceptionCode();
    }
}
bool ReadAnimatorIKLayers(uintptr_t animator) {
    if (!animator || !job.animatorLayerMethodsReady) return false;
    const auto layerCount = reinterpret_cast<IntGetter>(job.animatorLayerCount.entry)(
        animator, job.animatorLayerCount.descriptor);
    if (layerCount < 0 || layerCount > 256) return false;
    job.animatorLayerCountValue = layerCount;
    job.animatorLayerEnabled.fill(-1);
    job.animatorLayerReadCount = 0;
    const auto enable = reinterpret_cast<IndexedBoolGetter>(job.animatorLayerEnable.entry);
    const auto sampled = std::min(layerCount, int(job.animatorLayerEnabled.size()));
    for (int layer = 0; layer < sampled; ++layer) {
        job.animatorLayerEnabled[static_cast<size_t>(layer)] = enable(
            animator, layer, job.animatorLayerEnable.descriptor) ? 1 : 0;
        ++job.animatorLayerReadCount;
    }
    job.animatorLayerRead = true;
    return true;
}
DWORD ReadAnimatorIKLayersGuarded(uintptr_t animator, bool& read) {
    __try {
        read = ReadAnimatorIKLayers(animator);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
        return GetExceptionCode();
    }
}

// Animator.GetIKPosition returns a 12-byte Vector3 through the same hidden
// sret wrapper used by the already validated Transform Vector3 getters.  The
// goal argument is an int32 and the MethodInfo descriptor remains the final
// argument on the x64 generated wrapper.  This probe is deliberately read
// only; a failed call disables only the optional IK evidence path.
using AnimatorIKPositionGetter = Vector3* (*)(Vector3*, uintptr_t, int, uintptr_t);

bool ValidateAnimatorIKPositionWrapper(const NativeMethod& method, std::ostream& report) {
    if (!method.entry || !method.target || !method.descriptor || method.parameterCount != 1) {
        report << "[ANIMATOR_IK_POSITION_ABI] ready=0 reason=descriptor_or_parameter_count\n";
        return false;
    }
    // The generated wrapper may save RCX after a short prologue (the Internal
    // variant starts with `push rsi`). Search only the entry block, and require
    // the established move into a callee-saved register before making a call.
    if (!Pattern(method.entry, 32, "48 89 CE")) {
        report << "[ANIMATOR_IK_POSITION_ABI] ready=0 reason=vector3_sret_prefix\n";
        return false;
    }
    report << "[ANIMATOR_IK_POSITION_ABI] ready=1 method=" << method.name
        << " metadataId=" << method.metadataId << " parameterCount="
        << unsigned(method.parameterCount) << " entry=0x" << std::hex
        << method.entry << " target=0x" << method.target << " descriptor=0x"
        << method.descriptor << std::dec
        << " RCX=sret RDX=Animator R8=AvatarIKGoal R9=descriptor bytes=12"
        << " invocation=READONLY_CANARY\n";
    return true;
}

bool ReadAnimatorIKPositions(uintptr_t animator) {
    if (!animator || !job.animatorIkPositionAbiReady) return false;
    uintptr_t native = 0;
    if (!Alive(animator, job.items[2].klass, native)) return false;
    const auto getter = reinterpret_cast<AnimatorIKPositionGetter>(job.animatorIkGetPosition.entry);
    constexpr uint32_t Canary = 0xD17A6C3B;
    job.animatorIkPositionSampleCount = 0;
    for (int goal = 0; goal < 4; ++goal) {
        struct Buffer { uint32_t first; Vector3 value; uint32_t last; } buffer{Canary, {}, Canary};
        const auto returned = getter(&buffer.value, animator, goal, job.animatorIkGetPosition.descriptor);
        if (returned != &buffer.value || buffer.first != Canary || buffer.last != Canary ||
            !std::isfinite(buffer.value.x) || !std::isfinite(buffer.value.y) ||
            !std::isfinite(buffer.value.z)) return false;
        job.animatorIkPositions[static_cast<size_t>(goal)] = buffer.value;
        ++job.animatorIkPositionSampleCount;
    }
    return job.animatorIkPositionSampleCount == job.animatorIkPositions.size();
}

DWORD ReadAnimatorIKPositionsGuarded(uintptr_t animator, bool& read) {
    __try {
        read = ReadAnimatorIKPositions(animator);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
        return GetExceptionCode();
    }
}

// Miscs.CalcCurrentGroundHeight(float worldX, float worldZ) is a static
// two-argument wrapper on the current metadata profile.  Its failure value is
// -1e8; reject that sentinel and never feed this read-only canary into a bone
// setter.  The exact metadata id gate prevents calling a same-named method
// with a different ABI after a game update.
bool ReadFootGroundHeights() {
    if (!job.groundHeightReady || !job.animatorIkPositionRead ||
        job.animatorIkPositionSampleCount < 4) return false;
    const auto getter = reinterpret_cast<GroundHeightGetter>(job.groundHeight.entry);
    for (unsigned side = 0; side < 2; ++side) {
        const auto& foot = job.animatorIkPositions[side + 2];
        const auto value = getter(foot.x, foot.z, job.groundHeight.descriptor);
        job.groundHeights[side] = value;
        job.footGroundDeltas[side] = foot.y - value;
        job.groundHeightValid[side] = std::isfinite(value) && value > -1.0e7f &&
            std::isfinite(job.footGroundDeltas[side]);
    }
    job.groundHeightRead = true;
    return true;
}

DWORD ReadFootGroundHeightsGuarded(bool& read) {
    __try {
        read = ReadFootGroundHeights();
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
        return GetExceptionCode();
    }
}
bool ReadCandidateIdentityRaw(uintptr_t object, wchar_t* result, int& length) {
    __try {
        if (!object) return false;
        const auto gameObject = reinterpret_cast<Getter>(job.gameObject)(object, job.gameObjectMethod);
        if (!gameObject) return false;
        const auto text = reinterpret_cast<Getter>(job.name)(gameObject, job.nameMethod);
        uintptr_t stringClass = 0;
        if (!Read(text, stringClass) || !Name(stringClass, "String") ||
            !Read(text + 0x10, length) || length < 0 || length >= 128) return false;
        for (int j = 0; j < length; ++j) if (!Read(text + 0x14 + size_t(j) * 2, result[j])) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void ReleaseCandidateRoots(GroundCandidate& candidate) {
    for (auto& handle : candidate.handles) {
        if (handle) {
            reinterpret_cast<FreeHandle>(job.context.handleFree)(handle);
            handle = 0;
        }
    }
}
const char* GroundStageName(int stage) {
    switch (stage) {
    case 1: return "TYPE_FACTORY_NULL";
    case 2: return "REFLECTION_TYPE_INVALID";
    case 3: return "REFLECTION_HANDLE_INVALID";
    case 4: return "FIND_OBJECTS_NULL";
    case 5: return "ARRAY_HANDLE_INVALID";
    case 6: return "ARRAY_HEADER_UNREADABLE";
    case 7: return "ARRAY_VALIDATION_FAILED";
    case 8: return "OK";
    case 9: return "NOT_UNITY_OBJECT";
    default: return "NOT_RUN";
    }
}
bool RootCandidateObject(uintptr_t object, uint32_t& handle) {
    if (!object) return false;
    handle = reinterpret_cast<NewHandle>(job.context.handleNew)(object, false);
    return handle && (handle >> 26) == 2 &&
        reinterpret_cast<TargetHandle>(job.context.handleTarget)(handle) == object;
}
void ReleaseGcHandle(uint32_t& handle) {
    if (!handle) return;
    reinterpret_cast<FreeHandle>(job.context.handleFree)(handle);
    handle = 0;
}
// Candidate component census only. Do not invoke candidate methods: their
// signatures and ownership are game-specific; presence/count is sufficient to
// select the next verified path without mutating animation or network state.
bool ReadGroundCandidateCounts() {
    for (auto& candidate : job.groundCandidates) {
        ReleaseCandidateRoots(candidate);
        candidate.count = 0;
        candidate.read = false;
        candidate.stage = 0;
        candidate.reflection = candidate.array = candidate.arrayClass = candidate.elementDescriptor = 0;
        candidate.bounds = UINT64_MAX; candidate.length = 0;
        candidate.arrayType = 0; candidate.elementMetadataId = candidate.expectedMetadataId = UINT32_MAX;
        candidate.sampleCount = 0;
        candidate.sampleObjects.fill(0);
        for (auto& name : candidate.sampleNames) name.clear();
        if (!candidate.resolved || !candidate.descriptor) continue;
        candidate.stage = 1;
        const auto reflection = reinterpret_cast<Factory>(job.context.reflectionType)(candidate.descriptor);
        candidate.reflection = reflection;
        if (!reflection) continue;
        candidate.stage = 2;
        if (!live_detail::ValidateReflection(reflection, job.reflectionClass, candidate.descriptor)) continue;
        candidate.stage = 3;
        if (!RootCandidateObject(reflection, candidate.handles[0])) continue;
        if (candidate.hierarchyValid && !candidate.derivesObject) {
            candidate.stage = 9;
            continue;
        }
        candidate.stage = 4;
        const auto array = reinterpret_cast<Getter>(job.find)(reflection, job.findMethod);
        candidate.array = array;
        if (!array) continue;
        candidate.stage = 5;
        if (!RootCandidateObject(array, candidate.handles[1])) continue;
        candidate.stage = 6;
        uintptr_t arrayClass = 0, elementDescriptor = 0, bounds = 0;
        uint8_t type = 0;
        uint64_t length = 0;
        if (!Read(array, arrayClass) || !Read(arrayClass + 0x68 + 0xA, type) ||
            !Read(arrayClass + 0x68, elementDescriptor) || !Read(array + 0x10, bounds) ||
            !Read(array + 0x18, length)) continue;
        candidate.arrayClass = arrayClass; candidate.elementDescriptor = elementDescriptor;
        candidate.arrayType = type; candidate.bounds = bounds; candidate.length = length;
        Read(elementDescriptor, candidate.elementMetadataId);
        Read(candidate.descriptor, candidate.expectedMetadataId);
        candidate.stage = 7;
        if (!live_detail::ValidateArray(array, candidate.klass, length)) continue;
        candidate.stage = 8;
        candidate.count = length;
        candidate.read = true;
        // Keep a small, bounded identity sample. This remains read-only and
        // lets one session distinguish a global service from the selected actor.
        for (uint64_t i = 0; i < std::min<uint64_t>(length, 8); ++i) {
            uintptr_t object = 0, klass = 0;
            if (!Read(array + 0x20 + i * 8, object) || !Read(object, klass) || klass != candidate.klass) continue;
            wchar_t sampleBuffer[128]{};
            int sampleLength = 0;
            candidate.sampleObjects[candidate.sampleCount] = object;
            if (candidate.derivesComponent && ReadCandidateIdentityRaw(object, sampleBuffer, sampleLength))
                candidate.sampleNames[candidate.sampleCount] = Utf8(sampleBuffer, sampleLength);
            ++candidate.sampleCount;
            if (candidate.sampleCount == candidate.sampleObjects.size()) break;
        }
    }
    return true;
}
bool ReadSelectedComponentHierarchy(uintptr_t animator) {
    job.componentScanRead = false;
    job.componentLayerCount = 0;
    for (auto& candidate : job.groundCandidates) {
        candidate.hierarchyMatches = candidate.hierarchySampleCount = 0;
        candidate.hierarchyObjects.fill(0); candidate.hierarchyDepth.fill(0);
        for (auto& name : candidate.hierarchyNames) name.clear();
    }
    if (!animator) return false;
    job.componentScanAnimator = animator;
    if (!job.components.entry || !job.parent.entry || !job.componentClass) return false;
    const auto type = reinterpret_cast<Factory>(job.context.reflectionType)(job.componentClass + 0x68);
    if (!live_detail::ValidateReflection(type, job.reflectionClass, job.componentClass + 0x68)) return false;
    if (job.componentTypeHandle) {
        if (reinterpret_cast<TargetHandle>(job.context.handleTarget)(job.componentTypeHandle) != type) return false;
    } else if (!RootCandidateObject(type, job.componentTypeHandle)) {
        return false;
    }
    auto transform = reinterpret_cast<Getter>(job.transform)(animator, job.transformMethod);
    if (!RootCandidateObject(transform, job.componentScanHandles[0])) return false;
    uintptr_t seen[24]{};
    bool reachedRoot = false;
    for (unsigned depth = 0; transform && depth < std::size(seen); ++depth) {
        for (unsigned i = 0; i < depth; ++i) if (seen[i] == transform) return false;
        seen[depth] = transform;
        auto& layer = job.componentLayers[job.componentLayerCount++];
        layer = ComponentLayer{}; layer.depth = depth; layer.transform = transform;
        uintptr_t native = 0;
        if (!Alive(transform, job.transformClass, native)) { layer.stage = 1; return false; }
        layer.gameObject = reinterpret_cast<Getter>(job.gameObject)(transform, job.gameObjectMethod);
        if (!layer.gameObject) { layer.stage = 2; return false; }
        if (!RootCandidateObject(layer.gameObject, job.componentScanHandles[1])) { layer.stage = 3; return false; }
        if (!ReadName(layer.gameObject, layer.name, layer.nameLength)) { layer.stage = 4; return false; }
        const auto array = reinterpret_cast<uintptr_t (*)(uintptr_t, uintptr_t, bool, bool, bool, bool, uintptr_t, uintptr_t)>(job.components.entry)
            (layer.gameObject, type, true, false, true, false, 0, job.components.descriptor);
        if (!array) { layer.stage = 5; ReleaseGcHandle(job.componentScanHandles[1]); return false; }
        if (!RootCandidateObject(array, job.componentScanHandles[2])) { layer.stage = 6; return false; }
        uint64_t length = 0;
        if (!live_detail::ValidateArray(array, job.componentClass, length)) { layer.stage = 7; return false; }
        layer.stage = 8; layer.componentCount = length;
        for (uint64_t i = 0; i < std::min<uint64_t>(length, 512); ++i) {
            uintptr_t component = 0, componentKlass = 0;
            if (!Read(array + 0x20 + i * 8, component) || !Read(component, componentKlass)) continue;
            for (auto& candidate : job.groundCandidates) {
                if (!candidate.resolved || componentKlass != candidate.klass) continue;
                ++candidate.hierarchyMatches;
                if (candidate.hierarchySampleCount == candidate.hierarchyObjects.size()) continue;
                auto sample = candidate.hierarchySampleCount++;
                candidate.hierarchyObjects[sample] = component;
                candidate.hierarchyDepth[sample] = depth;
                wchar_t objectName[128]{}; int objectNameLength = 0;
                if (ReadCandidateIdentityRaw(component, objectName, objectNameLength))
                    candidate.hierarchyNames[sample] = Utf8(objectName, objectNameLength);
            }
        }
        ReleaseGcHandle(job.componentScanHandles[2]);
        ReleaseGcHandle(job.componentScanHandles[1]);
        const auto parent = reinterpret_cast<Getter>(job.parent.entry)(transform, job.parent.descriptor);
        if (!parent) { layer.stage = 9; reachedRoot = true; break; }
        if (!RootCandidateObject(parent, job.componentScanHandles[3])) return false;
        ReleaseGcHandle(job.componentScanHandles[0]);
        job.componentScanHandles[0] = job.componentScanHandles[3];
        job.componentScanHandles[3] = 0;
        transform = parent;
    }
    if (transform && !reachedRoot) return false;
    job.componentScanRead = true;
    return true;
}

bool ReadSelectedComponentSubtree(uintptr_t animator) {
    job.componentSubtreeFresh = false;
    if (!animator) return false;
    if (job.componentSubtreeRead && job.componentSubtreeAnimator == animator) return true;

    job.componentSubtreeRead = false;
    job.componentSubtreeAnimator = 0;
    job.componentSubtreeRoot = 0;
    job.componentSubtreeComponentCount = 0;
    job.componentSubtreeTypeCount = 0;
    job.componentSubtreeRootName.clear();
    job.componentSubtreeTruncated = false;
    job.componentSubtreeTypes.fill(ComponentClassCount{});
    for (auto& candidate : job.groundCandidates) {
        candidate.subtreeMatches = candidate.subtreeSampleCount = 0;
        candidate.subtreeObjects.fill(0);
        for (auto& name : candidate.subtreeNames) name.clear();
    }
    for (auto& handle : job.componentScanHandles) ReleaseGcHandle(handle);
    if (!job.components.entry || !job.componentClass || !job.transform || !job.gameObject) return false;

    const auto type = reinterpret_cast<Factory>(job.context.reflectionType)(job.componentClass + 0x68);
    if (!live_detail::ValidateReflection(type, job.reflectionClass, job.componentClass + 0x68)) return false;
    if (job.componentTypeHandle) {
        if (reinterpret_cast<TargetHandle>(job.context.handleTarget)(job.componentTypeHandle) != type) return false;
    } else if (!RootCandidateObject(type, job.componentTypeHandle)) {
        return false;
    }

    const auto transform = reinterpret_cast<Getter>(job.transform)(animator, job.transformMethod);
    uintptr_t native = 0;
    if (!Alive(transform, job.transformClass, native) || !RootCandidateObject(transform, job.componentScanHandles[0])) return false;
    const auto gameObject = reinterpret_cast<Getter>(job.gameObject)(transform, job.gameObjectMethod);
    if (!gameObject || !RootCandidateObject(gameObject, job.componentScanHandles[1])) return false;
    wchar_t rootName[128]{};
    int rootNameLength = 0;
    if (!ReadName(gameObject, rootName, rootNameLength)) return false;

    const auto array = reinterpret_cast<uintptr_t (*)(uintptr_t, uintptr_t, bool, bool, bool, bool, uintptr_t, uintptr_t)>(job.components.entry)
        (gameObject, type, true, true, true, false, 0, job.components.descriptor);
    if (!array || !RootCandidateObject(array, job.componentScanHandles[2])) return false;
    uint64_t length = 0;
    if (!live_detail::ValidateArray(array, job.componentClass, length)) return false;

    constexpr uint64_t MaxSubtreeComponents = 4096;
    job.componentSubtreeRoot = gameObject;
    job.componentSubtreeRootName = Utf8(rootName, rootNameLength);
    job.componentSubtreeComponentCount = length;
    job.componentSubtreeTruncated = length > MaxSubtreeComponents;
    for (uint64_t i = 0; i < std::min(length, MaxSubtreeComponents); ++i) {
        uintptr_t component = 0, klass = 0;
        if (!Read(array + 0x20 + i * sizeof(uintptr_t), component) || !component || !Read(component, klass) || !klass) continue;
        unsigned typeIndex = 0;
        while (typeIndex < job.componentSubtreeTypeCount && job.componentSubtreeTypes[typeIndex].klass != klass) ++typeIndex;
        if (typeIndex == job.componentSubtreeTypeCount) {
            if (job.componentSubtreeTypeCount == job.componentSubtreeTypes.size()) {
                job.componentSubtreeTruncated = true;
            } else {
                job.componentSubtreeTypes[typeIndex].klass = klass;
                ++job.componentSubtreeTypeCount;
            }
        }
        if (typeIndex < job.componentSubtreeTypeCount && job.componentSubtreeTypes[typeIndex].count < UINT32_MAX)
            ++job.componentSubtreeTypes[typeIndex].count;
        for (auto& candidate : job.groundCandidates) {
            if (!candidate.resolved || klass != candidate.klass) continue;
            ++candidate.subtreeMatches;
            if (candidate.subtreeSampleCount == candidate.subtreeObjects.size()) continue;
            const auto sample = candidate.subtreeSampleCount++;
            candidate.subtreeObjects[sample] = component;
            wchar_t objectName[128]{};
            int objectNameLength = 0;
            if (ReadCandidateIdentityRaw(component, objectName, objectNameLength))
                candidate.subtreeNames[sample] = Utf8(objectName, objectNameLength);
        }
    }
    for (auto& handle : job.componentScanHandles) ReleaseGcHandle(handle);
    job.componentSubtreeAnimator = animator;
    job.componentSubtreeRead = true;
    job.componentSubtreeFresh = true;
    return true;
}

bool Root(Item& item, unsigned slot, uintptr_t object) {
    if (!object) return false;
    item.handles[slot] = reinterpret_cast<NewHandle>(job.context.handleNew)(object, false);
    const auto valid = item.handles[slot] && (item.handles[slot] >> 26) == 2 &&
        reinterpret_cast<TargetHandle>(job.context.handleTarget)(item.handles[slot]) == object;
    if (valid) ++item.rooted;
    return valid;
}
bool Alive(uintptr_t object, uintptr_t expected, uintptr_t& native) {
    uintptr_t klass = 0;
    return Read(object, klass) && klass == expected && Read(object + 0x10, native) && native;
}
bool ReadName(uintptr_t object, wchar_t (&name)[128], int& length) {
    const auto text = reinterpret_cast<Getter>(job.name)(object, job.nameMethod);
    uintptr_t klass = 0;
    if (!Read(text, klass) || !Name(klass, "String") || !Read(text + 0x10, length) || length < 0 || length >= 128) return false;
    for (int i = 0; i < length; ++i) if (!Read(text + 0x14 + size_t(i) * 2, name[i])) return false;
    return true;
}
// 此叶函数没有 C++ 对象析构；记录 SEH 码后停止调用，不把异常穿过 Windows callback。
bool ExecuteGuarded() {
    __try {
        job.step = 1;
        job.actualThread = GetCurrentThreadId();
        DWORD expected = 0;
        if (!Read(job.context.mainThreadSlot, expected) || expected != job.actualThread || expected != job.mainThread) return false;
        job.runtimeThread = reinterpret_cast<uintptr_t (*)()>(job.context.threadCurrent)();
        if (!job.runtimeThread) return false;
        if (job.mode) { job.step=20+job.mode; job.complete=job.mode>=6?MotionMainStep(job.mode):BoneWriteStep(job.mode); return job.complete; }
        BeginRigSelection();
        for (size_t target = 1; target < job.items.size(); ++target) {
            auto& item = job.items[target];
            job.step = 2;
            item.reflection = reinterpret_cast<Factory>(job.context.reflectionType)(item.descriptor);
            if (!Read(item.reflection + 0x10, item.reflectionDescriptor) ||
                !live_detail::ValidateReflection(item.reflection, job.reflectionClass, item.descriptor) || !Root(item, 0, item.reflection)) return false;
            item.repeat = reinterpret_cast<Factory>(job.context.reflectionType)(item.descriptor);
            if (item.repeat != item.reflection) return false;
            job.step = 3;
            item.array = reinterpret_cast<Getter>(job.find)(item.reflection, job.findMethod);
            if (!Root(item, 1, item.array)) return false;
            item.arrayValid = live_detail::ValidateArray(item.array, item.klass, item.length);
            if (!item.arrayValid || !Read(item.array, item.arrayClass)) return false;
            if (!item.length) continue;
            job.step = 4;
            int largestBones = -1;
            for (uint64_t i = 0; i < std::min<uint64_t>(item.length, 256); ++i) {
                uintptr_t object = 0, klass = 0;
                if (!Read(item.array + 0x20 + i * 8, object) || !Read(object, klass)) return false;
                if (klass != item.klass) continue;
                uintptr_t native = 0;
                int count = 0;
                if (!Alive(object, item.klass, native)) return false;
                if (target == 2) {
                    const auto transform = reinterpret_cast<Getter>(job.transform)(object, job.transformMethod);
                    uintptr_t nativeTransform = 0;
                    if (!Alive(transform, job.transformClass, nativeTransform)) { ++item.skippedTransforms; continue; }
                    item.object = object; item.objectClass = klass;
                    break;
                }
                // 已验证 get_bones consumer 从 native+430 容器的 +10 读取骨数量。
                if (!Read(native + 0x440, count) || count < 0) return false;
                if (RigHasTarget()) {
                    const auto match=MatchRigCandidate(object);
                    if (match<0) return false;
                    if (!match) continue;
                }
                // 在有界候选集合中优先骨数量最多且可完整导出的 SMR，不用名称猜角色身份。
                if (count <= int(item.bones.size()) && count > largestBones) {
                    item.object = object; item.objectClass = klass; largestBones = count;
                }
            }
            // 同一数组可能含 RectTransform 等派生实例，本阶段只取可直接交叉核验的精确类型。
            if (!item.object) continue;
            if (!Alive(item.object, item.klass, item.nativeObject)) return false;
            job.step = 5;
            // 已确认 childCount 返回 EAX，并与 native 字段交叉核验。
            item.transform = item.klass == job.transformClass ? item.object :
                reinterpret_cast<Getter>(job.transform)(item.object, job.transformMethod);
            uintptr_t nativeTransform = 0;
            if (!Alive(item.transform, job.transformClass, nativeTransform) || !Root(item, 2, item.transform)) return false;
            item.childCount = reinterpret_cast<IntGetter>(job.child)(item.transform, job.childMethod);
            if (!Read(nativeTransform + 0x90, item.nativeChildCount) || item.childCount < 0 || item.childCount > 100000 || item.childCount != item.nativeChildCount) return false;
            job.step = 6;
            item.gameObject = reinterpret_cast<Getter>(job.gameObject)(item.object, job.gameObjectMethod);
            uintptr_t nativeGameObject = 0;
            if (!Alive(item.gameObject, job.gameObjectClass, nativeGameObject) || !Root(item, 3, item.gameObject)) return false;
            job.step = 7;
            item.nameObject = reinterpret_cast<Getter>(job.name)(item.gameObject, job.nameMethod);
            uintptr_t stringClass = 0;
            if (!Read(item.nameObject, stringClass) || !Name(stringClass, "String") || !Read(item.nameObject + 0x10, item.nameLength) ||
                item.nameLength < 0 || item.nameLength >= 128) return false;
            for (int i = 0; i < item.nameLength; ++i) if (!Read(item.nameObject + 0x14 + size_t(i) * 2, item.name[i])) return false;
            item.liveValid = true;
            if (target == 2) {
                job.step = 9;
                item.isHuman = reinterpret_cast<bool (*)(uintptr_t, uintptr_t)>(job.human.entry)(item.object, job.human.descriptor);
                item.humanRead = true;
            } else {
                job.step = 10;
                item.rootBone = reinterpret_cast<Getter>(job.rootBone.entry)(item.object, job.rootBone.descriptor);
                uintptr_t nativeRoot = 0;
                if (item.rootBone && (!Alive(item.rootBone, job.transformClass, nativeRoot) || !Root(item, 4, item.rootBone))) return false;
                job.step = 11;
                item.bonesArray = reinterpret_cast<Getter>(job.bones.entry)(item.object, job.bones.descriptor);
                if (!Root(item, 5, item.bonesArray) || !live_detail::ValidateArray(item.bonesArray, job.transformClass, item.bonesLength) ||
                    !Read(item.nativeObject + 0x440, item.nativeBonesLength) || item.nativeBonesLength < 0 ||
                    item.bonesLength != uint64_t(item.nativeBonesLength) || item.bonesLength > item.bones.size()) return false;
                item.bonesValid = true;
                job.step = 12;
                item.sharedMesh = reinterpret_cast<Getter>(job.mesh.entry)(item.object, job.mesh.descriptor);
                if (item.sharedMesh) {
                    uintptr_t meshClass = 0, nativeMesh = 0;
                    if (!Read(item.sharedMesh, meshClass) || !Name(meshClass, "Mesh") || !Read(item.sharedMesh + 0x10, nativeMesh) || !nativeMesh ||
                        !Root(item, 6, item.sharedMesh) || !ReadName(item.sharedMesh, item.meshName, item.meshNameLength)) return false;
                    item.meshClass=meshClass;
                }
                job.step = 13;
                for (size_t i = 0; i < item.bonesLength; ++i) {
                    auto& bone = item.bones[i];
                    if (!Read(item.bonesArray + 0x20 + i * 8, bone.object)) return false;
                    // 保留 null slot 的索引，不能对空/已销毁 Transform 调用 getter。
                    if (!bone.object) continue;
                    if (!Read(bone.object, bone.klass) || !Alive(bone.object, job.transformClass, bone.native) ||
                        !ReadName(bone.object, bone.name, bone.nameLength)) return false;
                }
            }
        }
        const auto& smr = job.items[1];
        const auto& animator = job.items[2];
        job.step = 14;
        ReadGroundCandidateCounts();
        if (smr.liveValid && smr.bonesValid && smr.bonesLength && !CaptureRig(smr.object, smr.transform, smr.bonesArray,
            smr.bonesLength, smr.array, smr.length, animator.array, animator.length)) return false;
        const auto selectedAnimator = SelectedRigAnimator();
        if (selectedAnimator) {
            ReadSelectedComponentHierarchy(selectedAnimator);
            ReadSelectedComponentSubtree(selectedAnimator);
        }
        if (job.fbikProbeEnabled && job.fbikMethodsReady) {
            if (!selectedAnimator) job.fbikReadFailure=1;
            else {
                job.fbikReadFault=ReadFBIKCountsGuarded(selectedAnimator,job.fbikRead);
                if (job.fbikReadFault) job.fbikReadFailure=3;
                else if (!job.fbikRead) job.fbikReadFailure=2;
            }
        }
        if (job.fbikProbeEnabled && job.animatorLayerMethodsReady && selectedAnimator) {
            job.animatorLayerReadFault = ReadAnimatorIKLayersGuarded(selectedAnimator, job.animatorLayerRead);
        }
        if (job.animatorIkPositionAbiReady && selectedAnimator) {
            job.animatorIkPositionReadFault = ReadAnimatorIKPositionsGuarded(
                selectedAnimator, job.animatorIkPositionRead);
        }
        if (job.groundHeightReady && job.animatorIkPositionRead)
            job.groundHeightFault = ReadFootGroundHeightsGuarded(job.groundHeightRead);
        job.step = 8;
        job.complete = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        job.fault = GetExceptionCode();
        return false;
    }
}
void ReleaseGuarded() {
    __try {
        ReleaseBoneScratch();
        if (!job.complete && MotionRigArmed()) { DisableMotionWrites(); MotionMainStep(6); }
        if (!job.complete && BoneWriteArmed()) BoneWriteStep(4);
        ReleaseBoneScratch();
        ReleaseRigProbe();
        for (auto& item : job.items) for (auto& handle : item.handles) {
            if (handle) { reinterpret_cast<FreeHandle>(job.context.handleFree)(handle); ++item.released; }
            handle = 0;
        }
        for (auto& candidate : job.groundCandidates) ReleaseCandidateRoots(candidate);
        ReleaseGcHandle(job.componentTypeHandle);
        for (auto& handle : job.componentScanHandles) ReleaseGcHandle(handle);
    } __except (EXCEPTION_EXECUTE_HANDLER) { job.fault = GetExceptionCode(); job.complete = false; }
}
LRESULT CALLBACK Dispatch(int code, WPARAM wParam, LPARAM lParam) {
    active.fetch_add(1, std::memory_order_acq_rel);
    if (code >= 0 && wParam == PM_REMOVE) {
        const auto* message = reinterpret_cast<const MSG*>(lParam);
        unsigned expected = 1;
        if (message && message->message == dispatchMessage && message->wParam == reinterpret_cast<WPARAM>(&job) &&
            state.compare_exchange_strong(expected, 2, std::memory_order_acq_rel)) {
            ExecuteGuarded();
            ReleaseGuarded();
            state.store(3, std::memory_order_release);
        }
    }
    const auto result = CallNextHookEx(nullptr, code, wParam, lParam);
    active.fetch_sub(1, std::memory_order_release);
    return result;
}
struct Window { DWORD thread; HWND hwnd; };
BOOL CALLBACK SelectWindow(HWND hwnd, LPARAM parameter) {
    auto& result = *reinterpret_cast<Window*>(parameter);
    DWORD pid = 0;
    const auto tid = GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && tid == result.thread && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
        result.hwnd = hwnd; return FALSE;
    }
    return TRUE;
}
bool DispatchJob(HANDLE stop,HMODULE module,Window window,std::ostream& report,bool cancellable) {
    job.complete=false; job.fault=0; job.step=0;
    auto hook=SetWindowsHookExW(WH_GETMESSAGE,Dispatch,module,job.mainThread);
    if (!hook) { report << "dispatch_hook_error=" << GetLastError() << '\n'; return false; }
    state.store(1,std::memory_order_release);
    if (!PostMessageW(window.hwnd,dispatchMessage,reinterpret_cast<WPARAM>(&job),0)) state.store(0,std::memory_order_release);
    const auto deadline=GetTickCount64()+30000;
    while (state.load(std::memory_order_acquire)!=3 && GetTickCount64()<deadline) {
        if (cancellable && WaitForSingleObject(stop,10)==WAIT_OBJECT_0) break;
        if (!cancellable) Sleep(10);
    }
    unsigned pending=1;
    state.compare_exchange_strong(pending,0,std::memory_order_acq_rel);
    while (state.load(std::memory_order_acquire)==2) Sleep(1);
    const auto removed=UnhookWindowsHookEx(hook);
    const auto error=removed?0:GetLastError();
    while (active.load(std::memory_order_acquire)) Sleep(1);
    if (!removed) {
        HMODULE pinned=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Dispatch),&pinned);
        state.store(0,std::memory_order_release);
        report << "dispatch_unhook=FAILED error=" << error << " module_pinned=" << bool(pinned) << '\n';
        return false;
    }
    report << "dispatch_unhook=OK; callbacks_in_flight=0\ndispatch_state=" << state.load() << " actual_tid=" << job.actualThread
        << " Unity_main_tid=" << job.mainThread << " step=" << job.step << " SEH=0x" << std::hex << job.fault << std::dec << '\n';
    return job.complete && !job.fault && state.load()==3;
}
std::string Utf8(const wchar_t* text,int length);
bool RunMotionSession(GenshinNativeRuntime& runtime,HANDLE stop,HMODULE module,Window window,std::ostream& report,const std::filesystem::path& directory,bool& rescan,std::wstring& requestedName) {
    rescan=false;
    bool staleActorRescan=false;
    const auto clearStaleTarget=[&](const char* stage) {
        const auto previous=requestedName.empty()?std::string("<auto-player-branch>"):Utf8(requestedName.c_str(),int(requestedName.size()));
        requestedName.clear();
        SetRigWriteTarget({});
        report << "[RIG_TARGET] action=CLEAR stage=" << stage << " target_before=" << previous
            << " target_after=<auto-player-branch> requested_actor_cleared=1\n";
        report << "motion_rescan=STALE_ACTOR_TARGET_CLEARED stage=" << stage << " previous_actor=" << previous << " next=AUTO_PLAYER_BRANCH\n";
    };
    if (!MotionRigArmed()) { report << "motion_session=NOT_ARMED; reason=Actor_or_rig_gate\n"; return false; }
    // 初始化只读 reference；失败后仍走统一的恢复 / roots 清理，禁止退回当前动画基准。
    bool passed=ConfigureMotionAB(directory,report)&&InitializeMotionReference(runtime,job.items[1].meshClass,job.mesh,report);
    if (passed) {
        std::span<const uintptr_t> physicsComponents;
        for(const auto& candidate:job.groundCandidates)if(std::string_view(candidate.name)=="DynamicBoneArray") {
            // 祖先扫描始终刷新；子树只在 fresh 时可用，禁止复用旧角色的缓存实例。
            if(job.componentSubtreeFresh&&candidate.subtreeMatches==candidate.subtreeSampleCount)
                physicsComponents={candidate.subtreeObjects.data(),candidate.subtreeSampleCount};
            else if(candidate.hierarchyMatches==candidate.hierarchySampleCount)
                physicsComponents={candidate.hierarchyObjects.data(),candidate.hierarchySampleCount};
            break;
        }
        native_physics::Initialize(runtime,SelectedRigAnimator(),physicsComponents,report);
        job.mode=9;
        passed=DispatchJob(stop,module,window,report,true);
        ReportMotion(report);
        if (passed) ExportMotionReferences(directory,report);
        else if (!job.fault && MotionIdentityRejected()) {
            report << "motion_reference=REJECTED_STALE_RIG; rescan=1; character_writes=NONE\n";
            report.flush();
            clearStaleTarget("REFERENCE");
            UnloadMotionClip();
            rescan=true;
            if (ui::Enabled()) {
                ui::Bound({},0);
                ui::State(L"角色在校验期间已切换，正在重新扫描；没有写入动作。",false,false,false);
            }
            return true;
        }
    }
    if (passed) passed=InstallAnimationReturnProbe(runtime,stop,report);
    bool ikTimingReady=false;
    if (passed) {
        // The game-owned callback is observed only after the existing MinHook
        // session is live; IK probe failure must not block motion playback.
        ikTimingReady=InstallIKTimingProbe(runtime,job.mainThread,SelectedRigAnimator(),
            job.items[2].klass,job.gameObjectClass,job.gameObject,job.gameObjectMethod,report);
    }
    if (passed) passed=ConfigureMotionAB(directory,report);
    if (passed) InitializeMotionPhysics(report);
    report << "phase=" << (passed?"MOTION_COMMANDS_READY":"MOTION_REFERENCE_BLOCKED") << "; NUMPAD1=load_or_reload_and_play; NUMPAD2=stop_and_restore; NUMPAD3=finish_session; NumLock=ON; playback_control=MANUAL\n";
    report << "motion_path_file=motion_path.txt; command_flags=motion_play.flag,motion_stop.flag,motion_exit.flag\n"; report.flush();
    report << "AB_controls=NUMPAD6_Legacy; NUMPAD7_Basis; NUMPAD8_cycle_Identity_X_Y_Z_Frame0_Frame30_Frame120; selection_requires_STOPPED; start=NUMPAD1; restore=NUMPAD2\n";report.flush();
    if (ui::Enabled()) ui::State(passed?L"骨架已就绪。选择 VMD 后载入，再点击播放。":L"静态绑定姿态校验未通过，已禁用写入；可刷新或重新绑定。",passed,false,false,!passed);
    auto consume=[&](const wchar_t* name) {
        const auto path=directory/name;
        if (GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) return false;
        return DeleteFileW(path.c_str())!=0;
    };
    bool previousPlay=false,previousStop=false,previousExit=false;
    bool previousAB[3]{};
    uint64_t nextProgress=0;
    unsigned previousStage=UINT_MAX;
    try {
        while (WaitForSingleObject(stop,25)!=WAIT_OBJECT_0) {
            DWORD foreground=0; GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
            const auto inGame=foreground==GetCurrentProcessId()&&GetForegroundWindow()==window.hwnd;
            for(unsigned i=0;i<3;++i) {
                const bool down=inGame&&(GetAsyncKeyState(VK_NUMPAD6+i)&0x8000);
                if(down&&!previousAB[i]) {
                    const bool selected=SelectMotionAB(6+i,report);
                    if(ui::Enabled())ui::State(selected?L"A/B 测试已选择。详见日志；点击播放或小键盘 1 开始。":L"请先停止并恢复，再选择 A/B 测试。",true,ui::Read().loaded,MotionStatus()==1);
                }
                previousAB[i]=down;
            }
            const bool play=inGame&&(GetAsyncKeyState(VK_NUMPAD1)&0x8000),stopKey=inGame&&(GetAsyncKeyState(VK_NUMPAD2)&0x8000),finish=inGame&&(GetAsyncKeyState(VK_NUMPAD3)&0x8000);
            bool playCommand=(play&&!previousPlay)||consume(L"motion_play.flag"),stopCommand=(stopKey&&!previousStop)||consume(L"motion_stop.flag"),exitCommand=(finish&&!previousExit)||consume(L"motion_exit.flag");
            bool loadOnly=false,unload=false,startOnly=false;std::wstring selectedPath;
            ui::Command command{ui::CommandKind::Refresh};
            if (ui::Enabled()&&ui::Take(command)) {
                if(command.kind==ui::CommandKind::LoadFace||command.kind==ui::CommandKind::ConfigureFace||command.kind==ui::CommandKind::MapFace) {
                    if(MotionStatus()==1){continue;}
                    job.mode=8;
                    if(!DispatchJob(stop,module,window,report,true))continue;
                    if(command.kind==ui::CommandKind::LoadFace)face::Load(command.text,report,stop);
                    else face::Configure(command,report);
                    continue;
                }
                if(command.kind==ui::CommandKind::ConfigurePhysics) {
                    ConfigureMotionPhysics(command.text,command.generation,report);continue;
                }
                if(command.kind==ui::CommandKind::BakePhysics) {
                    BakeMotionPhysics(directory.parent_path()/L"PhysicsCache",command.generation,stop,report);continue;
                }
                if (command.kind==ui::CommandKind::Refresh||command.kind==ui::CommandKind::BindActor||command.kind==ui::CommandKind::ReleaseRig) {
                    if (MotionStatus()==1) {ui::State(L"请先停止动作，再切换或刷新骨架。",true,true,true);continue;}
                    if (command.kind==ui::CommandKind::BindActor&&!ui::ValidateSelection(command)) {ui::State(L"骨架列表已更新或目标不可绑定，请重新选择。",true,ui::Read().loaded,false);continue;}
                    if (command.kind==ui::CommandKind::BindActor) {
                        requestedName=command.text;
                        report << "[RIG_TARGET] action=SET stage=MOTION_SESSION target_after="
                            << Utf8(requestedName.c_str(),int(requestedName.size())) << " source=USER_BIND\n";
                    }
                    if (command.kind==ui::CommandKind::ReleaseRig) clearStaleTarget("RELEASE");
                    if (command.kind==ui::CommandKind::Refresh) {
                        if (!requestedName.empty()) {
                            report << "[UI_RIG_REFRESH] target_cleared=1 previous_actor="
                                << Utf8(requestedName.c_str(),int(requestedName.size())) << " reason=MANUAL_REFRESH\n";
                        }
                        requestedName.clear();
                        SetRigWriteTarget({});
                        report << "[UI_RIG_REFRESH] accepted=1 stage=MOTION_SESSION target_cleared=1 scan_follows=1\n";
                    }
                    rescan=true;break;
                }
                playCommand|=command.kind==ui::CommandKind::Play;
                startOnly=command.kind==ui::CommandKind::Play;
                stopCommand|=command.kind==ui::CommandKind::Stop;
                loadOnly=command.kind==ui::CommandKind::LoadFile;
                selectedPath=loadOnly?command.text:L"";
                unload=command.kind==ui::CommandKind::Unload;
            }
            if (stopCommand) {playCommand=false;loadOnly=false;startOnly=false;unload=false;}
            if(playCommand&&ui::Enabled()) {
                const auto controls=ui::Read();
                if(controls.physics.mode==2&&controls.loaded)startOnly=true;
            }
            previousPlay=play; previousStop=stopKey; previousExit=finish;
            if (exitCommand) {if (ui::Enabled()) {rescan=true;clearStaleTarget("EXIT");}break;}
            if (!passed) continue;
            if (stopCommand || MotionStatus()==2 || MotionStatus()==3 || playCommand || loadOnly || unload) {
                const auto prior=MotionStatus();
                DisableMotionWrites(); job.mode=8;
                passed=DispatchJob(stop,module,window,report,false);
                ReportMotion(report);
                ReportMotionAB(report);
                const auto stopOutcome=MotionStopOutcome();
                const char* stopName=stopOutcome==MotionStopDisposition::Restored?"RESTORED":
                    stopOutcome==MotionStopDisposition::RestoredInactive?"RESTORED_INACTIVE_OWNER":
                    stopOutcome==MotionStopDisposition::OwnerInactiveSkipped?"OWNER_INACTIVE_SKIPPED":
                    stopOutcome==MotionStopDisposition::IdentityLost?"IDENTITY_LOST":
                    stopOutcome==MotionStopDisposition::RestoreFailed?"RESTORE_FAILED":"NOT_CAPTURED";
                report << "motion_stop=" << stopName << " dispatch=" << (passed?"OK":"FAILED")
                    << " prior_status=" << prior << '\n'; report.flush();
                if (stopOutcome==MotionStopDisposition::RestoredInactive ||
                    stopOutcome==MotionStopDisposition::OwnerInactiveSkipped ||
                    stopOutcome==MotionStopDisposition::IdentityLost) {
                    report << "motion_restore="
                        << (stopOutcome==MotionStopDisposition::RestoredInactive
                            ? "RESTORED_INACTIVE_OWNER"
                            : "SKIPPED_STALE_OR_INACTIVE_OWNER")
                        << "; old_rig=RELEASE_ONLY; rescan=1\n";
                    clearStaleTarget("STOP_INACTIVE");
                    staleActorRescan=true; rescan=true; passed=true; break;
                }
                if (prior==3) {
                    if (!job.fault && MotionIdentityRejected()) {
                        report << "motion_restore=UNAVAILABLE_STALE_ACTOR; old_rig=RELEASE_ONLY; rescan=1\n";
                        clearStaleTarget("STOP");
                        staleActorRescan=true; rescan=true; passed=true; break;
                    }
                    if (ui::Enabled()) ui::State(L"播放过程异常，已停止写入；请刷新或重新绑定角色。",false,false,false,true);
                    passed=false; break;
                }
                if (ui::Enabled()) ui::State(passed?L"动作已停止，现场姿态已恢复。":L"恢复校验失败；已停止写入，可刷新或重新绑定。",passed,ui::Read().loaded,false,!passed);
            }
            if (unload&&passed) {passed=UnloadMotionClip();if (ui::Enabled()) ui::State(passed?L"动作已载出。骨架保持绑定。":L"动作载出失败。",passed,false,false,!passed);}
            if (((playCommand&&!startOnly)||loadOnly) && passed) {
                if(!ConfigureMotionAB(directory,report))continue;
                std::string text;
                if (loadOnly) text=Utf8(selectedPath.c_str(),int(selectedPath.size()));
                else {std::ifstream pathFile(directory/L"motion_path.txt",std::ios::binary);if (pathFile) std::getline(pathFile,text);}
                if (!text.empty()&&text.back()=='\r') text.pop_back();
                if (text.starts_with("\xEF\xBB\xBF")) text.erase(0,3);
                const auto size=text.size()<=32767?MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0):0;
                if (!size) { report << "motion_load=REJECTED; reason=MOTION_PATH_EMPTY_OR_INVALID_UTF8\n"; report.flush();if (ui::Enabled()) ui::State(L"请选择有效的动作文件路径。",true,ui::Read().loaded,false);continue; }
                std::wstring path(size,L'\0'); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),path.data(),size);
                report << "motion_source=" << text << '\n';
                UnloadMotionClip();
                if (!LoadMotionClip(path,report,stop)) {if (ui::Enabled()) ui::State(L"动作载入被拒绝。请确认文件含角色骨骼动作，并查看日志。",true,false,false);continue;}
                if (loadOnly) {
                    std::ofstream pathFile(directory/L"motion_path.txt",std::ios::binary);pathFile << text;pathFile.flush();
                    if (ui::Enabled()) ui::State(L"动作已载入。点击播放开始。",true,true,false);
                }
            }
            if (playCommand&&passed) {
                if (startOnly&&!ui::Read().loaded) {ui::State(L"请先载入动作文件。",true,false,false);continue;}
                if(ui::Enabled()&&ui::Read().physics.mode==2&&!ui::Read().physics.bakedFrames) {
                    ui::State(L"预烘焙模式请先点击生成 / 读取，再播放。",true,true,false);continue;
                }
                if(!ConfigureMotionAB(directory,report))continue;
                job.mode=7; passed=DispatchJob(stop,module,window,report,true);
                // A character switch can invalidate the managed/native identities
                // between the scene snapshot and NUMPAD1. Keep the identity gate
                // strict, but return to the scanner instead of killing the session.
                // DispatchJob may release the stale rig while unwinding the failed
                // main-thread call. Use the explicit identity result instead of
                // MotionRigArmed(), which is expected to be false at this point.
                if (!passed && !job.fault && MotionStatus()==0 && MotionIdentityRejected()) {
                    report << "motion_start=REJECTED_STALE_RIG; rescan=1; writes=NONE\n";
                    clearStaleTarget("START");
                    rescan=true;
                    passed=true;
                    break;
                }
                report << "motion_start=" << (passed?"PLAYING":"FAILED") << '\n'; report.flush();
                if (ui::Enabled()) ui::State(passed?L"正在播放。点击停止可恢复现场姿态。":L"播放校验失败，禁止写入。",passed,ui::Read().loaded,passed,!passed);
            }
            if (ui::Enabled()&&GetTickCount64()>=nextProgress) {ui::Progress(MotionPosition());nextProgress=GetTickCount64()+200;}
            const auto currentStage=MotionABStage();
            if(MotionStatus()==1&&currentStage!=previousStage) {
                previousStage=currentStage;const auto text=ReportMotionABStage(currentStage,report);
                if(!text.empty()&&ui::Enabled())ui::State(text,true,true,true);
            }
        }
    } catch (const std::exception& error) { passed=false; report << "motion_worker_error=" << error.what() << '\n'; }
    DisableMotionWrites();
    if (MotionRigArmed()) { job.mode=8; passed=DispatchJob(stop,module,window,report,false)&&passed; ReportMotion(report); ReportMotionAB(report); }
    if (ikTimingReady) passed=RemoveIKTimingProbe(report)&&passed;
    ReportIKTimingProbe(report);
    passed=RemoveAnimationReturnProbe(report)&&passed;
    if (MotionRigArmed()) { job.mode=6; passed=DispatchJob(stop,module,window,report,false)&&passed; }
    if (BoneWriteArmed()) { job.mode=4; passed=DispatchJob(stop,module,window,report,false)&&passed; }
    ReportMotion(report); ReportBoneWrite(report); ReportAnimationReturnProbe(report);
    UnloadMotionClip();
    if (ui::Enabled()) {
        ui::Bound({},0);
        ui::State(passed?L"骨架已释放，等待刷新或重新绑定。":L"会话校验或恢复失败，动作写入已停止；点击刷新或绑定以重新采集。",false,false,false,!passed);
    }
    if (!passed && !rescan && ui::Enabled()) {
        report << "motion_session=FAILED_WAITING_FOR_USER_RESCAN; writes=DISABLED\n"; report.flush();
        while (WaitForSingleObject(stop,50)!=WAIT_OBJECT_0) {
            ui::Command recovery{ui::CommandKind::Refresh};
            if (!ui::Take(recovery)) continue;
            if (recovery.kind==ui::CommandKind::Refresh) {
                report << "[UI_RECOVERY] command=REFRESH generation=" << recovery.generation << '\n';
                // The failed-session recovery loop runs outside the normal command
                // path. Clear the requested actor here as well, otherwise the outer
                // scanner can immediately reapply the stale identity after Refresh.
                clearStaleTarget("RECOVERY_REFRESH");
                rescan=true; break;
            }
            if (recovery.kind==ui::CommandKind::BindActor) {
                if (!ui::ValidateSelection(recovery)) {
                    ui::State(L"角色列表已变化，请先点击刷新，再重新绑定。",false,false,false,true);
                    continue;
                }
                requestedName=recovery.text;
                report << "[UI_RECOVERY] command=BIND_ACTOR; target=USER_SELECTED\n";
                rescan=true; break;
            }
            if (recovery.kind==ui::CommandKind::ReleaseRig) {
                clearStaleTarget("RECOVERY_RELEASE");
                report << "[UI_RECOVERY] command=RELEASE_AND_REFRESH\n";
                rescan=true; break;
            }
        }
    }
    report << "motion_session=" << (staleActorRescan?"STALE_ACTOR_RELEASED_AND_RESCAN":rescan?"RESCAN_REQUESTED":passed?"STOPPED_AND_RELEASED":"FAILED") << "; visual_confirmation=USER_OBSERVATION_REQUIRED\n"; report.flush();
    return passed||rescan;
}
bool RunBoneExperiment(GenshinNativeRuntime& runtime,HANDLE stop,HMODULE module,Window window,std::ostream& report,const std::filesystem::path& directory) {
    if (!BoneWriteArmed()) { report << "write_result=NOT_ARMED; reason=target_or_rig_gate\n"; return true; }
    const auto arm=IsArmExperiment();
    report << "phase=" << (arm?"WAITING_ARM_WRITE; trigger=foreground_NUMPAD5_or_write_ready.flag":"WAITING_HEAD_WRITE; trigger=foreground_NUMPAD4_or_write_ready.flag") << '\n';
    ReportBoneWrite(report); report.flush();
    const auto marker=directory/L"write_ready.flag";
    const auto deadline=GetTickCount64()+5*60*1000;
    bool trigger=false;
    while (GetTickCount64()<deadline && WaitForSingleObject(stop,100)!=WAIT_OBJECT_0) {
        DWORD foreground=0; GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        if ((foreground==GetCurrentProcessId() && (GetAsyncKeyState(arm?VK_NUMPAD5:VK_NUMPAD4)&0x8000)) || GetFileAttributesW(marker.c_str())!=INVALID_FILE_ATTRIBUTES) { trigger=true; break; }
    }
    bool passed=true;
    if (trigger) {
        report << "character_writes=" << (arm?"LEFT_UPPERARM_LOCAL_ROTATION_ONLY; amplitude_degrees=60":"HEAD_LOCAL_ROTATION_ONLY; amplitude_degrees=4") << "; Animator_disable=NO\n";
        // 上臂实验执行两次独立的修改/恢复，中间留出正常动画对照间隔。
        if (arm) passed=InstallAnimationReturnProbe(runtime,stop,report);
        for (unsigned cycle=0;passed && cycle<(arm?2u:1u);++cycle) {
            report << "[WRITE_CYCLE] index=" << cycle << '\n';
            job.mode=1; passed=DispatchJob(stop,module,window,report,true);
            if (arm && passed) EnableAnimationReturnWrites(true);
            if (passed && WaitForSingleObject(stop,250)!=WAIT_OBJECT_0) { job.mode=2; passed=DispatchJob(stop,module,window,report,true); }
            if (arm) {
                if (passed) WaitForSingleObject(stop,1700);
                EnableAnimationReturnWrites(false);
            } else {
                for (unsigned i=0;passed && i<30 && WaitForSingleObject(stop,33)!=WAIT_OBJECT_0;++i) {
                    job.mode=3; passed=DispatchJob(stop,module,window,report,true);
                }
            }
            if (arm && cycle==0 && passed) {
                job.mode=5; passed=DispatchJob(stop,module,window,report,false);
                if (WaitForSingleObject(stop,1000)==WAIT_OBJECT_0) break;
            }
        }
        if (arm) {
            EnableAnimationReturnWrites(false);
            passed=RemoveAnimationReturnProbe(report) && passed;
            ReportAnimationReturnProbe(report);
            passed=AnimationReturnWritesConfirmed() && passed;
        }
    }
    // 取消也要向同一主线程派发恢复；恢复派发不因已经 signaled 的 stopEvent 提前退出。
    if (BoneWriteArmed()) { job.mode=4; passed=DispatchJob(stop,module,window,report,false) && passed; }
    ReportBoneWrite(report);
    report << "write_result=" << (trigger && passed?"READBACK_AND_RESTORE_CONFIRMED":trigger?"FAILED":"NOT_TRIGGERED_CANCELLED_OR_TIMEOUT")
        << "; visual_confirmation=USER_OBSERVATION_REQUIRED; callback="
        << (arm?"SCRIPT_LATEUPDATE_RETURN_TIMING_EXPERIMENT":"WINDOW_MESSAGE_NO_LATE_PHASE_CLAIM") << '\n';
    report.flush();
    return passed;
}
bool Method(GenshinNativeRuntime& runtime, const char* owner, const char* name, uint8_t parameters, NativeMethod& result) {
    NativeClass klass;
    return runtime.FindClass("UnityEngine", owner, klass) && runtime.FindMethod(klass, name, parameters, result) &&
        result.descriptor && result.entry && result.target;
}
void ReportMethodCensus(GenshinNativeRuntime& runtime, const char* namespaze, const char* name,
    std::ostream& report) {
    NativeClass klass;
    if (!runtime.FindClass(namespaze, name, klass)) return;
    std::vector<NativeMethod> methods;
    if (!runtime.EnumerateMethods(klass, methods)) {
        report << "[IK_METHOD_CENSUS] " << namespaze << '.' << name
            << " read=0 reason=METADATA_ENUMERATION_FAILED\n";
        return;
    }
    report << "[IK_METHOD_CENSUS] " << namespaze << '.' << name
        << " read=1 method_count=" << methods.size() << " invocation=NONE\n";
    constexpr size_t MaxReportedMethods = 256;
    for (size_t i = 0; i < methods.size() && i < MaxReportedMethods; ++i) {
        const auto& method = methods[i];
        report << "[IK_METHOD] " << namespaze << '.' << name << '.' << method.name
            << " metadataId=" << method.metadataId
            << " parameterCount=" << unsigned(method.parameterCount)
            << " descriptor=" << (method.descriptor ? "VERIFIED" : "UNMATERIALIZED_OR_MISMATCH")
            << " callable=" << (method.descriptor && method.entry && method.target ? "UNVERIFIED_ABI" : "NO")
            << " invoked=NO\n";
    }
    if (methods.size() > MaxReportedMethods)
        report << "[IK_METHOD_CENSUS] " << namespaze << '.' << name
            << " methods_truncated=1 reported=" << MaxReportedMethods << '\n';
}
void Address(std::ostream& report, const char* key, uintptr_t value) { report << key << "=0x" << std::hex << value << std::dec << '\n'; }
std::string Utf8(const wchar_t* text, int length) {
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    return result;
}
}
namespace live_detail {
bool ValidateReflection(uintptr_t object, uintptr_t runtimeClass, uintptr_t descriptor) {
    uintptr_t klass = 0, type = 0;
    std::array<uint8_t, 16> actual{}, expected{};
    // 反射缓存按类型内容比较：缓存可引用 registration descriptor，class 保存的是其副本。
    return object && runtimeClass && descriptor && Read(object, klass) && klass == runtimeClass &&
        Read(object + 0x10, type) && Read(type, actual) && Read(descriptor, expected) && actual == expected;
}
bool ValidateArray(uintptr_t array, uintptr_t elementClass, uint64_t& length) {
    uintptr_t klass = 0, bounds = 1, elementDescriptor = 0;
    uint32_t actualId = UINT32_MAX, expectedId = UINT32_MAX;
    // 当前 profile 的 allocator 在 +18 写长度、枚举器在 +20 写引用。仅接受 SZARRAY。
    uint8_t type = 0;
    if (!array || !elementClass || !Read(array, klass) || !Read(klass + 0x68 + 0xA, type) || type != 0x1D ||
        !Read(array + 0x10, bounds) || bounds || !Read(array + 0x18, length) || length > 1000000) return false;
    return Read(klass + 0x68, elementDescriptor) && Read(elementDescriptor, actualId) &&
        Read(elementClass + 0x68, expectedId) && actualId == expectedId;
}
}
bool ProbeLiveUnity(GenshinNativeRuntime& runtime, HANDLE stop, std::ostream& report, const std::filesystem::path& directory) {
    job = Job{};
    if (!runtime.ResolveCallContext(stop, job.context, report)) return false;
    NativeClass reflection;
    if (!Read(job.context.reflectionClassSlot, job.reflectionClass) || !runtime.DescribeClass(job.reflectionClass, reflection) ||
        reflection.namespaze != "System" || reflection.name != "RuntimeType") {
        report << "blocker=RuntimeType class identity\n"; return false;
    }
    Address(report, "RuntimeType_class", job.reflectionClass);
    report << "RuntimeType_metadataId=" << reflection.metadataId << '\n';
    const std::array names = {"Transform", "SkinnedMeshRenderer", "Animator"};
    for (size_t i = 0; i < names.size(); ++i) {
        NativeClass klass;
        if (!runtime.FindClass("UnityEngine", names[i], klass)) return false;
        job.items[i].klass = klass.address; job.items[i].metadataId = klass.metadataId;
        job.items[i].descriptor = klass.address + 0x68;
        uint32_t id = 0; uint8_t kind = 0;
        if (!Read(klass.address + 0x68, id) || id != klass.metadataId || !Read(klass.address + 0x72, kind) || kind != 0x12) {
            report << "blocker=class by-value descriptor identity\n"; return false;
        }
    }
    job.transformClass = job.items[0].klass;
    NativeClass go;
    if (!runtime.FindClass("UnityEngine", "GameObject", go)) return false;
    job.gameObjectClass = go.address;
    NativeClass componentClass, objectClass;
    if (runtime.FindClass("UnityEngine", "Component", componentClass)) job.componentClass = componentClass.address;
    if (runtime.FindClass("UnityEngine", "Object", objectClass)) job.objectClass = objectClass.address;
    NativeMethod find, child, transform, gameObject, name;
    if (!Method(runtime, "Object", "FindObjectsOfType", 1, find) || !Method(runtime, "Transform", "get_childCount", 0, child) ||
        !Method(runtime, "Component", "get_transform", 0, transform) || !Method(runtime, "Component", "get_gameObject", 0, gameObject) ||
        !Method(runtime, "Object", "get_name", 0, name)) {
        report << "blocker=typed method descriptor\n"; return false;
    }
    // 验证实际 callee 数据流，不依据公共头文件臆测隐藏参数与 managed array 布局。
    if (!Pattern(find.target, 32, "48 83 EC 28 48 8B D1 41 B8 01 00 00 00 48 8D 4C 24 30 E8 ?? ?? ?? ?? 48 8B 00") ||
        !Pattern(child.target, 96, "8B 80 90 00 00 00") ||
        !Pattern(transform.target, 80, "48 8B 50 40") || !Pattern(gameObject.target, 112, "48 8B 78 40") ||
        !Pattern(name.target, 224, "E9 ?? ?? ?? ??")) {
        report << "blocker=typed callee dataflow\n"; return false;
    }
    if (Method(runtime, "Transform", "get_parent", 0, job.parent) && job.componentClass &&
        Method(runtime, "GameObject", "GetComponentsInternalType", 6, job.components) &&
        Pattern(job.parent.target, 48, "48 8B 90 A0 00 00 00") &&
        Pattern(job.components.target, 224, "41 0F B6 E9 45 0F B6 F0") &&
        Pattern(job.components.target, 224, "40 38 B4 24 80 00 00 00 0F 95 44 24 3A") &&
        Pattern(job.components.target, 224, "48 8B 00 48 8B 7C 24 78")) {
        report << "[GROUND_IK_HIERARCHY_METHODS] parent=VERIFIED get_components=VERIFIED includeInactive=SUPPORTED scope=SELECTED_ANIMATOR_ANCESTORS\n";
        Address(report, "get_components_entry", job.components.entry);
        Address(report, "get_components_target", job.components.target);
    } else {
        job.parent = {}; job.components = {};
        report << "[GROUND_IK_HIERARCHY_METHODS] parent=UNAVAILABLE get_components=UNAVAILABLE\n";
    }
    const auto enumeration = Relative(find.target + 18, 1, 5);
    if (!Pattern(enumeration, 0x440, "49 8B 17 4A 89 54 39 20")) {
        report << "blocker=array element writer not recovered\n"; return false;
    }
    // 由枚举器的数组分配 call 找到 allocator，再验证长度写入及字符串构造字段。
    std::vector<uint8_t> enumerationBytes(0x440);
    SIZE_T copied = 0;
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(enumeration), enumerationBytes.data(), enumerationBytes.size(), &copied);
    const auto arrayCalls = native_detail::FindPattern(enumerationBytes, "48 8B CB 48 8B D6 E8 ?? ?? ?? ?? 45 85 F6");
    if (copied != enumerationBytes.size() || arrayCalls.size() != 1) return false;
    const auto arrayWrapper = Relative(enumeration + arrayCalls[0] + 6, 1, 5);
    const auto arrayAllocator = Relative(arrayWrapper + 32, 1, 5);
    if (!Pattern(arrayWrapper, 37, "48 89 C1 48 89 F2 48 83 C4 20 5E E9 ?? ?? ?? ??") ||
        !Pattern(arrayAllocator, 0x180, "48 89 58 18")) { report << "blocker=array length writer\n"; return false; }
    // Object.get_name 尾调用的 string constructor 自身负责 UTF-8 到 managed UTF-16 转换。
    std::vector<uint8_t> nameBytes(224);
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(name.target), nameBytes.data(), nameBytes.size(), &copied);
    const auto tail = native_detail::FindPattern(nameBytes, "5B E9 ?? ?? ?? ??");
    if (tail.size() != 1) return false;
    const auto stringConstructor = Relative(name.target + tail[0] + 1, 1, 5);
    if (!Pattern(stringConstructor, 0xC0, "89 78 10 01 FF 4C 63 C7") ||
        !Pattern(stringConstructor, 0xC0, "48 8D 4B 14")) { report << "blocker=managed string writer\n"; return false; }
    Address(report, "array_allocator", arrayAllocator); Address(report, "string_constructor", stringConstructor);
    job.find = find.entry; job.findMethod = find.descriptor; job.child = child.entry; job.childMethod = child.descriptor;
    job.transform = transform.entry; job.transformMethod = transform.descriptor;
    job.gameObject = gameObject.entry; job.gameObjectMethod = gameObject.descriptor; job.name = name.entry; job.nameMethod = name.descriptor;
    report << "[GROUND_IK_METHODS] candidate_component_scan=READONLY method_calls=NONE\n";
    report << "[CLASS_BASE_CHAIN] validated=0 reason=IL2CPP_parent_layout_not_established; hierarchy_component_scan=DEFERRED\n";
    for (auto& candidate : job.groundCandidates) {
        NativeClass klass;
        candidate.resolved = runtime.FindClass(candidate.namespaze, candidate.name, klass);
        if (candidate.resolved) {
            candidate.klass = klass.address;
            candidate.descriptor = klass.address + 0x68;
        }
        report << "[GROUND_IK_CANDIDATE] " << candidate.namespaze << "." << candidate.name
            << " resolved=" << candidate.resolved << " metadataId=" << (candidate.resolved ? klass.metadataId : 0)
            << " hierarchy_valid=" << candidate.hierarchyValid << " derives_Component=" << candidate.derivesComponent
            << " derives_Object=" << candidate.derivesObject << " base_chain=" << candidate.baseChain << '\n';
        if (candidate.resolved)
            ReportMethodCensus(runtime, candidate.namespaze, candidate.name, report);
    }
    for (const auto& type : {std::pair{"MoleMole", "Miscs"},
             std::pair{"UnityEngine", "Physics"}, std::pair{"UnityEngine", "PhysicsScene"},
             std::pair{"UnityEngine", "Animator"}})
        ReportMethodCensus(runtime, type.first, type.second, report);
    {
        NativeClass miscs;
        job.groundHeightReady = runtime.FindClass("MoleMole", "Miscs", miscs) &&
            runtime.FindMethod(miscs, "CalcCurrentGroundHeight", 2, job.groundHeight) &&
            job.groundHeight.metadataId == 702885 && job.groundHeight.parameterCount == 2 &&
            job.groundHeight.entry && job.groundHeight.descriptor;
        report << "[GROUND_HEIGHT_METHOD] name=CalcCurrentGroundHeight metadataId="
            << job.groundHeight.metadataId << " parameters=" << unsigned(job.groundHeight.parameterCount)
            << " status=" << (job.groundHeightReady ? "READONLY_ABI_GATED" : "REJECTED")
            << " signature=float(float worldX,float worldZ) failure_sentinel=-100000000\n";
    }
    if (!Method(runtime, "Animator", "get_isHuman", 0, job.human) ||
        !Method(runtime, "SkinnedMeshRenderer", "get_rootBone", 0, job.rootBone) ||
        !Method(runtime, "SkinnedMeshRenderer", "get_bones", 0, job.bones) ||
        !Method(runtime, "SkinnedMeshRenderer", "get_sharedMesh", 0, job.mesh)) {
        report << "blocker=scene getter descriptors\n"; return false;
    }
    if (!Pattern(job.bones.target, 100, "48 8D B8 30 04 00 00") ||
        !Pattern(job.bones.target, 100, "48 63 57 10") ||
        !Pattern(job.rootBone.target, 60, "48 8B C8 E8 ?? ?? ?? ?? 48 8B D0 48 8D 4C 24 30 E8 ?? ?? ?? ?? 48 8B 00") ||
        !Pattern(job.mesh.target, 60, "48 8B C8 E8 ?? ?? ?? ?? 48 8B D0 48 8D 4C 24 30 E8 ?? ?? ?? ?? 48 8B 00") ||
        !Pattern(job.human.target, 48, "48 8B C8 48 83 C4 20 5B E9 ?? ?? ?? ??")) {
        report << "blocker=scene getter callee dataflow\n"; return false;
    }
    // FBIK 先只验证无副作用的标量 getter；位置/旋转结构体 ABI 留到实机证据确认后再调用。
    job.fbikMethodsReady =
        Method(runtime, "Animator", "GetFBIKEffectorCount", 0, job.fbikEffectorCount) &&
        Method(runtime, "Animator", "GetFBIKChainCount", 0, job.fbikChainCount) &&
        Method(runtime, "Animator", "CheckIfInIKPass", 0, job.fbikCheckPass);
    job.fbikChainElementMethodReady = Method(runtime, "Animator", "GetFBIKChainElementCount", 1, job.fbikChainElementCount);
    // Layer IK state is a scalar read-only API. Resolve it separately from
    // FBIK so an unavailable optional method never blocks the existing probe.
    job.animatorLayerMethodsReady =
        Method(runtime, "Animator", "get_layerCount", 0, job.animatorLayerCount) &&
        Method(runtime, "Animator", "GetLayerEnableOnAnimatorIK", 1, job.animatorLayerEnable);
    // The Internal wrapper exposes the same Vector3 ABI without the public
    // managed guard. Prefer it for read-only evidence, with the public and
    // NoThreadCheck variants as compatibility fallbacks.
    job.animatorIkPositionMethodReady =
        Method(runtime, "Animator", "GetIKPositionInternal", 1, job.animatorIkGetPosition) ||
        Method(runtime, "Animator", "GetIKPositionNoThreadCheck", 1, job.animatorIkGetPosition) ||
        Method(runtime, "Animator", "GetIKPosition", 1, job.animatorIkGetPosition);
    if (job.animatorIkPositionMethodReady)
        job.animatorIkPositionAbiReady = ValidateAnimatorIKPositionWrapper(job.animatorIkGetPosition, report);
    // Keep all public/internal/no-thread-check variants visible in one
    // read-only census. This is needed because the generated Vector3 wrapper
    // can use a different ABI from its Internal and NoThreadCheck callees.
    ReportAnimatorIKVariants(runtime, report);
    report << "[FBIK_METHODS] available=" << job.fbikMethodsReady
        << " chain_element_count=" << job.fbikChainElementMethodReady
        << " position_rotation_calls=DEFERRED_ABI_VALIDATION"
        << " animator_ik_layers=" << job.animatorLayerMethodsReady
        << " animator_ik_position=" << job.animatorIkPositionAbiReady << '\n';
    if (job.fbikMethodsReady) {
        for (const auto& method : {job.fbikEffectorCount, job.fbikChainCount, job.fbikCheckPass}) {
            report << "[FBIK_METHOD] " << method.name << " metadataId=" << method.metadataId
                << " parameterCount=" << unsigned(method.parameterCount) << '\n';
            Address(report, "entry", method.entry); Address(report, "target", method.target);
            Address(report, "descriptor", method.descriptor);
        }
    }
    if (job.fbikChainElementMethodReady) {
        const auto& method = job.fbikChainElementCount;
        report << "[FBIK_METHOD] " << method.name << " metadataId=" << method.metadataId
            << " parameterCount=" << unsigned(method.parameterCount) << '\n';
        Address(report, "entry", method.entry); Address(report, "target", method.target); Address(report, "descriptor", method.descriptor);
    }
    if (job.animatorLayerMethodsReady) {
        for (const auto& method : {job.animatorLayerCount, job.animatorLayerEnable}) {
            report << "[ANIMATOR_IK_METHOD] " << method.name << " metadataId=" << method.metadataId
                << " parameterCount=" << unsigned(method.parameterCount) << '\n';
            Address(report, "entry", method.entry); Address(report, "target", method.target); Address(report, "descriptor", method.descriptor);
        }
    }
    if (job.animatorIkPositionMethodReady) {
        const auto& method = job.animatorIkGetPosition;
        report << "[ANIMATOR_IK_METHOD] " << method.name << " metadataId=" << method.metadataId
            << " parameterCount=" << unsigned(method.parameterCount)
            << " abi_ready=" << job.animatorIkPositionAbiReady << '\n';
        Address(report, "entry", method.entry); Address(report, "target", method.target);
        Address(report, "descriptor", method.descriptor);
    }
    for (const auto& method : {job.human, job.rootBone, job.bones, job.mesh}) {
        report << "[SCENE_GETTER] " << method.name << " metadataId=" << method.metadataId << '\n';
        Address(report, "entry", method.entry); Address(report, "target", method.target); Address(report, "descriptor", method.descriptor);
    }
    RigCalls rigCalls{job.context, job.transformClass, job.gameObjectClass, job.name, job.nameMethod,
        job.transform, job.transformMethod, job.gameObject, job.gameObjectMethod, job.child, job.childMethod, job.human, job.bones,
        job.items[1].klass,job.items[2].klass};
    if (!InitializeRigProbe(runtime, rigCalls, report)) return false;
    dispatchMessage = RegisterWindowMessageW(L"SSMT.Poser.Readonly.NativeProbe.V1");
    HMODULE module = nullptr;
    if (!dispatchMessage || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Dispatch), &module)) return false;
    const auto marker = directory / L"scene_ready.flag";
    const bool autoScene=ui::Enabled() ||
        GetFileAttributesW((directory/L"auto_scene.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
    report << (autoScene?"phase=AUTO_SCENE_DISCOVERY; gate=unique_active_configured_Actor_rig\n":"phase=WAITING_PLAYABLE_SCENE; trigger=foreground_NUMPAD0_or_scene_ready.flag; NumLock=ON\n");
    report.flush();
    const auto sceneDeadline = GetTickCount64() + 15 * 60 * 1000;
    bool confirmed = autoScene;
    while (!confirmed && GetTickCount64() < sceneDeadline && WaitForSingleObject(stop, 25) != WAIT_OBJECT_0) {
        DWORD foreground = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
        if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES ||
            (foreground == GetCurrentProcessId() && (GetAsyncKeyState(VK_NUMPAD0) & 0x8000))) { confirmed = true; break; }
    }
    if (!confirmed) { report << "blocker=scene entry not confirmed or cancelled\n"; return false; }
    if (ui::Enabled()||GetFileAttributesW((directory/L"motion_mode.flag").c_str())!=INVALID_FILE_ATTRIBUTES) {
        ConfigureMotionMode(); report << "write_experiment=DYNAMIC_VMD_FK_USER_REQUESTED\n";
        if (!InitializeBoneGeometry(runtime,report)) {report << "blocker=reference pose requires verified world rotation getters\n";return false;}
    }
    const auto ikScalePath=directory/L"foot_ik_scale.txt";
    float requestedIkUnits=0.0f;
    if (GetFileAttributesW(ikScalePath.c_str())!=INVALID_FILE_ATTRIBUTES) {
        std::ifstream scaleFile(ikScalePath);scaleFile>>requestedIkUnits;
    }
    const bool requestFootIKWrites=GetFileAttributesW((directory/L"foot_ik_write.flag").c_str())!=INVALID_FILE_ATTRIBUTES;
    ConfigureFootIKDiagnostics(requestedIkUnits,requestFootIKWrites,job.groundHeight.metadataId,
        job.groundHeightReady?job.groundHeight.entry:0,job.groundHeightReady?job.groundHeight.descriptor:0);
    report << "foot_IK=ANALYTIC_TWO_BONE; requested_units=" << requestedIkUnits
        << " write_requested=" << requestFootIKWrites
        << " valid_scale=" << (std::isfinite(requestedIkUnits)&&requestedIkUnits>0.0f&&requestedIkUnits<=100.0f)
        << " ground_method=" << (job.groundHeightReady?"702885_VERIFIED":"UNAVAILABLE")
        << "; writes=" << (requestFootIKWrites&&job.groundHeightReady&&std::isfinite(requestedIkUnits)&&requestedIkUnits>0.0f&&requestedIkUnits<=100.0f?"OPT_IN_AFTER_RUNTIME_GATES":"BLOCKED_BY_DEFAULT") << '\n';
    job.fbikProbeEnabled = GetFileAttributesW((directory/L"fbik_probe.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
    report << "fbik_probe=" << (job.fbikProbeEnabled ? "ARMED_READONLY_COUNTS" : "DISABLED_METADATA_ONLY") << '\n';
    if (GetFileAttributesW((directory/L"arm_test.flag").c_str())!=INVALID_FILE_ATTRIBUTES) {
        ConfigureArmExperiment(); report << "write_experiment=LEFT_UPPERARM_60_DEGREES_TWO_CYCLES_USER_REQUESTED\n";
    }
    std::ifstream targetFile(directory/L"target_actor_name.txt",std::ios::binary);
    std::string targetName;
    std::wstring requestedName;
    bool targetFromFile=false;
    unsigned targetMisses=0;
    const auto clearRequestedTarget=[&](const char* stage) {
        const auto previous=requestedName.empty()?std::string("<auto-player-branch>"):Utf8(requestedName.c_str(),int(requestedName.size()));
        requestedName.clear();
        SetRigWriteTarget({});
        targetFromFile=false;
        targetMisses=0;
        report << "[RIG_TARGET] action=CLEAR stage=" << stage << " target_before=" << previous
            << " target_after=<auto-player-branch> requested_actor_cleared=1 scan_follows=1\n";
    };
    if (targetFile) std::getline(targetFile,targetName);
    if (!targetName.empty() && targetName.size()<128) {
        const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,targetName.data(),int(targetName.size()),nullptr,0);
        std::wstring actorName(size,L'\0');
        if (size && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,targetName.data(),int(targetName.size()),actorName.data(),size)) {requestedName=actorName;SetRigWriteTarget(actorName);targetFromFile=true;}
        report << "requested_actor_name=" << targetName << '\n';
    }
    report << (autoScene?"scene_entry=AUTOMATIC_READONLY_DISCOVERY; character_writes=NONE\n":"scene_entry=USER_CONFIRMED; character_writes=NONE\n");
    if (ui::Enabled()) ui::State(L"自动等待角色场景。可从列表选择当前激活的骨架。",false,false,false);
    for (unsigned attempt = 0; attempt < 120||ui::Enabled(); ++attempt) {
        // 每轮 discovery 有界。UI 超时后等待手动刷新，不持续扫描；新选择仍由下一快照重新核验。
        const bool fbikProbeRequested = GetFileAttributesW((directory/L"fbik_probe.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
        if (fbikProbeRequested != job.fbikProbeEnabled) {
            job.fbikProbeEnabled = fbikProbeRequested;
            report << "[FBIK_PROBE_FLAG] enabled=" << job.fbikProbeEnabled
                << " refreshed_before_main_thread_dispatch=1\n";
            report.flush();
        }
        if (ui::Enabled()) {
            ui::Command command{ui::CommandKind::Refresh};
            bool received=false;
            if (attempt>=120) {
                ui::State(L"场景等待超时。进入角色场景后点击刷新。",false,false,false);
                while (!(received=ui::Take(command))) if (WaitForSingleObject(stop,100)==WAIT_OBJECT_0) return false;
                attempt=0;
            } else received=ui::Take(command);
            if (received) {
                if (command.kind==ui::CommandKind::BindActor) {
                    if (!ui::ValidateSelection(command)) {ui::State(L"列表已更新或骨架不可绑定，请重新选择。",false,false,false);continue;}
                    requestedName=command.text;SetRigWriteTarget(requestedName);targetFromFile=false;targetMisses=0;attempt=0;
                    report << "[RIG_TARGET] action=SET stage=SCENE_DISCOVERY target_after="
                        << Utf8(requestedName.c_str(),int(requestedName.size())) << " source=USER_BIND\n";
                    ui::State(L"正在重新采集并校验所选骨架。",false,false,false);
                } else if (command.kind==ui::CommandKind::Refresh) {
                    clearRequestedTarget("MANUAL_REFRESH");
                    report << "[UI_RIG_REFRESH] accepted=1 stage=SCENE_DISCOVERY target_cleared=1 scan_follows=1\n";
                } else if (command.kind==ui::CommandKind::ReleaseRig) {clearRequestedTarget("RELEASE");}
                else if (command.kind!=ui::CommandKind::Refresh) {ui::State(L"请先绑定可控角色骨架，再载入动作。",false,false,false);}
            }
        }
        report << "\n[scene snapshot " << attempt + 1 << "] tick=" << GetTickCount64() << '\n';
        // 清空上次历史观测，只保留已经核验的 native class/descriptor 身份。
        for (auto& item : job.items) {
            const auto klass = item.klass, descriptor = item.descriptor;
            const auto id = item.metadataId;
            item = Item{}; item.klass = klass; item.descriptor = descriptor; item.metadataId = id;
        }
        job.complete = false; job.fault = 0; job.step = 0; job.mode=0; job.runtimeThread = 0;
        job.fbikRead = false; job.fbikPass = false; job.fbikEffectors = -1; job.fbikChains = -1;
        job.fbikReadFailure = 0; job.fbikReadFault = 0; job.fbikElementRead = 0;
        job.animatorLayerRead = false; job.animatorLayerCountValue = -1;
        job.animatorLayerEnabled.fill(-1); job.animatorLayerReadCount = 0; job.animatorLayerReadFault = 0;
        job.animatorIkPositionRead = false; job.animatorIkPositionSampleCount = 0;
        job.animatorIkPositionReadFault = 0; job.animatorIkPositions.fill({});
        job.groundHeightRead = false; job.groundHeights.fill(0.0f);
        job.footGroundDeltas.fill(0.0f); job.groundHeightValid.fill(0); job.groundHeightFault = 0;
        if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return false;
        if (Read(job.context.mainThreadSlot, job.mainThread) && job.mainThread) {
            Window window{job.mainThread, nullptr};
            EnumWindows(SelectWindow, reinterpret_cast<LPARAM>(&window));
            if (window.hwnd) {
                auto hook = SetWindowsHookExW(WH_GETMESSAGE, Dispatch, module, job.mainThread);
                if (!hook) { report << "dispatch_hook_error=" << GetLastError() << '\n'; return false; }
                state.store(1, std::memory_order_release);
                if (!PostMessageW(window.hwnd, dispatchMessage, reinterpret_cast<WPARAM>(&job), 0)) state.store(0, std::memory_order_release);
                const auto deadline = GetTickCount64() + 30000;
                while (state.load(std::memory_order_acquire) != 3 && GetTickCount64() < deadline && WaitForSingleObject(stop, 10) != WAIT_OBJECT_0) {}
                unsigned pending = 1;
                state.compare_exchange_strong(pending, 0, std::memory_order_acq_rel);
                // 执行中的任务必须退出才能卸钩/返回，避免 shutdown 释放在用 DLL。
                while (state.load(std::memory_order_acquire) == 2) Sleep(1);
                const auto removed = UnhookWindowsHookEx(hook);
                const auto removeError = removed ? 0 : GetLastError();
                while (active.load(std::memory_order_acquire)) Sleep(1);
                if (!removed) {
                    // 无法确认卸钩时保留代码映射，避免宿主卸载后 Windows 调用悬空 callback。
                    HMODULE pinned = nullptr;
                    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                        reinterpret_cast<LPCWSTR>(&Dispatch), &pinned);
                    state.store(0, std::memory_order_release);
                    report << "dispatch_unhook=FAILED error=" << removeError << " module_pinned=" << bool(pinned) << '\n';
                    return false;
                }
                report << "dispatch_unhook=OK; callbacks_in_flight=0\n";
                report << "dispatch_state=" << state.load() << " actual_tid=" << job.actualThread << " Unity_main_tid=" << job.mainThread
                    << " step=" << job.step << " SEH=0x" << std::hex << job.fault << std::dec << '\n';
                Address(report, "borrowed_IL2CPP_thread", job.runtimeThread);
                report << "thread_attach=NOT_NEEDED_ALREADY_ATTACHED; thread_detach=NOT_OWNED_NOT_CALLED\n";
                bool success = false;
                for (size_t i = 1; i < job.items.size(); ++i) {
                    const auto& item = job.items[i];
                    report << "[LIVE] UnityEngine." << names[i] << " typeId=" << item.metadataId << '\n';
                    Address(report, "class", item.klass); Address(report, "type_descriptor", item.descriptor);
                    Address(report, "System.Type", item.reflection); Address(report, "System.Type_repeat", item.repeat);
                    Address(report, "reflection_type_descriptor", item.reflectionDescriptor);
                    Address(report, "array", item.array); Address(report, "array_class", item.arrayClass);
                    report << "array_length=" << item.length << " array_valid=" << item.arrayValid
                        << " skipped_nonexact_transform=" << item.skippedTransforms << '\n';
                    Address(report, "object", item.object); Address(report, "object_class", item.objectClass); Address(report, "native_object", item.nativeObject);
                    Address(report, "transform", item.transform); Address(report, "gameObject", item.gameObject); Address(report, "name_object", item.nameObject);
                    report << "childCount=" << item.childCount << " native_childCount=" << item.nativeChildCount
                        << " name=" << Utf8(item.name, std::clamp(item.nameLength, 0, 127))
                        << " GC_handles_verified=" << item.rooted << " GC_handles_freed=" << item.released
                        << " live_valid=" << item.liveValid << " GC_handles_released=" <<
                        std::all_of(std::begin(item.handles), std::end(item.handles), [](auto h) { return h == 0; }) << '\n';
                    success |= item.liveValid;
                    if (i == 2) report << "isHuman=" << item.isHuman << " isHuman_read=" << item.humanRead << '\n';
                    else {
                        Address(report, "rootBone", item.rootBone); Address(report, "bones_array", item.bonesArray);
                        Address(report, "sharedMesh", item.sharedMesh);
                        report << "bones_length=" << item.bonesLength << " native_bones_length=" << item.nativeBonesLength
                            << " bones_valid=" << item.bonesValid << " sharedMesh.name=" << Utf8(item.meshName, std::clamp(item.meshNameLength, 0, 127)) << '\n';
                    }
                    if (i == 2 && job.fbikMethodsReady) {
                        report << "[FBIK_READ] read=" << job.fbikRead
                            << " effector_count=" << job.fbikEffectors
                            << " chain_count=" << job.fbikChains
                            << " in_ik_pass=" << job.fbikPass
                            << " chain_element_read=" << job.fbikElementRead
                            << " failure=" << (job.fbikReadFailure==1?"NO_SELECTED_ANIMATOR":job.fbikReadFailure==2?"INVALID_OR_UNAVAILABLE_VALUE":job.fbikReadFailure==3?"OPTIONAL_READ_SEH":"NONE")
                            << " seh=0x" << std::hex << job.fbikReadFault << std::dec;
                        if (job.fbikElementRead) {
                            report << " chain_element_counts=";
                            for (unsigned chain = 0; chain < job.fbikElementRead; ++chain) {
                                if (chain) report << ',';
                                report << job.fbikElementCounts[chain];
                            }
                        }
                        report << '\n';
                    }
                    if (i == 2 && job.animatorLayerMethodsReady) {
                        report << "[ANIMATOR_IK_LAYERS] read=" << job.animatorLayerRead
                            << " layer_count=" << job.animatorLayerCountValue
                            << " sampled=" << job.animatorLayerReadCount
                            << " fault=0x" << std::hex << job.animatorLayerReadFault << std::dec
                            << " enabled=";
                        for (unsigned layer = 0; layer < job.animatorLayerReadCount; ++layer) {
                            if (layer) report << ',';
                            report << int(job.animatorLayerEnabled[layer]);
                        }
                        report << " max_sampled=8 call_mode=READONLY_MAIN_THREAD\n";
                    }
                    if (i == 2 && job.animatorIkPositionMethodReady) {
                        report << "[ANIMATOR_IK_POSITION_READ] read=" << job.animatorIkPositionRead
                            << " sampled=" << job.animatorIkPositionSampleCount
                            << " fault=0x" << std::hex << job.animatorIkPositionReadFault << std::dec
                            << " goals=";
                        for (unsigned goal = 0; goal < job.animatorIkPositionSampleCount; ++goal) {
                            if (goal) report << ';';
                            const auto& value = job.animatorIkPositions[goal];
                            report << goal << "=(" << value.x << ',' << value.y << ',' << value.z << ')';
                        }
                        report << " call_mode=READONLY_MAIN_THREAD\n";
                    }
                    if (i == 2 && job.groundHeightReady) {
                        report << "[GROUND_HEIGHT_READ] read=" << job.groundHeightRead
                            << " fault=0x" << std::hex << job.groundHeightFault << std::dec
                            << " source=AnimatorIKPosition goals=LeftFoot(2),RightFoot(3)"
                            << " input_space=WORLD_XZ sentinel=-100000000 values=";
                        for (unsigned side = 0; side < 2; ++side) {
                            if (side) report << ';';
                            const auto& foot = job.animatorIkPositions[side + 2];
                            report << side << "=(x=" << foot.x << ",z=" << foot.z
                                << ",ground_y=" << job.groundHeights[side]
                                << ",foot_delta_y=" << job.footGroundDeltas[side]
                                << ",valid=" << unsigned(job.groundHeightValid[side]) << ')';
                        }
                        report << " invocation=READONLY_MAIN_THREAD_CANARY\n";
                    }
                }
                report << "[GROUND_IK_HIERARCHY_SCAN] read=" << job.componentScanRead
                    << " animator=0x" << std::hex << job.componentScanAnimator << std::dec
                    << " scope=SELECTED_ANIMATOR_AND_ANCESTOR_GAMEOBJECTS"
                    << " match_mode=EXACT_RUNTIME_CLASS parent_layout=UNUSED\n";
                report << "[GROUND_IK_SUBTREE_SCAN] read=" << job.componentSubtreeRead
                    << " fresh=" << job.componentSubtreeFresh
                    << " animator=0x" << std::hex << job.componentSubtreeAnimator
                    << " root=0x" << job.componentSubtreeRoot << std::dec
                    << " root_name=" << job.componentSubtreeRootName
                    << " component_count=" << job.componentSubtreeComponentCount
                    << " unique_runtime_types=" << job.componentSubtreeTypeCount
                    << " truncated=" << job.componentSubtreeTruncated
                    << " scope=ANIMATOR_GAMEOBJECT_AND_DESCENDANTS include_inactive=1\n";
                std::array<uint64_t, SecondaryKindCount> secondaryTypeCounts{};
                std::array<uint64_t, SecondaryKindCount> secondaryComponentCounts{};
                if (job.componentSubtreeFresh) {
                    for (unsigned i = 0; i < job.componentSubtreeTypeCount; ++i) {
                        const auto& type = job.componentSubtreeTypes[i];
                        NativeClass described;
                        SecondaryKind secondaryKind = SecondaryKind::None;
                        report << "[GROUND_IK_COMPONENT_TYPE] count=" << type.count << " class=0x"
                            << std::hex << type.klass << std::dec;
                        if (runtime.DescribeClass(type.klass, described)) {
                            const auto fullName = described.namespaze.empty()
                                ? described.name : described.namespaze + "." + described.name;
                            secondaryKind = ClassifySecondaryType(fullName);
                            if (secondaryKind != SecondaryKind::None) {
                                const auto index = static_cast<size_t>(secondaryKind);
                                ++secondaryTypeCounts[index];
                                secondaryComponentCounts[index] += type.count;
                            }
                            report << " metadata_id=" << described.metadataId << " name=" << fullName
                                << " secondary_kind=" << SecondaryKindName(secondaryKind);
                            const bool relevant = fullName.find("IK") != std::string::npos ||
                                fullName.find("Foot") != std::string::npos ||
                                fullName.find("Ground") != std::string::npos ||
                                fullName.find("Heightfield") != std::string::npos ||
                                fullName.find("AnimationIK") != std::string::npos ||
                                fullName.find("Collider") != std::string::npos ||
                                fullName.find("Rigidbody") != std::string::npos;
                            if (relevant)
                                ReportMethodCensus(runtime, described.namespaze.c_str(), described.name.c_str(), report);
                        } else {
                            report << " metadata_id=UNRESOLVED name=UNRESOLVED secondary_kind=NONE";
                        }
                        report << '\n';
                    }
                }
                report << "[SECONDARY_MOTION_SUMMARY] read=" << job.componentSubtreeFresh
                    << " scope=ANIMATOR_GAMEOBJECT_AND_DESCENDANTS"
                    << " classification=RUNTIME_TYPE_NAME_AND_BONE_NAME"
                    << " simulation=GAME_OWNED_READ_ONLY"
                    << " passthrough=UNTOUCHED_NATIVE"
                    << " write_calls=NONE";
                for (size_t i = 1; i < SecondaryKindCount; ++i) {
                    const auto kind = static_cast<SecondaryKind>(i);
                    report << ' ' << SecondaryKindName(kind) << "_types=" << secondaryTypeCounts[i]
                        << ' ' << SecondaryKindName(kind) << "_components=" << secondaryComponentCounts[i];
                }
                report << '\n';
                std::array<unsigned, SecondaryKindCount> secondaryBoneCounts{};
                std::array<std::string, SecondaryKindCount> secondaryBoneSamples{};
                const auto& renderer = job.items[1];
                if (renderer.bonesValid) {
                    for (unsigned i = 0; i < renderer.bonesLength && i < renderer.bones.size(); ++i) {
                        const auto& bone = renderer.bones[i];
                        const auto boneName = Utf8(bone.name, std::clamp(bone.nameLength, 0, 127));
                        const auto kind = ClassifySecondaryBone(boneName);
                        if (kind == SecondaryKind::None) continue;
                        const auto index = static_cast<size_t>(kind);
                        ++secondaryBoneCounts[index];
                        if (secondaryBoneSamples[index].empty()) secondaryBoneSamples[index] = boneName;
                    }
                }
                report << "[SECONDARY_BONE_CHAINS] read=" << renderer.bonesValid
                    << " source=SKINNED_MESH_BONES classification=NAME_ONLY passthrough=UNTOUCHED_NATIVE";
                for (size_t i = 1; i < SecondaryKindCount; ++i) {
                    const auto kind = static_cast<SecondaryKind>(i);
                    if (!secondaryBoneCounts[i]) continue;
                    report << ' ' << SecondaryKindName(kind) << "_bones=" << secondaryBoneCounts[i]
                        << "_sample=" << secondaryBoneSamples[i];
                }
                report << '\n';
                for (const auto& candidate : job.groundCandidates) {
                    const bool runtimeComponent = std::string_view(candidate.name) == "MonoAnimationIKEventHelper" ||
                        std::string_view(candidate.name) == "DynamicBoneArray";
                    report << (runtimeComponent ? "[RUNTIME_COMPONENT_SCAN] " : "[GROUND_IK_SCAN] ")
                        << candidate.namespaze << "." << candidate.name
                        << " resolved=" << candidate.resolved << " read=" << candidate.read
                        << " stage=" << GroundStageName(candidate.stage)
                        << " instance_count=" << candidate.count << " sample_count=" << candidate.sampleCount
                        << " reflection=0x" << std::hex << candidate.reflection << " array=0x" << candidate.array
                        << " array_class=0x" << candidate.arrayClass << " element_descriptor=0x" << candidate.elementDescriptor
                        << std::dec << " array_type=0x" << std::hex << unsigned(candidate.arrayType) << std::dec
                        << " bounds=" << candidate.bounds << " length=" << candidate.length
                        << " element_metadata_id=" << candidate.elementMetadataId
                        << " expected_metadata_id=" << candidate.expectedMetadataId
                        << " hierarchy_valid=" << candidate.hierarchyValid
                        << " derives_Component=" << candidate.derivesComponent
                        << " derives_Object=" << candidate.derivesObject
                        << " actor_hierarchy_matches=" << candidate.hierarchyMatches
                        << " actor_subtree_fresh=" << job.componentSubtreeFresh
                        << " actor_subtree_matches=" << candidate.subtreeMatches << '\n';
                    for (unsigned i = 0; i < candidate.sampleCount; ++i) {
                        report << "[GROUND_IK_INSTANCE] " << candidate.namespaze << "." << candidate.name
                            << " index=" << i << " object=0x" << std::hex << candidate.sampleObjects[i] << std::dec
                            << " gameObject=" << candidate.sampleNames[i] << '\n';
                    }
                    for (unsigned i = 0; i < candidate.hierarchySampleCount; ++i) {
                        report << "[GROUND_IK_HIERARCHY_INSTANCE] " << candidate.namespaze << "." << candidate.name
                            << " depth=" << candidate.hierarchyDepth[i] << " object=0x" << std::hex
                            << candidate.hierarchyObjects[i] << std::dec << " gameObject=" << candidate.hierarchyNames[i] << '\n';
                    }
                    if (job.componentSubtreeFresh) {
                        for (unsigned i = 0; i < candidate.subtreeSampleCount; ++i) {
                            report << (runtimeComponent ? "[RUNTIME_COMPONENT_INSTANCE] " : "[GROUND_IK_SUBTREE_INSTANCE] ")
                                << candidate.namespaze << "." << candidate.name
                                << " index=" << i << " object=0x" << std::hex << candidate.subtreeObjects[i]
                                << std::dec << " gameObject=" << candidate.subtreeNames[i] << '\n';
                        }
                    }
                }
                for (unsigned i = 0; i < job.componentLayerCount; ++i) {
                    const auto& layer = job.componentLayers[i];
                    report << "[GROUND_IK_LAYER] depth=" << layer.depth << " stage=" << layer.stage
                        << " transform=0x" << std::hex << layer.transform << " gameObject=0x" << layer.gameObject << std::dec
                        << " name=" << Utf8(layer.name, std::clamp(layer.nameLength, 0, 127))
                        << " component_count=" << layer.componentCount << '\n';
                }
                report.flush();
                if (autoScene && targetFromFile && !job.complete && !job.fault && state.load()==3) {
                    ++targetMisses;
                    report << "target_match=MISS requested_actor=" << targetName << " consecutive=" << targetMisses << " snapshot=INCOMPLETE\n";
                    if (targetMisses>=3) {
                        clearRequestedTarget("AUTO_FALLBACK_INCOMPLETE");
                        report << "requested_actor_fallback=AUTO_PLAYER_BRANCH_AFTER_3_MISSES\n";
                    }
                }
                if (!job.complete || job.fault || state.load() != 3) {
                    ExportRigProbe(directory, report);
                    if (autoScene && !job.fault && state.load()==3 && !BoneWriteArmed() && !MotionRigArmed()) {
                        report << "auto_scene=WAITING_TARGET; partial_readonly_snapshot_released=1\n";report.flush();
                        if (WaitForSingleObject(stop,5000)==WAIT_OBJECT_0) return false;
                        continue;
                    }
                    return false;
                }
                if (autoScene && targetFromFile && RigTargetUnavailable()) {
                    ++targetMisses;
                    report << "target_match=MISS requested_actor=" << targetName << " consecutive=" << targetMisses << '\n';
                    if (targetMisses>=3) {
                        clearRequestedTarget("AUTO_FALLBACK_UNAVAILABLE");
                        report << "requested_actor_fallback=AUTO_PLAYER_BRANCH_AFTER_3_MISSES\n";
                    }
                } else if (RigHasTarget()) targetMisses=0;
                if (autoScene && !MotionRigArmed()) {
                    if (ui::Enabled()) {ExportRigProbe(directory,report);ui::State(requestedName.empty()?L"选择一个可绑定骨架，然后点击绑定。":L"等待所选角色激活及骨架校验。",false,false,false);}
                    report << "auto_scene=WAITING_VALIDATED_PLAYER_RIG; writes=NONE\n";report.flush();
                    if (WaitForSingleObject(stop,5000)==WAIT_OBJECT_0) return false;
                    continue;
                }
                if (success) {
                    const auto& item = job.items[1];
                    if (item.liveValid && item.bonesValid) {
                        std::ofstream bonesFile(directory / L"genshin_poser_bones.txt", std::ios::binary);
                        if (!bonesFile) { report << "bones_export=FILE_OPEN_FAILED\n"; return false; }
                        bonesFile << "SSMT Poser scene readonly bone snapshot; pid=" << GetCurrentProcessId()
                            << "\nobject_name=" << Utf8(item.name, item.nameLength) << "\nsharedMesh.name=" << Utf8(item.meshName, item.meshNameLength)
                            << "\nbones_length=" << item.bonesLength << "\nindex\tTransform\truntimeClass\tnativeTransform\tname\n";
                        for (size_t i = 0; i < item.bonesLength; ++i) {
                            const auto& bone = item.bones[i];
                            bonesFile << i << "\t0x" << std::hex << bone.object << "\t0x" << bone.klass << "\t0x" << bone.native << std::dec
                                << '\t' << (bone.object ? Utf8(bone.name, bone.nameLength) : "<null>") << '\n';
                        }
                        bonesFile.flush();
                        if (!bonesFile) { report << "bones_export=WRITE_FAILED\n"; return false; }
                        report << "bones_export=genshin_poser_bones.txt; slots=" << item.bonesLength << '\n';
                        if (!ExportRigProbe(directory, report)) return false;
                    }
                    if (IsMotionMode()) {
                        if (ui::Enabled()) ui::Bound(requestedName,unsigned(item.bonesLength));
                        bool rescan=false;
                        const auto passed=RunMotionSession(runtime,stop,module,window,report,directory,rescan,requestedName);
                        if (passed&&rescan) {
                            const auto target=requestedName.empty()?std::string("<auto-player-branch>"):Utf8(requestedName.c_str(),int(requestedName.size()));
                            SetRigWriteTarget(requestedName);
                            report << "[RIG_TARGET] action=APPLY stage=RESCAN_RETURN target_after=" << target
                                << " requested_actor_cleared=" << requestedName.empty() << "\n";
                            attempt=0;continue;
                        }
                        return passed;
                    }
                    return RunBoneExperiment(runtime,stop,module,window,report,directory);
                }
            }
        }
        if (WaitForSingleObject(stop, 5000) == WAIT_OBJECT_0) return false;
    }
    report << "blocker=Unity main thread/window not ready\n";
    return false;
}
}
