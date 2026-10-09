#include "RuntimeProbe.h"
#include "PoserUi.h"
#include "UiBridge.h"
#include <mutex>
#include <thread>

namespace {
std::mutex lifecycle;
std::thread worker;
HANDLE stopEvent = nullptr;

poser::Status Initialize(const poser::HostServices* host) noexcept {
    try {
        if (!host) return poser::InvalidArgument;
        if (host->struct_size < sizeof(poser::HostServices)) return poser::TooSmall;
        if (host->abi_version != poser::AbiVersion) return poser::UnsupportedAbi;
        if (!host->log) return poser::InvalidArgument;
        std::lock_guard lock(lifecycle);
        if (worker.joinable()) return poser::Ok;
        // 宿主服务由宿主拥有；复制函数指针，不跨 shutdown 保存借用的结构指针。
        const auto log = host->log;
        if (!poser::IsGenshinProcess()) {
            log(0, "[Poser] Non-Genshin process: probe inactive.");
            return poser::Ok;
        }
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent) return poser::Failed;
        try {
            poser::ui::Reset(true);
            if (!poser::StartPoserUi(stopEvent)) {CloseHandle(stopEvent);stopEvent=nullptr;poser::ui::Reset(false);return poser::Failed;}
            worker = std::thread([log] {
                try { poser::RunProbe(stopEvent, log); }
                catch (...) {poser::ui::State(L"探针出现异常，请查看诊断日志。",false,false,false,true);log(2, "[Poser] Probe failed; see diagnostic state.");}
            });
        } catch (...) {
            SetEvent(stopEvent);poser::StopPoserUi();poser::ui::Reset(false);
            CloseHandle(stopEvent);
            stopEvent = nullptr;
            return poser::Failed;
        }
        return poser::Ok;
    } catch (...) { return poser::Failed; }
}
poser::Status Shutdown() noexcept {
    try {
        std::lock_guard lock(lifecycle);
        if (stopEvent) SetEvent(stopEvent);
        // 必须等 worker 退出才能允许宿主卸载 DLL，禁止 detached thread。
        if (worker.joinable()) worker.join();
        poser::StopPoserUi();poser::ui::Reset(false);
        if (stopEvent) CloseHandle(stopEvent);
        stopEvent = nullptr;
        return poser::Ok;
    } catch (...) { return poser::Failed; }
}
}

extern "C" __declspec(dllexport) poser::Status SSMTPlugin_Query(
    uint32_t hostAbi, poser::PluginInfo* info, poser::PluginApi* api) noexcept {
    if (hostAbi != poser::AbiVersion) return poser::UnsupportedAbi;
    if (!info || !api) return poser::InvalidArgument;
    const auto infoCapacity = info->struct_size, apiCapacity = api->struct_size;
    if (infoCapacity < sizeof(poser::PluginInfo) || apiCapacity < poser::BaseApiSize)
        return poser::TooSmall;
    info->abi_version = poser::AbiVersion;
    info->name = "SSMT Poser - Genshin Runtime Probe";
    info->version = "0.1.0";
    info->author = "SSMT";
    api->abi_version = poser::AbiVersion;
    api->initialize = Initialize;
    api->shutdown = Shutdown;
    if (apiCapacity >= offsetof(poser::PluginApi, on_d3d11_ready) + sizeof(api->on_d3d11_ready))
        api->on_d3d11_ready = nullptr;
    if (apiCapacity >= sizeof(poser::PluginApi)) api->on_present = nullptr;
    return poser::Ok;
}
