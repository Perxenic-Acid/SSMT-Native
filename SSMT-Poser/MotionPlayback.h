#pragma once
#include "VmdMotion.h"
#include <iosfwd>

namespace poser {
struct MotionRigBone {
    uintptr_t object=0,native=0,parentNative=0;
    int parent=-1;
    bool owned=false;
    wchar_t name[128]{};
};
void ConfigureMotionMode();
bool IsMotionMode();
bool ArmMotionRig(const RigCalls& calls,uintptr_t root,uintptr_t renderer,uintptr_t animator,
    std::span<const MotionRigBone> bones,uintptr_t activeEntry,uintptr_t activeDescriptor);
bool MotionRigArmed();
// worker 操作，调用前必须完成主线程停止/恢复派发；下一次开始派发发布给主线程。
bool LoadMotionClip(const std::filesystem::path& path,std::ostream& report,HANDLE stop=nullptr);
// 6=恢复并释放 rig roots，7=开始，8=停止并恢复，保留 rig 供动态重载。
bool MotionMainStep(unsigned mode);
void DisableMotionWrites();
void MotionLateTick(); // SEH leaf，限定已核验 Unity 主线程。
unsigned MotionStatus(); // 0=idle, 1=playing, 2=finished, 3=fault。
void ReportMotion(std::ostream& report);
}
