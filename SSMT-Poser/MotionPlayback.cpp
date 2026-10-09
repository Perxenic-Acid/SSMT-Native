#include "MotionPlayback.h"
#include "LiveUnityProbe.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <iomanip>
#include <ostream>
#include "LegIK.h"
#include "ReferencePose.h"
#include "UiBridge.h"
#include "BasisRetarget.h"
#include "AvatarReference.h"
#include <fstream>
#include <sstream>

namespace poser {
namespace {
constexpr unsigned MaxRig=256,MaxSources=128,MaxSamples=64,MaxSnapshots=16;
constexpr unsigned MaxReferenceMeshes=64;
struct ReferenceMesh {
    uintptr_t renderer=0,native=0,transform=0,transformNative=0,array=0,mesh=0,meshNative=0;
    uint32_t handles[3]{};
    unsigned count=0,failure=0;
    bool valid=false;
    wchar_t name[128]{},meshName[128]{};
    int nodes[MaxRig]{};
    wchar_t boneNames[MaxRig][128]{};
    reference::Matrix matrices[MaxRig]{};
};
struct Bone {
    MotionRigBone identity;
    uint32_t handle=0;
    Quaternion restoreRotation{0,0,0,1},desiredWorld{0,0,0,1};
    reference::Pose rest;
    Quaternion bindLocal{0,0,0,1},basisLocal{0,0,0,1},lastInput{0,0,0,1},lastWritten{0,0,0,1};
    int source=-1,depth=0;
    bool write=false,hasReference=false;
    bool parentReference=false;
    bool upperWrite=false,touched=false;
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
struct ABSnapshot {
    bool valid=false;basis::Settings settings;double frame=0,lastFrame=0;unsigned frames=0,writes=0;
    Quaternion auxiliaryBefore{0,0,0,1};bool auxiliaryCaptured=false;
    Quaternion rootWorld{0,0,0,1},rotations[MaxRig]{},world[MaxRig]{},parentWorld[MaxRig]{};
    Vector3 positions[MaxRig]{};bool written[MaxRig]{};
};
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
    bool abReportPending=false;
    double lastFrame=0;
    ABSnapshot abSnapshots[MaxSnapshots];
    Sample samples[MaxSamples]{};
    Leg legs[2];
    unsigned ikWrites=0;
    unsigned referenceBones=0,referenceFailure=0;
    bool referenceReady=false;
    uint32_t referenceRoots[2]{};
    uintptr_t referenceMeshObject=0,referenceMeshNative=0;
    unsigned meshCount=0,rendererCount=0,rendererScan=0;
    bool inventoryComplete=false;
    uintptr_t referenceAvatar=0,referenceAvatarNative=0;
    uint32_t avatarHandle=0;
    wchar_t avatarName[128]{};
    uint32_t inventoryRoots[2]{};
    uint32_t nameHandle=0;
    bool inventoryInactive=false;
    unsigned avatarCount=0,avatarCompared=0,avatarFailure=0;
    bool avatarProfile=false,avatarReady=false;
    float avatarMaxAngle=0,avatarMaxPosition=0;
    int pelvisAuxiliary=-1;bool auxiliaryRejected=false;
};
Playback playback{};
ReferenceMesh referenceMeshes[MaxReferenceMeshes]{};
struct AvatarScratch {
    avatar_reference::Node nodes[avatar_reference::Limit];
    avatar_reference::Local locals[avatar_reference::Limit];
    uint32_t names[avatar_reference::Limit],paths[avatar_reference::Limit];
    reference::Pose model[avatar_reference::Limit];
    int sourceNode[MaxRig];
    uint8_t required[avatar_reference::Limit];
};
AvatarScratch avatarScratch{};
int Named(std::wstring_view name);
RigCalls rigCalls;
vmd::Clip clip;
std::atomic<unsigned> status{0};
std::atomic<bool> enabled{false};
std::atomic<bool> configured{false};
std::atomic<float> progressSeconds{0};
bool footIK=false;
float ikUnits=0;
basis::Settings abSettings;
std::filesystem::path abOutput;
std::atomic<unsigned> abStage{UINT_MAX};
uintptr_t activeEntry=0,activeDescriptor=0;
NativeMethod meshGetter,bindGetter,avatarGetter,componentsGetter;
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
    if(playback.nameHandle){reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(playback.nameHandle);playback.nameHandle=0;++playback.freed;}
    for(auto& handle:playback.inventoryRoots)if(handle){reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(handle);handle=0;++playback.freed;}
    if(playback.avatarHandle){reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(playback.avatarHandle);playback.avatarHandle=0;++playback.freed;}
    for (auto& mesh:referenceMeshes) for (auto& handle:mesh.handles) if (handle) {
        reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(handle);handle=0;++playback.freed;
    }
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
bool CopyName(uintptr_t object,wchar_t (&out)[128]) {
    const auto value=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(rigCalls.name)(object,rigCalls.nameMethod);
    uintptr_t klass=0,name=0;char className[7]{};int length=0;
    if(!Read(value,klass)||!Read(klass+0x28,name))return false;
    SIZE_T classBytes=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(name),className,sizeof(className),&classBytes)||classBytes!=sizeof(className)||std::memcmp(className,"String",7))return false;
    playback.nameHandle=Root(value);if(!playback.nameHandle)return false;
    const bool valid=Read(value+0x10,length)&&length>=0&&length<128;
    SIZE_T copied=0;out[valid?length:0]=0;
    const bool passed=valid&&(!length||(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(value+0x14),out,size_t(length)*2,&copied)&&copied==size_t(length)*2));
    reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(playback.nameHandle);playback.nameHandle=0;++playback.freed;
    return passed;
}
// 只判断已核验 native Transform parent 链的归属，不用当前旋转推导静态参考。
bool OwnedTransform(uintptr_t object,uintptr_t native) {
    if (!Alive(object,native,rigCalls.transformClass)) return false;
    uintptr_t seen[32]{};
    for (unsigned depth=0;native&&depth<32;++depth) {
        if (native==playback.ownerNative[0]) return true;
        for (unsigned i=0;i<depth;++i) if (seen[i]==native) return false;
        seen[depth]=native;
        if (!Read(native+0xA0,native)) return false;
    }
    return false;
}
bool CaptureActorRenderers() {
    if(!componentsGetter.entry){playback.inventoryComplete=false;return false;}
    const auto type=reinterpret_cast<uintptr_t (*)(uintptr_t)>(rigCalls.context.reflectionType)(rigCalls.rendererClass+0x68);
    auto& typeRoot=playback.inventoryRoots[0];auto& arrayRoot=playback.inventoryRoots[1];
    typeRoot=Root(type);if(!typeRoot)return false;
    // 当前 wrapper 的 native consumer 已核验 6 个参数及 includeInactive 位。
    const auto array=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t,bool,bool,bool,bool,uintptr_t,uintptr_t)>(componentsGetter.entry)
        (playback.owner[3],type,true,true,true,false,0,componentsGetter.descriptor);
    arrayRoot=Root(array);uint64_t count=0;
    bool passed=arrayRoot&&live_detail::ValidateArray(array,rigCalls.rendererClass,count);
    if(passed) {
        for(auto& item:referenceMeshes)for(auto& handle:item.handles)if(handle){reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(handle);handle=0;++playback.freed;}
        std::memset(referenceMeshes,0,sizeof(referenceMeshes));playback.meshCount=0;playback.rendererScan=0;
        playback.inventoryInactive=true;passed=CaptureMotionRenderers(array,count);
    }
    if(arrayRoot){reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(arrayRoot);arrayRoot=0;++playback.freed;}
    reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(typeRoot);typeRoot=0;++playback.freed;
    if(!passed)playback.inventoryComplete=false;
    return passed;
}
void CaptureReferenceInventory() {
    CaptureActorRenderers();
    if(avatarGetter.entry&&!playback.avatarHandle) {
        const auto object=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(avatarGetter.entry)(playback.owner[2],avatarGetter.descriptor);
        uintptr_t native=0;
        if(object&&Read(object+0x10,native)&&native&&(playback.avatarHandle=Root(object))) {
            playback.referenceAvatar=object;playback.referenceAvatarNative=native;CopyName(object,playback.avatarName);
        }
    }
    for (unsigned m=0;m<playback.meshCount;++m) {
        auto& item=referenceMeshes[m];item.failure=1;
        if (!Alive(item.renderer,item.native,rigCalls.rendererClass)||!OwnedTransform(item.transform,item.transformNative)) continue;
        item.mesh=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(meshGetter.entry)(item.renderer,meshGetter.descriptor);
        if (!Read(item.mesh+0x10,item.meshNative)||!Alive(item.mesh,item.meshNative,referenceMeshClass)) continue;
        auto& meshRoot=playback.referenceRoots[0];auto& arrayRoot=playback.referenceRoots[1];
        meshRoot=Root(item.mesh);if (!meshRoot) continue;
        item.failure=2;
        const auto array=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(bindGetter.entry)(item.mesh,bindGetter.descriptor);
        bool passed=array&&(arrayRoot=Root(array));
        uintptr_t klass=0,element=0,bounds=1,data=0,values=0;uint8_t kind=0,elementKind=0;uint64_t length=0,count=0;
        passed=passed&&Read(array,klass)&&Read(klass+0x72,kind)&&kind==0x1D&&Read(klass+0x68,element)&&Read(element+0xA,elementKind)&&elementKind==0x11&&
            Read(array+0x10,bounds)&&!bounds&&Read(array+0x18,length)&&length==item.count&&
            Read(item.meshNative+0x48,data)&&Read(data+0xD0,values)&&Read(data+0xE0,count)&&count==length&&CopyName(item.mesh,item.meshName);
        item.failure=3;
        for (unsigned b=0;passed&&b<item.count;++b) {
            reference::Matrix original{};reference::Pose decoded{};uintptr_t object=0,native=0;
            passed=Read(item.array+0x20+size_t(b)*8,object)&&Read(object+0x10,native)&&OwnedTransform(object,native)&&
                CopyName(object,item.boneNames[b])&&Read(array+0x20+size_t(b)*64,item.matrices[b])&&Read(values+size_t(b)*64,original)&&
                std::memcmp(&original,&item.matrices[b],sizeof(original))==0&&reference::Decode(original,decoded);
            item.nodes[b]=-1;
            for (unsigned i=0;passed&&i<playback.boneCount;++i) if (playback.bones[i].identity.owned&&playback.bones[i].identity.object==object) {
                passed=BoneIdentity(playback.bones[i]);item.nodes[b]=int(i);break;
            }
        }
        item.valid=passed;if (passed) item.failure=0;
        if (arrayRoot) {reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(arrayRoot);arrayRoot=0;++playback.freed;}
        reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(meshRoot);meshRoot=0;++playback.freed;
    }
}
bool RelativeData(uintptr_t field,uintptr_t& out) {
    int64_t offset=0;if(!Read(field,offset)||!offset)return false;
    if(offset>0&&uint64_t(offset)>UINTPTR_MAX-field)return false;
    if(offset<0&&uint64_t(-(offset+1))+1>field)return false;
    out=field+offset;return true;
}
bool ReadBytes(uintptr_t address,void* output,size_t bytes) {
    SIZE_T count=0;return address&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,bytes,&count)&&count==bytes;
}
bool AvatarProfile(std::ostream& report) {
    const auto image=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!Read(image,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||!Read(image+dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE)return false;
    uintptr_t anchor=0;unsigned matches=0;
    for(unsigned s=0;s<nt.FileHeader.NumberOfSections;++s) {
        IMAGE_SECTION_HEADER section{};
        if(!Read(image+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader+s*sizeof(section),section))return false;
        if(!(section.Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;
        constexpr size_t block=1024*1024,overlap=256;
        if(section.VirtualAddress>nt.OptionalHeader.SizeOfImage||section.Misc.VirtualSize>nt.OptionalHeader.SizeOfImage-section.VirtualAddress)return false;
        for(size_t offset=0;offset<section.Misc.VirtualSize;offset+=block) {
            std::vector<uint8_t> bytes(std::min(block+overlap,size_t(section.Misc.VirtualSize)-offset));
            const auto start=image+section.VirtualAddress+offset;if(!ReadBytes(start,bytes.data(),bytes.size()))return false;
            for(const auto found:native_detail::FindPattern(bytes,"48 8B 5E 78 48 85 DB 0F 85 ?? ?? ?? ?? 48 8B 5F 08 8B 96 90 00 00 00"))
                if(found<block){anchor=start+found;++matches;}
        }
    }
    if(matches!=1){report<<"avatar_profile_blocker=SERIALIZER_NOT_UNIQUE matches="<<matches<<'\n';return false;}
    auto target=[](uintptr_t at,uintptr_t& out){uint8_t opcode=0;int32_t displacement=0;if(!Read(at,opcode)||opcode!=0xE8||!Read(at+1,displacement))return false;out=native_detail::RelativeTarget(at,displacement,5);return true;};
    auto literal=[](uintptr_t at,const char* expected){int32_t displacement=0;if(!Read(at+3,displacement))return false;const auto address=native_detail::RelativeTarget(at,displacement,7);
        for(size_t i=0;i<=std::strlen(expected);++i){char actual=0;if(!Read(address+i,actual)||actual!=expected[i])return false;}return true;};
    uintptr_t serializer=0,firstPose=0,secondPose=0;
    uint8_t avatarName[3]{},defaultMember[4]{};
    const bool passed=ReadBytes(anchor+0xB5,avatarName,3)&&std::memcmp(avatarName,"\x48\x8d\x15",3)==0&&literal(anchor+0xB5,"m_Avatar")&&
        target(anchor+0xD0,serializer)&&ReadBytes(serializer+0x55,defaultMember,4)&&std::memcmp(defaultMember,"\x48\x8d\x56\x10",4)==0&&
        literal(serializer+0x46,"m_AvatarSkeletonPose")&&literal(serializer+0x5C,"m_DefaultPose")&&
        target(serializer+0x50,firstPose)&&target(serializer+0x66,secondPose)&&firstPose==secondPose;
    report<<"avatar_profile="<<(passed?"SERIALIZER_FIELDS_AND_DEFAULT_POSE_VERIFIED":"REJECTED_FIELD_SEMANTICS")<<"; native_field=0x78; sample_RVA=NOT_USED\n";return passed;
}
bool CaptureAvatarReference() {
    playback.avatarReady=false;playback.avatarFailure=1;playback.avatarCount=0;playback.avatarCompared=0;
    if(!playback.avatarProfile||!playback.avatarHandle||!OwnerIdentity()||
        reinterpret_cast<uintptr_t (*)(uint32_t)>(rigCalls.context.handleTarget)(playback.avatarHandle)!=playback.referenceAvatar)return false;
    uintptr_t native=0,constant=0,skeleton=0,pose=0,names=0,nodes=0,paths=0,locals=0;unsigned count=0,poseCount=0,nameCount=0;
    if(!Read(playback.referenceAvatar+0x10,native)||native!=playback.referenceAvatarNative||!Read(native+0x78,constant)||
        !RelativeData(constant,skeleton)||!RelativeData(constant+0x10,pose)||!Read(skeleton,count)||!count||count>avatar_reference::Limit||
        !Read(pose,poseCount)||poseCount!=count||!Read(constant+0x18,nameCount)||nameCount!=count||
        !RelativeData(skeleton+8,nodes)||!RelativeData(skeleton+0x10,paths)||!RelativeData(pose+8,locals)||!RelativeData(constant+0x20,names))return false;
    playback.avatarCount=count;playback.avatarFailure=2;
    if(!ReadBytes(nodes,avatarScratch.nodes,count*sizeof(avatarScratch.nodes[0]))||!ReadBytes(locals,avatarScratch.locals,count*sizeof(avatarScratch.locals[0]))||
        !ReadBytes(names,avatarScratch.names,count*4)||!ReadBytes(paths,avatarScratch.paths,count*4))return false;
    // 空路径资产根由 Body/Bip001 的真实共同父 Actor 锚定；它不是根据角色名称猜出的骨映射。
    if(avatarScratch.nodes[0].parent!=-1||avatarScratch.names[0]||avatarScratch.paths[0])return false;
    std::fill(std::begin(avatarScratch.sourceNode),std::end(avatarScratch.sourceNode),-1);
    avatarScratch.sourceNode[playback.root]=0;playback.avatarFailure=3;
    for(unsigned i=0;i<playback.boneCount;++i) {
        const auto& bone=playback.bones[i];if(!bone.identity.owned||int(i)==playback.root)continue;
        char name[512]{};if(!WideCharToMultiByte(CP_UTF8,0,bone.identity.name,-1,name,sizeof(name),nullptr,nullptr))return false;
        const auto hash=avatar_reference::Hash(name);int match=-1;
        for(unsigned n=1;n<count;++n)if(avatarScratch.names[n]==hash){if(match>=0)return false;match=int(n);}
        avatarScratch.sourceNode[i]=match;
        if(bone.hasReference&&match<0)return false;
    }
    const auto bip=Named(L"Bip001"),pelvis=Named(L"Bip001 Pelvis"),body=Named(L"Body");
    if(bip<0||pelvis<0||body<0||avatarScratch.sourceNode[bip]<0||avatarScratch.sourceNode[pelvis]<0||avatarScratch.sourceNode[body]<0||
        playback.bones[bip].identity.parent!=playback.root||playback.bones[body].identity.parent!=playback.root)return false;
    std::memset(avatarScratch.required,0,sizeof(avatarScratch.required));
    for(unsigned i=0;i<playback.boneCount;++i) {
        auto node=avatarScratch.sourceNode[i];unsigned depth=0;
        while(node>=0&&depth++<32) {if(unsigned(node)>=count)return false;avatarScratch.required[node]=1;node=avatarScratch.nodes[node].parent;}
        if(node>=0)return false;
    }
    // 未映射的表情分支可能含负 scale；只对本次 rig 及其完整祖先闭包核验 TRS。
    if(!avatar_reference::Compose({avatarScratch.nodes,count},{avatarScratch.locals,count},{avatarScratch.model,count},{avatarScratch.required,count})||
        basis::Angle(avatarScratch.model[0].rotation,{0,0,0,1})>0.001f||ik::Length(avatarScratch.model[0].position)>0.00001f)return false;
    const auto& modelBody=avatarScratch.model[avatarScratch.sourceNode[body]];
    if(basis::Angle(modelBody.rotation,{0,0,0,1})>0.001f||ik::Length(modelBody.position)>0.00001f)return false;
    playback.avatarFailure=4;
    for(unsigned i=0;i<playback.boneCount;++i) {
        const auto& bone=playback.bones[i];const auto source=avatarScratch.sourceNode[i];if(source<0||int(i)==playback.root)continue;
        if(!BoneIdentity(bone)||bone.identity.parent<0||avatarScratch.sourceNode[bone.identity.parent]!=avatarScratch.nodes[source].parent)return false;
        // 除名称 CRC 外再核验完整路径 CRC，拒绝重名或同名错层级。
        int chain[32]{};unsigned depth=0;auto ancestor=int(i);
        while(ancestor!=playback.root&&ancestor>=0&&depth<32){chain[depth++]=ancestor;ancestor=playback.bones[ancestor].identity.parent;}
        if(ancestor!=playback.root)return false;
        char path[4096]{};size_t length=0;
        while(depth) {
            const auto& part=playback.bones[chain[--depth]].identity.name;
            if(length)path[length++]='/';
            const auto bytes=WideCharToMultiByte(CP_UTF8,0,part,-1,path+length,int(sizeof(path)-length),nullptr,nullptr);
            if(!bytes)return false;length+=size_t(bytes-1);
        }
        if(avatar_reference::Hash(std::string_view(path,length))!=avatarScratch.paths[source])return false;
        if(!bone.hasReference)continue;
        const auto& candidate=avatarScratch.model[source];const auto angle=basis::Angle(bone.rest.rotation,candidate.rotation);
        const auto distance=ik::Length(ik::Sub(bone.rest.position,candidate.position));
        playback.avatarMaxAngle=std::max(playback.avatarMaxAngle,angle);playback.avatarMaxPosition=std::max(playback.avatarMaxPosition,distance);
        if(angle>0.01f||distance>0.0001f)return false;++playback.avatarCompared;
    }
    if(playback.avatarCompared!=playback.referenceBones)return false;
    // 再读关键源数据，拒绝捕获期间 Avatar 资源切换；所有参考以值保存。
    avatar_reference::Local second[avatar_reference::Limit];uintptr_t current=0;
    if(!Read(native+0x78,current)||current!=constant||!ReadBytes(locals,second,count*sizeof(second[0]))||std::memcmp(second,avatarScratch.locals,count*sizeof(second[0]))!=0)return false;
    for(const auto index:{bip,pelvis}){playback.bones[index].rest=avatarScratch.model[avatarScratch.sourceNode[index]];playback.bones[index].hasReference=true;++playback.referenceBones;}
    playback.avatarReady=true;playback.avatarFailure=0;return true;
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
    playback.sourceCount=0; playback.mapped=0;playback.pelvisAuxiliary=-1;playback.auxiliaryRejected=false;
    for (auto& bone:playback.bones) { bone.source=-1; bone.write=false;bone.upperWrite=false; }
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
    if (abSettings.enabled) {
        playback.mapped=0;
        for (auto& bone:playback.bones) {bone.write=false;bone.source=-1;bone.parentReference=false;}
        for (unsigned i=0;i<playback.sourceCount;++i) {
            const auto& source=playback.sources[i];
            if ((!basis::MainFk(source.name)&&!(basis::FkTest(abSettings)&&basis::LegsFk(source.name)))||source.node<0) continue;
            auto& bone=playback.bones[source.node];if (!bone.hasReference) continue;
            bone.source=int(i);bone.write=true;++playback.mapped;
            const bool upperScope=basis::MainFk(source.name)&&std::wstring_view(source.name)!=L"下半身";
            bone.upperWrite=upperScope;
            // 仅静态锁定所选 FK 链中的真实中间骨；腿部只在完整 FK 对照中接管。
            for (auto parent=bone.identity.parent;parent>=0;parent=playback.bones[parent].identity.parent)
                if (playback.bones[parent].hasReference) {
                    auto& ancestor=playback.bones[parent];ancestor.write=true;
                    if(upperScope&&std::wstring_view(ancestor.identity.name)!=L"Bip001 Pelvis"&&std::wstring_view(ancestor.identity.name)!=L"Bip001")ancestor.upperWrite=true;
                }
        }
        if(basis::FkTest(abSettings)) {
            const auto helper=Named(L"+PelvisTwist CF A01"),pelvis=Named(L"Bip001 Pelvis"),bip=Named(L"Bip001");
            if(helper!=-1) {
                // 此蒙皮骨与 Pelvis 并列挂在 Bip001 下，真实父链不会自动继承 Pelvis 的转身。
                // 仅扩展现有下半身 FK 映射；不猜测原生 twist 约束、Grant 或物理混合权重。
                const bool valid=helper>=0&&pelvis>=0&&bip>=0&&playback.bones[helper].identity.owned&&playback.bones[helper].hasReference&&
                    playback.bones[helper].identity.parent==bip&&playback.bones[pelvis].identity.parent==bip&&playback.bones[pelvis].source>=0;
                playback.auxiliaryRejected=!valid;
                if(valid) {
                    auto& bone=playback.bones[helper];bone.source=playback.bones[pelvis].source;bone.write=true;bone.upperWrite=false;
                    playback.pelvisAuxiliary=helper;++playback.mapped;
                }
            }
        }
        for (auto& bone:playback.bones) if (bone.write) {
            const auto parent=bone.identity.parent;
            bone.parentReference=parent>=0&&playback.bones[parent].hasReference;
            bone.basisLocal=vmd::Inverse(bone.rest.rotation);
            if (bone.parentReference) bone.bindLocal=basis::BindLocal(playback.bones[parent].rest.rotation,bone.rest.rotation);
        }
    }
    for (unsigned side=0;side<2;++side) {
        auto& leg=playback.legs[side];leg={};
        if (!footIK||abSettings.enabled) continue;
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
        Quaternion bodyRotation{};Vector3 bodyPosition{},bodyScale{};playback.referenceFailure=4;
        passed=passed&&bodyIndex>=0&&playback.bones[bodyIndex].identity.parent==playback.root&&BoneIdentity(playback.bones[bodyIndex])&&
            ReadBoneRotation(body,playback.bones[bodyIndex].identity.native,bodyRotation)&&bone_detail::Same(bodyRotation,{0,0,0,1})&&
            ReadBonePosition(body,playback.bones[bodyIndex].identity.native,bodyPosition,false)&&ik::Length(bodyPosition)<0.0001f&&
            ReadBoneScale(body,playback.bones[bodyIndex].identity.native,bodyScale)&&ik::Length(ik::Sub(bodyScale,{1,1,1}))<0.0001f;
        if (passed) {playback.referenceMeshObject=mesh;playback.referenceMeshNative=native;playback.referenceBones=expected;playback.referenceReady=true;playback.referenceFailure=0;}
    }
    if (arrayRoot) {reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(arrayRoot);arrayRoot=0;++playback.freed;}
    reinterpret_cast<void (*)(uint32_t)>(rigCalls.context.handleFree)(meshRoot);meshRoot=0;++playback.freed;
    if (!passed) for (auto& bone:playback.bones) bone.hasReference=false;
    if (passed) {
        CaptureReferenceInventory();
        if(basis::FkTest(abSettings))passed=CaptureAvatarReference();
    }
    if(!passed)playback.referenceReady=false;
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
    if (!footIK||abSettings.enabled) return true;
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
    if (!footIK||abSettings.enabled) return true;
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
void SourcePose(double frame,const basis::Settings& settings) {
    for (unsigned i=0;i<playback.sourceCount;++i) {
        auto& source=playback.sources[i];
        const auto sampled=source.track>=0&&(!settings.enabled||basis::MainFk(source.name)||(basis::FkTest(settings)&&basis::LegsFk(source.name)))?vmd::Sample(clip.tracks[source.track],frame):Quaternion{0,0,0,1};
        const auto local=basis::Input(settings,source.name,sampled);
        source.currentWorld=source.parent>=0?vmd::Multiply(playback.sources[source.parent].currentWorld,local):local;
    }
}
bool Stop() {
    enabled=false;
    bool restored=true;
    if (playback.captured) {
        restored=OwnerIdentity(false); playback.restoredBones=0;
        for (unsigned i=0;restored && i<playback.boneCount;++i) if (playback.bones[i].touched) {
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
    const auto stage=basis::FkTest(abSettings)?basis::FkStage(tick-playback.startTick,abSettings.pose):abSettings.enabled&&abSettings.pose==basis::Pose::Sequence?basis::Stage(tick-playback.startTick):0;
    const auto settings=basis::FkTest(abSettings)?basis::FkStageSettings(stage,abSettings.pose):abSettings.enabled&&abSettings.pose==basis::Pose::Sequence?basis::StageSettings(stage):abSettings;
    // 连续预览只在首次跨过 0/30/120 时各采样一帧精确参考，随后立即继续时间轴。
    const double frame=basis::SampleFrame(settings,tick-playback.startTick,!playback.abSnapshots[stage].valid);
    progressSeconds.store(float(frame/30.0),std::memory_order_relaxed);
    if (frame>clip.lastFrame) { enabled=false; status=2; return; }
    LARGE_INTEGER begin{},end{}; QueryPerformanceCounter(&begin);
    if(!settings.enabled||settings.route==basis::Route::Legacy)SourcePose(frame,settings);
    const auto blend=abSettings.enabled?1.0f:std::min(1.0f,float(tick-playback.startTick)/350.0f);
    Quaternion rootWorld{};
    if (!WorldRotation(playback.root,rootWorld)) {playback.fault=5;enabled=false;status=3;return;}
    if(basis::FkTest(settings)&&settings.fullFK&&playback.pelvisAuxiliary>=0&&!playback.abSnapshots[stage].valid) {
        auto& snapshot=playback.abSnapshots[stage];const auto& bone=playback.bones[playback.pelvisAuxiliary];
        if(!BoneIdentity(bone)||!ReadBoneRotation(bone.identity.object,bone.identity.native,snapshot.auxiliaryBefore)) {playback.fault=6;enabled=false;status=3;return;}
        snapshot.auxiliaryCaptured=true;
    }
    unsigned matched=0;
    for (unsigned order=0;order<playback.boneCount;++order) {
        auto& bone=playback.bones[playback.order[order]];
        if (!bone.write || !bone.hasReference || (basis::FkTest(settings)&&!settings.fullFK&&!bone.upperWrite)) continue;
        const auto parent=bone.identity.parent;
        auto desired=bone.rest.rotation;
        if (bone.source>=0&&(!settings.enabled||settings.route==basis::Route::Legacy)) {
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
        auto desiredLocal=vmd::Multiply(vmd::Inverse(actualParent),vmd::Multiply(rootWorld,bone.desiredWorld));
        if (abSettings.enabled) {
            bone.lastInput=bone.source>=0?basis::Input(settings,playback.sources[bone.source].name,
                playback.sources[bone.source].track>=0?vmd::Sample(clip.tracks[playback.sources[bone.source].track],frame):Quaternion{0,0,0,1}):Quaternion{0,0,0,1};
            if (settings.route==basis::Route::PerBone) {
                const auto delta=vmd::Multiply(bone.basisLocal,vmd::Multiply(bone.lastInput,bone.rest.rotation));
                // 有 parent bind 的骨直接设置局部增量，父子累积只交给 Unity hierarchy。
                if (bone.parentReference) desiredLocal=vmd::Multiply(bone.bindLocal,delta);
                else {
                    // 静态父骨缺失时仅锚定模型空间；不能声称已知该骨的 bind local。
                    desiredLocal=vmd::Multiply(vmd::Inverse(actualParent),vmd::Multiply(rootWorld,vmd::Multiply(bone.rest.rotation,delta)));
                }
            }
        }
        Quaternion after{};
        bone.touched=true;
        if (!BoneIdentity(bone) || !WriteBoneRotation(bone.identity.object,desiredLocal) || !ReadBoneRotation(bone.identity.object,bone.identity.native,after) || !bone_detail::Same(after,desiredLocal)) {
            playback.fault=2; enabled=false; status=3; return;
        }
        ++matched; ++playback.writes;
        if (abSettings.enabled) bone.lastWritten=after;
    }
    if (!SolveIK(frame,blend)) {playback.fault=4;enabled=false;status=3;return;}
    playback.lastFrame=frame;++playback.frames; QueryPerformanceCounter(&end);
    if(abSettings.enabled) {
        auto& snapshot=playback.abSnapshots[stage];const bool first=!snapshot.valid;
        snapshot.settings=settings;snapshot.lastFrame=frame;
        // 连续预览也只保存首次完整写入的旋转；不能用结束帧给这份快照重新贴标签。
        if(first||settings.pose!=basis::Pose::FkDance)snapshot.frame=frame;
        ++snapshot.frames;snapshot.writes+=matched;
        if(basis::FkTest(settings)&&first) {
            snapshot.rootWorld=rootWorld;
            for(unsigned i=0;i<playback.boneCount;++i)if(playback.bones[i].write) {
                auto& bone=playback.bones[i];snapshot.written[i]=settings.fullFK||bone.upperWrite;
                if(!BoneIdentity(bone)||!ReadBoneRotation(bone.identity.object,bone.identity.native,snapshot.rotations[i])||
                    !WorldRotation(int(i),snapshot.world[i])||!WorldRotation(bone.identity.parent,snapshot.parentWorld[i])||!Position(int(i),snapshot.positions[i])) {
                    playback.fault=6;enabled=false;status=3;return;
                }
            }
        } else if(!basis::FkTest(settings))for(unsigned i=0;i<playback.boneCount;++i)if(playback.bones[i].write)snapshot.rotations[i]=playback.bones[i].lastWritten;
        snapshot.valid=true;
        abStage.store(stage,std::memory_order_release);
    }
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
bool ConfigureMotionAB(const std::filesystem::path& directory,std::ostream& report) {
    if (enabled.load()||playback.captured) return false;
    abOutput=directory;basis::Settings next;
    std::ifstream input(directory/L"motion_ab.txt");
    if (input) {
        std::string route,pose,extra;input>>route>>pose;
        next.enabled=true;
        if (route=="legacy") next.route=basis::Route::Legacy;
        else if (route=="basis") next.route=basis::Route::PerBone;
        else {report<<"AB_config=REJECTED_ROUTE\n";return false;}
        if (pose=="identity") next.pose=basis::Pose::Identity;
        else if (pose=="x") next.pose=basis::Pose::X;
        else if (pose=="y") next.pose=basis::Pose::Y;
        else if (pose=="z") next.pose=basis::Pose::Z;
        else if (pose=="dance") next.pose=basis::Pose::Dance;
        else if (pose=="sequence") next.pose=basis::Pose::Sequence;
        else if (route=="legacy"&&pose=="fk_sequence") next.pose=basis::Pose::FkSequence;
        else if (route=="legacy"&&pose=="fk_axes") next.pose=basis::Pose::FkAxes;
        else if (route=="legacy"&&pose=="fk_dance") {next.pose=basis::Pose::FkDance;next.fullFK=true;}
        else if (pose=="frame"&&input>>next.frame&&std::isfinite(next.frame)&&next.frame>=0&&next.frame<=1000000) next.pose=basis::Pose::Frame;
        else {report<<"AB_config=REJECTED_POSE\n";return false;}
        if (input>>extra) {report<<"AB_config=REJECTED_TRAILING_DATA\n";return false;}
    }
    abSettings=next;
    if (!clip.tracks.empty()&&playback.referenceReady) BuildSources();
    report<<"[MMD_TOOLS_BASIS_AB_TEST] enabled="<<next.enabled<<" route="<<basis::RouteName(next.route)<<" pose="<<basis::PoseName(next.pose)<<" frame="<<next.frame
        <<" axes=MMD_XYZ_TO_UNITY_MODEL_XYZ; axis_alignment=EXPERIMENT_ASSUMPTION; IK="<<(next.enabled?"OFF":footIK?"CONFIGURED":"OFF")<<"; source_frame0=PRESERVED\n";
    report.flush();return true;
}
bool SelectMotionAB(unsigned command,std::ostream& report) {
    if (enabled.load()||playback.captured||abOutput.empty()) return false;
    if(basis::FkTest(abSettings)){report<<"FK_experiment=FIXED_LEGACY_SCOPE; mode_switch=IGNORED\n";return false;}
    auto next=abSettings;next.enabled=true;
    if (command==6) next.route=basis::Route::Legacy;
    else if (command==7) next.route=basis::Route::PerBone;
    else if (command==8) {
        if (!abSettings.enabled) next.pose=basis::Pose::Identity;
        else switch (next.pose) {
            case basis::Pose::Identity:next.pose=basis::Pose::X;break;
            case basis::Pose::X:next.pose=basis::Pose::Y;break;
            case basis::Pose::Y:next.pose=basis::Pose::Z;break;
            case basis::Pose::Z:next.pose=basis::Pose::Frame;next.frame=0;break;
            case basis::Pose::Frame:if(next.frame==0)next.frame=30;else if(next.frame==30)next.frame=120;else{next.pose=basis::Pose::Identity;next.frame=0;}break;
            default:next.pose=basis::Pose::Identity;next.frame=0;break;
        }
    } else return false;
    std::ofstream config(abOutput/L"motion_ab.txt",std::ios::trunc);
    config<<(next.route==basis::Route::Legacy?"legacy":"basis")<<' ';
    const char* pose=next.pose==basis::Pose::Identity?"identity":next.pose==basis::Pose::X?"x":next.pose==basis::Pose::Y?"y":next.pose==basis::Pose::Z?"z":next.pose==basis::Pose::Frame?"frame":next.pose==basis::Pose::Sequence?"sequence":"dance";
    config<<pose;if(next.pose==basis::Pose::Frame)config<<' '<<next.frame;
    config.close();if(!config)return false;
    return ConfigureMotionAB(abOutput,report);
}
void ReportMotionFK(std::ostream& report) {
    auto jq=[](std::ostream& out,Quaternion q){out<<'['<<q.x<<','<<q.y<<','<<q.z<<','<<q.w<<']';};
    for(unsigned stage=0;stage<MaxSnapshots;++stage) {
        const auto& snapshot=playback.abSnapshots[stage];if(!snapshot.valid)continue;
        SourcePose(snapshot.frame,snapshot.settings);Quaternion desired[MaxRig]{};
        std::ostringstream json;json<<std::setprecision(9)<<"{\"route\":\"Legacy\",\"scope\":\""<<(snapshot.settings.fullFK?"FullFK":"UpperOnly")
            <<"\",\"pose\":\""<<basis::PoseName(snapshot.settings.pose)<<"\",\"frame\":"<<snapshot.frame<<",\"last_frame\":"<<snapshot.lastFrame<<",\"stage\":"<<stage<<",\"axis_bone\":"<<snapshot.settings.axisBone<<",\"axis\":"<<snapshot.settings.axis
            <<",\"frames\":"<<snapshot.frames<<",\"writes\":"<<snapshot.writes<<",\"fault\":"<<playback.fault<<",\"restore\":"<<(playback.restore?"true":"false")
            <<",\"restored_bones\":"<<playback.restoredBones<<",\"avatar_reference_ready\":"<<(playback.avatarReady?"true":"false")
            <<",\"axis_alignment\":\"S=I_PENDING_VISUAL_AXIS_CALIBRATION\",\"translation\":\"NOT_WRITTEN\",\"IK\":false,\"root_motion\":false,\"bones\":[";
        bool first=true;
        for(unsigned order=0;order<playback.boneCount;++order) {
            const auto index=playback.order[order];const auto& bone=playback.bones[index];if(!bone.write)continue;
            const auto parent=bone.identity.parent;
            desired[index]=bone.source>=0?vmd::Multiply(playback.sources[bone.source].currentWorld,bone.rest.rotation):
                parent>=0&&playback.bones[parent].hasReference&&snapshot.written[parent]?vmd::Multiply(desired[parent],bone.bindLocal):bone.rest.rotation;
            const auto expectedWorld=vmd::Multiply(snapshot.rootWorld,desired[index]);
            const auto expectedLocal=vmd::Multiply(vmd::Inverse(snapshot.parentWorld[index]),expectedWorld);
            const auto raw=bone.source>=0&&playback.sources[bone.source].track>=0?vmd::Sample(clip.tracks[playback.sources[bone.source].track],snapshot.frame):Quaternion{0,0,0,1};
            const auto input=bone.source>=0?basis::Input(snapshot.settings,playback.sources[bone.source].name,raw):Quaternion{0,0,0,1};
            if(!first)json<<',';first=false;
            char name[512]{},source[128]{};WideCharToMultiByte(CP_UTF8,0,bone.identity.name,-1,name,sizeof(name),nullptr,nullptr);
            if(bone.source>=0)WideCharToMultiByte(CP_UTF8,0,playback.sources[bone.source].name,-1,source,sizeof(source),nullptr,nullptr);
            json<<"{\"name\":\""<<name<<"\",\"source\":\""<<source<<"\",\"index\":"<<index<<",\"parent\":"<<parent<<",\"written\":"<<(snapshot.written[index]?"true":"false")
                <<",\"pelvis_auxiliary\":"<<(int(index)==playback.pelvisAuxiliary?"true":"false")<<",\"before_local\":";
            if(int(index)==playback.pelvisAuxiliary&&snapshot.auxiliaryCaptured)jq(json,snapshot.auxiliaryBefore);else json<<"null";
            json<<",\"restore_rotation\":";jq(json,bone.restoreRotation);json<<",\"bind_model_rotation\":";jq(json,bone.rest.rotation);
            json<<",\"bind_model_position\":["<<bone.rest.position.x<<','<<bone.rest.position.y<<','<<bone.rest.position.z<<"],\"bind_local_rotation\":";
            if(bone.parentReference)jq(json,bone.bindLocal);else json<<"null";
            json<<",\"vmd_raw\":";jq(json,raw);json<<",\"vmd_input\":";jq(json,input);json<<",\"unity_local\":";jq(json,snapshot.rotations[index]);
            json<<",\"unity_world\":";jq(json,snapshot.world[index]);json<<",\"unity_position\":["<<snapshot.positions[index].x<<','<<snapshot.positions[index].y<<','<<snapshot.positions[index].z<<']';
            json<<",\"expected_local\":";if(snapshot.written[index])jq(json,expectedLocal);else json<<"null";
            json<<",\"expected_world\":";if(snapshot.written[index])jq(json,expectedWorld);else json<<"null";
            json<<",\"local_error_deg\":";if(snapshot.written[index])json<<basis::Angle(snapshot.rotations[index],expectedLocal);else json<<"null";
            json<<",\"world_error_deg\":";if(snapshot.written[index])json<<basis::Angle(snapshot.world[index],expectedWorld);else json<<"null";json<<'}';
        }
        json<<"]}";
        const auto filename=std::string("pelvis_fk.")+basis::PoseName(snapshot.settings.pose)+'.'+std::to_string(stage)+'.'+std::to_string(playback.startTick)+".json";
        std::ofstream out(abOutput/filename,std::ios::binary);out<<json.str();out.close();
        report<<"FK_diagnostic="<<(abOutput/filename).string()<<" stage="<<stage<<" full="<<snapshot.settings.fullFK<<" frame="<<snapshot.frame<<"; visual_confirmation=PENDING\n";
    }
    report.flush();
}
void ReportMotionAB(std::ostream& report) {
    // 每次真实开始最多导出一次。停止状态下切换配置不能重标记上一轮的数据。
    if (!playback.abReportPending) return;
    playback.abReportPending=false;
    if (!abSettings.enabled||!playback.frames||abOutput.empty()) return;
    if(basis::FkTest(abSettings)){ReportMotionFK(report);return;}
    for(unsigned stage=0;stage<basis::SequenceStages;++stage) {
    const auto& snapshot=playback.abSnapshots[stage];if(!snapshot.valid)continue;
    const auto& settings=snapshot.settings;const auto frame=snapshot.frame;
    SourcePose(frame,settings);
    Quaternion legacyWorld[MaxRig]{},basisWorld[MaxRig]{};
    auto jq=[](std::ostream& out,Quaternion q){out<<'['<<q.x<<','<<q.y<<','<<q.z<<','<<q.w<<']';};
    std::ostringstream json;json<<std::setprecision(9)<<"{\"route\":\""<<basis::RouteName(settings.route)<<"\",\"pose\":\""<<basis::PoseName(settings.pose)
        <<"\",\"frame\":"<<frame<<",\"stage\":"<<stage<<",\"frames\":"<<snapshot.frames<<",\"writes\":"<<snapshot.writes<<",\"fault\":"<<playback.fault<<",\"restore\":"<<(playback.restore?"true":"false")
        <<",\"motion_keys\":"<<clip.boneKeys<<",\"motion_tracks\":"<<clip.tracks.size()<<",\"motion_last_frame\":"<<clip.lastFrame
        <<",\"axis_alignment\":\"S=I_EXPERIMENT_ASSUMPTION\",\"missing_reference\":[\"Bip001 Pelvis\"],\"visual_verdict\":\"PENDING\",\"bones\":[";
    bool first=true;
    for(unsigned order=0;order<playback.boneCount;++order) {
        auto& bone=playback.bones[playback.order[order]];if(!bone.write||!bone.hasReference)continue;
        const auto parent=bone.identity.parent;
        const auto sampled=bone.source>=0&&playback.sources[bone.source].track>=0?vmd::Sample(clip.tracks[playback.sources[bone.source].track],frame):Quaternion{0,0,0,1};
        const auto input=bone.source>=0?basis::Input(settings,playback.sources[bone.source].name,sampled):Quaternion{0,0,0,1};
        const auto index=playback.order[order];
        legacyWorld[index]=bone.source>=0?vmd::Multiply(playback.sources[bone.source].currentWorld,bone.rest.rotation):bone.parentReference?vmd::Multiply(legacyWorld[parent],bone.bindLocal):bone.rest.rotation;
        const auto local=basis::Local(bone.bindLocal,bone.rest.rotation,input);
        basisWorld[index]=bone.parentReference?vmd::Multiply(basisWorld[parent],local):vmd::Multiply(input,bone.rest.rotation);
        if (!first) json<<',';first=false;
        char name[512]{};WideCharToMultiByte(CP_UTF8,0,bone.identity.name,-1,name,sizeof(name),nullptr,nullptr);
        json<<"{\"bone\":\""<<name<<"\",\"vmd_quaternion\":";jq(json,sampled);json<<",\"test_input\":";jq(json,input);
        json<<",\"target_bind_model\":";jq(json,bone.rest.rotation);json<<",\"reference_basis\":";jq(json,bone.basisLocal);
        json<<",\"target_bind_local\":";if(bone.parentReference)jq(json,bone.bindLocal);else json<<"null";
        json<<",\"legacy_local\":";if(bone.parentReference)jq(json,vmd::Multiply(vmd::Inverse(legacyWorld[parent]),legacyWorld[index]));else json<<"null";
        json<<",\"basis_local\":";if(bone.parentReference)jq(json,local);else json<<"null";
        json<<",\"angular_difference_deg\":";if(bone.parentReference)json<<basis::Angle(vmd::Multiply(vmd::Inverse(legacyWorld[parent]),legacyWorld[index]),local);else json<<"null";
        json<<",\"last_unity_readback\":";jq(json,snapshot.rotations[index]);json<<",\"local_reference_valid\":"<<(bone.parentReference?"true":"false")<<'}';
    }
    json<<"]}";
    const auto name=std::string("mmd_basis_ab.")+basis::RouteName(settings.route)+'.'+basis::PoseName(settings.pose)+'.'+std::to_string(unsigned(frame))+'.'+std::to_string(playback.startTick)+".json";
    for(const auto& path:{abOutput/L"mmd_basis_ab.json",abOutput/name}) {std::ofstream out(path,std::ios::binary);out<<json.str();}
    report<<"AB_diagnostic="<<(abOutput/name).string()<<"; numeric_readback=CHECKED_EACH_WRITE; visual_confirmation=PENDING\n";report.flush();
    }
}
unsigned MotionABStage(){return abStage.load(std::memory_order_acquire);}
std::wstring ReportMotionABStage(unsigned stage,std::ostream& report) {
    if(basis::FkTest(abSettings)) {
        if(stage>=(abSettings.pose==basis::Pose::FkDance?3:abSettings.pose==basis::Pose::FkAxes?basis::FkAxesStages:basis::FkStages))return {};
        const auto settings=basis::FkStageSettings(stage,abSettings.pose);
        report<<"[FK_STAGE] index="<<stage<<" scope="<<(settings.fullFK?"FullFK":"UpperOnly")<<" pose="<<basis::PoseName(settings.pose)<<" frame="<<settings.frame
            <<" axis_bone="<<settings.axisBone<<" axis="<<settings.axis<<"; manual_stop=NUMPAD2\n";report.flush();
        if(settings.pose==basis::Pose::FkDance)return L"Legacy FK：完整骨架，连续 VMD 播放。小键盘 2 停止恢复。";
        if(settings.pose==basis::Pose::FkAxes) {
            if(settings.axisBone<0)return L"FK 单轴对照 1/13：静态参考。小键盘 2 停止恢复。";
            const wchar_t* names[]={L"Pelvis",L"Spine",L"左 Thigh",L"右 Thigh"};const wchar_t* axes[]={L"X",L"Y",L"Z"};
            return L"FK 单轴对照 "+std::to_wstring(stage+1)+L"/13："+names[settings.axisBone]+L" "+axes[settings.axis]+L" +30°。小键盘 2 停止恢复。";
        }
        return L"Legacy FK："+std::wstring(settings.fullFK?L"完整骨架":L"仅上半身")+L"，帧 "+std::to_wstring(unsigned(settings.frame))+L"。小键盘 2 停止恢复。";
    }
    if(!abSettings.enabled||abSettings.pose!=basis::Pose::Sequence||stage>=basis::SequenceStages)return {};
    const auto settings=basis::StageSettings(stage);
    report<<"[AB_STAGE] index="<<stage<<" route="<<basis::RouteName(settings.route)<<" pose="<<basis::PoseName(settings.pose)<<" frame="<<settings.frame<<"; manual_stop=NUMPAD2\n";report.flush();
    const auto route=settings.route==basis::Route::Legacy?L"Legacy":L"Basis";
    const auto pose=settings.pose==basis::Pose::Identity?L"A-pose":settings.pose==basis::Pose::X?L"左臂 X +30°":settings.pose==basis::Pose::Y?L"左臂 Y +30°":settings.pose==basis::Pose::Z?L"左臂 Z +30°":L"固定帧 "+std::to_wstring(unsigned(settings.frame));
    return std::wstring(L"A/B ")+std::to_wstring(stage+1)+L"/11："+route+L" "+pose+(stage==10?L"。约 40 秒后可按小键盘 2 恢复。":L"；自动进入下一段。小键盘 2 随时停止。");
}
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
    avatarGetter={};NativeClass animator;
    if(runtime.FindClass("UnityEngine","Animator",animator)&&runtime.FindMethod(animator,"get_avatar",0,avatarGetter)&&avatarGetter.target) {
        // 只读备用来源：验证 getter 确实访问 Animator 的 Avatar PPtr；不调用 Rebind 或任何 setter。
        uint8_t opcode=0;int32_t relative=0;
        const auto avatarCall=avatarGetter.target+48;
        const auto valid=pattern(avatarGetter.target,80,"48 8B C8 E8 ?? ?? ?? ?? 48 8B D0 48 8D 4C 24 30 E8 ?? ?? ?? ?? 48 8B 00")&&
            Read(avatarCall,opcode)&&opcode==0xE8&&Read(avatarCall+1,relative)&&
            pattern(native_detail::RelativeTarget(avatarCall,relative,5),12,"48 81 C1 20 01 00 00 E9 ?? ?? ?? ??");
        if(!valid)avatarGetter={};
    }
    report<<"reference_avatar_getter="<<(avatarGetter.entry?"READONLY_PPTR_VERIFIED":"UNAVAILABLE")<<'\n';
    playback.avatarProfile=basis::FkTest(abSettings)&&avatarGetter.entry&&AvatarProfile(report);
    componentsGetter={};NativeClass gameObject;
    if(runtime.FindClass("UnityEngine","GameObject",gameObject)&&runtime.FindMethod(gameObject,"GetComponentsInternalType",6,componentsGetter)&&componentsGetter.target) {
        if(!pattern(componentsGetter.target,224,"41 0F B6 E9 45 0F B6 F0")||
            !pattern(componentsGetter.target,224,"40 38 B4 24 80 00 00 00 0F 95 44 24 3A")||
            !pattern(componentsGetter.target,224,"48 8B 00 48 8B 7C 24 78"))componentsGetter={};
    }
    report<<"reference_actor_children="<<(componentsGetter.entry?"INCLUDE_INACTIVE_TYPE_ARRAY_VERIFIED":"UNAVAILABLE")<<'\n';
    report << "reference_ABI=MANAGED_MATRIX4X4_SZARRAY; matrix_bytes=64; matrix_source=NATIVE_COPY_CROSSCHECKED; descriptorId=" << bindGetter.metadataId << '\n';report.flush();return true;
}
bool ArmMotionRig(const RigCalls& calls,uintptr_t root,uintptr_t renderer,uintptr_t animator,std::span<const MotionRigBone> bones,
    uintptr_t active,uintptr_t activeMethod) {
    if (playback.armed || bones.empty() || bones.size()>MaxRig) return false;
    playback={};std::memset(referenceMeshes,0,sizeof(referenceMeshes)); rigCalls=calls; status=0; enabled=false;
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
bool CaptureMotionRenderers(uintptr_t renderers,uint64_t count) {
    if (!OwnerIdentity()||playback.meshCount) return false;
    playback.rendererCount=unsigned(count);playback.inventoryComplete=count<=4096;
    for (uint64_t i=0;i<std::min<uint64_t>(count,4096);++i) {
        ++playback.rendererScan;
        uintptr_t object=0,klass=0,native=0,transformNative=0;
        if (!Read(renderers+0x20+i*8,object)||!Read(object,klass)) return false;
        if (klass!=rigCalls.rendererClass||!Read(object+0x10,native)||!native) continue;
        const auto transform=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(rigCalls.transform)(object,rigCalls.transformMethod);
        if (!Read(transform+0x10,transformNative)||!OwnedTransform(transform,transformNative)) continue;
        int bones=0;if (!Read(native+0x440,bones)||bones<0) return false;
        if (!bones) continue;
        if (bones>int(MaxRig)||playback.meshCount==MaxReferenceMeshes) {playback.inventoryComplete=false;continue;}
        auto& item=referenceMeshes[playback.meshCount++];item.renderer=object;item.native=native;
        item.transform=transform;item.transformNative=transformNative;item.count=unsigned(bones);
        item.handles[0]=Root(object);item.handles[1]=Root(transform);
        item.array=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(rigCalls.bones.entry)(object,rigCalls.bones.descriptor);
        item.handles[2]=Root(item.array);uint64_t length=0;
        if (!item.handles[0]||!item.handles[1]||!item.handles[2]||!live_detail::ValidateArray(item.array,rigCalls.transformClass,length)||
            length!=item.count||!CopyName(object,item.name)) return false;
    }
    return true;
}
void ExportMotionReferences(const std::filesystem::path& directory,std::ostream& report) {
    // 主线程派发完成后导出 POD；这里不再访问任何 managed 对象。
    auto utf8=[](const wchar_t* name) {
        char value[512]{};WideCharToMultiByte(CP_UTF8,0,name,-1,value,sizeof(value),nullptr,nullptr);
        std::string out;for (const char c:std::string(value)) {if(c=='"'||c=='\\')out+='\\';if(static_cast<unsigned char>(c)<32)out+='?';else out+=c;}return out;
    };
    std::ofstream out(directory/L"same_actor_bindposes.json",std::ios::trunc);out<<std::setprecision(9);
    out<<"{\"complete\":"<<(playback.inventoryComplete?"true":"false")<<",\"enumeration\":\""<<(playback.inventoryInactive?"Actor_GetComponentsInChildren_includeInactive":"FindObjectsOfType_active_objects")<<"\",\"avatar_name\":\""<<utf8(playback.avatarName)
        <<"\",\"avatar_native\":\"0x"<<std::hex<<playback.referenceAvatarNative<<std::dec<<"\",\"actor_name\":\""<<utf8(playback.bones[playback.root].identity.name)
        <<"\",\"actor_native\":\"0x"<<std::hex<<playback.ownerNative[0]<<"\",\"animator_native\":\"0x"<<playback.ownerNative[2]<<std::dec<<"\",\"renderer_total\":"<<playback.rendererCount
        <<",\"renderer_scanned\":"<<playback.rendererScan<<",\"meshes\":[";
    for (unsigned m=0;m<playback.meshCount;++m) {
        const auto& item=referenceMeshes[m];if(m)out<<',';
        out<<"{\"name\":\""<<utf8(item.name)<<"\",\"mesh_name\":\""<<utf8(item.meshName)<<"\",\"valid\":"<<(item.valid?"true":"false")
            <<",\"failure\":"<<item.failure<<",\"selected_body\":"<<(item.renderer==playback.owner[1]?"true":"false")<<",\"count\":"<<item.count<<",\"bones\":[";
        for(unsigned b=0;item.valid&&b<item.count;++b) {
            if(b)out<<',';out<<"{\"name\":\""<<utf8(item.boneNames[b])<<"\",\"rig_index\":"<<item.nodes[b]<<",\"matrix_column_major\":[";
            for(unsigned k=0;k<16;++k){if(k)out<<',';out<<item.matrices[b].value[k];}out<<"]}";
        }
        out<<"]}";
        report<<"[REFERENCE_MESH] name="<<utf8(item.name)<<" mesh="<<utf8(item.meshName)<<" bones="<<item.count<<" valid="<<item.valid<<" failure="<<item.failure<<'\n';
    }
    out<<"],\"rig\":[";bool first=true;
    for(unsigned i=0;i<playback.boneCount;++i) {
        const auto& bone=playback.bones[i];if(!bone.identity.owned)continue;
        if(!first)out<<',';first=false;
        out<<"{\"index\":"<<i<<",\"parent\":"<<bone.identity.parent<<",\"name\":\""<<utf8(bone.identity.name)
            <<"\",\"transform\":\"0x"<<std::hex<<bone.identity.object<<"\",\"native\":\"0x"<<bone.identity.native<<std::dec
            <<"\",\"body_bound\":"<<(bone.hasReference?"true":"false")<<"}";
    }
    out<<"]}";out.flush();
    report<<"reference_avatar=0x"<<std::hex<<playback.referenceAvatar<<" native=0x"<<playback.referenceAvatarNative<<std::dec<<" name="<<utf8(playback.avatarName)<<"; avatar_reference=NOT_DECODED\n";
    report<<"reference_inventory="<<(out?"EXPORTED":"IO_FAILED")<<" complete="<<playback.inventoryComplete<<" same_actor_meshes="<<playback.meshCount
        <<" scanned="<<playback.rendererScan<<'/'<<playback.rendererCount<<"; reference_merge=NOT_PERFORMED\n";report.flush();
}
bool LoadMotionClip(const std::filesystem::path& path,std::ostream& report,HANDLE stop) {
    if (enabled.load() || status.load()==1 || playback.captured || !playback.armed) return false;
    vmd::Clip loaded; std::string error;
    if (!vmd::Load(path,loaded,error,stop)) { report << "motion_load=REJECTED; reason=" << error << '\n'; report.flush(); return false; }
    report << "[VMD_FILE] bytes=" << loaded.bytes << " bone_keys=" << loaded.boneKeys << " tracks=" << loaded.tracks.size()
        << " last_frame=" << loaded.lastFrame << " fps=30 camera_keys=" << loaded.cameraKeys << " morph_keys=" << loaded.morphKeys << '\n';
    if (loaded.tracks.empty() || !loaded.lastFrame) { report << "motion_load=REJECTED; reason=" << (loaded.tracks.empty()?"NO_BONE_TRACKS_CAMERA_OR_MORPH_ONLY":"STATIC_POSE_NO_TIMED_MOTION") << '\n'; report.flush(); return false; }
    char sourceModel[256]{};WideCharToMultiByte(CP_UTF8,0,loaded.model.c_str(),-1,sourceModel,sizeof(sourceModel),nullptr,nullptr);
    unsigned initialRotations=0,initialPositions=0,lateFirstKeys=0;
    for (const auto& track:loaded.tracks) {
        const auto rotation=vmd::Sample(track,0);const auto position=vmd::SamplePosition(track,0);
        initialRotations+=2*std::acos(std::clamp(std::fabs(rotation.w),0.0f,1.0f))>0.0174532925f;
        initialPositions+=ik::Length(position)>0.0001f;
        lateFirstKeys+=!track.keys.empty()&&track.keys.front().frame>0;
    }
    // 开场姿态属于动作数据，不能把首帧当作源模型 rest pose 并自动抵消。
    report << "[VMD_START_POSE] model=" << sourceModel << " nonidentity_rotations_gt_1deg=" << initialRotations
        << " nonzero_positions=" << initialPositions << " tracks_without_frame0=" << lateFirstKeys
        << "; first_pose=PRESERVED; source_PMX_reference=NOT_PROVIDED; frame0_reference_subtraction=NONE\n";
    if (!playback.referenceReady) {report << "motion_load=REJECTED; reason=STATIC_BIND_REFERENCE_REQUIRED\n";report.flush();return false;}
    clip=std::move(loaded); BuildSources();
    for (unsigned i=0;i<playback.sourceCount;++i) {
        const auto& source=playback.sources[i];if (source.track<0||source.node<0) continue;
        char name[128]{};WideCharToMultiByte(CP_UTF8,0,source.name,-1,name,sizeof(name),nullptr,nullptr);
        const auto& track=clip.tracks[source.track];const auto rotation=vmd::Sample(track,0);const auto position=vmd::SamplePosition(track,0);
        report << "[VMD_START_BONE] name=" << name << " first_key=" << track.keys.front().frame << " rotation=";Q(report,rotation);
        report << " position=" << position.x << ',' << position.y << ',' << position.z << '\n';
    }
    report << "motion_load=" << (playback.mapped && playback.writeBones?"READY":"REJECTED_UNMAPPED") << " mapped_tracks=" << playback.mapped
        << " write_bones=" << playback.writeBones << " retarget=STATIC_MESH_BIND_REFERENCE; positions=NOT_WRITTEN; foot_IK=" << (abSettings.enabled?"EXCLUDED_AB_TEST":footIK?"ANALYTIC_TWO_BONE_PREVIEW":"NOT_SOLVED") << "; morphs=NOT_APPLIED\n";
    if (footIK&&!abSettings.enabled) {
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
    report.flush();
    report<<"[FK_AUXILIARY] mapped="<<(playback.pelvisAuxiliary>=0)<<" rejected="<<playback.auxiliaryRejected<<" source=LowerBody_FK; bone=+PelvisTwist_CF_A01; static_reference=MESH_BIND; physics=NOT_SOLVED\n";
    const auto ready=playback.mapped&&playback.writeBones&&!playback.auxiliaryRejected;
    if (ready&&ui::Enabled()) ui::Motion(path.wstring(),unsigned(clip.tracks.size()),float(clip.lastFrame)/30);
    return ready;
}
bool UnloadMotionClip() {
    if (enabled.load()||status.load()==1||playback.captured) return false;
    clip={};playback.mapped=0;playback.sourceCount=0;playback.writeBones=0;progressSeconds=0;
    for (auto& bone:playback.bones) {bone.source=-1;bone.write=false;}
    if (ui::Enabled()) ui::ClearMotion();return true;
}
float MotionPosition() {return progressSeconds.load(std::memory_order_relaxed);}
bool MotionMainStep(unsigned mode) {
    if (!playback.armed || !MainThread()) return false;
    if (mode==6) { const auto passed=Stop(); FreeRoots(); return passed; }
    if (mode==8) return Stop();
    if (mode==9) return CaptureReference();
    if (mode!=7 || !OwnerIdentity() || !playback.referenceReady || playback.captured || !playback.mapped || !playback.writeBones) return false;
    if(basis::FkTest(abSettings)) {
        if(playback.auxiliaryRejected||!playback.avatarReady||!playback.avatarHandle||reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(avatarGetter.entry)(playback.owner[2],avatarGetter.descriptor)!=playback.referenceAvatar)return false;
        uintptr_t native=0;if(!Read(playback.referenceAvatar+0x10,native)||native!=playback.referenceAvatarNative)return false;
    }
    const auto currentMesh=reinterpret_cast<uintptr_t (*)(uintptr_t,uintptr_t)>(meshGetter.entry)(playback.owner[1],meshGetter.descriptor);
    if (currentMesh!=playback.referenceMeshObject||!Alive(currentMesh,playback.referenceMeshNative,referenceMeshClass)) return false;
    for (unsigned i=0;i<playback.boneCount;++i) if (playback.bones[i].identity.owned) {
        auto& bone=playback.bones[i];
        if (!BoneIdentity(bone) || !ReadBoneRotation(bone.identity.object,bone.identity.native,bone.restoreRotation)) return false;
    }
    for (auto& bone:playback.bones) {bone.desiredWorld=bone.rest.rotation;bone.touched=false;}
    if(!abSettings.enabled||abSettings.route==basis::Route::Legacy)SourcePose(0,abSettings);
    if (!PrepareIK()) return false;
    playback.frames=0; playback.writes=0; playback.restoredBones=0; playback.fault=0; playback.sampleCount=0; playback.maxTickMicroseconds=0;
    playback.startTick=GetTickCount64(); playback.nextSample=playback.startTick; playback.captured=true; playback.restore=false;
    playback.lastFrame=0;playback.abReportPending=abSettings.enabled;
    for(auto& snapshot:playback.abSnapshots)snapshot={};abStage=UINT_MAX;
    progressSeconds=0;
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
    report<<"[AVATAR_REFERENCE] ready="<<playback.avatarReady<<" failure="<<playback.avatarFailure<<" nodes="<<playback.avatarCount<<" compared_body_binds="<<playback.avatarCompared
        <<" max_angle_deg="<<playback.avatarMaxAngle<<" max_position="<<playback.avatarMaxPosition<<" source=STATIC_AVATAR_DEFAULT_POSE; live_pose=RESTORE_ONLY\n";
    report << "[MOTION_REFERENCE] ready=" << playback.referenceReady << " bind_bones=" << playback.referenceBones << " failure_step=" << playback.referenceFailure
        << " basis=STATIC_MESH_BIND; live_pose=RESTORE_ONLY; rotation_isolation=" << (abSettings.enabled?"FK_AND_STATIC_ANCESTORS_AFTER_LATEUPDATE":"ALL_BOUND_BONES_AFTER_LATEUPDATE") << "; translation_isolation=NOT_IMPLEMENTED; full_A_pose_isolation=NOT_CONFIRMED\n";
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
