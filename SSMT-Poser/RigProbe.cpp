#include "RigProbe.h"
#include "LiveUnityProbe.h"
#include "BoneWriteProbe.h"
#include "MotionPlayback.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <ostream>

namespace poser {
namespace {
constexpr int MaxNodes = 4096, MaxHandles = 8192, MaxCandidates = 512, MaxDepth = 32, MaxBones = 512;
struct Node {
    uintptr_t transform, native, gameObject, parentObject;
    wchar_t name[128];
    int nameLength, parent, skinIndex, childCount;
    bool active;
};
struct AnimatorCandidate { uintptr_t object; int node; bool human; };
struct ActorCandidate { uintptr_t renderer; int body, head, animator, boneCount; };
struct Snapshot {
    Node nodes[MaxNodes];
    uint32_t handles[MaxHandles];
    AnimatorCandidate animators[MaxCandidates];
    ActorCandidate actors[MaxCandidates];
    int skin[MaxBones];
    int nodeCount, handleCount, rooted, freed, animatorCount, actorCount, boneCount, selectedBody, selectedActor;
    int skippedAnimators, checkedEdges, failureStep;
    int activeAvatarRoots, selectedNodeCount;
    int failureCandidate, skippedUnsupported, skippedDestroyed;
    uintptr_t failureObject, failureClass, failureNative;
    bool complete, coreChains;
};
// 只存 POD，避免在 SEH callback 栈上构造大型临时对象或带析构的容器。
Snapshot snapshot{};
RigCalls calls;
NativeMethod parentMethod, getChildMethod, activeMethod;
wchar_t requestedActor[128]{};
unsigned selectionRooted=0,selectionFreed=0;
using Getter = uintptr_t (*)(uintptr_t, uintptr_t);
using BoolGetter = bool (*)(uintptr_t, uintptr_t);
template<class T> bool Read(uintptr_t address, T& value) {
    SIZE_T copied = 0;
    return address && address <= UINTPTR_MAX - sizeof(T) &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(value), &copied) && copied == sizeof(value);
}
bool Alive(uintptr_t object, uintptr_t klass, uintptr_t& native) {
    uintptr_t actual = 0;
    return Read(object, actual) && actual == klass && Read(object + 0x10, native) && native;
}
bool Root(uintptr_t object) {
    if (!object || snapshot.handleCount == MaxHandles) return false;
    auto& handle = snapshot.handles[snapshot.handleCount++];
    handle = reinterpret_cast<uint32_t (*)(uintptr_t, bool)>(calls.context.handleNew)(object, false);
    if (!handle || (handle >> 26) != 2 || reinterpret_cast<uintptr_t (*)(uint32_t)>(calls.context.handleTarget)(handle) != object) return false;
    ++snapshot.rooted;
    return true;
}
bool Name(uintptr_t object, wchar_t (&text)[128], int& length) {
    const auto string = reinterpret_cast<Getter>(calls.name)(object, calls.nameMethod);
    uintptr_t klass = 0, className = 0;
    char actual[7]{};
    return Read(string, klass) && Read(klass + 0x28, className) && Read(className, actual) &&
        std::memcmp(actual, "String", 7) == 0 && Read(string + 0x10, length) && length >= 0 && length < 128 &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(string + 0x14), text, size_t(length) * 2, nullptr);
}
bool Pattern(uintptr_t address, size_t size, const char* pattern) {
    std::vector<uint8_t> bytes(size);
    SIZE_T count = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.data(), size, &count) && count == size &&
        native_detail::FindPattern(bytes, pattern).size() == 1;
}
bool Method(GenshinNativeRuntime& runtime, const char* owner, const char* name, uint8_t count, NativeMethod& result) {
    NativeClass klass;
    return runtime.FindClass("UnityEngine", owner, klass) && runtime.FindMethod(klass, name, count, result) && result.descriptor && result.entry && result.target;
}
int FindNode(uintptr_t transform) {
    for (int i = 0; i < snapshot.nodeCount; ++i) if (snapshot.nodes[i].transform == transform) return i;
    return -1;
}
int AddNode(uintptr_t transform) {
    const auto existing = FindNode(transform);
    if (existing >= 0) return existing;
    if (snapshot.nodeCount == MaxNodes) return -1;
    uintptr_t actualClass=0;
    if (!Read(transform,actualClass)) return -1;
    if (actualClass!=calls.transformClass) return -2;
    auto& node = snapshot.nodes[snapshot.nodeCount];
    node.parent = -1; node.skinIndex = -1; node.transform = transform;
    if (!Alive(transform, calls.transformClass, node.native) || !Root(transform)) return -1;
    node.gameObject = reinterpret_cast<Getter>(calls.gameObject)(transform, calls.gameObjectMethod);
    uintptr_t nativeGameObject = 0;
    if (!Alive(node.gameObject, calls.gameObjectClass, nativeGameObject) || !Root(node.gameObject) ||
        !Name(node.gameObject, node.name, node.nameLength)) return -1;
    node.active = reinterpret_cast<BoolGetter>(activeMethod.entry)(node.gameObject, activeMethod.descriptor);
    node.parentObject = reinterpret_cast<Getter>(parentMethod.entry)(transform, parentMethod.descriptor);
    uintptr_t expectedParent = 0, nativeParent = 0;
    if (node.parentObject) {
        uintptr_t parentClass=0;
        if (!Read(node.parentObject,parentClass)) return -1;
        if (parentClass!=calls.transformClass) return -2;
    }
    // get_parent 的实际 callee 使用 native+0xA0；用它核验真实 getter 返回。
    if (!Read(node.native + 0xA0, expectedParent) ||
        (node.parentObject && (!Alive(node.parentObject, calls.transformClass, nativeParent) || !Root(node.parentObject))) || nativeParent != expectedParent) return -1;
    node.childCount = reinterpret_cast<int (*)(uintptr_t, uintptr_t)>(calls.child)(transform, calls.childMethod);
    int expectedChildren = -1;
    if (!Read(node.native + 0x90, expectedChildren) || node.childCount < 0 || node.childCount != expectedChildren) return -1;
    return snapshot.nodeCount++;
}
int Track(uintptr_t transform) {
    const auto first = AddNode(transform);
    if (first < 0) return first;
    int visited[MaxDepth]{};
    auto current = first;
    for (int depth = 0; depth < MaxDepth; ++depth) {
        for (int j = 0; j < depth; ++j) if (visited[j] == current) return -1;
        visited[depth] = current;
        const auto parent = snapshot.nodes[current].parentObject;
        if (!parent) return first;
        const auto next = AddNode(parent);
        if (next < 0) return next;
        snapshot.nodes[current].parent = next;
        current = next;
    }
    return -1;
}
int Distance(int child, int ancestor) {
    for (int depth = 0; child >= 0 && depth < MaxDepth; ++depth, child = snapshot.nodes[child].parent)
        if (child == ancestor) return depth;
    return -1;
}
int Named(const wchar_t* name) {
    // Neck 等主体父骨可以不参与 skin 权重；校验完整追踪图，不能局限 SMR.bones。
    const auto limit=snapshot.selectedNodeCount?snapshot.selectedNodeCount:snapshot.nodeCount;
    for (int i = 0; i < limit; ++i) if (std::wcscmp(snapshot.nodes[i].name,name)==0) return i;
    return -1;
}
bool Chain(const wchar_t* child, const wchar_t* parent) {
    const auto a = Named(child), b = Named(parent);
    const auto distance = a >= 0 && b >= 0 ? Distance(a, b) : -1;
    return distance > 0 && distance <= 8;
}
bool CoreChains() {
    return Chain(L"Bip001 Head", L"Bip001 Neck") && Chain(L"Bip001 Neck", L"Bip001 Spine2") &&
        Chain(L"Bip001 Spine2", L"Bip001 Spine1") && Chain(L"Bip001 Spine1", L"Bip001 Spine") &&
        Chain(L"Bip001 L Forearm", L"Bip001 L UpperArm") && Chain(L"Bip001 L Hand", L"Bip001 L Forearm") &&
        Chain(L"Bip001 R Forearm", L"Bip001 R UpperArm") && Chain(L"Bip001 R Hand", L"Bip001 R Forearm") &&
        Chain(L"Bip001 L Calf", L"Bip001 L Thigh") && Chain(L"Bip001 L Foot", L"Bip001 L Calf") &&
        Chain(L"Bip001 R Calf", L"Bip001 R Thigh") && Chain(L"Bip001 R Foot", L"Bip001 R Calf") &&
        Chain(L"Bip001 L Finger02", L"Bip001 L Finger01") && Chain(L"Bip001 L Finger01", L"Bip001 L Finger0") &&
        Chain(L"Bip001 R Finger02", L"Bip001 R Finger01") && Chain(L"Bip001 R Finger01", L"Bip001 R Finger0");
}
int FindAnimator(int body, int head) {
    int selected = -1, best = MaxDepth;
    for (int i = 0; i < snapshot.animatorCount; ++i) {
        const auto ancestor = snapshot.animators[i].node;
        const auto bodyDistance = Distance(body, ancestor), headDistance = Distance(head, ancestor);
        if (bodyDistance >= 0 && headDistance >= 0 && bodyDistance < best) { best = bodyDistance; selected = i; }
    }
    return selected;
}
bool PlayerBranch(int node) {
    bool avatar=false,entity=false;
    for (int depth=0;node>=0 && depth<MaxDepth;++depth,node=snapshot.nodes[node].parent) {
        avatar|=std::wcscmp(snapshot.nodes[node].name,L"AvatarRoot")==0;
        entity|=std::wcscmp(snapshot.nodes[node].name,L"EntityRoot")==0;
    }
    return avatar && entity;
}
std::string Utf8(const wchar_t* text, int length) {
    const auto count = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    if (count) WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), count, nullptr, nullptr);
    for (auto& c : result) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return result;
}
void Ancestry(std::ostream& out, int start) {
    for (int depth = 0; start >= 0 && depth < MaxDepth; ++depth, start = snapshot.nodes[start].parent) {
        const auto& node = snapshot.nodes[start];
        out << depth << '\t' << start << "\t0x" << std::hex << node.transform << std::dec << '\t'
            << Utf8(node.name, node.nameLength) << "\tactive=" << node.active << '\n';
    }
}
void Tree(std::ostream& out, int index, int depth) {
    const auto& node = snapshot.nodes[index];
    out << std::string(size_t(depth) * 2, ' ') << Utf8(node.name, node.nameLength)
        << " [idx=" << index << ", skin=" << node.skinIndex << ", Transform=0x" << std::hex << node.transform << std::dec << "]\n";
    if (depth == MaxDepth) return;
    for (int i = 0; i < snapshot.selectedNodeCount; ++i) if (snapshot.nodes[i].parent == index) Tree(out, i, depth + 1);
}
}

