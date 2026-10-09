#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace poser::ui {
enum class CommandKind { Refresh, BindActor, LoadFile, Unload, Play, Stop, ReleaseRig };
struct Command { CommandKind kind; std::wstring text; uint64_t generation=0; };
struct Rig { std::wstring name,label; unsigned bones=0; bool active=false,selectable=false; };
struct Snapshot {
    std::vector<Rig> rigs;
    std::wstring actor,path,message=L"等待进入角色场景。";
    uint64_t generation=0,revision=0;
    unsigned tracks=0,bones=0;
    float seconds=0,position=0;
    bool ready=false,loaded=false,playing=false,failed=false;
};
void Reset(bool enabled);
bool Enabled();
// 仅 GUI / worker 使用；Unity 高频 tick 不访问锁、字符串或此队列。
bool Submit(Command command);
bool Take(Command& command);
Snapshot Read();
void PublishCatalog(std::vector<Rig> rigs);
bool ValidateSelection(const Command& command);
void State(const std::wstring& message,bool ready,bool loaded,bool playing,bool failed=false);
void Bound(const std::wstring& actor,unsigned bones);
void Motion(const std::wstring& path,unsigned tracks,float duration);
void Progress(float position);
void ClearMotion();
}
