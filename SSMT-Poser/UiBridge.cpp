#include "UiBridge.h"
#include <atomic>
#include <algorithm>
#include <deque>
#include <mutex>

namespace poser::ui {
namespace { std::mutex mutex;std::deque<Command> commands;Snapshot snapshot;std::atomic<bool> enabled{false}; }
void Reset(bool value) {std::lock_guard lock(mutex);commands.clear();snapshot={};enabled=value;}
bool Enabled() {return enabled.load();}
bool Submit(Command command) {
    if (!Enabled()) return false;
    std::lock_guard lock(mutex);
    if (command.kind==CommandKind::Stop) {
        // 停止命令优先，且取消尚未执行的播放，避免停止后被积压的点击重新启动。
        std::erase_if(commands,[](const Command& pending){return pending.kind==CommandKind::Play;});
        if (commands.size()>=16) commands.pop_back();commands.push_front(std::move(command));return true;
    }
    if (commands.size()>=16||command.text.size()>32767) return false;
    commands.push_back(std::move(command));return true;
}
bool Take(Command& command) {
    std::lock_guard lock(mutex);if (commands.empty()) return false;
    command=std::move(commands.front());commands.pop_front();return true;
}
Snapshot Read() {std::lock_guard lock(mutex);return snapshot;}
void PublishCatalog(std::vector<Rig> rigs) {
    std::lock_guard lock(mutex);snapshot.rigs=std::move(rigs);++snapshot.generation;++snapshot.revision;
}
bool ValidateSelection(const Command& command) {
    std::lock_guard lock(mutex);
    if (command.kind!=CommandKind::BindActor||command.generation!=snapshot.generation||command.text.empty()) return false;
    return std::count_if(snapshot.rigs.begin(),snapshot.rigs.end(),[&](const Rig& rig){return rig.selectable&&rig.name==command.text;})==1;
}
void State(const std::wstring& text,bool ready,bool loaded,bool playing,bool failed) {
    std::lock_guard lock(mutex);snapshot.message=text;snapshot.ready=ready;snapshot.loaded=loaded;snapshot.playing=playing;snapshot.failed=failed;++snapshot.revision;
}
void Bound(const std::wstring& actor,unsigned bones) {std::lock_guard lock(mutex);snapshot.actor=actor;snapshot.bones=bones;++snapshot.revision;}
void Motion(const std::wstring& path,unsigned tracks,float duration) {std::lock_guard lock(mutex);snapshot.path=path;snapshot.tracks=tracks;snapshot.seconds=duration;snapshot.loaded=true;snapshot.position=0;++snapshot.revision;}
void Progress(float position) {std::lock_guard lock(mutex);snapshot.position=std::clamp(position,0.0f,snapshot.seconds);++snapshot.revision;}
void ClearMotion() {std::lock_guard lock(mutex);snapshot.path.clear();snapshot.tracks=0;snapshot.seconds=0;snapshot.position=0;snapshot.loaded=false;++snapshot.revision;}
}
