#include "PoserUi.h"
#include "UiBridge.h"
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <iostream>
#include <stdexcept>
#include <filesystem>

using namespace poser;
namespace {
void Require(bool passed,const char* message) {if (!passed) throw std::runtime_error(message);}
void BridgeTests() {
    ui::Reset(false);Require(!ui::Submit({ui::CommandKind::Play}),"disabled UI accepted command");
    ui::Reset(true);ui::PublishCatalog({{L"Avatar_A",L"角色 A",137,true,true},{L"NPC_A",L"NPC A",66,true,false}});
    const auto snapshot=ui::Read();
    Require(ui::ValidateSelection({ui::CommandKind::BindActor,L"Avatar_A",snapshot.generation}),"current selectable rig rejected");
    Require(!ui::ValidateSelection({ui::CommandKind::BindActor,L"NPC_A",snapshot.generation}),"readonly NPC selectable");
    ui::PublishCatalog(snapshot.rigs);
    Require(!ui::ValidateSelection({ui::CommandKind::BindActor,L"Avatar_A",snapshot.generation}),"stale snapshot accepted");
    ui::PublishCatalog({{L"Avatar_A",L"A",137,true,true},{L"Avatar_A",L"A duplicate",137,true,true}});
    Require(!ui::ValidateSelection({ui::CommandKind::BindActor,L"Avatar_A",ui::Read().generation}),"ambiguous selectable names accepted");
    for (unsigned i=0;i<16;++i) Require(ui::Submit({ui::CommandKind::Play}),"bounded command queue rejected valid capacity");
    Require(!ui::Submit({ui::CommandKind::LoadFile,L"too many"}),"command queue exceeded bound");
    Require(ui::Submit({ui::CommandKind::Stop}),"stop rejected behind queued plays");
    ui::Command command{ui::CommandKind::Refresh};Require(ui::Take(command)&&command.kind==ui::CommandKind::Stop&&!ui::Take(command),"stop did not cancel queued playback");
    ui::Motion(L"C:\\动作\\桃花笑.vmd",167,64.8f);ui::Progress(80);Require(ui::Read().position==64.8f,"progress exceeds duration");
    ui::ClearMotion();Require(!ui::Read().loaded&&ui::Read().path.empty()&&!ui::Read().seconds,"unload retained clip state");
}
void Screenshot(HWND window,const wchar_t* target) {
    const auto prior=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    RECT rect{};Require(GetWindowRect(window,&rect)!=0,"preview bounds unavailable");
    const auto dc=GetDC(window),memory=CreateCompatibleDC(dc);const auto bitmap=CreateCompatibleBitmap(dc,rect.right-rect.left,rect.bottom-rect.top);
    const auto previous=SelectObject(memory,bitmap);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;Require(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"PNG encoder init failed");
    {
        Gdiplus::Bitmap image(bitmap,nullptr);
        const CLSID encoder{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
        Require(image.Save(target,&encoder)==Gdiplus::Ok,"preview PNG save failed");
    }
    Gdiplus::GdiplusShutdown(token);SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,dc);
    SetThreadDpiAwarenessContext(prior);
}
void WindowTests(HANDLE stop,bool preview,const wchar_t* screenshot) {
    ui::Reset(true);
    ui::PublishCatalog({{L"Avatar_Lady_Catalyst_Nicole",L"尼可 · Nicole",137,true,true},{L"Npc_Demo",L"大世界 NPC",66,true,false},{L"Avatar_Demo",L"其他角色",126,false,false}});
    Require(StartPoserUi(stop,preview),"control panel thread creation failed");
    const auto deadline=GetTickCount64()+5000;HWND window=nullptr;
    while (!(window=PoserUiWindow())&&GetTickCount64()<deadline) Sleep(10);
    Require(window!=nullptr,"control panel window unavailable");
    SendMessageW(window,WM_TIMER,1,0);
    Require(SendMessageW(GetDlgItem(window,ui_control::Rigs),LB_GETCOUNT,0,0)==3,"rig catalog not displayed");
    Require(!IsWindowEnabled(GetDlgItem(window,ui_control::Play)),"unloaded clip can play");
    SendMessageW(window,WM_COMMAND,ui_control::Play,0);ui::Command command{ui::CommandKind::Refresh};Require(!ui::Take(command),"disabled play button submitted command");
    SendMessageW(GetDlgItem(window,ui_control::Rigs),LB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,ui_control::Bind,0);
    Require(!ui::Take(command),"readonly NPC bind button submitted command");
    SendMessageW(GetDlgItem(window,ui_control::Rigs),LB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,ui_control::Bind,0);
    Require(ui::Take(command)&&ui::ValidateSelection(command),"selected rig command did not preserve generation");
    ui::Bound(L"尼可 · Nicole",137);ui::State(L"骨架已就绪。选择文件后载入，再点击播放。",true,false,false);
    SendMessageW(window,WM_TIMER,1,0);
    const wchar_t* sample=L"C:\\动作\\桃花笑-常羽·游麟·泛用式.vmd";SetWindowTextW(GetDlgItem(window,ui_control::Path),sample);
    SendMessageW(window,WM_COMMAND,ui_control::Load,0);
    Require(ui::Take(command)&&command.kind==ui::CommandKind::LoadFile&&command.text==sample,"Unicode motion path lost in UI command");
    ui::Motion(sample,167,64.8f);ui::State(L"动作已载入。点击播放开始。",true,true,false);SendMessageW(window,WM_TIMER,1,0);
    Require(IsWindowEnabled(GetDlgItem(window,ui_control::Play))&&!IsWindowEnabled(GetDlgItem(window,ui_control::Stop)),"loaded control states incorrect");
    SendMessageW(window,WM_COMMAND,ui_control::Play,0);Require(ui::Take(command)&&command.kind==ui::CommandKind::Play,"UI play command missing");
    Require(!IsWindowEnabled(GetDlgItem(window,ui_control::Browse))&&IsWindowEnabled(GetDlgItem(window,ui_control::Stop)),"pending play can open modal picker or cannot stop");
    ui::State(L"正在播放。点击停止可恢复现场姿态。",true,true,true);ui::Progress(18);SendMessageW(window,WM_TIMER,1,0);
    Require(IsWindowEnabled(GetDlgItem(window,ui_control::Stop))&&!IsWindowEnabled(GetDlgItem(window,ui_control::Bind)),"playing allows rig replacement or disables stop");
    Require(!IsWindowEnabled(GetDlgItem(window,ui_control::Browse))&&!IsWindowEnabled(GetDlgItem(window,ui_control::Load)),"playing permits modal picker or clip replacement");
    SendMessageW(window,WM_COMMAND,ui_control::Stop,0);Require(ui::Take(command)&&command.kind==ui::CommandKind::Stop,"UI stop command missing");
    ui::State(L"UI 演示 · 动作已停止 · 此窗口不连接游戏。",true,true,false);SendMessageW(window,WM_TIMER,1,0);
    if (screenshot) {ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);Screenshot(window,screenshot);}
    SendMessageW(window,WM_COMMAND,ui_control::Unload,0);Require(ui::Take(command)&&command.kind==ui::CommandKind::Unload,"UI unload command missing");
    SendMessageW(window,WM_CLOSE,0,0);Require(!ui::Take(command)&&!IsWindowVisible(window),"closing UI changed motion state");
    wchar_t before[MAX_PATH]{},after[MAX_PATH]{};GetCurrentDirectoryW(MAX_PATH,before);
    if (preview) {
        ShowWindow(window,SW_SHOWNOACTIVATE);
        PostMessageW(window,WM_COMMAND,ui_control::Browse,0);
        const auto dialogDeadline=GetTickCount64()+5000;
        while (!PoserUiFileDialog()&&GetTickCount64()<dialogDeadline) Sleep(10);
        Require(PoserUiFileDialog()!=nullptr,"native file picker not created");
    }
    SetEvent(stop);StopPoserUi();Require(!PoserUiWindow(),"UI window retained after shutdown");
    GetCurrentDirectoryW(MAX_PATH,after);
    if (std::wstring(before)!=after) std::wcerr << L"Picker directory: " << before << L" -> " << after << L'\n';
    Require(std::wstring(before)==after&&!PoserUiFileDialog(),"picker changed process directory or leaked dialog");
    ResetEvent(stop);Require(StartPoserUi(stop,false),"UI could not initialize again");SetEvent(stop);StopPoserUi();
}
}
int wmain(int argc,wchar_t** argv) {
    HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    try {
        Require(stop!=nullptr,"stop event creation failed");BridgeTests();
        WindowTests(stop,argc>1,argc>2?argv[2]:nullptr);ui::Reset(false);CloseHandle(stop);
        std::cout << "Poser UI: selection guards, Unicode file commands, controls, priority stop and lifecycle passed.\n";return 0;
    } catch (const std::exception& error) {
        if (stop) SetEvent(stop);StopPoserUi();if (stop) CloseHandle(stop);std::cerr << error.what() << '\n';return 1;
    }
}
