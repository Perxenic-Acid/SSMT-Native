#include "CursorInput.h"
#include <MinHook.h>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <thread>
#include <future>

namespace {
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int messages=0;
UINT WINAPI OtherHook(){return 432;}
constexpr UINT DisplayCount=WM_APP+1,HideCursor=WM_APP+2,QuitGame=WM_APP+3,TakeMouse=WM_APP+4,CapturedMouse=WM_APP+5;
LRESULT CALLBACK Game(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==DisplayCount){const auto value=ShowCursor(TRUE);ShowCursor(FALSE);return value-1;}
    if(message==HideCursor)return ShowCursor(FALSE);
    if(message==TakeMouse){SetCapture(window);return reinterpret_cast<LRESULT>(GetCapture());}
    if(message==CapturedMouse)return reinterpret_cast<LRESULT>(GetCapture());
    if(message==QuitGame){DestroyWindow(window);PostQuitMessage(0);return 0;}
    if(message==WM_MOUSEMOVE||message==WM_KEYDOWN)++messages;return DefWindowProcW(window,message,w,l);
}
struct GameThread {
    std::thread thread;HWND window=nullptr;
    GameThread() {
        std::promise<HWND> ready;auto future=ready.get_future();
        thread=std::thread([&ready] {
            const auto instance=GetModuleHandleW(nullptr);
            const auto window=CreateWindowW(L"SSMT.Poser.Cursor.Test",L"",WS_OVERLAPPED,0,0,10,10,nullptr,nullptr,instance,nullptr);
            ready.set_value(window);MSG message{};
            if(window)while(GetMessageW(&message,nullptr,0,0)>0)DispatchMessageW(&message);
        });window=future.get();
    }
    ~GameThread(){if(window)PostMessageW(window,QuitGame,0,0);if(thread.joinable())thread.join();}
};
void HiddenCursor() {
    using namespace poser::cursor;
    GameThread game;Check(game.window!=nullptr,"separate game input thread unavailable");
    const auto panel=CreateWindowW(L"SSMT.Poser.Cursor.Test",L"",WS_OVERLAPPED,0,0,10,10,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(panel&&GetWindow(panel,GW_OWNER)==nullptr,"panel must keep an independent input queue");
    const auto original=int(SendMessageW(game.window,DisplayCount,0,0));
    auto hidden=original;while(hidden>=-2)hidden=int(SendMessageW(game.window,HideCursor,0,0));
    Check(SendMessageW(game.window,TakeMouse,0,0)==reinterpret_cast<LRESULT>(game.window),"game mouse capture setup failed");
    Check(Install(),"hidden cursor hooks failed to install");Context(game.window,GetCurrentThreadId());Capture(true);
    Check(SendMessageW(game.window,CapturedMouse,0,0)==0,"existing game mouse capture was not released");
    Check(SendMessageW(game.window,TakeMouse,0,0)==0,"game reacquired mouse while panel was open");
    SetCapture(panel);Check(GetCapture()==panel,"panel control cannot capture mouse during dragging");ReleaseCapture();
    const auto showTarget=reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"ShowCursor"));
    Check(showTarget&&MH_DisableHook(showTarget)==MH_OK,"real visibility counter observation unavailable");
    const auto visible=int(SendMessageW(game.window,DisplayCount,0,0));
    Check(MH_EnableHook(showTarget)==MH_OK,"visibility virtualization not re-enabled");
    Check(visible>=0,"separate game input thread still hides the real cursor");
    Check(int(SendMessageW(game.window,DisplayCount,0,0))==hidden,"capture changed game ShowCursor counter semantics");
    Check(int(SendMessageW(game.window,HideCursor,0,0))==hidden-1,"game hide request was not virtualized");
    Capture(false,false);
    Check(SendMessageW(game.window,TakeMouse,0,0)==reinterpret_cast<LRESULT>(game.window),"game cannot reacquire mouse after panel releases input");
    Check(int(SendMessageW(game.window,DisplayCount,0,0))==hidden-1,"game input thread visibility counter was not restored");
    Check(Uninstall(),"hidden cursor hooks did not unload");
    Check(int(SendMessageW(game.window,DisplayCount,0,0))==hidden-1,"unhooked game visibility counter differs from native requests");
    if(panel)DestroyWindow(panel);
}
void RawPackets() {
    RAWINPUT mouse{};mouse.header.dwType=RIM_TYPEMOUSE;mouse.header.dwSize=sizeof(mouse);mouse.header.hDevice=reinterpret_cast<HANDLE>(1);
    mouse.data.mouse.lLastX=52;mouse.data.mouse.lLastY=-23;mouse.data.mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN;
    poser::cursor::detail::FilterRaw(&mouse,sizeof(mouse));
    Check(!mouse.header.hDevice&&mouse.header.wParam==RIM_INPUTSINK&&!mouse.data.mouse.lLastX&&!mouse.data.mouse.lLastY&&!mouse.data.mouse.usButtonFlags,"raw mouse movement/buttons survived capture");
    RAWINPUT keyboard{};keyboard.header.dwType=RIM_TYPEKEYBOARD;keyboard.header.dwSize=sizeof(keyboard);keyboard.data.keyboard.VKey='A';
    const auto original=keyboard;poser::cursor::detail::FilterRaw(&keyboard,sizeof(keyboard));Check(!std::memcmp(&keyboard,&original,sizeof(keyboard)),"mouse capture changed keyboard input");
    mouse.data.mouse.lLastX=42;poser::cursor::detail::FilterRaw(&mouse,sizeof(RAWINPUTHEADER));Check(mouse.data.mouse.lLastX==42,"truncated raw packet was written");
}
void Lifecycle(HWND game,bool preinitialized) {
    using namespace poser::cursor;
    void* other=nullptr;
    if(preinitialized) {
        Check(MH_Initialize()==MH_OK,"shared MinHook initialization failed");
        Check(MH_CreateHookApiEx(L"user32.dll","GetDoubleClickTime",reinterpret_cast<void*>(&OtherHook),nullptr,&other)==MH_OK&&MH_EnableHook(other)==MH_OK,"shared test hook installation failed");
    }
    Check(Install()&&Available()&&!Active(),"input hooks failed to install or default to capture before UI is visible");
    Context(game,GetCurrentThreadId());POINT original{};Check(GetCursorPos(&original)!=FALSE,"desktop cursor position unavailable");
    Capture(true);Check(Active(),"visible panel did not capture mouse");
    RECT bounds{};Check(GetClipCursor(&bounds)!=FALSE,"released cursor bounds unavailable");
    // 模拟游戏线程；UI 线程随后读取实际指针，验证游戏回中没有移动桌面光标。
    Context(game,0);Check(SetCursorPos(original.x+31,original.y+19)!=FALSE,"suppressed game warp failed");
    POINT reported{};Check(GetCursorPos(&reported)&&reported.x==original.x+31&&reported.y==original.y+19,"game cursor warp bookkeeping changed camera semantics");
    Context(game,GetCurrentThreadId());POINT actual{};Check(GetCursorPos(&actual)&&actual.x==original.x&&actual.y==original.y,"game warp moved the panel cursor");
    RECT requested{bounds.left+2,bounds.top+2,bounds.right-2,bounds.bottom-2};Context(game,0);Check(ClipCursor(&requested)!=FALSE,"suppressed game clip failed");
    RECT observed{};Check(GetClipCursor(&observed)&&EqualRect(&bounds,&observed),"game clip locked the panel cursor");
    BYTE before[256]{};GetKeyboardState(before);BYTE keys[256]{};keys[VK_LBUTTON]=0x80;keys['A']=0x80;Check(SetKeyboardState(keys)!=FALSE,"test keyboard state unavailable");
    BYTE filtered[256]{};Check(GetKeyboardState(filtered)&&filtered[VK_LBUTTON]==0&&(filtered['A']&0x80),"capture changed keyboard or retained mouse buttons");
    Context(game,GetCurrentThreadId());Check(GetKeyboardState(filtered)&&(filtered[VK_LBUTTON]&0x80),"panel controls lost mouse state");SetKeyboardState(before);
    Context(game,0);MSG message{};PostMessageW(game,WM_MOUSEMOVE,0,MAKELPARAM(12,23));
    Check(PeekMessageW(&message,game,WM_MOUSEMOVE,WM_MOUSEMOVE,PM_REMOVE)&&message.message==WM_NULL,"legacy game mouse message survived capture");
    PostMessageW(game,WM_KEYDOWN,'A',0);Check(PeekMessageW(&message,game,WM_KEYDOWN,WM_KEYDOWN,PM_REMOVE)&&message.message==WM_KEYDOWN,"capture swallowed keyboard hotkeys");
    const int prior=messages;MSG sent{};sent.hwnd=game;sent.message=WM_MOUSEMOVE;DispatchMessageW(&sent);Check(messages==prior,"direct game mouse dispatch survived capture");
    sent.message=WM_KEYDOWN;DispatchMessageW(&sent);Check(messages==prior+1,"game keyboard dispatch was blocked");
    Context(game,GetCurrentThreadId());Capture(false,true);
    Check(!Active()&&GetClipCursor(&observed)&&EqualRect(&requested,&observed),"pass-through/hide did not restore latest game clip");
    Capture(true);Context(game,0);ClipCursor(&requested);Context(game,GetCurrentThreadId());Capture(false,false);
    Check(GetClipCursor(&observed)&&EqualRect(&bounds,&observed),"Alt-Tab restored game clip onto another application");
    Check(Uninstall()&&!Available()&&!Active(),"input hooks did not shut down");
    const auto state=MH_Initialize();
    Check(state==(preinitialized?MH_ERROR_ALREADY_INITIALIZED:MH_OK),"input cleanup changed shared MinHook ownership");
    if(preinitialized)Check(GetDoubleClickTime()==432&&MH_DisableHook(other)==MH_OK&&MH_RemoveHook(other)==MH_OK,"cursor cleanup removed another component's hook");
    Check(MH_Uninitialize()==MH_OK,"test MinHook cleanup failed");
}
}
int main() {
    RECT clip{};const bool hadClip=GetClipCursor(&clip)!=FALSE;
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSW type{};type.lpfnWndProc=Game;type.hInstance=instance;type.lpszClassName=L"SSMT.Poser.Cursor.Test";RegisterClassW(&type);
    const auto window=CreateWindowW(type.lpszClassName,L"",WS_OVERLAPPED,0,0,10,10,nullptr,nullptr,instance,nullptr);
    int result=0;
    try{Check(window!=nullptr,"test game window unavailable");RawPackets();Lifecycle(window,false);Lifecycle(window,true);HiddenCursor();std::cout<<"Cursor capture, hidden game cursor, game/UI isolation, raw/legacy mouse, keyboard, clip restore and shared MinHook ownership PASS\n";}
    catch(const std::exception& error){poser::cursor::Uninstall();std::cerr<<error.what()<<'\n';result=1;}
    ClipCursor(hadClip?&clip:nullptr);if(window)DestroyWindow(window);UnregisterClassW(type.lpszClassName,instance);return result;
}
