#include "PoserUi.h"
#include "UiBridge.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <atomic>
#include <thread>
#include <algorithm>
#include <string>
#include <string_view>

namespace poser {
namespace {
constexpr wchar_t ClassName[]=L"SSMT.Poser.ControlPanel.V1";
std::thread thread;
std::atomic<HWND> panel{nullptr},dialog{nullptr};
std::atomic<bool> closing{false};
HANDLE stopEvent=nullptr;
bool previewMode=false;
HWND game=nullptr,rigs=nullptr,path=nullptr,statusLabel=nullptr,actorLabel=nullptr,motionLabel=nullptr,progress=nullptr;
HFONT normalFont=nullptr,titleFont=nullptr;
HBRUSH background=nullptr,fieldBackground=nullptr;
ui::Snapshot view;
uint64_t shownRevision=UINT64_MAX,shownGeneration=UINT64_MAX;
bool previousToggle=false,visible=true;
bool browsing=false;
IFileOpenDialog* picker=nullptr; // 仅 UI 线程使用，跨线程只投递窗口消息。
int scale=100;
int S(int value) {return MulDiv(value,scale,100);}
HWND Control(HWND parent,const wchar_t* type,const wchar_t* text,int id,DWORD style,int x,int y,int width,int height) {
    if (std::wstring_view(type)==L"BUTTON") style=BS_OWNERDRAW|WS_TABSTOP;
    const auto result=CreateWindowExW(type==std::wstring_view(L"EDIT")?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,S(x),S(y),S(width),S(height),parent,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(normalFont),TRUE);return result;
}
void Label(HWND parent,const wchar_t* text,int x,int y,int width,int height,bool title=false) {
    const auto label=Control(parent,L"STATIC",text,0,SS_LEFT,x,y,width,height);
    if (title) SendMessageW(label,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
}
std::wstring Text(HWND control) {
    const auto length=GetWindowTextLengthW(control);if (length<0||length>32767) return {};
    std::wstring result(size_t(length)+1,L'\0');GetWindowTextW(control,result.data(),length+1);result.resize(length);return result;
}
bool Send(ui::CommandKind kind,std::wstring text={},uint64_t generation=0) {
    const auto accepted=ui::Submit({kind,std::move(text),generation});
    if (!accepted) SetWindowTextW(statusLabel,L"命令队列繁忙，请稍后重试。");
    return accepted;
}
void GateControls(HWND window) {
    EnableWindow(GetDlgItem(window,ui_control::Load),view.ready&&!view.playing);
    EnableWindow(GetDlgItem(window,ui_control::Play),view.ready&&view.loaded&&!view.playing);
    EnableWindow(GetDlgItem(window,ui_control::Stop),view.playing);
    EnableWindow(GetDlgItem(window,ui_control::Unload),view.loaded&&!view.playing);
    EnableWindow(GetDlgItem(window,ui_control::Release),view.ready&&!view.playing);
    EnableWindow(GetDlgItem(window,ui_control::Bind),!view.playing&&!view.failed);
    EnableWindow(GetDlgItem(window,ui_control::Refresh),!view.playing&&!view.failed);
    EnableWindow(GetDlgItem(window,ui_control::Browse),!view.playing);
    EnableWindow(path,!view.playing);
}
void Browse(HWND window) {
    if (closing.load()) return;
    Microsoft::WRL::ComPtr<IFileOpenDialog> request;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&request)))) {
        SetWindowTextW(statusLabel,L"文件选择器无法打开，请直接填写动作文件路径。");return;
    }
    FILEOPENDIALOGOPTIONS options{};request->GetOptions(&options);
    request->SetOptions(options|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
    const COMDLG_FILTERSPEC filters[]={{L"VMD 动作文件 (*.vmd)",L"*.vmd"},{L"所有文件",L"*.*"}};
    request->SetFileTypes(UINT(std::size(filters)),filters);request->SetTitle(L"选择角色动作文件");request->SetDefaultExtension(L"vmd");
    // 现代 Shell 对话框不借用进程 CWD；取消和卸载也不能改变游戏的相对路径语义。
    browsing=true;
    picker=request.Get();
    if (SUCCEEDED(request->Show(window))) {
        Microsoft::WRL::ComPtr<IShellItem> selected;PWSTR name=nullptr;
        if (SUCCEEDED(request->GetResult(&selected))&&SUCCEEDED(selected->GetDisplayName(SIGDN_FILESYSPATH,&name))) {
            SetWindowTextW(path,name);CoTaskMemFree(name);
        }
    }
    picker=nullptr;browsing=false;
    dialog.store(nullptr);
}
BOOL CALLBACK FindGame(HWND window,LPARAM) {
    DWORD process=0;GetWindowThreadProcessId(window,&process);
    if (process==GetCurrentProcessId()&&window!=panel.load()&&IsWindowVisible(window)&&!GetWindow(window,GW_OWNER)) {game=window;return FALSE;}
    return TRUE;
}
void Update(HWND window) {
    if (!previewMode&&!game) {EnumWindows(FindGame,0);if (game) SetWindowLongPtrW(window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(game));}
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const auto toggle=foreground==GetCurrentProcessId()&&(GetAsyncKeyState(VK_DECIMAL)&0x8000);
    if (toggle&&!previousToggle) {visible=!visible;ShowWindow(window,visible?SW_SHOWNOACTIVATE:SW_HIDE);}
    previousToggle=toggle;
    if (!previewMode&&game) {
        const auto show=visible&&!IsIconic(game)&&foreground==GetCurrentProcessId();
        ShowWindow(window,show?SW_SHOWNOACTIVATE:SW_HIDE);
        if (show) SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
    auto next=ui::Read();if (next.revision==shownRevision) return;
    const auto priorSelection=int(SendMessageW(rigs,LB_GETCURSEL,0,0));
    const auto selected=priorSelection>=0&&size_t(priorSelection)<view.rigs.size()?view.rigs[priorSelection].name:L"";
    if (next.generation!=shownGeneration) {
        SendMessageW(rigs,LB_RESETCONTENT,0,0);int selection=-1;
        for (size_t i=0;i<next.rigs.size();++i) {
            const auto& rig=next.rigs[i];const auto line=rig.label+L"   ·   "+std::to_wstring(rig.bones)+L" 骨   ·   "+(rig.selectable?L"可绑定":rig.active?L"只读":L"未激活");
            SendMessageW(rigs,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(line.c_str()));
            if (rig.name==selected||(selected.empty()&&rig.selectable)) selection=int(i);
        }
        if (selection>=0) SendMessageW(rigs,LB_SETCURSEL,selection,0);
        shownGeneration=next.generation;
    }
    if (next.path!=view.path&&!next.path.empty()) SetWindowTextW(path,next.path.c_str());
    view=std::move(next);shownRevision=view.revision;
    SetWindowTextW(statusLabel,view.message.c_str());
    const auto actor=view.actor.empty()?L"尚未绑定骨架":L"当前骨架："+view.actor+L"   ·   "+std::to_wstring(view.bones)+L" 骨";
    SetWindowTextW(actorLabel,actor.c_str());
    const auto motion=view.loaded?L"已载入   ·   "+std::to_wstring(view.tracks)+L" 轨道   ·   "+std::to_wstring(int(view.seconds))+L" 秒":L"未载入动作";
    SetWindowTextW(motionLabel,motion.c_str());
    SendMessageW(progress,PBM_SETPOS,view.seconds>0?WPARAM(view.position/view.seconds*1000):0,0);
    GateControls(window);
}
LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if (message==WM_CREATE) {
        Label(window,L"SSMT  Poser",22,18,490,30,true);
        Label(window,L"骨架与动作控制",22,55,490,22);
        Label(window,L"场景中的骨架",22,98,380,22);
        Control(window,L"BUTTON",L"刷新",ui_control::Refresh,BS_PUSHBUTTON,450,92,86,30);
        rigs=Control(window,L"LISTBOX",L"",ui_control::Rigs,LBS_NOTIFY|WS_VSCROLL|WS_BORDER|WS_TABSTOP,22,130,514,154);
        Control(window,L"BUTTON",L"绑定所选骨架",ui_control::Bind,BS_PUSHBUTTON,22,298,166,32);
        Control(window,L"BUTTON",L"释放骨架",ui_control::Release,BS_PUSHBUTTON,202,298,120,32);
        actorLabel=Control(window,L"STATIC",L"尚未绑定骨架",0,SS_LEFT,22,344,514,36);
        Label(window,L"动作文件",22,394,514,22);
        path=Control(window,L"EDIT",L"",ui_control::Path,ES_AUTOHSCROLL|WS_TABSTOP,22,424,414,30);
        SendMessageW(path,EM_SETLIMITTEXT,32767,0);
        Control(window,L"BUTTON",L"选择…",ui_control::Browse,BS_PUSHBUTTON,448,423,88,32);
        Control(window,L"BUTTON",L"载入 / 重载",ui_control::Load,BS_PUSHBUTTON,22,468,142,32);
        Control(window,L"BUTTON",L"载出",ui_control::Unload,BS_PUSHBUTTON,178,468,96,32);
        Control(window,L"BUTTON",L"播放",ui_control::Play,BS_PUSHBUTTON,288,468,116,32);
        Control(window,L"BUTTON",L"停止并恢复",ui_control::Stop,BS_PUSHBUTTON,418,468,118,32);
        motionLabel=Control(window,L"STATIC",L"未载入动作",0,SS_LEFT,22,517,514,24);
        progress=Control(window,PROGRESS_CLASSW,L"",0,0,22,549,514,6);SendMessageW(progress,PBM_SETRANGE32,0,1000);
        statusLabel=Control(window,L"STATIC",L"等待进入角色场景。",0,SS_LEFT,22,578,514,36);
        Label(window,L"NumLock 开启：小键盘 1 播放 · 2 停止 · . 显示 / 隐藏",22,623,514,24);
        Label(window,L"当前为动作预览。物理与完整姿态隔离仍在开发。",22,651,514,24);
        DragAcceptFiles(window,TRUE);SetTimer(window,1,100,nullptr);Update(window);return 0;
    }
    if (message==WM_TIMER) {
        if (picker) {Microsoft::WRL::ComPtr<IOleWindow> ole;HWND handle=nullptr;if (SUCCEEDED(picker->QueryInterface(IID_PPV_ARGS(&ole)))&&SUCCEEDED(ole->GetWindow(&handle))) dialog.store(handle);}
        if (closing.load()||WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0) {
            // 通用文件对话框有嵌套消息循环，先取消它，返回后再销毁 owner。
            if (browsing) {if (const auto active=dialog.load()) PostMessageW(active,WM_COMMAND,IDCANCEL,0);}
            else DestroyWindow(window);
        } else Update(window);
        return 0;
    }
    if (message==WM_COMMAND) {
        if (const auto control=GetDlgItem(window,LOWORD(wParam));control&&!IsWindowEnabled(control)) return 0;
        switch (LOWORD(wParam)) {
        case ui_control::Refresh:Send(ui::CommandKind::Refresh);break;
        case ui_control::Bind:{const auto selected=int(SendMessageW(rigs,LB_GETCURSEL,0,0));if (selected>=0&&size_t(selected)<view.rigs.size()&&view.rigs[selected].selectable) Send(ui::CommandKind::BindActor,view.rigs[selected].name,view.generation);else SetWindowTextW(statusLabel,L"请选择当前激活、可绑定的角色骨架。");break;}
        case ui_control::Browse:Browse(window);break;
        case ui_control::Load:Send(ui::CommandKind::LoadFile,Text(path));break;
        case ui_control::Unload:Send(ui::CommandKind::Unload);break;
        case ui_control::Play:if (Send(ui::CommandKind::Play)) {view.playing=true;GateControls(window);SetWindowTextW(statusLabel,L"正在请求播放，可点击停止取消。");}break;
        case ui_control::Stop:Send(ui::CommandKind::Stop);break;
        case ui_control::Release:Send(ui::CommandKind::ReleaseRig);break;
        default:break;
        }return 0;
    }
    if (message==WM_DROPFILES) {const auto drop=reinterpret_cast<HDROP>(wParam);wchar_t selected[32768]{};if (!view.playing&&DragQueryFileW(drop,0,selected,DWORD(std::size(selected)))) SetWindowTextW(path,selected);DragFinish(drop);return 0;}
    if (message==WM_CLOSE) {visible=false;ShowWindow(window,SW_HIDE);return 0;}
    if (message==WM_CTLCOLORSTATIC||message==WM_CTLCOLORLISTBOX||message==WM_CTLCOLOREDIT) {
        const auto dc=reinterpret_cast<HDC>(wParam);const bool field=message!=WM_CTLCOLORSTATIC;
        SetTextColor(dc,RGB(231,237,245));SetBkColor(dc,field?RGB(31,40,54):RGB(19,27,39));return reinterpret_cast<LRESULT>(field?fieldBackground:background);
    }
    if (message==WM_DRAWITEM) {
        const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lParam);if (item->CtlType!=ODT_BUTTON) return FALSE;
        const bool disabled=(item->itemState&ODS_DISABLED)!=0,pressed=(item->itemState&ODS_SELECTED)!=0;
        const auto color=disabled?RGB(28,35,45):item->CtlID==ui_control::Play?RGB(25,120,94):item->CtlID==ui_control::Stop?RGB(140,64,63):RGB(42,54,71);
        const auto brush=CreateSolidBrush(color);const auto pen=CreatePen(PS_SOLID,1,pressed?RGB(178,196,218):RGB(72,90,112));
        FillRect(item->hDC,&item->rcItem,background);
        const auto oldBrush=SelectObject(item->hDC,brush),oldPen=SelectObject(item->hDC,pen),oldFont=SelectObject(item->hDC,normalFont);
        const auto& r=item->rcItem;RoundRect(item->hDC,r.left,r.top,r.right,r.bottom,S(8),S(8));
        SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,disabled?RGB(103,118,138):RGB(239,244,252));
        auto rect=r;const auto text=Text(item->hwndItem);DrawTextW(item->hDC,text.c_str(),int(text.size()),&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(item->hDC,oldBrush);SelectObject(item->hDC,oldPen);SelectObject(item->hDC,oldFont);DeleteObject(brush);DeleteObject(pen);return TRUE;
    }
    if (message==WM_DESTROY) {KillTimer(window,1);panel.store(nullptr);PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wParam,lParam);
}
void Run() {
    // DPI 设置只影响此 UI 线程，不更改游戏进程或主线程的 DPI 策略。
    const auto previousDpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    scale=MulDiv(int(GetDpiForSystem()),100,96);
    if (!previewMode) EnumWindows(FindGame,0);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(game?game:GetForegroundWindow(),MONITOR_DEFAULTTOPRIMARY),&monitor);
    const auto& work=monitor.rcWork;
    // 高 DPI 的小屏幕仍需完整显示停止按钮与状态，面板初始位置限制在工作区内。
    if (work.bottom>work.top&&work.right>work.left) scale=std::min({scale,std::max(70,int((work.bottom-work.top-80)*100/697)),std::max(70,int((work.right-work.left-60)*100/558))});
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_PROGRESS_CLASS};InitCommonControlsEx(&common);
    background=CreateSolidBrush(RGB(19,27,39));fieldBackground=CreateSolidBrush(RGB(31,40,54));
    normalFont=CreateFontW(-S(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    titleFont=CreateFontW(-S(24),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    WNDCLASSEXW type{};type.cbSize=sizeof(type);type.lpfnWndProc=Procedure;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hbrBackground=background;type.lpszClassName=ClassName;
    const auto registered=RegisterClassExW(&type);
    if (registered||GetLastError()==ERROR_CLASS_ALREADY_EXISTS) {
        RECT rect{0,0,S(558),S(697)};AdjustWindowRectEx(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_TOOLWINDOW|WS_EX_TOPMOST);
        const auto window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,ClassName,L"SSMT Poser",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,work.right-work.left>0?work.right-(rect.right-rect.left)-24:CW_USEDEFAULT,work.bottom-work.top>0?work.top+24:CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,game,nullptr,type.hInstance,nullptr);
        if (window) {
            panel.store(window);BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
            const COLORREF caption=RGB(19,27,39),captionText=RGB(231,237,245);
            DwmSetWindowAttribute(window,35,&caption,sizeof(caption));DwmSetWindowAttribute(window,36,&captionText,sizeof(captionText));
            if (previewMode) ShowWindow(window,SW_SHOW);else if (game) ShowWindow(window,SW_SHOWNOACTIVATE);
            MSG message{};while (GetMessageW(&message,nullptr,0,0)>0) {if (!IsDialogMessageW(window,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}}
        }
        if (registered) UnregisterClassW(ClassName,type.hInstance);
    }
    DeleteObject(normalFont);DeleteObject(titleFont);DeleteObject(background);DeleteObject(fieldBackground);
    normalFont=nullptr;titleFont=nullptr;background=nullptr;fieldBackground=nullptr;
    SetThreadDpiAwarenessContext(previousDpi);
    if (SUCCEEDED(com)) CoUninitialize();
}
}
bool StartPoserUi(HANDLE stop,bool preview) {
    if (thread.joinable()||!stop) return false;stopEvent=stop;previewMode=preview;closing=false;shownRevision=UINT64_MAX;shownGeneration=UINT64_MAX;view={};game=nullptr;visible=true;previousToggle=false;
    try {thread=std::thread([]{try {Run();}catch (...) {const auto window=panel.load();if (window) DestroyWindow(window);}});return true;}catch (...) {return false;}
}
void StopPoserUi() {
    closing=true;if (const auto active=dialog.load()) PostMessageW(active,WM_COMMAND,IDCANCEL,0);
    if (const auto window=panel.load()) PostMessageW(window,WM_TIMER,1,0);
    if (thread.joinable()) thread.join();stopEvent=nullptr;
}
HWND PoserUiWindow() {return panel.load();}
HWND PoserUiFileDialog() {return dialog.load();}
}
