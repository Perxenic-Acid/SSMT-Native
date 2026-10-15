#pragma once
#include "GenshinNativeRuntime.h"
#include "BoneWriteProbe.h"
#include <filesystem>
#include <span>

namespace poser::native_physics {
struct Bone {
    unsigned index=0;
    uintptr_t transform=0,native=0;
    const wchar_t* name=nullptr;
    int parent=-1;
    Vector3 staticLocalPosition{};
    Quaternion staticLocalRotation{0,0,0,1};
    bool staticLocalReady=false;
};
struct Mesh { uintptr_t object=0,native=0;const wchar_t* name=nullptr; };
// worker 配置，随后在既有主线程 STATIC 捕获派发中只读采集；不调用候选物理方法。
void Initialize(GenshinNativeRuntime& runtime,uintptr_t animator,std::span<const uintptr_t> components,std::ostream& report);
void Capture(uintptr_t animator,std::span<const Bone> staticBones,std::span<const Mesh> meshes);
// worker 导出冻结的数据；这里不再访问游戏对象。
void Export(const std::filesystem::path& directory,std::ostream& report);
}
