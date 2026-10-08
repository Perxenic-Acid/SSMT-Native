#include "RuntimeProbe.h"
#include "GenshinNativeRuntime.h"
#include "LiveUnityProbe.h"
#include "BoneWriteProbe.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <MinHook.h>
#include "VmdMotion.h"
#include "MotionPlayback.h"
#include <fstream>
#include "LegIK.h"
#include "ReferencePose.h"

using namespace poser;
namespace {
void Require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
void BoneMathTests() {
    using namespace bone_detail;
    const Quaternion identity{0,0,0,1};
    const auto rotated=DeltaYaw(identity);
    Require(Valid(rotated) && !Same(identity,rotated),"four degree rotation not applied");
    Require(std::fabs(2*std::acos(rotated.w)*180/3.141592653589793-4)<0.0002,"rotation amplitude exceeds expected bound");
    Require(Same(rotated,Quaternion{-rotated.x,-rotated.y,-rotated.z,-rotated.w}),"equivalent Quaternion sign rejected");
    Require(!Valid(Quaternion{0,0,0,0}) && !Valid(Quaternion{0,0,0,2}),"invalid Quaternion norm accepted");
    Require(!Valid(Quaternion{std::numeric_limits<float>::quiet_NaN(),0,0,1}),"NaN Quaternion accepted");
    const Quaternion original{0.382683432f,0,0,0.923879533f};
    Require(Valid(DeltaYaw(original)) && Same(DeltaYaw(original),DeltaYaw(original)),"delta composition not stable");
    const auto arm=DeltaArm(identity);
    Require(Valid(arm) && std::fabs(2*std::acos(arm.w)*180/3.141592653589793-60)<0.0002,"arm rotation amplitude incorrect");
    Require(Valid(DeltaArm(original)),"arm Quaternion composition invalid");
}
using BatchFunction=void (*)(uintptr_t,bool,bool,bool,bool);
volatile uintptr_t batchObserved=0;
volatile unsigned batchFlags=0;
unsigned batchDetours=0;
BatchFunction batchOriginal=nullptr;
void (*noArgumentOriginal)()=nullptr;
__declspec(noinline) void NoArgumentFixture() { batchObserved=batchObserved+1; batchFlags=batchFlags^3; }
void NoArgumentDetour() { ++batchDetours; noArgumentOriginal(); }
__declspec(noinline) void BatchFixture(uintptr_t tag,bool a,bool b,bool c,bool d) {
    batchObserved=tag; batchFlags=unsigned(a)|(unsigned(b)<<1)|(unsigned(c)<<2)|(unsigned(d)<<3);
}
void BatchDetour(uintptr_t tag,bool a,bool b,bool c,bool d) {
    ++batchDetours; batchOriginal(tag,a,b,c,d);
}
void AnimationHookAbiTests() {
    Require(MH_Initialize()==MH_OK,"hook initialization failed");
    Require(MH_CreateHook(reinterpret_cast<void*>(&BatchFixture),reinterpret_cast<void*>(&BatchDetour),
        reinterpret_cast<void**>(&batchOriginal))==MH_OK,"five argument fixture trampoline failed");
    Require(MH_EnableHook(reinterpret_cast<void*>(&BatchFixture))==MH_OK,"fixture hook enable failed");
    BatchFunction volatile invoke=&BatchFixture;
    for (unsigned flags=0;flags<16;++flags) {
        invoke(0x1234567887654321,flags&1,flags&2,flags&4,flags&8);
        Require(batchObserved==0x1234567887654321 && batchFlags==flags,"trampoline changed register or stack bool arguments");
    }
    Require(batchDetours==16,"fixture hook not reached");
    Require(MH_DisableHook(reinterpret_cast<void*>(&BatchFixture))==MH_OK &&
        MH_RemoveHook(reinterpret_cast<void*>(&BatchFixture))==MH_OK,"fixture hook cleanup failed");
    invoke(91,false,false,false,true);
    Require(batchDetours==16 && batchObserved==91 && batchFlags==8,"removed hook still active");
    Require(MH_CreateHook(reinterpret_cast<void*>(&NoArgumentFixture),reinterpret_cast<void*>(&NoArgumentDetour),
        reinterpret_cast<void**>(&noArgumentOriginal))==MH_OK && MH_EnableHook(reinterpret_cast<void*>(&NoArgumentFixture))==MH_OK,
        "LateUpdate void() fixture hook failed");
    void (*volatile invokeNoArgument)()=&NoArgumentFixture;
    invokeNoArgument();
    Require(batchDetours==17 && batchObserved==92 && batchFlags==11,"void() trampoline changed original callback effect");
    Require(MH_DisableHook(reinterpret_cast<void*>(&NoArgumentFixture))==MH_OK && MH_RemoveHook(reinterpret_cast<void*>(&NoArgumentFixture))==MH_OK,
        "LateUpdate fixture cleanup failed");
    invokeNoArgument();
    Require(batchDetours==17 && batchObserved==93 && batchFlags==8,"removed void() hook still active");
    Require(MH_Uninitialize()==MH_OK,"fixture hook shutdown failed");
}
template<class T> void Append(std::vector<uint8_t>& bytes,const T& value) {
    const auto offset=bytes.size(); bytes.resize(offset+sizeof(T)); std::memcpy(bytes.data()+offset,&value,sizeof(T));
}
std::vector<uint8_t> MotionFixture() {
    std::vector<uint8_t> bytes(50);
    std::memcpy(bytes.data(),"Vocaloid Motion Data 0002",sizeof("Vocaloid Motion Data 0002")-1);
    std::memcpy(bytes.data()+30,"Fixture",7);
    Append(bytes,uint32_t(3));
    for (unsigned record=0;record<3;++record) {
        char name[15]{};
        WideCharToMultiByte(932,0,L"右腕",2,name,15,nullptr,nullptr);
        for (char value:name) bytes.push_back(uint8_t(value));
        Append(bytes,uint32_t(record==0?30:0));
        for (unsigned axis=0;axis<3;++axis) Append(bytes,0.0f);
        Append(bytes,record==0?Quaternion{0,0,0.5f,0.866025404f}:record==1?Quaternion{0.5f,0,0,0.866025404f}:Quaternion{0,0,0,1});
        std::array<uint8_t,64> interpolation{};
        for (unsigned channel=0;channel<4;++channel) {
            interpolation[channel*16]=20; interpolation[channel*16+4]=20;
            interpolation[channel*16+8]=107; interpolation[channel*16+12]=107;
        }
        // 冗余首块的 rotation 列不参与采样。
        interpolation[3]=0;interpolation[7]=0;interpolation[11]=0;interpolation[15]=0;
        bytes.insert(bytes.end(),interpolation.begin(),interpolation.end());
    }
    for (unsigned section=0;section<5;++section) Append(bytes,uint32_t(0));
    return bytes;
}
void VmdTests() {
    auto bytes=MotionFixture(); vmd::Clip clip; std::string error;
    Require(vmd::Parse(bytes,clip,error),"valid VMD rejected");
    Require(clip.boneKeys==3 && clip.tracks.size()==1 && clip.tracks[0].keys.size()==2 && clip.lastFrame==30,"VMD sort/dedup failed");
    Require(vmd::FindTrack(clip,L"右腕")==0 && vmd::FindTrack(clip,L"右足")==-1,"Shift-JIS track decoding failed");
    const auto middle=vmd::Sample(clip.tracks[0],15);
    clip.tracks[0].keys.front().position={0,0,0};clip.tracks[0].keys.back().position={2,4,6};
    const auto position=vmd::SamplePosition(clip.tracks[0],15);
    Require(std::fabs(position.x-1)<0.0001f&&std::fabs(position.y-2)<0.0001f&&std::fabs(position.z-3)<0.0001f,"VMD IK position curves incorrect");
    Require(bone_detail::Valid(middle) && std::fabs(2*std::acos(middle.w)*180/3.141592653589793-30)<0.01,"VMD rotation block or Slerp incorrect");
    Require(bone_detail::Same(vmd::Sample(clip.tracks[0],-1),Quaternion{0,0,0,1}),"duplicate final frame not selected");
    Require(bone_detail::Same(vmd::Sample(clip.tracks[0],100),clip.tracks[0].keys.back().rotation),"end frame not held");
    Require(bone_detail::Same(vmd::Slerp({0,0,0,1},{0,0,0,-1},0.5f),{0,0,0,1}),"Slerp hemisphere mismatch");
    auto asymmetric=clip.tracks[0].keys.back().interpolation;
    asymmetric[48]=10;asymmetric[52]=110;asymmetric[56]=60;asymmetric[60]=127;
    Require(vmd::Bezier(asymmetric,3,0.5f)>0.8f && vmd::Bezier(asymmetric,3,0)==0 && vmd::Bezier(asymmetric,3,1)==1,"Bezier time inversion incorrect");
    auto bad=bytes; bad.resize(54+110);
    Require(!vmd::Parse(bad,clip,error) && clip.tracks.empty(),"truncated VMD accepted");
    bad=bytes; std::memset(bad.data()+50,0xff,4);
    Require(!vmd::Parse(bad,clip,error),"oversized key count accepted");
    bad=bytes; const auto nan=std::numeric_limits<float>::quiet_NaN(); std::memcpy(bad.data()+54+31,&nan,4);
    Require(!vmd::Parse(bad,clip,error),"nonfinite VMD Quaternion accepted");
    bad=bytes; bad[54+47+48]=255;
    Require(!vmd::Parse(bad,clip,error),"invalid rotation control accepted");
    bad=bytes; bad[0]='x'; Require(!vmd::Parse(bad,clip,error),"invalid VMD header accepted");
    auto camera=std::vector<uint8_t>(bytes.begin(),bytes.begin()+50);
    Append(camera,uint32_t(0));Append(camera,uint32_t(0));Append(camera,uint32_t(1));camera.resize(camera.size()+61);
    Require(vmd::Parse(camera,clip,error) && clip.tracks.empty() && clip.cameraKeys==1,"camera-only VMD classification failed");
    const auto cancelled=CreateEventW(nullptr,TRUE,TRUE,nullptr);
    Require(cancelled && !vmd::Parse(bytes,clip,error,cancelled) && error=="VMD_CANCELLED","VMD parser ignored cancellation");
    CloseHandle(cancelled);
}
void LegIKTests() {
    ik::Solution out;
    Require(ik::Solve({0,0,0},{0,-1,0},{0,0,1},1,1,out),"reachable leg IK rejected");
    Require(ik::Length(ik::Sub(out.ankle,{0,-1,0}))<0.0001f &&
        std::fabs(ik::Length(out.knee)-1)<0.0001f && std::fabs(ik::Length(ik::Sub(out.ankle,out.knee))-1)<0.0001f && out.knee.z>0,"IK failed endpoint, lengths or knee pole");
    Require(ik::Solve({0,0,0},{0,-4,0},{0,-1,0},1,1,out)&&out.clamped&&ik::Length(out.ankle)<2,"unreachable goal not clamped");
    Require(ik::Solve({0,0,0},{0,-0.2f,0},{0,0,1},1,0.4f,out)&&out.clamped&&ik::Finite(out.knee),"too-close unequal limb goal invalid");
    Require(!ik::Solve({},{},{0,0,1},1,1,out)&&!ik::Solve({},{0,-1,0},{0,0,1},0,1,out),"degenerate IK accepted");
    const auto flip=ik::FromTo({1,0,0},{-1,0,0});
    Require(bone_detail::Valid(flip)&&ik::Length(ik::Sub(ik::Rotate(flip,{1,0,0}),{-1,0,0}))<0.0001f,"antipodal aiming rotation invalid");
    const auto small=ik::Unit({1,0.0005f,0});
    Require(ik::Length(ik::Sub(ik::Rotate(ik::FromTo({1,0,0},small),{1,0,0}),small))<0.000001f,"small IK correction discarded by rounded dot");
    const auto almostOpposite=ik::Unit({-1,0.0005f,0});
    Require(ik::Length(ik::Sub(ik::Rotate(ik::FromTo({1,0,0},almostOpposite),{1,0,0}),almostOpposite))<0.000001f,"near antipodal correction lost");
}
void ReferencePoseTests() {
    reference::Matrix matrix{{1,0,0,0,0,1,0,0,0,0,1,0,-1,-2,-3,1}};
    reference::Pose pose;
    Require(reference::Decode(matrix,pose)&&bone_detail::Same(pose.rotation,{0,0,0,1})&&ik::Length(ik::Sub(pose.position,{1,2,3}))<0.00001f,"bind translation inverse failed");
    // 正 scale 与 90 度旋转共同存在时，位置必须经过逆线性部分，不能只取负平移。
    matrix={{0,2,0,0,-2,0,0,0,0,0,2,0,4,-2,-6,1}};
    Require(reference::Decode(matrix,pose)&&ik::Length(ik::Sub(pose.position,{1,2,3}))<0.00001f&&
        ik::Length(ik::Sub(ik::Rotate(pose.rotation,{1,0,0}),{0,-1,0}))<0.00001f,"scaled rotated bind inversion failed");
    matrix.value[0]=0.5f;Require(!reference::Decode(matrix,pose),"sheared bind matrix accepted");
    matrix={{-1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};Require(!reference::Decode(matrix,pose),"mirrored bind matrix accepted");
    matrix.value[0]=0;Require(!reference::Decode(matrix,pose),"singular bind matrix accepted");
}
uintptr_t motionRootTable[32]{};
unsigned motionRootNext=1,motionRootNew=0,motionRootFree=0;
uintptr_t motionGameObject=0;
uint32_t MotionRootNew(uintptr_t object,bool) {
    if (motionRootNext>=std::size(motionRootTable)) return 0;
    const auto index=motionRootNext++; motionRootTable[index]=object; ++motionRootNew;
    return (2u<<26)|index;
}
uintptr_t MotionRootTarget(uint32_t handle) { return motionRootTable[handle&0x3ffffff]; }
void MotionRootFree(uint32_t handle) { const auto index=handle&0x3ffffff; if (motionRootTable[index]) { motionRootTable[index]=0; ++motionRootFree; } }
uintptr_t MotionThread() { return 1; }
uintptr_t MotionGameObject(uintptr_t,uintptr_t) { return motionGameObject; }
bool MotionActive(uintptr_t,uintptr_t) { return true; }
void MotionRigGuards() {
    uintptr_t classes[4]{};
    struct Native { unsigned char bytes[0xA8]; } native[5]{};
    struct Managed { uintptr_t klass,padding,native; } objects[5]{};
    objects[0]={uintptr_t(&classes[0]),0,uintptr_t(&native[0])};
    objects[1]={uintptr_t(&classes[0]),0,uintptr_t(&native[1])};
    objects[2]={uintptr_t(&classes[1]),0,uintptr_t(&native[2])};
    objects[3]={uintptr_t(&classes[2]),0,uintptr_t(&native[3])};
    objects[4]={uintptr_t(&classes[3]),0,uintptr_t(&native[4])};
    const auto parent=uintptr_t(&native[0]);std::memcpy(native[1].bytes+0xA0,&parent,sizeof(parent));
    motionGameObject=uintptr_t(&objects[4]); motionRootNext=1;motionRootNew=0;motionRootFree=0;
    const auto thread=GetCurrentThreadId(); RigCalls calls;
    calls.transformClass=uintptr_t(&classes[0]);calls.rendererClass=uintptr_t(&classes[1]);calls.animatorClass=uintptr_t(&classes[2]);calls.gameObjectClass=uintptr_t(&classes[3]);
    calls.context.mainThreadSlot=uintptr_t(&thread);calls.context.threadCurrent=uintptr_t(&MotionThread);
    calls.context.handleNew=uintptr_t(&MotionRootNew);calls.context.handleTarget=uintptr_t(&MotionRootTarget);calls.context.handleFree=uintptr_t(&MotionRootFree);
    calls.gameObject=uintptr_t(&MotionGameObject);
    MotionRigBone bones[2];
    bones[0].object=uintptr_t(&objects[0]);bones[0].native=uintptr_t(&native[0]);bones[0].owned=true;
    wcscpy_s(bones[0].name,L"Actor");
    bones[1].object=uintptr_t(&objects[1]);bones[1].native=uintptr_t(&native[1]);bones[1].parent=0;bones[1].parentNative=parent;bones[1].owned=true;
    wcscpy_s(bones[1].name,L"Bip001 R UpperArm");
    Require(ArmMotionRig(calls,bones[0].object,uintptr_t(&objects[2]),uintptr_t(&objects[3]),bones,uintptr_t(&MotionActive),0x42),"valid owned motion rig rejected");
    wchar_t temporary[MAX_PATH]{},path[MAX_PATH]{};GetTempPathW(MAX_PATH,temporary);Require(GetTempFileNameW(temporary,L"vmd",0,path)!=0,"motion fixture path unavailable");
    const auto bytes=MotionFixture();
    { std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size())); }
    std::ostringstream report;
    Require(!LoadMotionClip(path,report) && report.str().find("STATIC_BIND_REFERENCE_REQUIRED")!=std::string::npos,"motion fell back to live pose without static reference");
    // 未初始化真实 Quaternion callee 的假 runtime 必须拒绝开始；仅做文件与身份准备不能写游戏。
    Require(!MotionMainStep(7) && MotionStatus()==0,"motion started with unverified Quaternion backend");
    Require(MotionMainStep(6) && motionRootNew==motionRootFree && !MotionRigArmed(),"motion rig roots leaked on no-write teardown");
    DeleteFileW(path);
    bones[1].parent=1;
    Require(!ArmMotionRig(calls,bones[0].object,uintptr_t(&objects[2]),uintptr_t(&objects[3]),bones,uintptr_t(&MotionActive),0x42),"motion rig accepted parent cycle");
    Require(motionRootNew==motionRootFree,"motion rig roots leaked on failed binding");
}
int domainValue, threadValue, assemblyValue, imageValue, classValue, methodValue;
int attachCount = 0, detachCount = 0, invokeCount = 0;
enum class Scenario { Ready, NoDomain, Empty, BadCount, BadImage, BadText, Fault, InfiniteMethods, Cancel };
Scenario scenario = Scenario::Ready;
HANDLE cancellation = nullptr;
Domain* DomainGet() {
    if (scenario == Scenario::Fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    return scenario == Scenario::NoDomain ? nullptr : reinterpret_cast<Domain*>(&domainValue);
}
Thread* Attach(Domain*) { ++attachCount; return reinterpret_cast<Thread*>(&threadValue); }
void ThreadDetach(Thread*) { ++detachCount; }
const Assembly** Assemblies(const Domain*, size_t* count) {
    static const Assembly* values[] = {reinterpret_cast<Assembly*>(&assemblyValue)};
    *count = scenario == Scenario::Empty ? 0 : scenario == Scenario::BadCount ? 8193 : 1;
    return values;
}
const Image* AssemblyImage(const Assembly*) {
    return scenario == Scenario::BadImage ? nullptr : reinterpret_cast<Image*>(&imageValue);
}
size_t ClassCount(const Image*) { return 1; }
Class* GetClass(const Image*, size_t) { return reinterpret_cast<Class*>(&classValue); }
const char* ClassName(Class*) { return scenario == Scenario::BadText ? reinterpret_cast<const char*>(1) : "Transform"; }
const char* ClassNamespace(Class*) { return "UnityEngine"; }
const Method* Methods(Class*, void** iterator) {
    if (scenario == Scenario::Cancel) SetEvent(cancellation);
    if (*iterator && scenario != Scenario::InfiniteMethods) return nullptr;
    *iterator = &methodValue;
    return reinterpret_cast<Method*>(&methodValue);
}
const char* MethodName(const Method*) { return "GetChild"; }
uint32_t ParamCount(const Method*) { return 1; }
void* Invoke(const Method*, void*, void**, void**) { ++invokeCount; return nullptr; }
Api FakeApi() {
    return {DomainGet, Attach, Assemblies, AssemblyImage, ClassCount, GetClass,
        ClassName, ClassNamespace, Methods, MethodName, ParamCount, Invoke, ThreadDetach, nullptr};
}
void MetadataTests() {
    const auto api = FakeApi();
    for (const auto test : {Scenario::Ready, Scenario::NoDomain, Scenario::Empty, Scenario::BadCount,
        Scenario::BadImage, Scenario::BadText, Scenario::Fault, Scenario::InfiniteMethods, Scenario::Cancel}) {
        scenario = test; attachCount = detachCount = invokeCount = 0;
        cancellation = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(cancellation != nullptr, "event creation failed");
        std::ostringstream report;
        const auto result = ProbeMetadata(api, report, cancellation);
        if (test == Scenario::Ready) {
            Require(result == MetadataResult::Ready, "metadata enumeration failed");
            Require(report.str().find("Transform.GetChild matches=1 invoked=NO") != std::string::npos,
                "Unity method not recorded");
        } else if (test == Scenario::NoDomain || test == Scenario::Empty) {
            Require(result == MetadataResult::Waiting, "not-ready runtime was not retriable");
        } else if (test == Scenario::Cancel) {
            Require(result == MetadataResult::Cancelled, "cancellation ignored");
        } else Require(result == MetadataResult::Failed, "invalid metadata was accepted");
        Require(attachCount == detachCount, "thread attachment leaked");
        Require(invokeCount == 0, "Unity runtime_invoke must remain unused during Phase 0");
        CloseHandle(cancellation);
    }
    cancellation = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    attachCount = detachCount = 0;
    std::ostringstream report;
    Require(ProbeMetadata(api, report, cancellation) == MetadataResult::Cancelled, "initial cancel ignored");
    Require(attachCount == 0, "API called after cancellation");
    CloseHandle(cancellation);
}
void NativeResolverGuards() {
    using namespace native_detail;
    const std::vector<uint8_t> code{0x48, 0x8B, 0x05, 1, 2, 3, 4, 0xC3,
        0x48, 0x8B, 0x05, 5, 6, 7, 8, 0xC3};
    Require(FindPattern(code, "48 8B 05 ?? ?? ?? ?? C3").size() == 2, "ambiguous native signature lost");
    Require(FindPattern(code, "48 8B 05 01 02 03 04 C3").size() == 1, "literal native signature rejected");
    Require(FindPattern(code, "48 8B 05 01 02 03 04 C3 90").empty(), "truncated/mismatched pattern accepted");
    Require(RelativeTarget(0x1000, -0x207, 7) == 0xE00, "negative RIP displacement broken");
    Require(RelativeTarget(0x1000, 0x200, 7) == 0x1207, "positive RIP displacement broken");
    bool overflow = false;
    try { RelativeTarget(1, INT32_MIN, 7); } catch (...) { overflow = true; }
    Require(overflow, "relative underflow accepted");
    std::array<uint8_t, 256> klass{};
    std::array<uint8_t, 56> method{};
    const char name[] = "Transform";
    const auto klassAddress = reinterpret_cast<uintptr_t>(klass.data());
    const auto methodAddress = reinterpret_cast<uintptr_t>(method.data());
    const uintptr_t record = 0x11223344, nameAddress = reinterpret_cast<uintptr_t>(name), entry = 0x100000;
    const uint32_t id = 123;
    const uint16_t count = 160;
    std::memcpy(klass.data()+0x60, &record, sizeof(record));
    std::memcpy(klass.data()+0x28, &nameAddress, sizeof(nameAddress));
    std::memcpy(klass.data()+0xC0, &count, sizeof(count));
    Require(ValidateClass(klassAddress, record, name, count), "valid class relationship rejected");
    Require(!ValidateClass(klassAddress, record+70, name, count), "stale metadata identity accepted");
    Require(!ValidateClass(klassAddress, record, "Animator", count), "same-ID wrong class name accepted");
    Require(!ValidateClass(klassAddress, record, name, 159), "method count mismatch accepted");
    Require(!ValidateClass(1, record, name, count), "unreadable class address accepted");
    std::memcpy(method.data(), &klassAddress, sizeof(klassAddress));
    std::memcpy(method.data()+8, &entry, sizeof(entry));
    std::memcpy(method.data()+0x10, &id, sizeof(id));
    method[0x2E] = 1;
    Require(ValidateMethod(methodAddress, klassAddress, id, 1, entry), "valid method relationship rejected");
    Require(!ValidateMethod(methodAddress, klassAddress+8, id, 1, entry), "wrong declaring class accepted");
    Require(!ValidateMethod(methodAddress, klassAddress, id+1, 1, entry), "wrong method ID accepted");
    Require(!ValidateMethod(methodAddress, klassAddress, id, 0, entry), "wrong hidden/public parameter identity accepted");
    Require(!ValidateMethod(methodAddress, klassAddress, id, 1, entry+16), "different code entry accepted");
    const uintptr_t noEntry = 0;
    std::memcpy(method.data()+8, &noEntry, sizeof(noEntry));
    Require(ValidateMethod(methodAddress, klassAddress, id, 1, noEntry), "generic definition null entry rejected");
    Require(!ValidateMethod(methodAddress, klassAddress, id, 1, entry), "generic null entry treated as callable");
    GenshinNativeRuntime runtime;
    NativeClass missing;
    Require(!runtime.FindClass("UnityEngine", "Transform", missing), "uninitialized class lookup accepted");
    HANDLE cancelled = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    std::ostringstream report;
    Require(runtime.Initialize(GetModuleHandleW(nullptr), cancelled, report) == NativeResult::Cancelled,
        "native scan/hash continued after cancellation");
    Require(runtime.Snapshot(cancelled, report) == NativeResult::Cancelled, "native snapshot cancellation ignored");
    CloseHandle(cancelled);
}
void LiveObjectGuards() {
    using namespace live_detail;
    std::array<uint8_t, 0xD0> element{}, arrayClass{};
    std::array<uint8_t, 0x28> array{}, reflection{};
    std::array<uint8_t, 16> descriptor{};
    const auto elementAddress = reinterpret_cast<uintptr_t>(element.data());
    const auto arrayClassAddress = reinterpret_cast<uintptr_t>(arrayClass.data());
    const auto arrayAddress = reinterpret_cast<uintptr_t>(array.data());
    const auto reflectionAddress = reinterpret_cast<uintptr_t>(reflection.data());
    const auto descriptorAddress = reinterpret_cast<uintptr_t>(descriptor.data());
    const uint32_t id = 2386;
    const uint64_t length = 3;
    std::memcpy(element.data() + 0x68, &id, 4);
    std::memcpy(descriptor.data(), &id, 4);
    std::memcpy(arrayClass.data() + 0x68, &descriptorAddress, 8);
    arrayClass[0x72] = 0x1D;
    std::memcpy(array.data(), &arrayClassAddress, 8);
    std::memcpy(array.data() + 0x18, &length, 8);
    uint64_t observed = 0;
    Require(ValidateArray(arrayAddress, elementAddress, observed) && observed == length, "valid array relationship rejected");
    arrayClass[0x72] = 0x12;
    Require(!ValidateArray(arrayAddress, elementAddress, observed), "ordinary object accepted as SZARRAY");
    arrayClass[0x72] = 0x1D;
    descriptor[0] ^= 1;
    Require(!ValidateArray(arrayAddress, elementAddress, observed), "wrong element descriptor accepted");
    descriptor[0] ^= 1;
    array[0x10] = 1;
    Require(!ValidateArray(arrayAddress, elementAddress, observed), "bounded multidimensional array accepted");
    array[0x10] = 0;
    const uint64_t huge = 1000001;
    std::memcpy(array.data() + 0x18, &huge, 8);
    Require(!ValidateArray(arrayAddress, elementAddress, observed), "unbounded array accepted");
    Require(!ValidateArray(1, elementAddress, observed), "unreadable array accepted");
    std::memcpy(reflection.data(), &elementAddress, 8);
    std::memcpy(reflection.data() + 0x10, &descriptorAddress, 8);
    Require(ValidateReflection(reflectionAddress, elementAddress, descriptorAddress), "valid RuntimeType rejected");
    auto descriptorCopy = descriptor;
    const auto copyAddress = reinterpret_cast<uintptr_t>(descriptorCopy.data());
    Require(ValidateReflection(reflectionAddress, elementAddress, copyAddress), "registration/class descriptor copies rejected");
    descriptorCopy[0] ^= 1;
    Require(!ValidateReflection(reflectionAddress, elementAddress, copyAddress), "different descriptor content accepted");
    Require(!ValidateReflection(reflectionAddress, arrayClassAddress, descriptorAddress), "wrong reflection class accepted");
    Require(!ValidateReflection(reflectionAddress, elementAddress, descriptorAddress + 16), "wrong reflection descriptor accepted");
    Require(!ValidateReflection(descriptorAddress, elementAddress, descriptorAddress), "raw descriptor accepted as System.Type");
    Require(!ValidateReflection(1, elementAddress, descriptorAddress), "unreadable RuntimeType accepted");
}
void ResolverTests() {
    std::ostringstream report;
    Require(SelectRuntime({}, report) == nullptr, "empty modules accepted");
    Require(report.str().find("NATIVE_RESOLVER_REQUIRED") != std::string::npos, "missing resolver diagnosis");
    Module partial, complete;
    partial.name = L"UserAssembly.dll";
    partial.exports[0] = reinterpret_cast<FARPROC>(DomainGet);
    complete.name = L"GameAssembly.dll";
    complete.exports.fill(reinterpret_cast<FARPROC>(DomainGet));
    std::vector<Module> split{partial, partial};
    split[1].exports[0] = nullptr;
    split[1].exports[1] = reinterpret_cast<FARPROC>(Attach);
    Require(SelectRuntime(split, report) == nullptr, "partial modules combined unsafely");
    std::vector<Module> one{partial, complete};
    Require(SelectRuntime(one, report) == &one[1], "unique complete provider not selected");
    std::vector<Module> ambiguous{complete, complete};
    Require(SelectRuntime(ambiguous, report) == nullptr, "ambiguous providers accepted");
    const auto modules = EnumerateModules();
    Require(!modules.empty(), "real process module inventory empty");
    Require(std::any_of(modules.begin(), modules.end(), [](const auto& module) {
        return module.handle == GetModuleHandleW(nullptr);
    }), "main EXE omitted from inventory");
}
int logCount = 0;
void LogMessage(uint8_t, const char*) { ++logCount; }
void AbiTests(const wchar_t* path) {
    HMODULE library = LoadLibraryW(path);
    Require(library != nullptr, "cannot load plugin DLL");
    auto query = reinterpret_cast<Status (*)(uint32_t, PluginInfo*, PluginApi*)>(GetProcAddress(library, "SSMTPlugin_Query"));
    Require(query != nullptr, "plugin query export missing");
    constexpr uint64_t Canary = 0xA55ADEADCAFEBEEF;
    PluginInfo info{sizeof(PluginInfo)};
    struct BaseApi {
        uint32_t size, abi;
        Status (*init)(const HostServices*);
        Status (*stop)();
        uint64_t canary;
    } base{static_cast<uint32_t>(BaseApiSize), 0, nullptr, nullptr, Canary};
    Require(query(2, &info, reinterpret_cast<PluginApi*>(&base)) == Ok, "base ABI rejected");
    Require(base.canary == Canary && base.size == BaseApiSize, "base API capacity overwritten");
    struct ReadyApi {
        uint32_t size, abi;
        Status (*init)(const HostServices*);
        Status (*stop)();
        Status (*ready)(const void*);
        uint64_t canary;
    } ready{32, 0, nullptr, nullptr, nullptr, Canary};
    Require(query(2, &info, reinterpret_cast<PluginApi*>(&ready)) == Ok, "ready ABI rejected");
    Require(ready.canary == Canary && !ready.ready, "optional tail overwritten");
    PluginApi api{sizeof(PluginApi)};
    Require(query(2, &info, &api) == Ok, "full ABI rejected");
    Require(api.struct_size == sizeof(PluginApi) && info.struct_size == sizeof(PluginInfo), "caller capacity changed");
    Require(api.initialize && api.shutdown && !api.on_present && !api.on_d3d11_ready, "unexpected callback table");
    Require(std::strcmp(info.version, "0.1.0") == 0, "plugin version mismatch");
    Require(query(1, &info, &api) == UnsupportedAbi, "wrong ABI accepted");
    Require(query(2, nullptr, &api) == InvalidArgument, "null info accepted");
    api.struct_size = 23;
    Require(query(2, &info, &api) == TooSmall, "small API accepted");
    api.struct_size = sizeof(PluginApi); info.struct_size = 31;
    Require(query(2, &info, &api) == TooSmall, "small info accepted");
    Require(api.initialize(nullptr) == InvalidArgument, "null host accepted");
    HostServices host{sizeof(HostServices), 2, LogMessage};
    host.struct_size = 8;
    Require(api.initialize(&host) == TooSmall, "short host read beyond capacity");
    host.struct_size = sizeof(HostServices); host.abi_version = 1;
    Require(api.initialize(&host) == UnsupportedAbi, "wrong host ABI accepted");
    host.abi_version = 2;
    Require(api.initialize(&host) == Ok, "plugin initialization failed");
    Require(logCount == 1 && !IsGenshinProcess(), "non-Genshin process not inert");
    Require(api.shutdown() == Ok && api.shutdown() == Ok, "repeated shutdown failed");
    FreeLibrary(library);
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc >= 2, "usage: PoserProbeTests <SSMT-Poser.dll> [VMD samples...]");
        MetadataTests(); ResolverTests(); NativeResolverGuards(); LiveObjectGuards(); BoneMathTests(); AnimationHookAbiTests(); VmdTests(); LegIKTests(); ReferencePoseTests(); MotionRigGuards(); AbiTests(argv[1]);
        for (int index=2;index<argc;++index) {
            if (std::filesystem::path(argv[index]).extension()==L".bindposes") {
                std::ifstream input(argv[index],std::ios::binary);Require(bool(input),"bind fixture unavailable");
                reference::Matrix matrix;unsigned count=0;
                while (input.read(reinterpret_cast<char*>(&matrix),sizeof(matrix))) {reference::Pose pose;Require(reference::Decode(matrix,pose),"real bind fixture invalid");++count;}
                Require(input.eof()&&input.gcount()==0&&count>0,"truncated bind fixture accepted");std::cout << "Bind reference matrices=" << count << '\n';continue;
            }
            vmd::Clip clip; std::string error;
            Require(vmd::Load(argv[index],clip,error),error.c_str());
            std::cout << "VMD keys=" << clip.boneKeys << " tracks=" << clip.tracks.size() << " camera=" << clip.cameraKeys << " last=" << clip.lastFrame << '\n';
        }
        std::cout << "Poser probe: metadata guards, cancellation, resolver, ABI and lifecycle tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
