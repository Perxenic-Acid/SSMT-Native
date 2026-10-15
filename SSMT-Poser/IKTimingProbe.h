#pragma once

#include "BoneWriteProbe.h"
#include "GenshinNativeRuntime.h"

namespace poser {

// Read-only timing evidence for the game's own Animator IK callback. The hook
// only records a bounded POD sample and then calls the original method.
bool InstallIKTimingProbe(GenshinNativeRuntime& runtime, DWORD mainThread,
    uintptr_t selectedAnimator, uintptr_t animatorClass, uintptr_t gameObjectClass,
    uintptr_t componentGameObjectEntry, uintptr_t componentGameObjectDescriptor,
    std::ostream& report);
bool RemoveIKTimingProbe(std::ostream& report);
bool IKTimingProbeInstalled();
bool IKTimingHooksReleased(); // worker 完成清理后才可查询。
void ReportIKTimingProbe(std::ostream& report);

// 主线程 LateUpdate 发布的足部目标。快照包含 Animator 身份，角色切换后
// 即使旧回调仍短暂存在也不会继续写入新一帧目标。
struct AnimatorIKTargetSnapshot {
    uintptr_t animator = 0;
    uintptr_t animatorNative = 0;
    uint64_t generation = 0;
    Vector3 leftFoot{};
    Vector3 rightFoot{};
    float leftWeight = 0.0f;
    float rightWeight = 0.0f;
    bool enabled = false;
    bool valid = false;
};

bool PublishAnimatorIKTargets(const AnimatorIKTargetSnapshot& snapshot);
void ClearAnimatorIKTargets();

}
