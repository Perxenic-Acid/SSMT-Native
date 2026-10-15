// 使用真实 Bridge/OptiScaler 的 FSR2 入口验证 API 选择，不依赖游戏或替换游戏文件。
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <fsr2/ffx_fsr2.h>
#include <cstdio>
#include <cwchar>

static LONG WINAPI crash(EXCEPTION_POINTERS* info)
{
    std::printf("exception=%08lx address=%p\n", info->ExceptionRecord->ExceptionCode,
        info->ExceptionRecord->ExceptionAddress);
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &module)) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        const auto* separator = std::wcsrchr(path, L'\\');
        std::printf("module=%ls rva=%llx\n", separator ? separator + 1 : path,
            reinterpret_cast<ULONG_PTR>(info->ExceptionRecord->ExceptionAddress) - reinterpret_cast<ULONG_PTR>(module));
    }
    std::fflush(stdout);
    ExitProcess(10);
    return EXCEPTION_EXECUTE_HANDLER;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) return 1; // ReShade、Bridge、OptiScaler；此夹具仅验证 FSR2 输入契约。
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(crash);
    // 在第三方 Hook 前选卡，避免 GPU 厂商伪装影响测试的硬件选择。
    IDXGIFactory* factory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)))) return 4;
    IDXGIAdapter* selected = nullptr;
    for (UINT index = 0; ; ++index) {
        IDXGIAdapter* adapter = nullptr;
        if (factory->EnumAdapters(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(adapter->GetDesc(&desc)) && desc.VendorId == 0x10de) {
            selected = adapter;
            break;
        }
        adapter->Release();
    }
    factory->Release();
    if (!selected) return 5;
    const auto system = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(system, "D3D11CreateDevice"));
    HMODULE modules[3]{};
    for (int i = 0; i < 3; ++i) {
        modules[i] = LoadLibraryW(argv[i + 1]);
        if (!modules[i]) {
            std::printf("LoadLibrary failed index=%d error=%lu path=%ls\n", i, GetLastError(), argv[i + 1]);
            return 2;
        }
    }
    const auto fsr_create = reinterpret_cast<decltype(&ffxFsr2ContextCreate)>(GetProcAddress(modules[1], "ffxFsr2ContextCreate"));
    const auto dispatch = reinterpret_cast<decltype(&ffxFsr2ContextDispatch)>(GetProcAddress(modules[1], "ffxFsr2ContextDispatch"));
    const auto destroy = reinterpret_cast<decltype(&ffxFsr2ContextDestroy)>(GetProcAddress(modules[1], "ffxFsr2ContextDestroy"));
    if (!create || !fsr_create || !dispatch || !destroy) return 3;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const HRESULT hr = create(selected, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context);
    selected->Release();
    if (FAILED(hr) || !device || !context) return 6;
    std::puts("Native D3D11 FSR2 input fixture; Runtime interop is tested separately");
    std::fflush(stdout);

    FfxFsr2Context fsr{};
    FfxFsr2ContextDescription init{};
    init.device = device;
    init.flags = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR2_ENABLE_AUTO_EXPOSURE;
    init.maxRenderSize = {64, 64};
    init.displaySize = {128, 128};
    const auto created = fsr_create(&fsr, &init);
    std::printf("FSR2 create=%u\n", static_cast<unsigned>(created));
    std::fflush(stdout);
    if (created != FFX_OK) return 8;

    FfxFsr2DispatchDescription frame{};
    frame.commandList = context;
    frame.renderSize = {64, 64};
    frame.motionVectorScale = {-64, -64};
    frame.frameTimeDelta = 16.67f;
    frame.preExposure = 1;
    frame.cameraNear = .1f;
    frame.cameraFar = 1000;
    frame.cameraFovAngleVertical = 1;
    frame.viewSpaceToMetersFactor = 1;
    frame.reset = true;
    ID3D11Texture2D* textures[4]{};
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT};
    FfxResource* resources[] = {&frame.color, &frame.depth, &frame.motionVectors, &frame.output};
    for (unsigned i = 0; i < 4; ++i) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = i == 3 ? 128 : 64;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = formats[i];
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &textures[i]))) return 9;
        resources[i]->resource = textures[i];
    }
    // 此夹具没有 SwapChain；两次即可覆盖首帧创建和后续 Evaluate，
    // 不能在这里耗尽 OptiScaler 等待 Present 回收的环形缓冲。
    for (unsigned i = 0; i < 2; ++i) {
        const auto result = dispatch(&fsr, &frame);
        std::printf("FSR2 dispatch[%u]=%u\n", i, static_cast<unsigned>(result));
        std::fflush(stdout);
        if (result != FFX_OK) return 11;
        frame.reset = false;
    }
    context->Flush();
    if (destroy(&fsr) != FFX_OK) return 12;
    for (auto* texture : textures) texture->Release();
    context->Release();
    device->Release();
    std::puts("FSR2 DX11 component dispatch completed");
    std::fflush(stdout);
    return 0;
}
