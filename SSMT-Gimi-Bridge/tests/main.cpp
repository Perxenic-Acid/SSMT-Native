// 编译同一份 Bridge 实现，直接验证安装 Hook 后的 COM 调用；不需要游戏。
#include SSMT_BRIDGE_SOURCE
#include <cstdio>
#include <thread>

namespace {
struct FakeObject {
    void** vtable;
    unsigned family;
    unsigned calls = 0;
};
std::atomic_uint mismatches{0};

LONG WINAPI crash(EXCEPTION_POINTERS* info)
{
    std::printf("exception=%08lx address=%p\n", info->ExceptionRecord->ExceptionCode,
        info->ExceptionRecord->ExceptionAddress);
    std::fflush(stdout);
    ExitProcess(10);
    return EXCEPTION_EXECUTE_HANDLER;
}

HRESULT STDMETHODCALLTYPE no_interface(IUnknown*, REFIID, void** output)
{
    *output = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE final_release(IUnknown*) { return 0; }

template<unsigned Family>
void record(void* instance)
{
    auto& object = *static_cast<FakeObject*>(instance);
    if (object.family != Family) ++mismatches;
    ++object.calls;
}

template<unsigned Family>
HRESULT STDMETHODCALLTYPE create_buffer(ID3D11Device* instance, const D3D11_BUFFER_DESC*,
    const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer** buffer)
{
    record<Family>(instance);
    *buffer = nullptr;
    return S_FALSE;
}

template<unsigned Family>
void STDMETHODCALLTYPE set_resources(ID3D11DeviceContext* instance, UINT, UINT,
    ID3D11ShaderResourceView* const*)
{
    record<Family>(instance);
}

template<unsigned Family>
void STDMETHODCALLTYPE set_buffers(ID3D11DeviceContext* instance, UINT, UINT, ID3D11Buffer* const*)
{
    record<Family>(instance);
}

template<unsigned Family>
std::array<void*, 150> table()
{
    std::array<void*, 150> result{};
    result[0] = reinterpret_cast<void*>(&no_interface);
    result[2] = reinterpret_cast<void*>(&final_release);
    result[k_idx_device_create_buffer] = reinterpret_cast<void*>(&create_buffer<Family>);
    result[k_idx_ps_set_shader_resources] = reinterpret_cast<void*>(&set_resources<Family>);
    result[k_idx_vs_set_constant_buffers] = reinterpret_cast<void*>(&set_buffers<Family>);
    return result;
}

void buffer_call(FakeObject& object)
{
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = 16;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    ID3D11Buffer* output = nullptr;
    auto* device = reinterpret_cast<ID3D11Device*>(&object);
    if (device->CreateBuffer(&desc, nullptr, &output) != S_FALSE) ++mismatches;
}

void context_call(FakeObject& object)
{
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(&object);
    context->PSSetShaderResources(0, 0, nullptr);
    context->VSSetConstantBuffers(0, 0, nullptr);
}
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(crash);
    g_config.enable_logging = false;
    g_config.fsr2_translation_mode = 2;
    g_config.fsr2_mode2_on_demand_state = true;
    if (argc == 2) {
        // 先创建显卡辅助设备，再载入 Runtime 创建游戏包装设备；Bridge 先 Hook
        // 游戏设备，再 Hook 辅助设备，复现真实 dump 中两种 this 被混用的路径。
        ID3D11Device* auxiliary = nullptr;
        ID3D11DeviceContext* auxiliary_context = nullptr;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &auxiliary, nullptr, &auxiliary_context))) return 2;
        HMODULE runtime = LoadLibraryA(argv[1]);
        if (!runtime) return 3;
        const auto create = reinterpret_cast<create_device_fn>(GetProcAddress(runtime, "D3D11CreateDevice"));
        ID3D11Device* game = nullptr;
        ID3D11DeviceContext* game_context = nullptr;
        if (!create || FAILED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &game, nullptr, &game_context))) return 4;
        const GUID wrapper_id = {0x83ffd841, 0xa5c9, 0x46f4, {0x81, 0x09, 0xbc, 0x25, 0x95, 0x58, 0xfe, 0xf4}};
        IUnknown* identity = nullptr;
        if (FAILED(game->QueryInterface(wrapper_id, reinterpret_cast<void**>(&identity)))) return 5;
        identity->Release();
        install_device_hooks(game);
        install_device_hooks(auxiliary);
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 16;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        for (auto* device : {game, auxiliary, game, auxiliary}) {
            ID3D11Buffer* buffer = nullptr;
            if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer)) || !buffer) return 6;
            buffer->Release();
        }
        install_context_hooks(game_context);
        install_context_hooks(auxiliary_context);
        for (auto* context : {game_context, auxiliary_context, game_context, auxiliary_context}) {
            context->PSSetShaderResources(0, 0, nullptr);
            context->VSSetConstantBuffers(0, 0, nullptr);
            context->ClearState();
            context->Flush();
        }
        game_context->Release();
        game->Release();
        auxiliary_context->Release();
        auxiliary->Release();
        std::puts("Runtime/native mixed device and context calls completed");
        std::fflush(stdout);
        return 0;
    }
    auto first = table<1>();
    auto second = table<2>();
    FakeObject a{first.data(), 1}, b{second.data(), 2};
    install_device_hooks(reinterpret_cast<ID3D11Device*>(&a));
    install_device_hooks(reinterpret_cast<ID3D11Device*>(&b));
    buffer_call(a);
    buffer_call(b);
    buffer_call(a);

    // 模拟其他组件再复制一次 COM 虚表；本桥仍要找到该对象的原方法。
    std::array<void*, 80> shadow{};
    std::copy_n(b.vtable, shadow.size(), shadow.data());
    b.vtable = shadow.data();
    install_device_hooks(reinterpret_cast<ID3D11Device*>(&b));
    buffer_call(b);

    // 同一地址承载另一个实现，原对象的全局记录和线程缓存必须失效。
    reinterpret_cast<IUnknown*>(&a)->Release();
    a = {second.data(), 2};
    install_device_hooks(reinterpret_cast<ID3D11Device*>(&a));
    if (a.vtable == second.data()) ++mismatches;
    buffer_call(a);

    FakeObject ca{first.data(), 1}, cb{second.data(), 2};
    install_context_hooks(reinterpret_cast<ID3D11DeviceContext*>(&ca));
    install_context_hooks(reinterpret_cast<ID3D11DeviceContext*>(&cb));
    context_call(ca);
    context_call(cb);
    context_call(ca);
    std::thread worker([&] { for (unsigned i = 0; i < 1000; ++i) context_call(cb); });
    for (unsigned i = 0; i < 1000; ++i) context_call(ca);
    worker.join();
    std::printf("mixed device/context forwarding: mismatches=%u\n", mismatches.load());
    return mismatches.load() ? 1 : 0;
}
