#include "MotionPlayback.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <iomanip>
#include <ostream>
#include "LegIK.h"
#include "ReferencePose.h"

namespace poser {
namespace {
constexpr unsigned MaxRig=256,MaxSources=128,MaxSamples=64;
struct Bone {
    MotionRigBone identity;
    uint32_t handle=0;
    Quaternion restoreRotation{0,0,0,1},desiredWorld{0,0,0,1};
    reference::Pose rest;
    int source=-1,depth=0;
    bool write=false,hasReference=false;
};
struct Source {
    wchar_t name[32]{},target[128]{};
    int parent=-1,track=-1,node=-1;
    Quaternion currentWorld{0,0,0,1};
};
struct Leg {
    int thigh=-1,calf=-1,foot=-1,track=-1,parentTrack=-1;
    Vector3 anchor,pole;
    Quaternion firstFoot{0,0,0,1};
    float upper=0,lower=0,maxResidual=0,lastUpper=0,lastLower=0,lastResidual=0;
    unsigned ticks=0,clamped=0,failureStep=0;
};
struct Sample { DWORD thread; uint64_t tick; double frame; unsigned matched; Quaternion left,right,head; };
struct Playback {
    Bone bones[MaxRig]; Source sources[MaxSources]; unsigned order[MaxRig];
    uintptr_t owner[4]{},ownerNative[4]{}; uint32_t ownerHandles[4]{};
    unsigned boneCount=0,sourceCount=0,writeBones=0,mapped=0,rooted=0,freed=0;
    unsigned frames=0,writes=0,restoredBones=0,fault=0,sampleCount=0;
    int root=-1,left=-1,right=-1,head=-1;
    uint64_t startTick=0,nextSample=0,maxTickMicroseconds=0;
    LARGE_INTEGER frequency{};
    DWORD mainThread=0;
    bool armed=false,captured=false,restore=false;
    Sample samples[MaxSamples]{};
    Leg legs[2];
    unsigned ikWrites=0;
    unsigned referenceBones=0,referenceFailure=0;
    bool referenceReady=false;
    uint32_t referenceRoots[2]{};
    uintptr_t referenceMeshObject=0,referenceMeshNative=0;
};
Playback playback{};
RigCalls rigCalls;
vmd::Clip clip;
std::atomic<unsigned> status{0};
std::atomic<bool> enabled{false};
std::atomic<bool> configured{false};
bool footIK=false;
float ikUnits=0;
uintptr_t activeEntry=0,activeDescriptor=0;
NativeMethod meshGetter,bindGetter;
uintptr_t referenceMeshClass=0;
template<class T> bool Read(uintptr_t address,T& out) {
    SIZE_T copied=0; return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&out,sizeof(T),&copied) && copied==sizeof(T);
}
bool MainThread() {
    return GetCurrentThreadId()==playback.mainThread && reinterpret_cast<uintptr_t (*)()>(rigCalls.context.threadCurrent)();
}
bool Alive(uintptr_t object,uintptr_t native,uintptr_t klass) {
    // 所有访问处于调用方的 SEH leaf 内，且 managed 对象持有强 root。
    return object && native && *reinterpret_cast<uintptr_t*>(object)==klass && *reinterpret_cast<uintptr_t*>(object+0x10)==native;
}
uint32_t Root(uintptr_t object) {
    const auto handle=reinterpret_cast<uint32_t (*)(uintptr_t,bool)>(rigCalls.context.handleNew)(object,false);
    if (!handle || (handle>>26)!=2 || reinterpret_cast<uintptr_t (*)(uint32_t)>(rigCalls.context.handleTarget)(handle)!=object) {
        if (handle) reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(handle);
        return 0;
    }
    ++playback.rooted; return handle;
}
void FreeRoots() {
    for (auto& handle:playback.referenceRoots) if (handle) {reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(handle);handle=0;++playback.freed;}
    for (auto& bone:playback.bones) if (bone.handle) { reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(bone.handle); bone.handle=0; ++playback.freed; }
    for (auto& handle:playback.ownerHandles) if (handle) { reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(handle); handle=0; ++playback.freed; }
    playback.armed=false;
}
bool OwnerIdentity(bool requireActive=true) {
    if (!playback.armed || !MainThread()) return false;
    const uintptr_t classes[]={rigCalls.transformClass,rigCalls.rendererClass,rigCalls.animatorClass,rigCalls.gameObjectClass};
    for (unsigned i=0;i<4;++i) if (!playback.ownerHandles[i] ||
        reinterpret_cast<uintptr_t (*)(uint32_t)>(rigCalls.context.handleTarget)(playback.ownerHandles[i])!=playback.owner[i] ||
        !Alive(playback.owner[i],playback.ownerNative[i],classes[i])) return false;
    return !requireActive || reinterpret_cast<bool (*)(uintptr_t,uintptr_t)>(activeEntry)(playback.owner[3],activeDescriptor);
}
bool BoneIdentity(const Bone& bone) {
    return bone.handle && reinterpret_cast<uintptr_t (*)(uint32_t)>(rigCalls.context.handleTarget)(bone.handle)==bone.identity.object &&
        Alive(bone.identity.object,bone.identity.native,rigCalls.transformClass) &&
        *reinterpret_cast<uintptr_t*>(bone.identity.native+0xA0)==bone.identity.parentNative;
}
int Named(std::wstring_view name) {
    int result=-1;
    for (unsigned i=0;i<playback.boneCount;++i) if (playback.bones[i].identity.owned && name==playback.bones[i].identity.name) {
        if (result>=0) return -2; result=int(i);
    }
    return result;
}
int Track(std::wstring name) {
    int result=vmd::FindTrack(clip,name);
    if (result>=0) return result;
    for (auto& ch:name) if (ch>=L'０' && ch<=L'９') ch=ch-L'０'+L'0';
    return vmd::FindTrack(clip,name);
}
int AddSource(const std::wstring& name,const std::wstring& target,int parent) {
    if (playback.sourceCount==MaxSources || name.size()>=32 || target.size()>=128) return -1;
    const auto index=int(playback.sourceCount++); auto& source=playback.sources[index]; source={};
    std::wmemcpy(source.name,name.data(),name.size()); std::wmemcpy(source.target,target.data(),target.size()); source.parent=parent;
    source.track=Track(name); source.node=target.empty()?-1:Named(target);
    if (source.node>=0 && source.track>=0 && playback.bones[source.node].hasReference) { playback.bones[source.node].source=index; ++playback.mapped; }
    return index;
}
void BuildSources() {
    playback.sourceCount=0; playback.mapped=0;
    for (auto& bone:playback.bones) { bone.source=-1; bone.write=false; }
    const auto all=AddSource(L"全ての親",L"",-1),center=AddSource(L"センター",L"",all),groove=AddSource(L"グルーブ",L"",center),waist=AddSource(L"腰",L"",groove);
    const auto lower=AddSource(L"下半身",L"Bip001 Pelvis",waist),upper=AddSource(L"上半身",L"Bip001 Spine",waist);
    const auto upper2=AddSource(L"上半身2",L"Bip001 Spine2",upper),upper3=AddSource(L"上半身3",L"",upper2);
    const auto neck=AddSource(L"首",L"Bip001 Neck",upper3); AddSource(L"頭",L"Bip001 Head",neck);
    const wchar_t* fingerNames[]={L"親指",L"人指",L"中指",L"薬指",L"小指"};
    for (unsigned side=0;side<2;++side) {
        const std::wstring prefix=side?L"右":L"左",target=side?L"Bip001 R ":L"Bip001 L ";
        const auto shoulderP=AddSource(prefix+L"肩P",L"",upper3),shoulder=AddSource(prefix+L"肩",target+L"Clavicle",shoulderP),shoulderC=AddSource(prefix+L"肩C",L"",shoulder);
        // 即使肩主轨只有一帧，也需要拥有 Clavicle，才能应用动态肩 P 的世界旋转。
        if (playback.sources[shoulder].node>=0 && playback.sources[shoulderP].track>=0 && playback.sources[shoulder].track<0) {
            if (playback.bones[playback.sources[shoulder].node].hasReference) {playback.bones[playback.sources[shoulder].node].source=shoulder; ++playback.mapped;}
        }
        const auto arm=AddSource(prefix+L"腕",target+L"UpperArm",shoulderC),twist=AddSource(prefix+L"腕捩",L"",arm),elbow=AddSource(prefix+L"ひじ",target+L"Forearm",twist);
        if (playback.sources[elbow].track<0) {
            playback.sources[elbow].track=Track(prefix+L"肘");
            if (playback.sources[elbow].track>=0 && playback.sources[elbow].node>=0 && playback.bones[playback.sources[elbow].node].hasReference) { playback.bones[playback.sources[elbow].node].source=elbow; ++playback.mapped; }
        }
        const auto handTwist=AddSource(prefix+L"手捩",L"",elbow),hand=AddSource(prefix+L"手首",target+L"Hand",handTwist);
        for (unsigned finger=0;finger<5;++finger) {
            int parent=hand;
            for (unsigned joint=0;joint<3;++joint) {
                const auto digit=wchar_t(L'０'+joint+(finger?1:0));
                auto bone=target+L"Finger"+std::to_wstring(finger);
                if (joint) bone+=std::to_wstring(joint);
                const auto source=AddSource(prefix+fingerNames[finger]+digit,bone,parent); parent=source;
                if (finger==1 && playback.sources[source].track<0) {
                    playback.sources[source].track=Track(prefix+L"人差指"+digit);
                    if (playback.sources[source].track>=0 && playback.sources[source].node>=0 && playback.bones[playback.sources[source].node].hasReference) { playback.bones[playback.sources[source].node].source=source; ++playback.mapped; }
                }
            }
        }
        const auto thigh=AddSource(prefix+L"足",target+L"Thigh",lower),knee=AddSource(prefix+L"ひざ",target+L"Calf",thigh),foot=AddSource(prefix+L"足首",target+L"Foot",knee);
        AddSource(prefix+L"つま先",target+L"Toe0",foot);
    }
    playback.writeBones=0;
    // 没有动作轨的 skin bone 也使用静态基准，避免衣物、头发等继续叠加游戏旋转。
    for (auto& bone:playback.bones) bone.write=bone.identity.owned&&bone.hasReference;
    for (unsigned side=0;side<2;++side) {
        auto& leg=playback.legs[side];leg={};
        if (!footIK) continue;
        const std::wstring prefix=side?L"右":L"左",target=side?L"Bip001 R ":L"Bip001 L ";
        leg.thigh=Named(target+L"Thigh");leg.calf=Named(target+L"Calf");leg.foot=Named(target+L"Foot");
        leg.track=Track(prefix+L"足ＩＫ");if (leg.track<0) leg.track=Track(prefix+L"足IK");
        leg.parentTrack=Track(prefix+L"足IK親");
        if (leg.track<0 || leg.thigh<0 || leg.calf<0 || leg.foot<0) {leg.track=-1;continue;}
        if (playback.bones[leg.calf].identity.parent!=leg.thigh || playback.bones[leg.foot].identity.parent!=leg.calf) {leg.track=-1;continue;}
        if (!playback.bones[leg.thigh].hasReference||!playback.bones[leg.calf].hasReference||!playback.bones[leg.foot].hasReference) leg.track=-1;
    }
    for (unsigned i=0;i<playback.boneCount;++i) if (playback.bones[i].write) ++playback.writeBones;
}
bool CaptureReference() {
    if (!OwnerIdentity() || !meshGetter.entry || !bindGetter.entry || !referenceMeshClass) return false;
    playback.referenceReady=false;playback.referenceBones=0;playback.referenceFailure=1;
    for (auto& bone:playback.bones) bone.hasReference=false;
    const auto mesh=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(meshGetter.entry)(playback.owner[1],meshGetter.descriptor);
    uintptr_t native=0;
    if (!Read(mesh+0x10,native)||!Alive(mesh,native,referenceMeshClass)) return false;
    auto& meshRoot=playback.referenceRoots[0];meshRoot=Root(mesh);if (!meshRoot) return false;
    // 所有临时 managed 引用在同一 main-thread leaf 释放；矩阵以值复制，不跨帧借用数组。
    auto& arrayRoot=playback.referenceRoots[1];bool passed=false;
    playback.referenceFailure=2;
    const auto array=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(bindGetter.entry)(mesh,bindGetter.descriptor);
    if (array && (arrayRoot=Root(array))) {
        uintptr_t klass=0,bounds=1,element=0,data=0,values=0;uint64_t length=0,count=0;uint8_t kind=0,elementKind=0;
        passed=Read(array,klass)&&Read(klass+0x72,kind)&&kind==0x1D&&Read(klass+0x68,element)&&Read(element+0xA,elementKind)&&elementKind==0x11&&
            Read(array+0x10,bounds)&&!bounds&&Read(array+0x18,length)&&length>0&&length<=MaxRig&&
            Read(native+0x48,data)&&Read(data+0xD0,values)&&Read(data+0xE0,count)&&length==count;
        unsigned expected=0;bool used[MaxRig]{};
        for (unsigned i=0;passed&&i<playback.boneCount;++i) {
            auto& bone=playback.bones[i];if (bone.identity.skinIndex<0) continue;
            playback.referenceFailure=3;
            reference::Matrix managed{},original{};
            passed=bone.identity.owned&&BoneIdentity(bone)&&uint64_t(bone.identity.skinIndex)<length&&!used[bone.identity.skinIndex]&&
                Read(array+0x20+size_t(bone.identity.skinIndex)*64,managed)&&Read(values+size_t(bone.identity.skinIndex)*64,original)&&
                std::memcmp(&managed,&original,sizeof(managed))==0&&reference::Decode(managed,bone.rest);
            if (passed) {used[bone.identity.skinIndex]=true;bone.hasReference=true;++expected;}
        }
        passed=passed&&expected==length;
        // mesh 空间必须与 Actor rig 的模型空间对齐；未知变换不能静默当成单位矩阵。
        const auto body=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(rigCalls.transform)(playback.owner[1],rigCalls.transformMethod);
        int bodyIndex=-1;for (unsigned i=0;i<playback.boneCount;++i) if (playback.bones[i].identity.object==body) bodyIndex=int(i);
        Quaternion bodyRotation{};Vector3 bodyPosition{};playback.referenceFailure=4;
        passed=passed&&bodyIndex>=0&&playback.bones[bodyIndex].identity.parent==playback.root&&BoneIdentity(playback.bones[bodyIndex])&&
            ReadBoneRotation(body,playback.bones[bodyIndex].identity.native,bodyRotation)&&bone_detail::Same(bodyRotation,{0,0,0,1})&&
            ReadBonePosition(body,playback.bones[bodyIndex].identity.native,bodyPosition,false)&&ik::Length(bodyPosition)<0.0001f;
        if (passed) {playback.referenceMeshObject=mesh;playback.referenceMeshNative=native;playback.referenceBones=expected;playback.referenceReady=true;playback.referenceFailure=0;}
    }
    if (arrayRoot) {reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(arrayRoot);arrayRoot=0;++playback.freed;}
    reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(meshRoot);meshRoot=0;++playback.freed;
    if (!passed) for (auto& bone:playback.bones) bone.hasReference=false;
    return passed;
}
bool Position(int index,Vector3& out) {
    const auto& bone=playback.bones[index];return BoneIdentity(bone)&&ReadBonePosition(bone.identity.object,bone.identity.native,out,true);
}
bool WorldRotation(int index,Quaternion& out) {
    const auto& bone=playback.bones[index];return BoneIdentity(bone)&&ReadBoneWorldRotation(bone.identity.object,bone.identity.native,out);
}
bool IKSet(int index,Quaternion world) {
    const auto& bone=playback.bones[index];Quaternion parent{},after{};
    if (bone.identity.parent<0 || !WorldRotation(bone.identity.parent,parent)) return false;
    const auto local=vmd::Multiply(vmd::Inverse(parent),world);
    if (!BoneIdentity(bone)||!WriteBoneRotation(bone.identity.object,local)||!ReadBoneRotation(bone.identity.object,bone.identity.native,after)||!bone_detail::Same(after,local)) return false;
    ++playback.ikWrites;return true;
}
bool PrepareIK() {
    if (!footIK) return true;
    Vector3 root{};Quaternion rotation{};
    if (!Position(playback.root,root)||!WorldRotation(playback.root,rotation)) return false;
    for (auto& leg:playback.legs) if (leg.track>=0) {
        Vector3 hip{},knee{},foot{};Quaternion footRotation{};
        for (int index : {leg.thigh,leg.calf,leg.foot}) {
            Vector3 local{};const auto& bone=playback.bones[index];
            if (!ReadBonePosition(bone.identity.object,bone.identity.native,local,false)) return false;
        }
        if (!Position(leg.thigh,hip)||!Position(leg.calf,knee)||!Position(leg.foot,foot)||!WorldRotation(leg.foot,footRotation)) return false;
        leg.upper=ik::Length(ik::Sub(knee,hip));leg.lower=ik::Length(ik::Sub(foot,knee));
        if (!std::isfinite(leg.upper)||!std::isfinite(leg.lower)||leg.upper<0.0001f||leg.lower<0.0001f||leg.upper>100||leg.lower>100) return false;
        const auto& restHip=playback.bones[leg.thigh].rest.position;
        const auto& restKnee=playback.bones[leg.calf].rest.position;
        const auto& restFoot=playback.bones[leg.foot].rest.position;
        const auto restUpper=ik::Length(ik::Sub(restKnee,restHip)),restLower=ik::Length(ik::Sub(restFoot,restKnee));
        if (restUpper<0.0001f||restLower<0.0001f) return false;
        const auto scale=leg.upper/restUpper;
        if (std::fabs(leg.lower/restLower-scale)>scale*0.01f) return false;
        const auto direction=ik::Unit(ik::Sub(restFoot,restHip)),first=ik::Sub(restKnee,restHip);
        const auto bend=ik::Sub(first,ik::Scale(direction,ik::Dot(first,direction)));
        leg.pole=ik::Unit(bend);
        if (ik::Length(leg.pole)<0.5f) leg.pole={0,0,1};
        leg.anchor=ik::Scale(restFoot,scale);
        leg.firstFoot=playback.bones[leg.foot].rest.rotation;
        leg.ticks=0;leg.clamped=0;leg.maxResidual=0;
    }
    return true;
}
bool SolveIK(double frame,float blend) {
    if (!footIK) return true;
    Vector3 root{};Quaternion rotation{};
    if (!Position(playback.root,root)||!WorldRotation(playback.root,rotation)) return false;
    for (auto& leg:playback.legs) if (leg.track>=0) {
        auto delta=vmd::SamplePosition(clip.tracks[leg.track],frame);
        if (leg.parentTrack>=0) delta=ik::Add(delta,vmd::SamplePosition(clip.tracks[leg.parentTrack],frame));
        const auto goal=ik::Add(root,ik::Rotate(rotation,ik::Add(leg.anchor,ik::Scale(delta,ikUnits*blend))));
        Vector3 hip{},knee{},foot{};Quaternion thigh{},calf{};
        leg.failureStep=1;
        if (!Position(leg.thigh,hip)||!Position(leg.calf,knee)||!Position(leg.foot,foot)||!WorldRotation(leg.thigh,thigh)) return false;
        const auto actualUpper=ik::Length(ik::Sub(knee,hip)),actualLower=ik::Length(ik::Sub(foot,knee));
        leg.lastUpper=actualUpper;leg.lastLower=actualLower;leg.failureStep=2;
        // 长度变化表示 scale / hierarchy 的假设失效；不能继续沿用启动时的腿几何。
        if (std::fabs(actualUpper-leg.upper)>leg.upper*0.01f||std::fabs(actualLower-leg.lower)>leg.lower*0.01f) return false;
        ik::Solution solution;
        leg.failureStep=3;
        if (!ik::Solve(hip,goal,ik::Rotate(rotation,leg.pole),leg.upper,leg.lower,solution)) return false;
        leg.failureStep=4;
        if (!IKSet(leg.thigh,vmd::Multiply(ik::FromTo(ik::Sub(knee,hip),ik::Sub(solution.knee,hip)),thigh))) return false;
        leg.failureStep=5;
        if (!Position(leg.calf,knee)||!Position(leg.foot,foot)||!WorldRotation(leg.calf,calf)) return false;
        leg.failureStep=6;
        if (!IKSet(leg.calf,vmd::Multiply(ik::FromTo(ik::Sub(foot,knee),ik::Sub(solution.ankle,knee)),calf))||!Position(leg.foot,foot)) return false;
        const auto residual=ik::Length(ik::Sub(foot,solution.ankle));
        leg.lastResidual=residual;leg.failureStep=7;
        if (!std::isfinite(residual)||residual>(leg.upper+leg.lower)*0.002f) return false;
        leg.maxResidual=std::max(leg.maxResidual,residual);leg.clamped+=solution.clamped; ++leg.ticks;
        const auto footWorld=vmd::Multiply(rotation,vmd::Slerp(leg.firstFoot,vmd::Multiply(vmd::Sample(clip.tracks[leg.track],frame),leg.firstFoot),blend));
        leg.failureStep=8;
        if (!IKSet(leg.foot,footWorld)) return false;
        leg.failureStep=0;
    }
    return true;
}
void SourcePose(double frame) {
    for (unsigned i=0;i<playback.sourceCount;++i) {
        auto& source=playback.sources[i];
        const auto local=source.track>=0?vmd::Sample(clip.tracks[source.track],frame):Quaternion{0,0,0,1};
        source.currentWorld=source.parent>=0?vmd::Multiply(playback.sources[source.parent].currentWorld,local):local;
    }
}
bool Stop() {
    enabled=false;
    bool restored=true;
    if (playback.captured) {
        restored=OwnerIdentity(false); playback.restoredBones=0;
        for (unsigned i=0;restored && i<playback.boneCount;++i) if (playback.bones[i].write) {
            const auto& bone=playback.bones[i]; Quaternion after{};
            restored=BoneIdentity(bone) && WriteBoneRotation(bone.identity.object,bone.restoreRotation) && ReadBoneRotation(bone.identity.object,bone.identity.native,after) && bone_detail::Same(after,bone.restoreRotation);
            if (restored) ++playback.restoredBones;
        }
        playback.restore=restored;
        if (restored) playback.captured=false;
    }
    status=restored?0:3;
    return restored;
}
void Tick() {
    if (!enabled.load() || status.load()!=1) return;
    if (!OwnerIdentity()) { playback.fault=1; enabled=false; status=3; return; }
    const auto tick=GetTickCount64();
    const double frame=double(tick-playback.startTick)*0.03;
    if (frame>clip.lastFrame) { enabled=false; status=2; return; }
    LARGE_INTEGER begin{},end{}; QueryPerformanceCounter(&begin);
    SourcePose(frame);
    const auto blend=std::min(1.0f,float(tick-playback.startTick)/350.0f);
    Quaternion rootWorld{};
    if (!WorldRotation(playback.root,rootWorld)) {playback.fault=5;enabled=false;status=3;return;}
    unsigned matched=0;
    for (unsigned order=0;order<playback.boneCount;++order) {
        auto& bone=playback.bones[playback.order[order]];
        if (!bone.write || !bone.hasReference) continue;
        const auto parent=bone.identity.parent;
        auto desired=bone.rest.rotation;
        if (bone.source>=0) {
            const auto& source=playback.sources[bone.source];
            desired=vmd::Multiply(source.currentWorld,bone.rest.rotation);
        } else if (parent>=0 && playback.bones[parent].hasReference) {
            const auto& ancestor=playback.bones[parent];
            desired=vmd::Multiply(ancestor.desiredWorld,vmd::Multiply(vmd::Inverse(ancestor.rest.rotation),bone.rest.rotation));
        }
        bone.desiredWorld=vmd::Slerp(bone.rest.rotation,desired,blend);
        Quaternion actualParent{};
        if (parent<0||!WorldRotation(parent,actualParent)) {playback.fault=5;enabled=false;status=3;return;}
        // 未蒙皮的中间骨没有 bind matrix，读取真实父旋转以抵消其动画；不把它作为动作基准。
        const auto desiredLocal=vmd::Multiply(vmd::Inverse(actualParent),vmd::Multiply(rootWorld,bone.desiredWorld));
        Quaternion after{};
        if (!BoneIdentity(bone) || !WriteBoneRotation(bone.identity.object,desiredLocal) || !ReadBoneRotation(bone.identity.object,bone.identity.native,after) || !bone_detail::Same(after,desiredLocal)) {
            playback.fault=2; enabled=false; status=3; return;
        }
        ++matched; ++playback.writes;
    }
    if (!SolveIK(frame,blend)) {playback.fault=4;enabled=false;status=3;return;}
    ++playback.frames; QueryPerformanceCounter(&end);
    if (playback.frequency.QuadPart>0) playback.maxTickMicroseconds=std::max(playback.maxTickMicroseconds,uint64_t((end.QuadPart-begin.QuadPart)*1000000/playback.frequency.QuadPart));
    if (tick>=playback.nextSample && playback.sampleCount<MaxSamples) {
        auto& sample=playback.samples[playback.sampleCount++]; sample={GetCurrentThreadId(),tick,frame,matched,{},{},{}};
        if (playback.left>=0) ReadBoneRotation(playback.bones[playback.left].identity.object,playback.bones[playback.left].identity.native,sample.left);
        if (playback.right>=0) ReadBoneRotation(playback.bones[playback.right].identity.object,playback.bones[playback.right].identity.native,sample.right);
        if (playback.head>=0) ReadBoneRotation(playback.bones[playback.head].identity.object,playback.bones[playback.head].identity.native,sample.head);
        playback.nextSample=tick+500;
    }
}
void Q(std::ostream& out,const Quaternion& q) { out << '(' << q.x << ',' << q.y << ',' << q.z << ',' << q.w << ')'; }
}
void ConfigureMotionMode() { configured=true; }
bool ConfigureFootIK(float units) {if (!std::isfinite(units)||units<=0||units>1) return false;ikUnits=units;footIK=true;return true;}
bool IsMotionMode() { return configured.load(); }
bool MotionRigArmed() { return playback.armed; }
bool InitializeMotionReference(GenshinNativeRuntime& runtime,uintptr_t meshClass,const NativeMethod& sharedMesh,std::ostream& report) {
    playback.referenceReady=false;meshGetter=sharedMesh;referenceMeshClass=0;
    NativeClass klass;
    if (!runtime.DescribeClass(meshClass,klass)||klass.namespaze!="UnityEngine"||klass.name!="Mesh"||
        !runtime.FindMethod(klass,"get_bindposes",0,bindGetter)||!bindGetter.entry||!bindGetter.descriptor||!bindGetter.target) {
        report << "reference_blocker=Mesh_bindposes_descriptor\n";return false;
    }
    auto pattern=[](uintptr_t address,size_t size,const char* signature) {
        std::vector<uint8_t> bytes(size);SIZE_T copied=0;
        return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes.data(),size,&copied)&&copied==size&&native_detail::FindPattern(bytes,signature).size()==1;
    };
    // 从当前 getter 的调用链恢复转换器，固定偏移来自已核验 wrapper，不部署样本 RVA。
    uint8_t call=0;int32_t displacement=0;
    const auto instruction=bindGetter.target+32;
    if (!pattern(bindGetter.target,81,"48 8B D0 48 8D 4C 24 30 E8 ?? ?? ?? ?? 48 8B 00")||
        !Read(instruction,call)||call!=0xE8||!Read(instruction+1,displacement)) {
        report << "reference_blocker=bindposes_getter_wrapper\n";return false;
    }
    const auto converter=native_detail::RelativeTarget(instruction,displacement,5);
    if (!pattern(converter,128,"48 8B 42 48")||!pattern(converter,128,"8B A8 E0 00 00 00")||
        !pattern(converter,128,"48 8B B8 D0 00 00 00")||
        !pattern(converter,128,"48 C1 E3 06 48 8B D7 4C 8B C3 48 89 06 48 8D 48 20 E8 ?? ?? ?? ??")) {
        report << "reference_blocker=bindposes_native_copy_layout\n";return false;
    }
    referenceMeshClass=meshClass;
    report << "reference_ABI=MANAGED_MATRIX4X4_SZARRAY; matrix_bytes=64; matrix_source=NATIVE_COPY_CROSSCHECKED; descriptorId=" << bindGetter.metadataId << '\n';report.flush();return true;
}
bool ArmMotionRig(const RigCalls& calls,uintptr_t root,uintptr_t renderer,uintptr_t animator,std::span<const MotionRigBone> bones,
    uintptr_t active,uintptr_t activeMethod) {
    if (playback.armed || bones.empty() || bones.size()>MaxRig) return false;
    playback={}; rigCalls=calls; status=0; enabled=false;
    activeEntry=active; activeDescriptor=activeMethod;
    if (!activeEntry || !activeDescriptor) return false;
    if (!Read(calls.context.mainThreadSlot,playback.mainThread) || !MainThread()) return false;
    const auto gameObject=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(calls.gameObject)(root,calls.gameObjectMethod);
    const uintptr_t values[]={root,renderer,animator,gameObject};
    const uintptr_t classes[]={calls.transformClass,calls.rendererClass,calls.animatorClass,calls.gameObjectClass};
    for (unsigned i=0;i<4;++i) {
        uintptr_t klass=0;
        if (!Read(values[i],klass) || klass!=classes[i] || !Read(values[i]+0x10,playback.ownerNative[i]) || !playback.ownerNative[i]) { FreeRoots(); return false; }
        playback.owner[i]=values[i]; playback.ownerHandles[i]=Root(values[i]);
        if (!playback.ownerHandles[i]) { FreeRoots(); return false; }
    }
    playback.boneCount=unsigned(bones.size());
    for (unsigned i=0;i<bones.size();++i) {
        auto& bone=playback.bones[i]; bone.identity=bones[i];
        if (bone.identity.object==root) playback.root=int(i);
        if (bone.identity.owned) {
            if (!Alive(bone.identity.object,bone.identity.native,calls.transformClass) || !(bone.handle=Root(bone.identity.object))) { FreeRoots(); return false; }
            auto parent=bone.identity.parent;
            for (unsigned depth=0;parent>=0 && depth<32;++depth) {
                if (unsigned(parent)>=bones.size() || parent==int(i)) { FreeRoots(); return false; }
                ++bone.depth; parent=bones[parent].parent;
            }
            if (parent>=0) { FreeRoots(); return false; }
        }
        playback.order[i]=i;
    }
    if (playback.root<0) { FreeRoots(); return false; }
    std::sort(playback.order,playback.order+playback.boneCount,[](unsigned a,unsigned b){return playback.bones[a].depth<playback.bones[b].depth;});
    playback.left=Named(L"Bip001 L UpperArm"); playback.right=Named(L"Bip001 R UpperArm"); playback.head=Named(L"Bip001 Head");
    QueryPerformanceFrequency(&playback.frequency); playback.armed=true; return OwnerIdentity();
}
bool LoadMotionClip(const std::filesystem::path& path,std::ostream& report,HANDLE stop) {
    if (enabled.load() || status.load()==1 || playback.captured || !playback.armed) return false;
    vmd::Clip loaded; std::string error;
    if (!vmd::Load(path,loaded,error,stop)) { report << "motion_load=REJECTED; reason=" << error << '\n'; report.flush(); return false; }
    report << "[VMD_FILE] bytes=" << loaded.bytes << " bone_keys=" << loaded.boneKeys << " tracks=" << loaded.tracks.size()
        << " last_frame=" << loaded.lastFrame << " fps=30 camera_keys=" << loaded.cameraKeys << " morph_keys=" << loaded.morphKeys << '\n';
    if (loaded.tracks.empty() || !loaded.lastFrame) { report << "motion_load=REJECTED; reason=" << (loaded.tracks.empty()?"NO_BONE_TRACKS_CAMERA_OR_MORPH_ONLY":"STATIC_POSE_NO_TIMED_MOTION") << '\n'; report.flush(); return false; }
    if (!playback.referenceReady) {report << "motion_load=REJECTED; reason=STATIC_BIND_REFERENCE_REQUIRED\n";report.flush();return false;}
    clip=std::move(loaded); BuildSources();
    report << "motion_load=" << (playback.mapped && playback.writeBones?"READY":"REJECTED_UNMAPPED") << " mapped_tracks=" << playback.mapped
        << " write_bones=" << playback.writeBones << " retarget=STATIC_MESH_BIND_REFERENCE; positions=NOT_WRITTEN; foot_IK=" << (footIK?"ANALYTIC_TWO_BONE_PREVIEW":"NOT_SOLVED") << "; morphs=NOT_APPLIED\n";
    if (footIK) {
        unsigned legs=0;for (const auto& leg:playback.legs) legs+=leg.track>=0;
        report << "foot_IK_legs=" << legs << " game_units_per_VMD_unit=" << ikUnits << " calibration=EXPLICIT_PREVIEW_PARAMETER; source_PMX_rest=NOT_AVAILABLE; D_grants=NOT_APPLIED\n";
        if (legs!=2) {report << "motion_load=REJECTED; reason=FOOT_IK_REQUIRES_BOTH_TARGET_CHAINS_AND_TRACKS\n";report.flush();return false;}
    }
    for (unsigned i=0;i<playback.sourceCount;++i) if (playback.sources[i].node>=0 && playback.bones[playback.sources[i].node].source==int(i)) {
        char from[128]{},to[256]{};
        WideCharToMultiByte(CP_UTF8,0,playback.sources[i].name,-1,from,sizeof(from),nullptr,nullptr);
        WideCharToMultiByte(CP_UTF8,0,playback.sources[i].target,-1,to,sizeof(to),nullptr,nullptr);
        report << "[MOTION_MAP] " << from << " -> " << to << '\n';
    }
    report.flush(); return playback.mapped && playback.writeBones;
}
bool MotionMainStep(unsigned mode) {
    if (!playback.armed || !MainThread()) return false;
    if (mode==6) { const auto passed=Stop(); FreeRoots(); return passed; }
    if (mode==8) return Stop();
    if (mode==9) return CaptureReference();
    if (mode!=7 || !OwnerIdentity() || !playback.referenceReady || playback.captured || !playback.mapped || !playback.writeBones) return false;
    const auto currentMesh=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(meshGetter.entry)(playback.owner[1],meshGetter.descriptor);
    if (currentMesh!=playback.referenceMeshObject||!Alive(currentMesh,playback.referenceMeshNative,referenceMeshClass)) return false;
    for (unsigned i=0;i<playback.boneCount;++i) if (playback.bones[i].identity.owned) {
        auto& bone=playback.bones[i];
        if (!BoneIdentity(bone) || !ReadBoneRotation(bone.identity.object,bone.identity.native,bone.restoreRotation)) return false;
    }
    for (auto& bone:playback.bones) bone.desiredWorld=bone.rest.rotation;
    SourcePose(0);
    if (!PrepareIK()) return false;
    playback.frames=0; playback.writes=0; playback.restoredBones=0; playback.fault=0; playback.sampleCount=0; playback.maxTickMicroseconds=0;
    playback.startTick=GetTickCount64(); playback.nextSample=playback.startTick; playback.captured=true; playback.restore=false;
    playback.ikWrites=0;
    status=1; enabled=true; return true;
}
void DisableMotionWrites() { enabled=false; }
unsigned MotionStatus() { return status.load(); }
void MotionLateTick() {
    __try { Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { playback.fault=GetExceptionCode(); enabled=false; status=3; }
}
void ReportMotion(std::ostream& report) {
    report << "[MOTION_REFERENCE] ready=" << playback.referenceReady << " bind_bones=" << playback.referenceBones << " failure_step=" << playback.referenceFailure
        << " basis=STATIC_MESH_BIND; live_pose=RESTORE_ONLY; rotation_isolation=ALL_BOUND_BONES_AFTER_LATEUPDATE; translation_isolation=NOT_IMPLEMENTED; full_A_pose_isolation=NOT_CONFIRMED\n";
    report << std::setprecision(9) << "[MOTION] armed=" << playback.armed << " status=" << status.load() << " captured=" << playback.captured
        << " restore=" << playback.restore << " fault=" << playback.fault << " mapped=" << playback.mapped << " write_bones=" << playback.writeBones
        << " frames=" << playback.frames << " writes=" << playback.writes << " restored_bones=" << playback.restoredBones
        << " GC_created=" << playback.rooted << " GC_freed=" << playback.freed << " max_tick_us=" << playback.maxTickMicroseconds << '\n';
    if (footIK) for (unsigned side=0;side<2;++side) {
        const auto& leg=playback.legs[side];
        report << "[FOOT_IK] side=" << side << " track=" << leg.track << " upper=" << leg.upper << " lower=" << leg.lower << " ticks=" << leg.ticks
            << " clamped=" << leg.clamped << " max_residual=" << leg.maxResidual << " units=" << ikUnits << " IK_quaternion_writes=" << playback.ikWrites
            << " failure_step=" << leg.failureStep << " last_upper=" << leg.lastUpper << " last_lower=" << leg.lastLower << " last_residual=" << leg.lastResidual << '\n';
    }
    for (unsigned i=0;i<playback.sampleCount;++i) {
        const auto& sample=playback.samples[i];
        report << "[MOTION_SAMPLE] index=" << i << " tid=" << sample.thread << " tick=" << sample.tick << " frame=" << sample.frame << " matched=" << sample.matched << " left=";
        Q(report,sample.left); report << " right="; Q(report,sample.right); report << " head="; Q(report,sample.head); report << '\n';
    }
    report.flush();
}
}
