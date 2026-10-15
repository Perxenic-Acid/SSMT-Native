#include "CursorInput.h"
#include <MinHook.h>
#include <atomic>
#include <array>
#include <cstring>

namespace poser::cursor {
namespace {
std::atomic<bool> active{false},available{false};
std::atomic<DWORD> uiThread{0};
std::atomic<DWORD> gameThread{0};
std::atomic<HWND> gameWindow{nullptr};
std::atomic<uint64_t> gamePosition{0};
std::atomic<HCURSOR> gameCursor{nullptr};
std::atomic<unsigned> inFlight{0};
HCURSOR arrow=nullptr;
bool ownsMinHook=false;
SRWLOCK clipLock=SRWLOCK_INIT;
RECT lastClip{};
bool clipRequested=false;
HHOOK windowHook=nullptr;
UINT refreshMessage=0;
thread_local bool visibleOverride=false;
thread_local int virtualDisplay=0,actualDisplay=0;

decltype(&ClipCursor) originalClip=nullptr;
decltype(&SetCursorPos) originalWarp=nullptr;
decltype(&GetCursorPos) originalPosition=nullptr;
decltype(&SetCursor) originalCursor=nullptr;
decltype(&ShowCursor) originalShow=nullptr;
decltype(&SetCapture) originalCapture=nullptr;
decltype(&GetAsyncKeyState) originalAsync=nullptr;
decltype(&GetKeyState) originalKey=nullptr;
decltype(&GetKeyboardState) originalKeyboard=nullptr;
decltype(&GetRawInputData) originalRaw=nullptr;
decltype(&GetRawInputBuffer) originalRawBuffer=nullptr;
decltype(&PeekMessageW) originalPeekW=nullptr;
decltype(&PeekMessageA) originalPeekA=nullptr;
decltype(&DispatchMessageW) originalDispatchW=nullptr;
decltype(&DispatchMessageA) originalDispatchA=nullptr;

struct Call {
    Call(){inFlight.fetch_add(1,std::memory_order_acq_rel);}
    ~Call(){inFlight.fetch_sub(1,std::memory_order_acq_rel);}
};
void Visibility() {
    if(!originalShow)return;
    const auto current=GetCurrentThreadId();
    const bool wanted=active.load(std::memory_order_acquire)&&
        (current==uiThread.load(std::memory_order_relaxed)||current==gameThread.load(std::memory_order_relaxed));
    if(wanted&&!visibleOverride) {
        actualDisplay=originalShow(TRUE);virtualDisplay=actualDisplay-1;
        while(actualDisplay<0)actualDisplay=originalShow(TRUE);
        visibleOverride=true;originalCursor(arrow);
    } else if(!wanted&&visibleOverride) {
        // 在所属输入线程恢复显隐计数，包括接管期间游戏提出的显隐请求。
        while(actualDisplay<virtualDisplay)actualDisplay=originalShow(TRUE);
        while(actualDisplay>virtualDisplay)actualDisplay=originalShow(FALSE);
        visibleOverride=false;
    }
}
bool Blocking(){Visibility();return active.load(std::memory_order_acquire)&&GetCurrentThreadId()!=uiThread.load(std::memory_order_relaxed);}
bool GameTarget(HWND window) {
    const auto game=gameWindow.load(std::memory_order_relaxed);
    return game&&window&&(window==game||IsChild(game,window));
}
void ReleaseGameCapture() {
    // ReleaseCapture 只作用于调用线程，必须在游戏窗口线程的消息回调执行。
    if(active.load(std::memory_order_acquire)&&GetCurrentThreadId()==gameThread.load(std::memory_order_relaxed)&&GameTarget(GetCapture()))ReleaseCapture();
}
LRESULT CALLBACK WindowMessages(int code,WPARAM w,LPARAM l) {
    Call call;
    if(code>=0&&l) {
        const auto* message=reinterpret_cast<const CWPSTRUCT*>(l);
        if(message->message==refreshMessage||message->message==WM_SETCURSOR){Visibility();ReleaseGameCapture();}
    }
    return CallNextHookEx(windowHook,code,w,l);
}
void RefreshGameCursor() {
    Visibility();
    const auto game=gameWindow.load(std::memory_order_relaxed);
    if(game&&windowHook){DWORD_PTR result=0;SendMessageTimeoutW(game,refreshMessage,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,100,&result);}
}
uint64_t Pack(int x,int y){return uint32_t(x)|(uint64_t(uint32_t(y))<<32);}
bool MouseKey(int key){return key==VK_LBUTTON||key==VK_RBUTTON||key==VK_MBUTTON||key==VK_XBUTTON1||key==VK_XBUTTON2;}

BOOL WINAPI Clip(const RECT* rect) {
    Call call;
    if(GetCurrentThreadId()==uiThread.load(std::memory_order_relaxed))return originalClip(rect);
    AcquireSRWLockExclusive(&clipLock);
    const bool blocked=active.load(std::memory_order_acquire);
    if(blocked){clipRequested=rect!=nullptr;if(rect)lastClip=*rect;}
    const auto result=originalClip(blocked?nullptr:rect);
    ReleaseSRWLockExclusive(&clipLock);return result;
}
BOOL WINAPI Warp(int x,int y) {
    Call call;if(!Blocking())return originalWarp(x,y);
    gamePosition.store(Pack(x,y),std::memory_order_relaxed);return TRUE;
}
BOOL WINAPI Position(LPPOINT point) {
    Call call;if(!Blocking()||!point)return originalPosition(point);
    const auto value=gamePosition.load(std::memory_order_relaxed);point->x=int32_t(value);point->y=int32_t(value>>32);return TRUE;
}
HCURSOR WINAPI Cursor(HCURSOR value) {
    Call call;if(!Blocking())return originalCursor(value);
    gameCursor.store(value,std::memory_order_relaxed);return originalCursor(arrow);
}
int WINAPI Show(BOOL show) {
    Call call;Visibility();
    if(visibleOverride)return virtualDisplay+=show?1:-1;
    return originalShow(show);
}
HWND WINAPI MouseCapture(HWND window) {
    Call call;
    if(Blocking()&&GameTarget(window)){const auto previous=GetCapture();ReleaseGameCapture();return previous;}
    return originalCapture(window);
}
SHORT WINAPI Async(int key){Call call;return Blocking()&&MouseKey(key)?0:originalAsync(key);}
SHORT WINAPI Key(int key){Call call;return Blocking()&&MouseKey(key)?0:originalKey(key);}
BOOL WINAPI Keyboard(PBYTE keys) {
    Call call;const auto result=originalKeyboard(keys);
    if(result&&keys&&Blocking())for(const int key:{VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2})keys[key]=0;
    return result;
}
UINT WINAPI Raw(HRAWINPUT input,UINT command,LPVOID data,PUINT bytes,UINT headerSize) {
    Call call;const auto result=originalRaw(input,command,data,bytes,headerSize);
    if(result!=UINT(-1)&&command==RID_INPUT&&data&&Blocking())detail::FilterRaw(static_cast<RAWINPUT*>(data),result);
    return result;
}
UINT WINAPI RawBuffer(PRAWINPUT data,PUINT bytes,UINT headerSize) {
    Call call;const auto capacity=bytes?*bytes:0;const auto result=originalRawBuffer(data,bytes,headerSize);
    if(result==UINT(-1)||!data||!Blocking())return result;
    // 使用调用者原有容量检查每个变长包；8 kHz 路径不分配、不加锁。
    auto current=reinterpret_cast<unsigned char*>(data);const auto end=current+capacity;
    for(UINT i=0;i<result&&size_t(end-current)>=sizeof(RAWINPUTHEADER);++i) {
        auto packet=reinterpret_cast<RAWINPUT*>(current);const auto size=packet->header.dwSize;
        if(size<sizeof(RAWINPUTHEADER)||size>size_t(end-current))break;
        detail::FilterRaw(packet,size);
        const auto aligned=(size+sizeof(void*)-1)&~(sizeof(void*)-1);
        if(aligned>size_t(end-current))break;current+=aligned;
    }
    return result;
}
void FilterMessage(LPMSG message,bool removed,bool wide) {
    if(!message||!Blocking())return;
    const auto game=gameWindow.load(std::memory_order_relaxed);
    if(!game||(message->hwnd!=game&&!IsChild(game,message->hwnd)))return;
    bool mouse=message->message>=WM_MOUSEFIRST&&message->message<=WM_MOUSELAST;
    mouse|=message->message>=WM_NCMOUSEMOVE&&message->message<=WM_NCXBUTTONDBLCLK;
    if(message->message==WM_INPUT) {
        RAWINPUTHEADER header{};UINT bytes=sizeof(header);
        mouse=originalRaw(reinterpret_cast<HRAWINPUT>(message->lParam),RID_HEADER,&header,&bytes,sizeof(header))!=UINT(-1)&&header.dwType==RIM_TYPEMOUSE;
        if(mouse&&removed&&GET_RAWINPUT_CODE_WPARAM(message->wParam)==RIM_INPUT) {
            // 前台 WM_INPUT 的 Win32 清理仍必须执行，不能直接丢掉消息。
            if(wide)DefWindowProcW(message->hwnd,message->message,message->wParam,message->lParam);
            else DefWindowProcA(message->hwnd,message->message,message->wParam,message->lParam);
        }
    }
    if(mouse){message->message=WM_NULL;message->wParam=0;message->lParam=0;}
}
BOOL WINAPI PeekW(LPMSG message,HWND window,UINT min,UINT max,UINT remove) {
    Call call;const auto result=originalPeekW(message,window,min,max,remove);if(result)FilterMessage(message,(remove&PM_REMOVE)!=0,true);return result;
}
BOOL WINAPI PeekA(LPMSG message,HWND window,UINT min,UINT max,UINT remove) {
    Call call;const auto result=originalPeekA(message,window,min,max,remove);if(result)FilterMessage(message,(remove&PM_REMOVE)!=0,false);return result;
}
LRESULT WINAPI DispatchW(const MSG* message) {
    Call call;if(!message||!Blocking())return originalDispatchW(message);
    auto filtered=*message;FilterMessage(&filtered,true,true);return filtered.message==WM_NULL?0:originalDispatchW(&filtered);
}
LRESULT WINAPI DispatchA(const MSG* message) {
    Call call;if(!message||!Blocking())return originalDispatchA(message);
    auto filtered=*message;FilterMessage(&filtered,true,false);return filtered.message==WM_NULL?0:originalDispatchA(&filtered);
}
struct Hook {const char* name;void* detour;void** original;void* target=nullptr;bool created=false;};
#define ENTRY(name,detour,original) Hook{#name,reinterpret_cast<void*>(&detour),reinterpret_cast<void**>(&original)}
std::array hooks{
    ENTRY(ClipCursor,Clip,originalClip),ENTRY(SetCursorPos,Warp,originalWarp),ENTRY(GetCursorPos,Position,originalPosition),ENTRY(SetCursor,Cursor,originalCursor),
    ENTRY(ShowCursor,Show,originalShow),
    ENTRY(SetCapture,MouseCapture,originalCapture),
    ENTRY(GetAsyncKeyState,Async,originalAsync),ENTRY(GetKeyState,Key,originalKey),ENTRY(GetKeyboardState,Keyboard,originalKeyboard),
    ENTRY(GetRawInputData,Raw,originalRaw),ENTRY(GetRawInputBuffer,RawBuffer,originalRawBuffer),
    ENTRY(PeekMessageW,PeekW,originalPeekW),ENTRY(PeekMessageA,PeekA,originalPeekA),ENTRY(DispatchMessageW,DispatchW,originalDispatchW),ENTRY(DispatchMessageA,DispatchA,originalDispatchA)};
#undef ENTRY
void Pin(){HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Install),&module);}
}
namespace detail {
void FilterRaw(RAWINPUT* input,UINT bytes) {
    if(!input||bytes<sizeof(RAWINPUTHEADER)+sizeof(RAWMOUSE)||input->header.dwType!=RIM_TYPEMOUSE||input->header.dwSize>bytes||input->header.dwSize<sizeof(RAWINPUTHEADER)+sizeof(RAWMOUSE))return;
    input->header.hDevice=nullptr;input->header.wParam=RIM_INPUTSINK;std::memset(&input->data.mouse,0,sizeof(RAWMOUSE));
}
}
bool Available(){return available.load(std::memory_order_acquire);}
bool Active(){return active.load(std::memory_order_acquire);}
void Context(HWND game,DWORD thread){
    const auto previous=gameWindow.exchange(game,std::memory_order_relaxed);uiThread.store(thread,std::memory_order_relaxed);
    if(previous==game)return;
    if(windowHook){UnhookWindowsHookEx(windowHook);windowHook=nullptr;}
    const auto id=game?GetWindowThreadProcessId(game,nullptr):0;gameThread.store(id,std::memory_order_relaxed);
    if(id&&id!=thread) {
        HMODULE module=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&WindowMessages),&module);
        windowHook=SetWindowsHookExW(WH_CALLWNDPROC,WindowMessages,module,id);
    }
}
void Capture(bool wanted,bool restoreClip) {
    if(!Available())return;
    bool changed=false;
    AcquireSRWLockExclusive(&clipLock);
    if(wanted!=active.load(std::memory_order_acquire)) {
        changed=true;
        if(wanted) {
            POINT position{};originalPosition(&position);gamePosition.store(Pack(position.x,position.y),std::memory_order_relaxed);
            gameCursor.store(GetCursor(),std::memory_order_relaxed);clipRequested=GetClipCursor(&lastClip)!=FALSE;
            active.store(true,std::memory_order_release);originalClip(nullptr);originalCursor(arrow);
        } else {
            active.store(false,std::memory_order_release);
            // 切到别的应用时禁止恢复游戏的矩形，防止把桌面光标锁回游戏。
            originalClip(restoreClip&&clipRequested?&lastClip:nullptr);originalCursor(gameCursor.load(std::memory_order_relaxed));
        }
    }
    ReleaseSRWLockExclusive(&clipLock);
    if(changed)RefreshGameCursor();
}
bool Install() {
    if(Available())return true;
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    ownsMinHook=init==MH_OK;arrow=LoadCursorW(nullptr,IDC_ARROW);refreshMessage=RegisterWindowMessageW(L"SSMT.Poser.CursorRefresh.V1");
    for(auto& hook:hooks) {
        auto result=MH_CreateHookApiEx(L"user32.dll",hook.name,hook.detour,hook.original,&hook.target);
        if(result==MH_OK){hook.created=true;result=MH_EnableHook(hook.target);}
        if(result!=MH_OK){Uninstall();return false;}
    }
    available.store(true,std::memory_order_release);return true;
}
bool Uninstall(bool sharedHooksReleased) {
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);Capture(false,foreground==GetCurrentProcessId());
    available.store(false,std::memory_order_release);active.store(false,std::memory_order_release);
    bool passed=true;
    if(windowHook){passed=UnhookWindowsHookEx(windowHook)!=FALSE;if(passed)windowHook=nullptr;}
    gameWindow.store(nullptr,std::memory_order_relaxed);gameThread.store(0,std::memory_order_relaxed);
    for(auto& hook:hooks)if(hook.created){const auto result=MH_DisableHook(hook.target);passed&=result==MH_OK||result==MH_ERROR_DISABLED;}
    const auto deadline=GetTickCount64()+3000;while(inFlight.load(std::memory_order_acquire)&&GetTickCount64()<deadline)Sleep(1);
    passed&=inFlight.load(std::memory_order_acquire)==0;
    if(passed)for(auto& hook:hooks)if(hook.created){const auto result=MH_RemoveHook(hook.target);passed&=result==MH_OK;if(result==MH_OK)hook.created=false;}
    // 不使用 MH_ALL_HOOKS；共享动画/IK 的在途 trampoline 不能被输入 UI 提前释放。
    if(passed&&ownsMinHook&&sharedHooksReleased){passed=MH_Uninitialize()==MH_OK;if(passed)ownsMinHook=false;}
    if(!passed||!sharedHooksReleased)Pin();
    return passed&&sharedHooksReleased;
}
}
