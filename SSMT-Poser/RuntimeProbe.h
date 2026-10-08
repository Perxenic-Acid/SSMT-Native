#pragma once
#include <Windows.h>
#include <array>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>
#include "PluginAbi.h"

namespace poser {
// 仅声明 opaque 指针，不能依赖某一 Unity 版本的对象/MethodInfo 布局。
struct Domain; struct Thread; struct Assembly; struct Image; struct Class; struct Method;
struct Api {
    Domain* (*domain_get)();
    Thread* (*thread_attach)(Domain*);
    const Assembly** (*domain_get_assemblies)(const Domain*, size_t*);
    const Image* (*assembly_get_image)(const Assembly*);
    size_t (*image_get_class_count)(const Image*);
    Class* (*image_get_class)(const Image*, size_t);
    const char* (*class_get_name)(Class*);
    const char* (*class_get_namespace)(Class*);
    const Method* (*class_get_methods)(Class*, void**);
    const char* (*method_get_name)(const Method*);
    uint32_t (*method_get_param_count)(const Method*);
    void* (*runtime_invoke)(const Method*, void*, void**, void**);
    void (*thread_detach)(Thread*);
    const char* (*image_get_name)(const Image*);
};
inline constexpr std::array ApiNames = {
    "il2cpp_domain_get", "il2cpp_thread_attach", "il2cpp_domain_get_assemblies",
    "il2cpp_assembly_get_image", "il2cpp_image_get_class_count", "il2cpp_image_get_class",
    "il2cpp_class_get_name", "il2cpp_class_get_namespace", "il2cpp_class_get_methods",
    "il2cpp_method_get_name", "il2cpp_method_get_param_count", "il2cpp_runtime_invoke",
    "il2cpp_thread_detach", "il2cpp_image_get_name"
};
struct Module {
    HMODULE handle = nullptr;
    std::wstring name, path;
    size_t size = 0;
    std::array<FARPROC, ApiNames.size()> exports{};
    std::shared_ptr<void> lifetime;
    bool complete() const;
};
std::vector<Module> EnumerateModules();
// 不把多个 DLL 的零散命中拼成 API，也不对歧义候选任意择一。
const Module* SelectRuntime(const std::vector<Module>& modules, std::ostream& report);
Api BindApi(const Module& module);
enum class MetadataResult { Ready, Waiting, Failed, Cancelled };
MetadataResult ProbeMetadata(const Api& api, std::ostream& report, HANDLE stop);
std::string Utf8(const std::wstring& value);
bool IsGenshinProcess();
std::filesystem::path DiagnosticsDirectory();
void RunProbe(HANDLE stop, Log log);
}
