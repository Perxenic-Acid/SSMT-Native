#pragma once
#include "VmdMotion.h"
#include <iosfwd>

namespace poser {
struct MotionRigBone {
    uintptr_t object=0,native=0,parentNative=0;
    int parent=-1;
    int skinIndex=-1;
    bool owned=false;
    wchar_t name[128]{};
};
void ConfigureMotionMode();
bool ConfigureFootIK(float gameUnitsPerVmdUnit);
bool IsMotionMode();
bool ArmMotionRig(const RigCalls& calls,uintptr_t root,uintptr_t renderer,uintptr_t animator,
    std::span<const MotionRigBone> bones,uintptr_t activeEntry,uintptr_t activeDescriptor);
bool MotionRigArmed();
// 主线程枚举数组仍持有 root 时，转交同 Actor 的全部 SMR；不要求网格含 Head。
bool CaptureMotionRenderers(uintptr_t renderers,uint64_t count);
void ExportMotionReferences(const std::filesystem::path& directory,std::ostream& report);
bool InitializeMotionReference(GenshinNativeRuntime& runtime,uintptr_t meshClass,const NativeMethod& sharedMesh,std::ostream& report);
// worker 操作，调用前必须完成主线程停止/恢复派发；下一次开始派发发布给主线程。
bool LoadMotionClip(const std::filesystem::path& path,std::ostream& report,HANDLE stop=nullptr);
bool UnloadMotionClip(); // worker，须先经过主线程停止与恢复。
// A/B 配置与切换仅允许在主线程已停止并恢复后，由 worker 调用。
bool ConfigureMotionAB(const std::filesystem::path& directory,std::ostream& report);
bool SelectMotionAB(unsigned command,std::ostream& report); // 6=Legacy，7=Basis，8=下个测试姿态。
void ReportMotionAB(std::ostream& report); // 停止派发同步后输出，热路径不做文件 IO。
unsigned MotionABStage();
std::wstring ReportMotionABStage(unsigned stage,std::ostream& report);
float MotionPosition(); // 原子进度；UI / worker 不读取 tick 的非原子统计。
// 6=恢复并释放 rig roots，7=开始，8=停止并恢复，9=只读捕获静态绑定姿态。
bool MotionMainStep(unsigned mode);
void DisableMotionWrites();
void MotionLateTick(); // SEH leaf，限定已核验 Unity 主线程。
unsigned MotionStatus(); // 0=idle, 1=playing, 2=finished, 3=fault。
void ReportMotion(std::ostream& report);
}
