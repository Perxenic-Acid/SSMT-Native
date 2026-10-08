#include "BoneWriteProbe.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <ostream>
#include <atomic>
#include <MinHook.h>
#include "MotionPlayback.h"

namespace poser {
namespace bone_detail {
bool Valid(const Quaternion& q) {
    const auto norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w) && norm > 0.95f && norm < 1.05f;
}
Quaternion DeltaYaw(const Quaternion& q) {
    // 固定局部 Y 轴 4 度：左乘 delta，重复实验始终使用 q1，避免逐次累加。
    constexpr float s = 0.0348994967f, c = 0.999390827f;
    Quaternion out{c*q.x+s*q.z, c*q.y+s*q.w, c*q.z-s*q.x, c*q.w-s*q.y};
    const auto inverse = 1.0f/std::sqrt(out.x*out.x+out.y*out.y+out.z*out.z+out.w*out.w);
    out.x*=inverse; out.y*=inverse; out.z*=inverse; out.w*=inverse;
    return out;
}
bool Same(const Quaternion& a, const Quaternion& b) {
    if (!Valid(a) || !Valid(b)) return false;
    const auto difference = std::max({std::fabs(a.x-b.x),std::fabs(a.y-b.y),std::fabs(a.z-b.z),std::fabs(a.w-b.w)});
    const auto opposite = std::max({std::fabs(a.x+b.x),std::fabs(a.y+b.y),std::fabs(a.z+b.z),std::fabs(a.w+b.w)});
    return std::min(difference,opposite) < 0.00001f;
}
Quaternion DeltaArm(const Quaternion& q) {
    // 用户要求显著的上臂对照实验；仍只对一根骨绕 Z 轴旋转 60 度，不逐次累加。
    constexpr float s=0.5f,c=0.8660254038f;
    Quaternion out{c*q.x-s*q.y,c*q.y+s*q.x,c*q.z+s*q.w,c*q.w-s*q.z};
    const auto inverse=1.0f/std::sqrt(out.x*out.x+out.y*out.y+out.z*out.z+out.w*out.w);
    out.x*=inverse; out.y*=inverse; out.z*=inverse; out.w*=inverse;
    return out;
}
}
namespace {
RigCalls calls;
NativeMethod getter, setter;
uintptr_t parentEntry = 0, parentDescriptor = 0, activeEntry = 0, activeDescriptor = 0;
struct Sample { unsigned mode; DWORD thread; uint64_t tick; Quaternion before, after; bool matched; };
struct Test {
    uintptr_t objects[4], native[4], parent;
    uint32_t handles[4];
    uint32_t scratch[96];
    unsigned scratchCount, scratchRooted, scratchFreed;
    unsigned rooted, freed, sampleCount, failure;
    Quaternion original, modified;
    Sample samples[128];
    bool armed, captured, applied, restored, ready;
    bool arm;
};
Test test{};
using AnimationBatch = void (*)();
uintptr_t animationBatch=0;
AnimationBatch originalBatch=nullptr;
std::atomic<bool> lateEnabled{false};
std::atomic<unsigned> batchInFlight{0}, batchCalls{0}, lateWrites{0}, lateFault{0}, offMain{0};
std::atomic<uint64_t> lateDeadline{0};
DWORD unityThread=0;
struct LateSample { DWORD thread; uint64_t tick; Quaternion before, animated, written; bool changed, matched; };
LateSample lateSamples[128]{};
unsigned lateSampleCount=0;
bool hookCreated=false, hookInitialized=false;
template<class T> bool Read(uintptr_t address, T& value) {
    SIZE_T count = 0;
    return address && address <= UINTPTR_MAX-sizeof(T) &&
        ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(value), &count) && count==sizeof(value);
}
bool Pattern(uintptr_t address, size_t size, const char* pattern) {
    std::vector<uint8_t> bytes(size); SIZE_T count = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.data(), size, &count) && count==size &&
        native_detail::FindPattern(bytes,pattern).size()==1;
}
uintptr_t Relative(uintptr_t instruction) {
    int32_t displacement=0;
    return Read(instruction+1,displacement) ? native_detail::RelativeTarget(instruction,displacement,5) : 0;
}
bool Method(GenshinNativeRuntime& runtime,const char* type,const char* name,uint8_t parameters,NativeMethod& method) {
    NativeClass klass;
    return runtime.FindClass("UnityEngine",type,klass) && runtime.FindMethod(klass,name,parameters,method) && method.descriptor && method.entry && method.target;
}
bool Alive(uintptr_t object,uintptr_t klass,uintptr_t expectedNative) {
    uintptr_t actual=0,native=0;
    return Read(object,actual) && actual==klass && Read(object+0x10,native) && native==expectedNative && native;
}
bool ScratchRoot(uintptr_t object) {
    if (!object || test.scratchCount==std::size(test.scratch)) return false;
    auto& handle=test.scratch[test.scratchCount++];
    handle=reinterpret_cast<uint32_t (*)(uintptr_t,bool)>(calls.context.handleNew)(object,false);
    if (!handle || (handle>>26)!=2 || reinterpret_cast<uintptr_t (*)(uint32_t)>(calls.context.handleTarget)(handle)!=object) return false;
    ++test.scratchRooted;
    return true;
}
bool Identity() {
    DWORD mainThread=0;
    if (!Read(calls.context.mainThreadSlot,mainThread) || mainThread!=GetCurrentThreadId() ||
        !reinterpret_cast<uintptr_t (*)()>(calls.context.threadCurrent)()) return false;
    for (unsigned i=0;i<4;++i) {
        const auto klass=i<2 ? calls.transformClass : i==2 ? calls.rendererClass : calls.animatorClass;
        if (!test.handles[i] || reinterpret_cast<uintptr_t (*)(uint32_t)>(calls.context.handleTarget)(test.handles[i])!=test.objects[i] ||
            !Alive(test.objects[i],klass,test.native[i])) return false;
    }
    uintptr_t parent=0;
    if (!Read(test.native[0]+0xA0,parent) || parent!=test.parent ||
        !native_detail::ValidateMethod(getter.descriptor,calls.transformClass,getter.metadataId,0,getter.entry) ||
        !native_detail::ValidateMethod(setter.descriptor,calls.transformClass,setter.metadataId,1,setter.entry)) return false;
    // 所有动作再次核验 Body 与目标骨仍拥有同一个已确认 Actor 根。
    for (unsigned source : {0u,2u}) {
        auto transform = source==0 ? test.objects[0] :
            reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(calls.transform)(test.objects[2],calls.transformMethod);
        bool found=false,valid=true;
        for (unsigned depth=0;transform && depth<32;++depth) {
            uintptr_t klass=0,native=0;
            if (!Read(transform,klass) || klass!=calls.transformClass || !Read(transform+0x10,native) || !native) { valid=false; break; }
            if (!ScratchRoot(transform)) { valid=false; break; }
            if (transform==test.objects[1]) { found=true; break; }
            transform=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(parentEntry)(transform,parentDescriptor);
        }
        if (!valid || !found) return false;
    }
    const auto gameObject=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(calls.gameObject)(test.objects[1],calls.gameObjectMethod);
    uintptr_t klass=0,native=0;
    if (!Read(gameObject,klass) || klass!=calls.gameObjectClass || !Read(gameObject+0x10,native) || !native || !ScratchRoot(gameObject)) return false;
    return reinterpret_cast<bool (*)(uintptr_t,uintptr_t)>(activeEntry)(gameObject,activeDescriptor);
}
bool RotationFor(uintptr_t object,uintptr_t expectedNative,Quaternion& out) {
    constexpr uint64_t Canary=0x8AE3C8ED37A99251;
    struct Buffer { uint64_t first; Quaternion q; uint64_t last; } buffer{Canary,{},Canary};
    // 实测 wrapper 的 RCX 是 sret，RDX 是 this，R8 是 descriptor；返回 RAX=sret。
    const auto returned=reinterpret_cast<Quaternion* (*)(Quaternion*,uintptr_t,uintptr_t)>(getter.entry)(&buffer.q,object,getter.descriptor);
    uintptr_t data=0,entries=0; uint32_t index=0; Quaternion native{};
    if (returned!=&buffer.q || buffer.first!=Canary || buffer.last!=Canary || !bone_detail::Valid(buffer.q) ||
        !Read(expectedNative+0x48,data) || !Read(expectedNative+0x50,index) || index>1000000 || !Read(data+8,entries) ||
        !Read(entries+size_t(index)*0x30+0x10,native) || !bone_detail::Same(buffer.q,native)) return false;
    out=buffer.q;
    return true;
}
bool Rotation(Quaternion& out) { return RotationFor(test.objects[0],test.native[0],out); }
bool SetFor(uintptr_t object,const Quaternion& q) {
    constexpr uint64_t Canary=0x81224FFE350877EE;
    struct Buffer { uint64_t first; Quaternion value; uint64_t last; } buffer{Canary,q,Canary};
    reinterpret_cast<void (*)(uintptr_t,const Quaternion*,uintptr_t)>(setter.entry)(object,&buffer.value,setter.descriptor);
    return buffer.first==Canary && buffer.last==Canary && std::memcmp(&buffer.value,&q,sizeof(q))==0;
}
bool Set(const Quaternion& q) { return SetFor(test.objects[0],q); }
void FreeTargets() {
    for (auto& handle:test.handles) if (handle) { reinterpret_cast<void (*)(uint32_t)>(calls.context.handleFree)(handle); ++test.freed; handle=0; }
    test.armed=false;
}
void PrintQ(std::ostream& out,const Quaternion& q) { out << '(' << q.x << ',' << q.y << ',' << q.z << ',' << q.w << ')'; }

