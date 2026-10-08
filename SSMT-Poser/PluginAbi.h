#pragma once
#include <cstddef>
#include <cstdint>

namespace poser {
// 与 crates/plugin-api 的 ABI v2 对齐；此插件不扩展宿主 ABI。
using Status = uint32_t;
using Log = void (*)(uint8_t, const char*);
constexpr Status Ok = 0, InvalidArgument = 1, UnsupportedAbi = 2, TooSmall = 3;
constexpr Status Failed = 0xFFFFFFFF;
constexpr uint32_t AbiVersion = 2;
struct HostServices { uint32_t struct_size, abi_version; Log log; };
struct PluginInfo {
    uint32_t struct_size, abi_version;
    const char *name, *version, *author;
};
struct PluginApi {
    uint32_t struct_size, abi_version;
    Status (*initialize)(const HostServices*);
    Status (*shutdown)();
    Status (*on_d3d11_ready)(const void*);
    Status (*on_present)(const void*);
};
constexpr size_t BaseApiSize = offsetof(PluginApi, shutdown) + sizeof(PluginApi::shutdown);
static_assert(sizeof(HostServices) == 16 && sizeof(PluginInfo) == 32);
static_assert(BaseApiSize == 24 && sizeof(PluginApi) == 40);
}
