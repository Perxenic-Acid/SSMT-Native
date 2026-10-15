#pragma once
#include "VmdMotion.h"
#include "UiBridge.h"
#include <iosfwd>

namespace poser::face {
// 接口由同版本元数据与 ABI 指纹共同门控；所有对象读写均复用已核验主线程。
bool Initialize(GenshinNativeRuntime& runtime,std::ostream& report);
void BeginInventory(const RigCalls& calls);
void Capture(uintptr_t renderer,uintptr_t native,uintptr_t mesh,uintptr_t meshNative,const wchar_t* label);
void Release(); // 主线程，先恢复，再释放强 GC roots。
void Publish(std::ostream& report);
bool Load(const std::filesystem::path& path,std::ostream& report,HANDLE stop=nullptr);
void UseMotion(const vmd::Clip& clip,const std::filesystem::path& path,std::ostream& report,HANDLE stop);
bool Configure(const ui::Command& command,std::ostream& report);
bool Start();
bool Tick(double frame);
bool Stop();
void Report(std::ostream& report);
}
