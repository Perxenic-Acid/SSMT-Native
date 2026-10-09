#pragma once
#include <Windows.h>
namespace poser {
// 独立 UI 线程拥有窗口；不改游戏 WndProc，不使用 Present 或 Unity callback 绘制。
bool StartPoserUi(HANDLE stop,bool preview=false);
void StopPoserUi();
HWND PoserUiWindow(); // 仅测试 / 生命周期查询；窗口 handle 不代表 Unity 对象。
HWND PoserUiFileDialog();
namespace ui_control {
constexpr int Rigs=101,Bind=102,Refresh=103,Path=104,Browse=105,Load=106,Unload=107,Play=108,Stop=109,Release=110;
}
}
