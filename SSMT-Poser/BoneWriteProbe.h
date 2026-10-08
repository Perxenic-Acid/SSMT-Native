#pragma once
#include "RigProbe.h"

namespace poser {
struct Quaternion { float x, y, z, w; };
struct Vector3 { float x=0,y=0,z=0; };
namespace bone_detail {
bool Valid(const Quaternion& value);
Quaternion DeltaYaw(const Quaternion& original);
Quaternion DeltaArm(const Quaternion& original);
bool Same(const Quaternion& a, const Quaternion& b);
}
bool InitializeBoneWrite(GenshinNativeRuntime& runtime, const RigCalls& calls, std::ostream& report);
bool ArmBoneWrite(uintptr_t head, uintptr_t parent, uintptr_t root, uintptr_t renderer, uintptr_t animator);
bool BoneWriteArmed();
bool BoneWriteApplied();
void ConfigureArmExperiment();
bool IsArmExperiment();
const wchar_t* BoneWriteTargetName();
// 1=保存原值并单次写入，2=观察，3=重写固定 q1，4=恢复并释放，5=恢复但保留 target roots。
bool BoneWriteStep(unsigned mode);
void ReleaseBoneScratch();
void ReportBoneWrite(std::ostream& report);
bool InstallAnimationReturnProbe(GenshinNativeRuntime& runtime,HANDLE stop,std::ostream& report);
void EnableAnimationReturnWrites(bool enabled);
bool AnimationReturnWritesConfirmed();
bool RemoveAnimationReturnProbe(std::ostream& report);
void ReportAnimationReturnProbe(std::ostream& report);
// 调用方必须在已核验主线程持有强 root；共用已经验证的 Quaternion callee。
bool ReadBoneRotation(uintptr_t object,uintptr_t expectedNative,Quaternion& out);
bool WriteBoneRotation(uintptr_t object,const Quaternion& value);
bool InitializeBoneGeometry(GenshinNativeRuntime& runtime,std::ostream& report);
bool ReadBonePosition(uintptr_t object,uintptr_t expectedNative,Vector3& out,bool world);
bool ReadBoneWorldRotation(uintptr_t object,uintptr_t expectedNative,Quaternion& out);
}
