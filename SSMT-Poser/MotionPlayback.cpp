#include "MotionPlayback.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <iomanip>
#include <ostream>

namespace poser {
namespace {
constexpr unsigned MaxRig=256,MaxSources=128,MaxSamples=64;
struct Bone {
    MotionRigBone identity;
    uint32_t handle=0;
    Quaternion base{0,0,0,1},firstWorld{0,0,0,1},desiredWorld{0,0,0,1};
    int source=-1,depth=0;
    bool write=false;
};
struct Source {
    wchar_t name[32]{},target[128]{};
    int parent=-1,track=-1,node=-1;
    Quaternion firstWorld{0,0,0,1},currentWorld{0,0,0,1};
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
};
Playback playback{};
RigCalls rigCalls;
vmd::Clip clip;
std::atomic<unsigned> status{0};
std::atomic<bool> enabled{false};
std::atomic<bool> configured{false};
uintptr_t activeEntry=0,activeDescriptor=0;
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
    if (source.node>=0 && source.track>=0) { playback.bones[source.node].source=index; ++playback.mapped; }
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
            playback.bones[playback.sources[shoulder].node].source=shoulder; ++playback.mapped;
        }
        const auto arm=AddSource(prefix+L"腕",target+L"UpperArm",shoulderC),twist=AddSource(prefix+L"腕捩",L"",arm),elbow=AddSource(prefix+L"ひじ",target+L"Forearm",twist);
        if (playback.sources[elbow].track<0) {
            playback.sources[elbow].track=Track(prefix+L"肘");
            if (playback.sources[elbow].track>=0 && playback.sources[elbow].node>=0) { playback.bones[playback.sources[elbow].node].source=elbow; ++playback.mapped; }
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
                    if (playback.sources[source].track>=0 && playback.sources[source].node>=0) { playback.bones[playback.sources[source].node].source=source; ++playback.mapped; }
                }
            }
        }
        const auto thigh=AddSource(prefix+L"足",target+L"Thigh",lower),knee=AddSource(prefix+L"ひざ",target+L"Calf",thigh),foot=AddSource(prefix+L"足首",target+L"Foot",knee);
        AddSource(prefix+L"つま先",target+L"Toe0",foot);
    }
    playback.writeBones=0;
    for (unsigned i=0;i<playback.boneCount;++i) if (playback.bones[i].source>=0) {
        auto current=int(i);
        for (unsigned depth=0;current>=0 && current!=playback.root && depth<32;++depth) {
            auto& bone=playback.bones[current];
            if (!bone.identity.owned) break;
            bone.write=true; current=bone.identity.parent;
        }
    }
    for (unsigned i=0;i<playback.boneCount;++i) if (playback.bones[i].write) ++playback.writeBones;
}
void SourcePose(double frame,bool first) {
    for (unsigned i=0;i<playback.sourceCount;++i) {
        auto& source=playback.sources[i];
        const auto local=source.track>=0?vmd::Sample(clip.tracks[source.track],frame):Quaternion{0,0,0,1};
        source.currentWorld=source.parent>=0?vmd::Multiply(playback.sources[source.parent].currentWorld,local):local;
        if (first) source.firstWorld=source.currentWorld;
    }
}
bool Stop() {
    enabled=false;
    bool restored=true;
    if (playback.captured) {
        restored=OwnerIdentity(false); playback.restoredBones=0;
        for (unsigned i=0;restored && i<playback.boneCount;++i) if (playback.bones[i].write) {
            const auto& bone=playback.bones[i]; Quaternion after{};
            restored=BoneIdentity(bone) && WriteBoneRotation(bone.identity.object,bone.base) && ReadBoneRotation(bone.identity.object,bone.identity.native,after) && bone_detail::Same(after,bone.base);
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
    SourcePose(frame,false);
    const auto blend=std::min(1.0f,float(tick-playback.startTick)/350.0f);
    unsigned matched=0;
    for (unsigned order=0;order<playback.boneCount;++order) {
        auto& bone=playback.bones[playback.order[order]];
        if (!bone.identity.owned) continue;
        const auto parent=bone.identity.parent;
        const auto parentWorld=parent>=0?playback.bones[parent].desiredWorld:Quaternion{0,0,0,1};
        if (int(playback.order[order])==playback.root) { bone.desiredWorld={0,0,0,1}; continue; }
        auto desiredLocal=bone.base;
        if (bone.source>=0) {
            const auto& source=playback.sources[bone.source];
            const auto delta=vmd::Multiply(source.currentWorld,vmd::Inverse(source.firstWorld));
            bone.desiredWorld=vmd::Multiply(delta,bone.firstWorld);
            desiredLocal=vmd::Multiply(vmd::Inverse(parentWorld),bone.desiredWorld);
        } else bone.desiredWorld=vmd::Multiply(parentWorld,bone.base);
        if (!bone.write) continue;
        desiredLocal=vmd::Slerp(bone.base,desiredLocal,blend);
        bone.desiredWorld=vmd::Multiply(parentWorld,desiredLocal);
        Quaternion after{};
        if (!BoneIdentity(bone) || !WriteBoneRotation(bone.identity.object,desiredLocal) || !ReadBoneRotation(bone.identity.object,bone.identity.native,after) || !bone_detail::Same(after,desiredLocal)) {
            playback.fault=2; enabled=false; status=3; return;
        }
        ++matched; ++playback.writes;
    }
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
bool IsMotionMode() { return configured.load(); }
bool MotionRigArmed() { return playback.armed; }
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
    clip=std::move(loaded); BuildSources();
    report << "motion_load=" << (playback.mapped && playback.writeBones?"READY":"REJECTED_UNMAPPED") << " mapped_tracks=" << playback.mapped
        << " write_bones=" << playback.writeBones << " retarget=FRAME0_RELATIVE_STANDARD_MMD_FK; positions=NOT_WRITTEN; foot_IK=NOT_SOLVED; morphs=NOT_APPLIED\n";
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
    if (mode!=7 || !OwnerIdentity() || playback.captured || !playback.mapped || !playback.writeBones) return false;
    for (unsigned i=0;i<playback.boneCount;++i) if (playback.bones[i].identity.owned) {
        auto& bone=playback.bones[i];
        if (!BoneIdentity(bone) || !ReadBoneRotation(bone.identity.object,bone.identity.native,bone.base)) return false;
    }
    for (unsigned order=0;order<playback.boneCount;++order) {
        auto& bone=playback.bones[playback.order[order]];
        if (!bone.identity.owned || int(playback.order[order])==playback.root) { bone.firstWorld={0,0,0,1}; bone.desiredWorld=bone.firstWorld; continue; }
        const auto parent=bone.identity.parent;
        bone.firstWorld=parent>=0?vmd::Multiply(playback.bones[parent].firstWorld,bone.base):bone.base;
        bone.desiredWorld=bone.firstWorld;
    }
    SourcePose(0,true);
    playback.frames=0; playback.writes=0; playback.restoredBones=0; playback.fault=0; playback.sampleCount=0; playback.maxTickMicroseconds=0;
    playback.startTick=GetTickCount64(); playback.nextSample=playback.startTick; playback.captured=true; playback.restore=false;
    status=1; enabled=true; return true;
}
void DisableMotionWrites() { enabled=false; }
unsigned MotionStatus() { return status.load(); }
void MotionLateTick() {
    __try { Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { playback.fault=GetExceptionCode(); enabled=false; status=3; }
}
void ReportMotion(std::ostream& report) {
    report << std::setprecision(9) << "[MOTION] armed=" << playback.armed << " status=" << status.load() << " captured=" << playback.captured
        << " restore=" << playback.restore << " fault=" << playback.fault << " mapped=" << playback.mapped << " write_bones=" << playback.writeBones
        << " frames=" << playback.frames << " writes=" << playback.writes << " restored_bones=" << playback.restoredBones
        << " GC_created=" << playback.rooted << " GC_freed=" << playback.freed << " max_tick_us=" << playback.maxTickMicroseconds << '\n';
    for (unsigned i=0;i<playback.sampleCount;++i) {
        const auto& sample=playback.samples[i];
        report << "[MOTION_SAMPLE] index=" << i << " tid=" << sample.thread << " tick=" << sample.tick << " frame=" << sample.frame << " matched=" << sample.matched << " left=";
        Q(report,sample.left); report << " right="; Q(report,sample.right); report << " head="; Q(report,sample.head); report << '\n';
    }
    report.flush();
}
}
