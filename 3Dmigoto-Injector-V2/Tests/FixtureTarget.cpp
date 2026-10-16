#include <windows.h>
#include <filesystem>
#include <fstream>
#include <d3d11.h>

static int ProbeD3D11(const wchar_t* marker)
{
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
    if (!create || FAILED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context))) return 5;
    const GUID hackerContext = {0xa3046b1e, 0x336b, 0x4d90, {0x9f, 0xd6, 0x23, 0x4b, 0xc0, 0x9b, 0x86, 0x87}};
    IUnknown* wrapper = nullptr;
    const bool wrapped = SUCCEEDED(context->QueryInterface(hackerContext, reinterpret_cast<void**>(&wrapper)));
    if (wrapper) wrapper->Release();
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
        return ProbeD3D11(argv[1]);
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
