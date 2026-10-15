#include "PoserUi.h"
#include "UiBridge.h"
#include <objidl.h>
#include <gdiplus.h>
#include <stdexcept>
#include <iostream>

using namespace poser;
namespace {
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void Screenshot(HWND window,const wchar_t* path) {
    const auto previousDpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto visible=IsWindowVisible(window);ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
    RECT rect{};Check(GetWindowRect(window,&rect)!=0,"UI bounds unavailable");
    const auto dc=GetDC(window),memory=CreateCompatibleDC(dc);const auto bitmap=CreateCompatibleBitmap(dc,rect.right-rect.left,rect.bottom-rect.top);
    const auto prior=SelectObject(memory,bitmap);
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;Check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"PNG init failed");
    {Gdiplus::Bitmap image(bitmap,nullptr);const CLSID encoder{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};Check(image.Save(path,&encoder)==Gdiplus::Ok,"PNG save failed");}
    Gdiplus::GdiplusShutdown(token);SelectObject(memory,prior);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,dc);
    if(!visible)ShowWindow(window,SW_HIDE);
    SetThreadDpiAwarenessContext(previousDpi);
}
void Test(HANDLE stop,const wchar_t* screenshot) {
    // 真实游戏存在时也不能设置跨线程 owner；否则独立显隐计数会相互覆盖。
    WNDCLASSW gameType{};gameType.lpfnWndProc=DefWindowProcW;gameType.hInstance=GetModuleHandleW(nullptr);gameType.lpszClassName=L"SSMT.Poser.PhysicsUi.Game";
    RegisterClassW(&gameType);
    const auto game=CreateWindowW(gameType.lpszClassName,L"",WS_OVERLAPPED|WS_VISIBLE,-32000,-32000,10,10,nullptr,nullptr,gameType.hInstance,nullptr);
    Check(game!=nullptr,"game window fixture unavailable");
    ui::Reset(true);ui::Command command{ui::CommandKind::Refresh};
    Check(ui::Submit({ui::CommandKind::ConfigurePhysics,L"1 1 22",7})&&ui::Submit({ui::CommandKind::ConfigurePhysics,L"1 1 22 33",7}),"physics configuration queue rejected");
    Check(ui::Take(command)&&command.text==L"1 1 22 33"&&!ui::Take(command),"rapid selection retained outdated settings");
    Check(ui::Submit({ui::CommandKind::BakePhysics,{},7})&&!ui::BakeCancelled(),"new bake retained prior cancellation");
    Check(ui::Submit({ui::CommandKind::Play})&&ui::Submit({ui::CommandKind::Stop})&&ui::BakeCancelled(),"stop did not cancel bake immediately");
    Check(ui::Take(command)&&command.kind==ui::CommandKind::Stop&&!ui::Take(command),"stop left pending bake/play behind");
    ui::Reset(true);ui::Bound(L"尼可 · Nicole",137);ui::Motion(L"桃花笑.vmd",365,64.8f);ui::State(L"骨架与动作已就绪。",true,true,false);
    ui::PhysicsView physics;physics.generation=11;physics.selected=3;
    physics.bones={{22,L"头发 · Bone_HairA01",true},{33,L"衣物 · Bone_DressA01",true},{44,L"饰物 · Bone_BowknotA01 · 末端估计",true}};
    ui::Physics(physics);Check(StartPoserUi(stop,false),"physics UI startup rejected");
    const auto deadline=GetTickCount64()+5000;HWND window=nullptr;
    while(!(window=PoserUiWindow())&&GetTickCount64()<deadline)Sleep(10);
    Check(window!=nullptr,"physics UI unavailable");SendMessageW(window,WM_TIMER,1,0);
    Check(GetWindow(window,GW_OWNER)==nullptr,"Poser panel shares the game's input queue through window ownership");
    const auto pass=GetDlgItem(window,ui_control::CursorPassThrough);wchar_t passLabel[64]{};GetWindowTextW(pass,passLabel,64);
    Check(pass&&std::wstring(passLabel)==L"光标穿透：关闭"&&!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TRANSPARENT),"cursor pass-through did not default off");
    SendMessageW(window,WM_COMMAND,ui_control::CursorPassThrough,0);
    Check((GetWindowLongPtrW(window,GWL_EXSTYLE)&(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE),"cursor pass-through did not enable cross-thread layered hit testing");
    SendMessageW(window,WM_COMMAND,ui_control::CursorPassThrough,0);
    Check(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE)),"cursor pass-through cannot be disabled");
    const auto mode=GetDlgItem(window,ui_control::PhysicsMode),bones=GetDlgItem(window,ui_control::PhysicsBones);
    Check(SendMessageW(mode,CB_GETCOUNT,0,0)==3&&SendMessageW(bones,LB_GETCOUNT,0,0)==3,"physics modes/candidates missing");
    Check(IsWindowEnabled(mode)&&!IsWindowEnabled(bones),"native physics gates incorrect");
    ui::Snapshot::FaceView face;face.generation=31;face.path=L"桃花笑-表情.vmd";face.available=true;
    face.tracks={L"まばたき",L"にやり"};face.targets={L"Face / EyeBlink_L",L"Face / EyeBlink_R"};face.mapping={-1,-1};ui::Face(face);SendMessageW(window,WM_TIMER,1,0);
    const auto faceMode=GetDlgItem(window,ui_control::FaceMode),faceTracks=GetDlgItem(window,ui_control::FaceTracks),faceTargets=GetDlgItem(window,ui_control::FaceTargets);
    Check(SendMessageW(faceMode,CB_GETCOUNT,0,0)==3&&SendMessageW(faceTargets,LB_GETCOUNT,0,0)==2&&IsWindowEnabled(faceMode),"face mode or actor targets unavailable");
    SendMessageW(faceMode,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(ui_control::FaceMode,CBN_SELCHANGE),0);
    Check(ui::Take(command)&&command.kind==ui::CommandKind::ConfigureFace&&command.text==L"1"&&command.generation==31,"face override request lost current actor generation");
    SendMessageW(faceTracks,CB_SETCURSEL,0,0);SendMessageW(faceTargets,LB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,ui_control::FaceMap,0);
    Check(ui::Take(command)&&command.kind==ui::CommandKind::MapFace&&command.text==L"0 1"&&command.generation==31,"manual face mapping request used wrong indices");
    ui::State(L"正在播放",true,true,true);SendMessageW(window,WM_TIMER,1,0);
    Check(!IsWindowEnabled(faceMode)&&!IsWindowEnabled(GetDlgItem(window,ui_control::FaceLoad))&&!IsWindowEnabled(GetDlgItem(window,ui_control::FaceMap)),"face input can change during playback");
    ui::State(L"已停止",true,true,false);SendMessageW(window,WM_TIMER,1,0);
    SendMessageW(mode,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(ui_control::PhysicsMode,CBN_SELCHANGE),0);
    Check(ui::Take(command)&&command.kind==ui::CommandKind::ConfigurePhysics&&command.generation==11&&command.text==L"1 1 22 33 44","physics mode command lost selection");
    physics.mode=1;ui::Physics(physics);SendMessageW(window,WM_TIMER,1,0);
    SendMessageW(bones,LB_SETSEL,FALSE,1);SendMessageW(window,WM_COMMAND,MAKEWPARAM(ui_control::PhysicsBones,LBN_SELCHANGE),0);
    Check(ui::Take(command)&&command.text==L"1 1 22 44","excluded physics bone remained selected");
    physics.mode=2;physics.bones[1].selected=false;physics.selected=2;physics.message=L"已选 2 根骨。点击生成 / 读取。";
    ui::Physics(physics);SendMessageW(window,WM_TIMER,1,0);
    Check(!IsWindowEnabled(GetDlgItem(window,ui_control::Play))&&IsWindowEnabled(GetDlgItem(window,ui_control::BakePhysics)),"unbaked mode can play or cannot bake");
    SendMessageW(window,WM_COMMAND,ui_control::BakePhysics,0);
    Check(ui::Take(command)&&command.kind==ui::CommandKind::BakePhysics&&command.generation==11,"bake command lost rig generation");
    Check(IsWindowEnabled(GetDlgItem(window,ui_control::Stop))&&!IsWindowEnabled(mode)&&!IsWindowEnabled(GetDlgItem(window,ui_control::Load)),"bake can change inputs or cannot cancel");
    SendMessageW(window,WM_COMMAND,ui_control::Stop,0);Check(ui::Take(command)&&command.kind==ui::CommandKind::Stop&&ui::BakeCancelled(),"bake cancel button failed");
    physics.baking=false;physics.bakedFrames=7777;physics.message=L"预烘焙已就绪并缓存，可播放。";ui::Physics(physics);SendMessageW(window,WM_TIMER,1,0);
    Check(IsWindowEnabled(GetDlgItem(window,ui_control::Play)),"ready physics bake cannot play");
    if(screenshot)Screenshot(window,screenshot);
    physics.preset=2;physics.bakedFrames=0;ui::Physics(physics);SendMessageW(window,WM_TIMER,1,0);
    Check(!IsWindowEnabled(GetDlgItem(window,ui_control::Play)),"changed settings retained playable bake");
    ui::Bound({},0);ui::State(L"骨架已释放。",false,true,false);SendMessageW(window,WM_TIMER,1,0);
    Check(SendMessageW(bones,LB_GETCOUNT,0,0)==0&&!IsWindowEnabled(mode),"released rig retained physics selection");
    Check(SendMessageW(faceTargets,LB_GETCOUNT,0,0)==0&&!IsWindowEnabled(faceMode),"released rig retained face target selection");
    SetEvent(stop);StopPoserUi();ui::Reset(false);
    DestroyWindow(game);UnregisterClassW(gameType.lpszClassName,gameType.hInstance);
}
}
int wmain(int argc,wchar_t** argv) {
    const auto stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    try {Check(stop!=nullptr,"stop event unavailable");Test(stop,argc==2?argv[1]:nullptr);CloseHandle(stop);std::cout<<"Physics UI modes, bone selection, bake gates/cancel, cache invalidation and release PASS\n";return 0;}
    catch(const std::exception& error){if(stop)SetEvent(stop);StopPoserUi();if(stop)CloseHandle(stop);std::cerr<<error.what()<<'\n';return 1;}
}
