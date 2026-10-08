#pragma once
#include "GenshinNativeRuntime.h"
#include <filesystem>

namespace poser {
struct RigCalls {
    NativeCallContext context;
    uintptr_t transformClass = 0, gameObjectClass = 0;
    uintptr_t name = 0, nameMethod = 0, transform = 0, transformMethod = 0;
    uintptr_t gameObject = 0, gameObjectMethod = 0, child = 0, childMethod = 0;
    NativeMethod human, bones;
    uintptr_t rendererClass = 0, animatorClass = 0;
};
// 沿用同一主线程派发和 GC 生命周期；本模块不创建自己的 hook 或 native resolver。
bool InitializeRigProbe(GenshinNativeRuntime& runtime, const RigCalls& calls, std::ostream& report);
bool CaptureRig(uintptr_t renderer, uintptr_t transform, uintptr_t bones, uint64_t length,
    uintptr_t renderers, uint64_t rendererCount, uintptr_t animators, uint64_t animatorCount);
void ReleaseRigProbe(); // 仅由受保护的主线程 callback 调用。
bool ExportRigProbe(const std::filesystem::path& directory, std::ostream& report);
void SetRigWriteTarget(const std::wstring& name);
bool RigHasTarget();
void BeginRigSelection();
int MatchRigCandidate(uintptr_t renderer); // 1=已配置 Actor 的活动 renderer，0=其他候选，-1=读取失败。
}
