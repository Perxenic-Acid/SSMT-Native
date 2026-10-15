#pragma once
#include <Windows.h>

namespace poser::cursor {
// UI 线程负责切换状态；StopPoserUi 在 worker 移除探针后才卸载输入 hooks。
bool Install();
bool Uninstall(bool sharedHooksReleased=true);
void Context(HWND game,DWORD uiThread);
void Capture(bool active,bool restoreClip=true);
bool Available();
bool Active();
namespace detail {
// 只清除鼠标载荷，保留键盘、设备包大小及返回值语义。
void FilterRaw(RAWINPUT* input,UINT bytes);
}
}