bool LateIdentity() {
    if (!test.armed || !test.applied || !test.captured || GetCurrentThreadId()!=unityThread ||
        !reinterpret_cast<uintptr_t (*)()>(calls.context.threadCurrent)()) return false;
    for (unsigned i=0;i<4;++i) {
        const auto klass=i<2?calls.transformClass:i==2?calls.rendererClass:calls.animatorClass;
        if (reinterpret_cast<uintptr_t (*)(uint32_t)>(calls.context.handleTarget)(test.handles[i])!=test.objects[i] ||
            !Alive(test.objects[i],klass,test.native[i])) return false;
    }
    uintptr_t parent=0;
    return Read(test.native[0]+0xA0,parent) && parent==test.parent;
}
bool LateBefore(Quaternion& q) {
    __try { return LateIdentity() && Rotation(q); }
    __except(EXCEPTION_EXECUTE_HANDLER) { lateFault.store(GetExceptionCode()); return false; }
}
void LateAfter(const Quaternion& before) {
    __try {
        Quaternion animated{},written{};
        if (!LateIdentity() || !Rotation(animated) || !Set(test.modified) || !Rotation(written) || !bone_detail::Same(written,test.modified)) {
            lateFault.store(1); lateEnabled.store(false); return;
        }
        ++lateWrites;
        if (lateSampleCount<std::size(lateSamples)) {
            auto& sample=lateSamples[lateSampleCount++];
            sample={GetCurrentThreadId(),GetTickCount64(),before,animated,written,
                !bone_detail::Same(before,animated),bone_detail::Same(written,test.modified)};
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { lateFault.store(GetExceptionCode()); lateEnabled.store(false); }
}
void AnimationReturn() {
    ++batchInFlight;
    __try {
        ++batchCalls;
        // 无 Unity 调用、分配、GC root 创建、文件 IO 或字符串格式化的普通透传路径。
        const bool enabled=lateEnabled.load() && GetTickCount64()<lateDeadline.load();
        Quaternion before{};
        bool capture=false;
        if (enabled) {
            if (GetCurrentThreadId()==unityThread) capture=LateBefore(before);
            else ++offMain;
        }
        originalBatch();
        if (IsMotionMode()) MotionLateTick();
        if (capture && lateEnabled.load() && GetTickCount64()<lateDeadline.load()) LateAfter(before);
    } __finally { --batchInFlight; }
}
}
bool InitializeBoneWrite(GenshinNativeRuntime& runtime,const RigCalls& input,std::ostream& report) {
    calls=input; test={};
    NativeMethod parent,active;
    if (!Method(runtime,"Transform","get_localRotation",0,getter) || !Method(runtime,"Transform","set_localRotation",1,setter) ||
        !Method(runtime,"Transform","get_parent",0,parent) || !Method(runtime,"GameObject","get_activeInHierarchy",0,active)) {
        report << "write_blocker=rotation descriptors\n"; return false;
    }
    parentEntry=parent.entry; parentDescriptor=parent.descriptor; activeEntry=active.entry; activeDescriptor=active.descriptor;
    // 调用关系相对解析；这些偏移属于已核验指令内部，不是 image+RVA fallback。
    if (!Pattern(getter.entry,34,"48 89 CE 0F 57 C0 0F 11 01 48 89 D1 48 89 F2 E8 ?? ?? ?? ?? 48 89 F0") ||
        !Pattern(setter.target,64,"48 8B FA 48 8B D9")) { report << "write_blocker=Quaternion wrapper ABI\n"; return false; }
    const auto getInternal=Relative(getter.entry+20),setNative=Relative(setter.target+47);
    const auto getNative=Relative(getInternal+39),setBuffer=Relative(setNative+39);
    if (!Pattern(getInternal,64,"0F 10 00 0F 11 07") ||
        !Pattern(getNative,32,"42 0F 10 44 C1 10 0F 11 02") ||
        !Pattern(setNative,48,"0F 10 1A") || !Pattern(setBuffer,128,"41 0F 11 52 10")) {
        report << "write_blocker=Quaternion read/write consumers\n"; return false;
    }
    report << "[ROTATION_ABI] getter id=" << getter.metadataId << " entry=0x" << std::hex << getter.entry << " internal=0x" << getInternal
        << " native=0x" << getNative << std::dec << " RCX=sret RDX=this R8=descriptor RAX=sret; bytes=16\n";
    report << "[ROTATION_ABI] setter id=" << setter.metadataId << " entry=0x" << std::hex << setter.entry << " target=0x" << setter.target
        << " native=0x" << setNative << " buffer_consumer=0x" << setBuffer << std::dec << " RCX=this RDX=Quaternion_pointer R8=descriptor; return=void\n";
    test.ready=true;
    return true;
}
bool ArmBoneWrite(uintptr_t head,uintptr_t parent,uintptr_t root,uintptr_t renderer,uintptr_t animator) {
    if (!test.ready || test.armed) return false;
    const uintptr_t values[]={head,root,renderer,animator};
    for (unsigned i=0;i<4;++i) {
        test.objects[i]=values[i];
        uintptr_t klass=0;
        const auto expected=i<2?calls.transformClass:i==2?calls.rendererClass:calls.animatorClass;
        if (!Read(values[i],klass) || klass!=expected || !Read(values[i]+0x10,test.native[i]) || !test.native[i]) { FreeTargets(); return false; }
        test.handles[i]=reinterpret_cast<uint32_t (*)(uintptr_t,bool)>(calls.context.handleNew)(values[i],false);
        if (!test.handles[i] || (test.handles[i]>>26)!=2 || reinterpret_cast<uintptr_t (*)(uint32_t)>(calls.context.handleTarget)(test.handles[i])!=values[i]) { FreeTargets(); return false; }
        ++test.rooted;
    }
    if (!Read(parent+0x10,test.parent) || !test.parent) { FreeTargets(); return false; }
    test.armed=true;
    if (!Identity()) { FreeTargets(); return false; }
    return true;
}
bool BoneWriteArmed() { return test.armed; }
bool ReadBoneRotation(uintptr_t object,uintptr_t expectedNative,Quaternion& out) {
    return test.ready && Alive(object,calls.transformClass,expectedNative) && RotationFor(object,expectedNative,out);
}
bool WriteBoneRotation(uintptr_t object,const Quaternion& value) {
    return test.ready && bone_detail::Valid(value) && SetFor(object,value);
}
bool BoneWriteApplied() { return test.applied; }
void ConfigureArmExperiment() { test.arm=true; }
bool IsArmExperiment() { return test.arm; }
const wchar_t* BoneWriteTargetName() { return test.arm?L"Bip001 L UpperArm":L"Bip001 Head"; }
void ReleaseBoneScratch() {
    for (auto& handle:test.scratch) if (handle) { reinterpret_cast<void (*)(uint32_t)>(calls.context.handleFree)(handle); ++test.scratchFreed; handle=0; }
    test.scratchCount=0;
}
bool BoneWriteStep(unsigned mode) {
    test.failure=mode;
    if (!test.armed) return false;
    if (mode==4 && !test.captured) { FreeTargets(); test.failure=0; return true; }
    if (!Identity() || test.sampleCount==std::size(test.samples)) { if (mode==4) FreeTargets(); return false; }
    auto& sample=test.samples[test.sampleCount++];
    sample.mode=mode; sample.thread=GetCurrentThreadId(); sample.tick=GetTickCount64();
    if (!Rotation(sample.before)) return false;
    if (mode==1) {
        if (test.applied) return false;
        test.original=sample.before; test.modified=test.arm?bone_detail::DeltaArm(test.original):bone_detail::DeltaYaw(test.original); test.captured=true; test.restored=false;
        if (!bone_detail::Valid(test.modified)) return false;
    }
    if (mode==1 || mode==3) {
        if (!test.captured) return false;
        // 在调用前标记，以便 setter 部分执行后发生 SEH 仍会尝试恢复。
        test.applied=true;
        if (!Set(test.modified) || !Rotation(sample.after) || !bone_detail::Same(sample.after,test.modified)) return false;
        sample.matched=true;
    } else if (mode==2) {
        sample.after=sample.before; sample.matched=bone_detail::Same(sample.before,test.modified);
    } else if (mode==4 || mode==5) {
        if (!Set(test.original) || !Rotation(sample.after) || !bone_detail::Same(sample.after,test.original)) return false;
        sample.matched=true; test.restored=true; test.applied=false;
        if (mode==4) FreeTargets();
    } else return false;
    test.failure=0;
    return true;
}
void ReportBoneWrite(std::ostream& report) {
    report << std::setprecision(9) << "[WRITE_TEST] armed=" << test.armed << " originalCaptured=" << test.captured << " modificationApplied=" << test.applied
        << " restore=" << test.restored << " failure=" << test.failure << " target_GC_created=" << test.rooted << " target_GC_freed=" << test.freed
        << " identity_GC_created=" << test.scratchRooted << " identity_GC_freed=" << test.scratchFreed << '\n';
    report << "Bone=" << (test.arm?"Bip001 L UpperArm":"Bip001 Head") << " Transform=0x" << std::hex << test.objects[0] << " ActorRoot=0x" << test.objects[1] << " renderer=0x" << test.objects[2]
        << " animator=0x" << test.objects[3] << std::dec << " delta_degrees=" << (test.arm?60:4) << " axis=" << (test.arm?"parent_Z":"parent_Y") << "\noriginal=";
    PrintQ(report,test.original); report << " modified="; PrintQ(report,test.modified); report << '\n';
    for (unsigned i=0;i<test.sampleCount;++i) {
        const auto& sample=test.samples[i];
        report << "[WRITE_SAMPLE] index=" << i << " mode=" << sample.mode << " tid=" << sample.thread << " tick=" << sample.tick << " before=";
        PrintQ(report,sample.before); report << " after="; PrintQ(report,sample.after); report << " matched=" << sample.matched << '\n';
    }
}
bool InstallAnimationReturnProbe(GenshinNativeRuntime& runtime,HANDLE stop,std::ostream& report) {
    if (!runtime.ResolveScriptLateUpdate(stop,animationBatch,report)) return false;
    if (!animationBatch || !test.armed || hookCreated || !Read(calls.context.mainThreadSlot,unityThread) || !unityThread) return false;
    batchCalls=0; lateWrites=0; lateFault=0; offMain=0; lateSampleCount=0; lateEnabled=false;
    const auto init=MH_Initialize();
    if (init!=MH_OK) { report << "animation_hook_initialize=" << MH_StatusToString(init) << '\n'; return false; }
    hookInitialized=true;
    auto status=MH_CreateHook(reinterpret_cast<void*>(animationBatch),reinterpret_cast<void*>(&AnimationReturn),reinterpret_cast<void**>(&originalBatch));
    if (status==MH_OK) { hookCreated=true; status=MH_EnableHook(reinterpret_cast<void*>(animationBatch)); }
    report << "animation_hook_install=" << MH_StatusToString(status) << '\n'; report.flush();
    if (status!=MH_OK) { RemoveAnimationReturnProbe(report); return false; }
    return true;
}
void EnableAnimationReturnWrites(bool enabled) {
    if (enabled) lateDeadline=GetTickCount64()+2500;
    lateEnabled=enabled;
}
bool AnimationReturnWritesConfirmed() { return lateWrites.load()>0 && lateFault.load()==0; }
bool RemoveAnimationReturnProbe(std::ostream& report) {
    lateEnabled=false;
    bool passed=true;
    if (hookCreated) {
        auto status=MH_DisableHook(reinterpret_cast<void*>(animationBatch));
        passed=status==MH_OK || status==MH_ERROR_DISABLED;
        report << "animation_hook_disable=" << MH_StatusToString(status) << '\n';
        const auto deadline=GetTickCount64()+3000;
        while (batchInFlight.load() && GetTickCount64()<deadline) Sleep(1);
        passed=passed && !batchInFlight.load();
        if (passed) { status=MH_RemoveHook(reinterpret_cast<void*>(animationBatch)); passed=status==MH_OK; report << "animation_hook_remove=" << MH_StatusToString(status) << '\n'; }
        if (passed) hookCreated=false;
    }
    if (passed && hookInitialized) { passed=MH_Uninitialize()==MH_OK; if (passed) hookInitialized=false; }
    if (!passed) {
        // 卸载失败时保留 trampoline 与代码生命期；禁止宿主卸载仍可能在途的探针。
        HMODULE pinned=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&AnimationReturn),&pinned);
        report << "animation_hook_cleanup=FAILED_MODULE_PINNED\n";
    }
    report << "animation_hook_inflight=" << batchInFlight.load() << '\n';
    return passed;
}
void ReportAnimationReturnProbe(std::ostream& report) {
    report << "[ANIMATION_RETURN] calls=" << batchCalls.load() << " writes=" << lateWrites.load() << " offMain=" << offMain.load()
        << " fault=" << lateFault.load() << " samples=" << lateSampleCount << " main_tid=" << unityThread << '\n';
    for (unsigned i=0;i<lateSampleCount;++i) {
        const auto& sample=lateSamples[i];
        report << "[ANIMATION_SAMPLE] index=" << i << " tid=" << sample.thread << " tick=" << sample.tick << " before=";
        PrintQ(report,sample.before); report << " animated="; PrintQ(report,sample.animated); report << " written="; PrintQ(report,sample.written);
        report << " animation_changed=" << sample.changed << " matched=" << sample.matched << '\n';
    }
}
}