bool InitializeRigProbe(GenshinNativeRuntime& runtime, const RigCalls& input, std::ostream& report) {
    calls = input;
    if (!Method(runtime, "Transform", "get_parent", 0, parentMethod) || !Method(runtime, "Transform", "GetChild", 1, getChildMethod) ||
        !Method(runtime, "GameObject", "get_activeInHierarchy", 0, activeMethod)) {
        report << "blocker=rig method descriptors\n"; return false;
    }
    if (!Pattern(parentMethod.target, 48, "48 8B 90 A0 00 00 00") ||
        !Pattern(getChildMethod.target, 76, "48 63 FA") || !Pattern(getChildMethod.target, 76, "48 8B 14 F8") ||
        !Pattern(activeMethod.target, 40, "48 8B C8 48 83 C4 20 5B E9 ?? ?? ?? ??")) {
        report << "blocker=rig callee dataflow\n"; return false;
    }
    for (const auto& method : {parentMethod, getChildMethod, activeMethod})
        report << "[RIG_GETTER] " << method.name << " id=" << method.metadataId << " entry=0x" << std::hex << method.entry
            << " target=0x" << method.target << " descriptor=0x" << method.descriptor << std::dec << '\n';
    if (!InitializeBoneWrite(runtime,input,report)) report << "write_calls_ready=0; rig_readonly_continues=1\n";
    return true;
}
void SetRigWriteTarget(const std::wstring& name) {
    std::memset(requestedActor,0,sizeof(requestedActor));
    if (name.size()<std::size(requestedActor)) std::copy(name.begin(),name.end(),requestedActor);
}
bool RigHasTarget() { return requestedActor[0]!=0; }
void BeginRigSelection() {
    std::memset(&snapshot,0,sizeof(snapshot));
    selectionRooted=selectionFreed=0;
}
int MatchRigCandidate(uintptr_t renderer) {
    const auto transform=reinterpret_cast<Getter>(calls.transform)(renderer,calls.transformMethod);
    const auto body=Track(transform);
    if (body==-2) return 0;
    if (body<0) return -1;
    if (!snapshot.nodes[body].active) return 0;
    for (auto node=body;node>=0;node=snapshot.nodes[node].parent) {
        if (std::wcscmp(snapshot.nodes[node].name,requestedActor)==0 && snapshot.nodes[node].active && PlayerBranch(node)) return 1;
    }
    return 0;
}
bool CaptureRig(uintptr_t renderer, uintptr_t transform, uintptr_t bones, uint64_t length,
    uintptr_t renderers, uint64_t rendererCount, uintptr_t animators, uint64_t animatorCount) {
    // 候选选择期间的引用也有完整 root/free 配对；释放后才清空快照 POD。
    ReleaseRigProbe();
    selectionRooted=unsigned(snapshot.rooted); selectionFreed=unsigned(snapshot.freed);
    std::memset(&snapshot, 0, sizeof(snapshot));
    snapshot.selectedActor = -1;
    for (auto& index : snapshot.skin) index = -1;
    snapshot.failureStep = 1;
    if (!length || length > MaxBones) return false;
    snapshot.boneCount = int(length);
    snapshot.selectedBody = Track(transform);
    if (snapshot.selectedBody < 0) return false;
    for (int i = 0; i < snapshot.boneCount; ++i) {
        uintptr_t bone = 0;
        if (!Read(bones + 0x20 + size_t(i) * 8, bone)) return false;
        if (!bone) continue;
        const auto node = Track(bone);
        if (node < 0) return false;
        snapshot.skin[i] = node;
        if (snapshot.nodes[node].skinIndex < 0) snapshot.nodes[node].skinIndex = i;
    }
    snapshot.failureStep = 2;
    // GetChild 只交叉核验所选 rig 的实际边，不扫描世界根的全部子树。
    for (int i = 0; i < snapshot.nodeCount; ++i) {
        const auto& node = snapshot.nodes[i];
        if (node.skinIndex < 0 || node.parent < 0) continue;
        const auto& parent = snapshot.nodes[node.parent];
        if (parent.childCount > MaxBones) return false;
        bool found = false;
        for (int j = 0; j < parent.childCount; ++j) {
            const auto child = reinterpret_cast<uintptr_t (*)(uintptr_t, int, uintptr_t)>(getChildMethod.entry)(parent.transform, j, getChildMethod.descriptor);
            if (child == node.transform) { found = true; break; }
        }
        if (!found) return false;
        ++snapshot.checkedEdges;
    }
    snapshot.coreChains = CoreChains();
    snapshot.selectedNodeCount=snapshot.nodeCount;
    snapshot.failureStep = 3;
    for (uint64_t i = 0; i < std::min<uint64_t>(animatorCount, MaxCandidates); ++i) {
        uintptr_t animator = 0, native = 0;
        snapshot.failureCandidate=int(i);
        if (!Read(animators + 0x20 + i * 8, animator)) return false;
        snapshot.failureObject=animator;
        if (!Read(animator,snapshot.failureClass) || !Read(animator+0x10,snapshot.failureNative)) return false;
        if (snapshot.failureClass!=calls.animatorClass) { ++snapshot.skippedUnsupported; continue; }
        if (!snapshot.failureNative) { ++snapshot.skippedDestroyed; continue; }
        const auto transformObject = reinterpret_cast<Getter>(calls.transform)(animator, calls.transformMethod);
        if (!Alive(transformObject, calls.transformClass, native)) { ++snapshot.skippedAnimators; continue; }
        auto& candidate = snapshot.animators[snapshot.animatorCount];
        candidate.object = animator; candidate.node = Track(transformObject);
        if (candidate.node==-2) { ++snapshot.skippedUnsupported; continue; }
        if (candidate.node < 0) return false;
        candidate.human = reinterpret_cast<BoolGetter>(calls.human.entry)(animator, calls.human.descriptor);
        ++snapshot.animatorCount;
    }
    snapshot.failureStep = 4;
    // 输出所有有主体 Head 且有同属祖先 Animator 的候选；骨数量不承担玩家判断。
    for (uint64_t i = 0; i < std::min<uint64_t>(rendererCount, MaxCandidates); ++i) {
        uintptr_t candidate = 0, native = 0;
        int count = 0;
        snapshot.failureCandidate=int(i);
        if (!Read(renderers + 0x20 + i * 8, candidate)) return false;
        snapshot.failureObject=candidate;
        if (!Read(candidate,snapshot.failureClass) || !Read(candidate+0x10,snapshot.failureNative)) return false;
        if (snapshot.failureClass!=calls.rendererClass) { ++snapshot.skippedUnsupported; continue; }
        if (!snapshot.failureNative) { ++snapshot.skippedDestroyed; continue; }
        native=snapshot.failureNative;
        if (!Read(native + 0x440, count)) return false;
        if (count <= 0 || count > MaxBones) continue;
        const auto array = reinterpret_cast<Getter>(calls.bones.entry)(candidate, calls.bones.descriptor);
        uint64_t actualCount = 0;
        if (!Root(array) || !live_detail::ValidateArray(array, calls.transformClass, actualCount) || actualCount != uint64_t(count)) return false;
        uintptr_t head = 0;
        for (int b = 0; b < count; ++b) {
            uintptr_t bone = 0, nativeBone = 0;
            wchar_t name[128]{}; int nameLength = 0;
            if (!Read(array + 0x20 + size_t(b) * 8, bone)) return false;
            if (!bone) continue;
            if (!Alive(bone, calls.transformClass, nativeBone) || !Name(bone, name, nameLength)) return false;
            if (std::wcscmp(name, L"Bip001 Head") == 0) { head = bone; break; }
        }
        if (!head) continue;
        const auto bodyObject = reinterpret_cast<Getter>(calls.transform)(candidate, calls.transformMethod);
        const auto bodyNode = Track(bodyObject), headNode = Track(head);
        if (bodyNode==-2 || headNode==-2) { ++snapshot.skippedUnsupported; continue; }
        if (bodyNode < 0 || headNode < 0) return false;
        const auto animator = FindAnimator(bodyNode, headNode);
        if (animator < 0) continue;
        const auto actor = snapshot.actorCount++;
        snapshot.actors[actor] = {candidate, bodyNode, headNode, animator, count};
        if (candidate == renderer) snapshot.selectedActor = actor;
    }
    snapshot.failureStep = 0;
    snapshot.complete = true;
    int roots[MaxCandidates]{};
    for (int i=0;i<snapshot.actorCount;++i) {
        const auto& actor=snapshot.actors[i];
        const auto root=snapshot.animators[actor.animator].node;
        if (!snapshot.nodes[actor.body].active || !snapshot.nodes[actor.head].active || !snapshot.nodes[root].active || !PlayerBranch(root)) continue;
        bool duplicate=false;
        for (int j=0;j<snapshot.activeAvatarRoots;++j) duplicate|=roots[j]==root;
        if (!duplicate) roots[snapshot.activeAvatarRoots++]=root;
    }
    if (requestedActor[0] && snapshot.coreChains && snapshot.selectedActor>=0) {
        const auto& actor=snapshot.actors[snapshot.selectedActor];
        const auto& animator=snapshot.animators[actor.animator];
        const auto& root=snapshot.nodes[animator.node];
        const auto& head=snapshot.nodes[actor.head];
        const auto targetIndex=Named(BoneWriteTargetName());
        if (snapshot.activeAvatarRoots==1 && PlayerBranch(animator.node) && std::wcscmp(root.name,requestedActor)==0 && root.active && head.active && snapshot.nodes[actor.body].active &&
            head.parent>=0 && std::wcscmp(snapshot.nodes[head.parent].name,L"Bip001 Neck")==0) {
            if (targetIndex<0 || Distance(targetIndex,animator.node)<0) return false;
            const auto& target=snapshot.nodes[targetIndex];
            const auto expectedParent=IsArmExperiment()?L"Bip001 L Clavicle":L"Bip001 Neck";
            if (!target.active || target.parent<0 || std::wcscmp(snapshot.nodes[target.parent].name,expectedParent)!=0) return false;
            if (!ArmBoneWrite(target.transform,snapshot.nodes[target.parent].transform,root.transform,actor.renderer,animator.object)) return false;
            if (IsMotionMode()) {
                // 在 snapshot roots 释放前，将所选 rig 的 managed 引用转交给动作会话。
                static MotionRigBone motionBones[256];
                if (snapshot.selectedNodeCount>int(std::size(motionBones))) return false;
                for (int i=0;i<snapshot.selectedNodeCount;++i) {
                    const auto& node=snapshot.nodes[i]; auto& bone=motionBones[i]; bone={};
                    bone.object=node.transform; bone.native=node.native; bone.parent=node.parent;
                    bone.parentNative=node.parent>=0?snapshot.nodes[node.parent].native:0;
                    bone.owned=Distance(i,animator.node)>=0;
                    std::wmemcpy(bone.name,node.name,std::size(bone.name));
                }
                if (!ArmMotionRig(calls,root.transform,actor.renderer,animator.object,
                    std::span<const MotionRigBone>(motionBones,size_t(snapshot.selectedNodeCount)),activeMethod.entry,activeMethod.descriptor)) return false;
            }
        }
    }
    return true;
}
void ReleaseRigProbe() {
    for (int i = 0; i < snapshot.handleCount; ++i) {
        auto& handle = snapshot.handles[i];
        if (handle) { reinterpret_cast<void (*)(uint32_t)>(calls.context.handleFree)(handle); ++snapshot.freed; handle = 0; }
    }
}
bool ExportRigProbe(const std::filesystem::path& directory, std::ostream& report) {
    report << "[RIG] complete=" << snapshot.complete << " nodes=" << snapshot.nodeCount << " rig_nodes=" << snapshot.selectedNodeCount << " bones=" << snapshot.boneCount
        << " parent_child_edges_checked=" << snapshot.checkedEdges << " core_chains=" << snapshot.coreChains << " failure_step=" << snapshot.failureStep
        << " GC_handles_verified=" << snapshot.rooted << " GC_handles_freed=" << snapshot.freed << '\n';
    report << "rig_candidate_index=" << snapshot.failureCandidate << " object=0x" << std::hex << snapshot.failureObject
        << " class=0x" << snapshot.failureClass << " native=0x" << snapshot.failureNative << std::dec
        << " skipped_unsupported=" << snapshot.skippedUnsupported << " skipped_destroyed=" << snapshot.skippedDestroyed << '\n';
    report << "[OWNERSHIP] animators=" << snapshot.animatorCount << " skipped_UI=" << snapshot.skippedAnimators
        << " actor_renderers=" << snapshot.actorCount << " selected_actor=" << snapshot.selectedActor << " active_AvatarRoot_branches=" << snapshot.activeAvatarRoots
        << " player_identity=HIERARCHY_PLUS_USER_SCENE_CONTEXT\n";
    report << "selection_GC_verified=" << selectionRooted << " selection_GC_freed=" << selectionFreed << '\n';
    if (!snapshot.complete || snapshot.rooted != snapshot.freed) return false;
    std::ofstream rig(directory / L"genshin_poser_rig.txt", std::ios::binary), tree(directory / L"genshin_poser_rig_tree.txt", std::ios::binary), actors(directory / L"genshin_poser_actors.txt", std::ios::binary);
    if (!rig || !tree || !actors) return false;
    rig << "idx\tparentIdx\tdepth\tTransform\tnativeTransform\tskinIndex\tactive\tname\n";
    for (int i = 0; i < snapshot.selectedNodeCount; ++i) {
        const auto& node = snapshot.nodes[i];
        int depth = 0;
        for (auto parent = node.parent; parent >= 0 && depth < MaxDepth; parent = snapshot.nodes[parent].parent) ++depth;
        rig << i << '\t' << node.parent << '\t' << depth << "\t0x" << std::hex << node.transform << "\t0x" << node.native << std::dec
            << '\t' << node.skinIndex << '\t' << node.active << '\t' << Utf8(node.name, node.nameLength) << '\n';
    }
    // 可读树只输出所选 rig 所属顶层树；其他 Actor 的祖先保留在表与 ownership 文件。
    auto root = snapshot.selectedBody;
    while (snapshot.nodes[root].parent >= 0) root = snapshot.nodes[root].parent;
    Tree(tree, root, 0);
    actors << "[SMR ancestry]\n"; Ancestry(actors, snapshot.selectedBody);
    for (int i = 0; i < snapshot.animatorCount; ++i) {
        const auto& candidate = snapshot.animators[i];
        actors << "\n[Animator #" << i << "] object=0x" << std::hex << candidate.object << std::dec << " isHuman=" << candidate.human << '\n';
        Ancestry(actors, candidate.node);
    }
    for (int i = 0; i < snapshot.actorCount; ++i) {
        const auto& actor = snapshot.actors[i];
        const auto& animator = snapshot.animators[actor.animator];
        actors << "\n[Actor renderer #" << i << "] renderer=0x" << std::hex << actor.renderer << " root=0x" << snapshot.nodes[animator.node].transform
            << " animator=0x" << animator.object << " head=0x" << snapshot.nodes[actor.head].transform << std::dec
            << " bones=" << actor.boneCount << " active=" << snapshot.nodes[actor.body].active << " selected=" << (i == snapshot.selectedActor) << '\n';
        actors << "[Body]\n"; Ancestry(actors, actor.body);
        actors << "[Head]\n"; Ancestry(actors, actor.head);
    }
    int actorId=0;
    for (int i=0;i<snapshot.actorCount;++i) {
        const auto& actor=snapshot.actors[i]; const auto actorRoot=snapshot.animators[actor.animator].node;
        bool previous=false;
        for (int j=0;j<i;++j) previous|=snapshot.animators[snapshot.actors[j].animator].node==actorRoot && snapshot.actors[j].head==actor.head;
        if (previous) continue;
        actors << "\n[Actor #" << actorId++ << "] root=0x" << std::hex << snapshot.nodes[actorRoot].transform << std::dec
            << " name=" << Utf8(snapshot.nodes[actorRoot].name,snapshot.nodes[actorRoot].nameLength) << " active=" << snapshot.nodes[actorRoot].active << " renderer_records=";
        for (int j=i;j<snapshot.actorCount;++j) if (snapshot.animators[snapshot.actors[j].animator].node==actorRoot && snapshot.actors[j].head==actor.head) actors << j << ',';
        actors << '\n';
    }
    rig.flush(); tree.flush(); actors.flush();
    return bool(rig) && bool(tree) && bool(actors);
}
}
