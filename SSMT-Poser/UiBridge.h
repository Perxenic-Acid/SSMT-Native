#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace poser::ui {
enum class CommandKind { Refresh, BindActor, LoadFile, Unload, Play, Stop, ReleaseRig, ConfigurePhysics, BakePhysics, LoadFace, ConfigureFace, MapFace };
struct Command { CommandKind kind; std::wstring text; uint64_t generation=0; };
struct Rig { std::wstring name,label; unsigned bones=0; bool active=false,selectable=false; };
struct PhysicsBone { unsigned index=0;std::wstring label;bool selected=true; };
struct PhysicsView {
    std::vector<PhysicsBone> bones;
    std::wstring message=L"绑定骨架后识别物理候选骨。";
    uint64_t generation=0;
    unsigned mode=0,preset=1,selected=0,bakedFrames=0,progress=0;
    bool baking=false;
};
struct Snapshot {
    std::vector<Rig> rigs;
    std::wstring actor,path,message=L"等待进入角色场景。";
    uint64_t generation=0,revision=0;
    unsigned tracks=0,bones=0;
    float seconds=0,position=0;
    bool ready=false,loaded=false,playing=false,failed=false,catalogStale=false;
    PhysicsView physics;
    struct FaceView {
        std::wstring path,message=L"绑定骨架后读取当前角色形态键。";
        std::vector<std::wstring> tracks,targets;
        std::vector<int> mapping;
        uint64_t generation=0;
        unsigned mapped=0,mode=0;
        bool overrideNative=false,available=false;
    } face;
};
void Reset(bool enabled);
bool Enabled();
// 仅 GUI / worker 使用；Unity 高频 tick 不访问锁、字符串或此队列。
bool Submit(Command command);
bool Take(Command& command);
Snapshot Read();
void PublishCatalog(std::vector<Rig> rigs);
void MarkCatalogStale();
bool ValidateSelection(const Command& command);
void State(const std::wstring& message,bool ready,bool loaded,bool playing,bool failed=false);
void Bound(const std::wstring& actor,unsigned bones);
void Motion(const std::wstring& path,unsigned tracks,float duration);
void Progress(float position);
void ClearMotion();
void Physics(PhysicsView value);
void Face(Snapshot::FaceView value);
void BakeProgress(unsigned progress);
bool BakeCancelled(); // GUI 提交 Stop 时立即置位，worker 烘焙无需等待命令出队。
}
