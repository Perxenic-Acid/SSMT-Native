#include <windows.h>
#include <filesystem>
#include <fstream>
#include <d3d11_1.h>
#include <dxgi1_2.h>

static bool ProbeModCommand(ID3D11Device* device, ID3D11DeviceContext* context)
{
    // 应用请求清成蓝色；INI 的 post clear 命令改为红色，回读验证实际替换路径。
    D3D11_TEXTURE2D_DESC description{};
    description.Width = description.Height = 4;
    description.MipLevels = description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* texture = nullptr;
    ID3D11Texture2D* staging = nullptr;
    ID3D11RenderTargetView* view = nullptr;
    bool ok = false;
    if (SUCCEEDED(device->CreateTexture2D(&description, nullptr, &texture)) &&
        SUCCEEDED(device->CreateRenderTargetView(texture, nullptr, &view))) {
        description.BindFlags = 0;
        description.Usage = D3D11_USAGE_STAGING;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (SUCCEEDED(device->CreateTexture2D(&description, nullptr, &staging))) {
            const float blue[] = {0, 0, 1, 1};
            context->ClearRenderTargetView(view, blue);
            context->CopyResource(staging, texture);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                const auto pixel = static_cast<const unsigned char*>(mapped.pData);
                ok = pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255;
                context->Unmap(staging, 0);
            }
        }
    }
    if (staging) staging->Release();
    if (view) view->Release();
    if (texture) texture->Release();
    return ok;
}

static const wchar_t* graphicsMarker;
static LONG WINAPI GraphicsCrash(EXCEPTION_POINTERS* info)
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &module);
    wchar_t name[MAX_PATH]{};
    GetModuleFileNameW(module, name, MAX_PATH);
    std::ofstream out{std::filesystem::path(graphicsMarker)};
    out << "exception=" << std::hex << info->ExceptionRecord->ExceptionCode
        << " module=" << std::filesystem::path(name).string()
        << " rva=" << reinterpret_cast<UINT_PTR>(info->ExceptionRecord->ExceptionAddress) - reinterpret_cast<UINT_PTR>(module);
    out.close();
    return EXCEPTION_EXECUTE_HANDLER;
}

static int ProbeD3D11(const wchar_t* marker, bool unity)
{
    graphicsMarker = marker;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(GraphicsCrash);
    // 不链接 d3d11.lib，复现 Unity 在远程预加载完成后才加载系统 D3D11 的顺序。
    char directory[MAX_PATH]{};
    GetSystemDirectoryA(directory, MAX_PATH);
    if (GetModuleHandleW(L"d3d11.dll") != GetModuleHandleA((std::filesystem::path(directory) / "d3d11.dll").string().c_str())) {
        std::ofstream(std::filesystem::path(marker)) << "runtime was loaded before system d3d11";
        Sleep(5000);
        return 8;
    }
    HMODULE system = LoadLibraryExA((std::filesystem::path(directory) / "d3d11.dll").string().c_str(), nullptr, 0);
    const auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(system, "D3D11CreateDevice"));
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    const auto driver = GetEnvironmentVariableW(L"SSMT_FIXTURE_HARDWARE", nullptr, 0)
        ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
    if (!create || FAILED(create(nullptr, driver, nullptr, unity ? 0x20 : 0,
        unity ? levels : nullptr, unity ? 4 : 0,
        D3D11_SDK_VERSION, &device, nullptr, &context))) return 5;
    const GUID hackerContext = {0xa3046b1e, 0x336b, 0x4d90, {0x9f, 0xd6, 0x23, 0x4b, 0xc0, 0x9b, 0x86, 0x87}};
    IUnknown* wrapper = nullptr;
    bool wrapped = SUCCEEDED(context->QueryInterface(hackerContext, reinterpret_cast<void**>(&wrapper)));
    if (wrapper) wrapper->Release();
    if (unity) {
        ID3D11Device1* device1 = nullptr;
        ID3D11DeviceContext1* context1 = nullptr;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
            device1->GetImmediateContext1(&context1);
            wrapper = nullptr;
            wrapped = wrapped && context1 && SUCCEEDED(context1->QueryInterface(hackerContext, reinterpret_cast<void**>(&wrapper)));
            if (wrapper) wrapper->Release();
            if (context1) { context1->Draw(0, 0); context1->Release(); }
            device1->Release();
        } else wrapped = false;
        // Unity 分开创建设备与交换链，并可能从交换链重新取得设备。
        HWND window = CreateWindowExW(0, L"STATIC", L"SSMT graphics fixture", WS_OVERLAPPEDWINDOW,
            0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory2* factory = nullptr;
        IDXGISwapChain1* swap = nullptr;
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = 640;
        description.Height = 480;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (window && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
            SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateSwapChainForHwnd(device, window, &description, nullptr, nullptr, &swap))) {
            ID3D11Device* fromSwap = nullptr;
            ID3D11DeviceContext* fromDevice = nullptr;
            if (SUCCEEDED(swap->GetDevice(IID_PPV_ARGS(&fromSwap)))) {
                fromSwap->GetImmediateContext(&fromDevice);
                wrapper = nullptr;
                wrapped = wrapped && fromDevice && SUCCEEDED(fromDevice->QueryInterface(hackerContext, reinterpret_cast<void**>(&wrapper)));
                if (wrapper) wrapper->Release();
                if (fromDevice) {
                    const bool commands = ProbeModCommand(fromSwap, fromDevice);
                    wrapped = wrapped && commands;
                    fromDevice->Draw(0, 0);
                    fromDevice->Release();
                }
                fromSwap->Release();
            } else wrapped = false;
            swap->Present(0, 0);
        } else wrapped = false;
        if (swap) swap->Release();
        if (factory) factory->Release();
        if (adapter) adapter->Release();
        if (dxgiDevice) dxgiDevice->Release();
        if (window) DestroyWindow(window);
    }
    context->Draw(0, 0);
    context->Release();
    device->Release();
    std::ofstream(std::filesystem::path(marker)) << (wrapped ? "runtime wrapped d3d11" : "system d3d11 bypassed runtime");
    Sleep(5000);
    return wrapped ? 0 : 7;
}

int wmain(int argc, wchar_t *argv[])
{
    if (argc < 3)
        return 2;
    if (wcscmp(argv[2], L"--probe-d3d11") == 0)
        return ProbeD3D11(argv[1], false);
    if (wcscmp(argv[2], L"--probe-d3d11-unity") == 0)
        return ProbeD3D11(argv[1], true);
    if (GetEnvironmentVariableW(L"SSMT_FIXTURE_ABSENT", nullptr, 0) != 0)
        return 4;
    wchar_t systemDirectory[MAX_PATH]{};
    GetSystemDirectoryW(systemDirectory, MAX_PATH);
    // 预先加载同名系统 DLL，注入器必须识别测试目录中的另一份 DLL。
    LoadLibraryW((std::filesystem::path(systemDirectory) / L"version.dll").c_str());
    const ULONGLONG deadline = GetTickCount64() + 30000;
    while (GetTickCount64() < deadline)
    {
        bool ready = true;
        for (int index = 2; index < argc; ++index)
            ready = ready && GetModuleHandleW(argv[index]) != nullptr;
        if (ready)
        {
            wchar_t environment[2048]{};
            GetEnvironmentVariableW(L"SSMT_FIXTURE_ENV", environment, 2048);
            std::wofstream(std::filesystem::path(argv[1])) << L"injected" << environment;
            Sleep(5000);
            return 0;
        }
        Sleep(10);
    }
    return 3;
}
