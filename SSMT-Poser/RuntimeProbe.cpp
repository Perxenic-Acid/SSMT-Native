#include "RuntimeProbe.h"
#include "GenshinNativeRuntime.h"
#include "LiveUnityProbe.h"
#include <TlHelp32.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <utility>

namespace poser {
namespace {
constexpr size_t RequiredApiCount = 13; // image_get_name 只用于诊断显示。
constexpr size_t MaxAssemblies = 8192, MaxClasses = 500000, MaxMethods = 8192;

bool Cancelled(HANDLE stop) {
    return stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0;
}
int ExceptionFilter(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
// 把 SEH 隔离在没有 C++ RAII 对象的函数内，不把损坏的返回值继续用于 metadata 遍历。
template<class R, class F, class... Args>
bool Call(R& out, F fn, Args... args) {
    if (!fn) return false;
    __try { out = fn(args...); return true; }
    __except(ExceptionFilter(GetExceptionCode())) { return false; }
}
bool Detach(const Api& api, Thread* thread) {
    __try { api.thread_detach(thread); return true; }
    __except(ExceptionFilter(GetExceptionCode())) { return false; }
}
struct AttachedThread {
    const Api& api;
    Thread* thread;
    ~AttachedThread() { if (thread) Detach(api, thread); }
};
template<class T> bool Read(const T* address, T& out) {
    SIZE_T copied = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, &out, sizeof(T), &copied)
        && copied == sizeof(T);
}
std::string Text(const char* address) {
    if (!address) throw std::runtime_error("metadata returned null text");
    std::string result;
    for (size_t i = 0; i < 4096; ++i) {
        char ch = 0;
        if (!Read(address + i, ch)) throw std::runtime_error("unreadable metadata text");
        if (!ch) return result;
        // 保持诊断文件一条记录一行。
        result += ch == '\n' || ch == '\r' || ch == '\t' ? ' ' : ch;
    }
    throw std::runtime_error("metadata text exceeded limit");
}
bool Executable(FARPROC pointer) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!pointer || !VirtualQuery(reinterpret_cast<void*>(pointer), &memory, sizeof(memory)))
        return false;
    if (memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto protection = memory.Protect & 0xFF;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}
struct TargetMethod { const char* type; const char* name; uint32_t parameters; };
constexpr const char* TargetTypes[] = {
    "Animator", "Transform", "Component", "GameObject", "Object", "SkinnedMeshRenderer"
};
constexpr TargetMethod TargetMethods[] = {
    {"Component", "get_transform", 0}, {"Component", "get_gameObject", 0},
    {"Object", "get_name", 0}, {"Transform", "get_parent", 0},
    {"Transform", "get_childCount", 0}, {"Transform", "GetChild", 1},
    {"Transform", "get_localPosition", 0}, {"Transform", "get_localRotation", 0},
    {"Transform", "get_position", 0}, {"Transform", "get_rotation", 0},
    {"Animator", "get_isHuman", 0}, {"Animator", "GetBoneTransform", 1},
    {"SkinnedMeshRenderer", "get_bones", 0}, {"SkinnedMeshRenderer", "get_rootBone", 0},
    {"SkinnedMeshRenderer", "get_sharedMesh", 0}
};
bool ListMetadata(const Api& api, Domain* domain, std::ostream& report, HANDLE stop) {
    size_t assemblyCount = 0;
    const Assembly** assemblies = nullptr;
    if (!Call(assemblies, api.domain_get_assemblies, domain, &assemblyCount))
        throw std::runtime_error("domain_get_assemblies fault");
    if (assemblyCount > MaxAssemblies || (assemblyCount && !assemblies))
        throw std::runtime_error("invalid assembly count/pointer");
    report << "assemblies=" << assemblyCount << '\n';
    if (!assemblyCount) return false;
    std::array<size_t, std::size(TargetTypes)> typeCounts{};
    std::array<size_t, std::size(TargetMethods)> methodCounts{};
    size_t totalClasses = 0, images = 0;
    for (size_t i = 0; i < assemblyCount; ++i) {
        if (Cancelled(stop)) return false;
        const Assembly* assembly = nullptr;
        const Image* image = nullptr;
        size_t count = 0;
        if (!Read(assemblies + i, assembly) || !assembly ||
            !Call(image, api.assembly_get_image, assembly) || !image ||
            !Call(count, api.image_get_class_count, image))
            throw std::runtime_error("assembly/image enumeration fault");
        if (count > MaxClasses - totalClasses) throw std::runtime_error("class budget exceeded");
        totalClasses += count;
        ++images;
        report << "image[" << i << "]=" << image;
        if (api.image_get_name) {
            const char* name = nullptr;
            if (!Call(name, api.image_get_name, image)) throw std::runtime_error("image name fault");
            report << " name=" << Text(name);
        }
        report << " classes=" << count << '\n';
        for (size_t index = 0; index < count; ++index) {
            if (Cancelled(stop)) return false;
            Class* type = nullptr;
            const char *rawName = nullptr, *rawNamespace = nullptr;
            if (!Call(type, api.image_get_class, image, index))
                throw std::runtime_error("image_get_class fault");
            if (!type) continue;
            if (!Call(rawName, api.class_get_name, type) ||
                !Call(rawNamespace, api.class_get_namespace, type))
                throw std::runtime_error("class name fault");
            if (Text(rawNamespace) != "UnityEngine") continue;
            const auto name = Text(rawName);
            const auto target = std::find(std::begin(TargetTypes), std::end(TargetTypes), name);
            if (target == std::end(TargetTypes)) continue;
            ++typeCounts[static_cast<size_t>(target - std::begin(TargetTypes))];
            report << "UnityEngine." << name << " class=" << type << " image=" << image << '\n';
            void* iterator = nullptr;
            bool ended = false;
            for (size_t m = 0; m < MaxMethods; ++m) {
                if (Cancelled(stop)) return false;
                const Method* method = nullptr;
                if (!Call(method, api.class_get_methods, type, &iterator))
                    throw std::runtime_error("class_get_methods fault");
                if (!method) { ended = true; break; }
                const char* rawMethodName = nullptr;
                uint32_t params = 0;
                if (!Call(rawMethodName, api.method_get_name, method) ||
                    !Call(params, api.method_get_param_count, method))
                    throw std::runtime_error("method metadata fault");
                const auto methodName = Text(rawMethodName);
                for (size_t t = 0; t < std::size(TargetMethods); ++t) {
                    const auto& wanted = TargetMethods[t];
                    if (name == wanted.type && methodName == wanted.name && params == wanted.parameters) {
                        ++methodCounts[t];
                        report << "  " << methodName << " params=" << params << " method=" << method << '\n';
                    }
                }
            }
            if (!ended) throw std::runtime_error("method enumeration exceeded limit");
        }
    }
    report << "images=" << images << " total_classes=" << totalClasses << '\n';
    for (size_t t = 0; t < std::size(TargetTypes); ++t)
        report << "[Unity type] " << TargetTypes[t] << " matches=" << typeCounts[t] << '\n';
    for (size_t t = 0; t < std::size(TargetMethods); ++t)
        report << "[Unity method] " << TargetMethods[t].type << '.' << TargetMethods[t].name
            << " matches=" << methodCounts[t] << " invoked=NO\n";
    report << "phase1=RESOLUTION_ONLY; invoke not attempted; Unity main-thread dispatch unverified\n";
    return true;
}
}

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("UTF-8 conversion failed");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}
bool Module::complete() const {
    return std::all_of(exports.begin(), exports.begin() + RequiredApiCount,
        [](auto pointer) { return pointer != nullptr; });
}
std::vector<Module> EnumerateModules() {
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 4; ++attempt) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
        if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
    }
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("module snapshot failed");
    const std::shared_ptr<void> snapshotLifetime(snapshot, [](void* handle) { CloseHandle(handle); });
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot, &entry)) throw std::runtime_error("module enumeration failed");
    std::vector<Module> modules;
    do {
        HMODULE retained = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(entry.modBaseAddr), &retained)) continue;
        Module module;
        module.lifetime = std::shared_ptr<void>(retained, [](void* handle) { FreeLibrary(static_cast<HMODULE>(handle)); });
        module.handle = retained;
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(retained, path, std::size(path));
        if (!length || length == std::size(path)) throw std::runtime_error("module path unavailable");
        module.path.assign(path, length);
        module.name = std::filesystem::path(module.path).filename().wstring();
        module.size = entry.modBaseSize;
        for (size_t i = 0; i < ApiNames.size(); ++i) {
            auto pointer = GetProcAddress(retained, ApiNames[i]);
            module.exports[i] = Executable(pointer) ? pointer : nullptr;
        }
        modules.push_back(std::move(module));
    } while (Module32NextW(snapshot, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) throw std::runtime_error("module enumeration incomplete");
    return modules;
}
const Module* SelectRuntime(const std::vector<Module>& modules, std::ostream& report) {
    const Module* selected = nullptr;
    size_t candidates = 0;
    for (const auto& module : modules) {
        const auto hits = std::count_if(module.exports.begin(), module.exports.end(), [](auto p) { return p != nullptr; });
        report << "[module] name=" << Utf8(module.name) << " base=" << module.handle
            << " image_size=" << module.size << " api_hits=" << hits << " path=" << Utf8(module.path) << '\n';
        if (module.complete()) { ++candidates; selected = &module; }
        if (hits || module.size >= 16 * 1024 * 1024 || module.handle == GetModuleHandleW(nullptr) ||
            _wcsicmp(module.name.c_str(), L"UnityPlayer.dll") == 0 ||
            _wcsicmp(module.name.c_str(), L"UserAssembly.dll") == 0 ||
            _wcsicmp(module.name.c_str(), L"GameAssembly.dll") == 0) {
            for (size_t i = 0; i < ApiNames.size(); ++i)
                report << "  " << ApiNames[i] << '=' << reinterpret_cast<void*>(module.exports[i])
                    << (i < RequiredApiCount ? " required" : " optional") << '\n';
        }
    }
    for (const auto* name : {L"UnityPlayer.dll", L"UserAssembly.dll", L"GameAssembly.dll"})
        report << "[known module] " << Utf8(name) << '='
            << (std::any_of(modules.begin(), modules.end(), [name](const auto& m) {
                return _wcsicmp(m.name.c_str(), name) == 0;
            }) ? "LOADED" : "NOT_LOADED") << '\n';
    report << "complete_api_modules=" << candidates << '\n';
    if (candidates != 1) {
        report << "il2cpp_main_module=UNCONFIRMED\napi_mode="
            << (candidates ? "AMBIGUOUS_EXPORT_MODULES" : "NATIVE_RESOLVER_REQUIRED")
            << "\ndomain=NOT_CALLED\nmetadata=NOT_CALLED\n";
        return nullptr;
    }
    report << "il2cpp_main_module=" << Utf8(selected->path) << "\napi_mode=DIRECT_EXPORTS\n";
    return selected;
}
Api BindApi(const Module& module) {
    if (!module.complete()) throw std::runtime_error("incomplete IL2CPP API");
    Api api{};
#define BIND(field, index) api.field = reinterpret_cast<decltype(api.field)>(module.exports[index])
    BIND(domain_get, 0); BIND(thread_attach, 1); BIND(domain_get_assemblies, 2);
    BIND(assembly_get_image, 3); BIND(image_get_class_count, 4); BIND(image_get_class, 5);
    BIND(class_get_name, 6); BIND(class_get_namespace, 7); BIND(class_get_methods, 8);
    BIND(method_get_name, 9); BIND(method_get_param_count, 10); BIND(runtime_invoke, 11);
    BIND(thread_detach, 12); BIND(image_get_name, 13);
#undef BIND
    return api;
}
MetadataResult ProbeMetadata(const Api& api, std::ostream& report, HANDLE stop) {
    if (Cancelled(stop)) return MetadataResult::Cancelled;
    Domain* domain = nullptr;
    if (!Call(domain, api.domain_get)) { report << "domain=CALL_FAULT\n"; return MetadataResult::Failed; }
    report << "domain=" << domain << '\n';
    if (!domain) return MetadataResult::Waiting;
    Thread* thread = nullptr;
    if (!Call(thread, api.thread_attach, domain) || !thread) {
        report << "thread_attach=FAILED\n";
        return MetadataResult::Failed;
    }
    AttachedThread attachment{api, thread};
    report << "thread_attach=OK\n";
    MetadataResult result = MetadataResult::Failed;
    try {
        result = ListMetadata(api, domain, report, stop) ? MetadataResult::Ready : MetadataResult::Waiting;
    } catch (const std::exception& error) { report << "metadata=FAILED reason=" << error.what() << '\n'; }
    // 在 attach 的同一 worker 上配对 detach，即使读取失败或用户取消。
    const bool detached = Detach(api, thread);
    attachment.thread = nullptr;
    if (!detached) { report << "thread_detach=FAULT\n"; return MetadataResult::Failed; }
    report << "thread_detach=OK\n";
    if (Cancelled(stop)) return MetadataResult::Cancelled;
    if (result == MetadataResult::Ready) report << "metadata=ENUMERATED\n";
    return result;
}
bool IsGenshinProcess() {
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, path, std::size(path));
    if (!length || length == std::size(path)) return false;
    const auto name = std::filesystem::path(path).filename().wstring();
    return _wcsicmp(name.c_str(), L"YuanShen.exe") == 0 || _wcsicmp(name.c_str(), L"GenshinImpact.exe") == 0;
}
std::filesystem::path DiagnosticsDirectory() {
    wchar_t local[32768]{};
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, std::size(local));
    if (!length || length >= std::size(local)) throw std::runtime_error("LOCALAPPDATA unavailable");
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER timestamp{};
    timestamp.LowPart = now.dwLowDateTime; timestamp.HighPart = now.dwHighDateTime;
    auto directory = std::filesystem::path(local) / L"SSMT" / L"Diagnostics" / L"SSMT-Poser" /
        (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(timestamp.QuadPart));
    std::filesystem::create_directories(directory);
    return directory;
}
void RunProbe(HANDLE stop, Log log) {
    const auto directory = DiagnosticsDirectory();
    std::ofstream report(directory / L"genshin_poser_runtime.txt", std::ios::binary);
    if (!report) throw std::runtime_error("cannot open diagnostic report");
    report.exceptions(std::ios::badbit | std::ios::failbit);
    auto message = "[Poser] Genshin native runtime resolver; report=" + Utf8(directory.wstring());
    log(0, message.c_str());
    // 当前已确认无标准导出的原神直接走 native backend，避免重复 Phase 0 discovery。
    GenshinNativeRuntime native;
    const auto initialized = native.Initialize(GetModuleHandleW(nullptr), stop, report);
    report.flush();
    if (initialized != NativeResult::Ready) {
        report << "result=NATIVE_RESOLVER_NOT_INITIALIZED; game_calls=NONE\n";
        log(1, "[Poser] Native resolver sample/anchor validation did not pass; see report.");
        return;
    }
    report << "phase=NATIVE_RUNTIME_RESOLVER; pid=" << GetCurrentProcessId()
        << "\ncharacter_writes=NONE\nUnity_runtime_invoke=NOT_CALLED\n";
    for (unsigned attempt = 0; attempt < 24; ++attempt) {
        if (Cancelled(stop)) { report << "result=CANCELLED\n"; return; }
        report << "\n[native snapshot " << attempt + 1 << "] tick=" << GetTickCount64() << '\n';
        const auto result = native.Snapshot(stop, report);
        report.flush();
        if (result == NativeResult::Ready) {
            const auto live = ProbeLiveUnity(native, stop, report, directory);
            report << (live ? "result=SCENE_COMPONENT_CONFIRMED\n" : "result=PARTIAL_SCENE_PROBE; component=NOT_CONFIRMED\n");
            report.flush();
            log(live ? 0 : 1, live ? "[Poser] Scene Animator/SMR readonly properties confirmed; see report and bones file." :
                "[Poser] Scene component probe stopped; see exact call context and step in report.");
            return;
        }
        if (result == NativeResult::Failed || result == NativeResult::Cancelled) {
            report << "result=NATIVE_RESOLVER_STOPPED; game_calls=NONE\n";
            log(1, "[Poser] Native runtime crosscheck failed or cancelled; see report.");
            return;
        }
        if (attempt != 23 && WaitForSingleObject(stop, 5000) == WAIT_OBJECT_0) {
            report << "result=CANCELLED\n"; return;
        }
    }
    report << "result=NATIVE_RUNTIME_CACHE_NOT_READY; game_calls=NONE\n";
    log(1, "[Poser] Native runtime cache did not become ready within the bounded observation period.");
}
// 保留导出 backend 的已测基础设施，不在当前无导出的原神执行链重复运行它。
void RunExportProbe(HANDLE stop, Log log) {
    const auto directory = DiagnosticsDirectory();
    std::ofstream report(directory / L"exported_runtime.txt", std::ios::binary);
    if (!report) throw std::runtime_error("cannot open exported runtime report");
    report << "SSMT-Poser 0.1.0; phase=0; pid=" << GetCurrentProcessId()
        << "\ncharacter_writes=NONE\nUnity_runtime_invoke=NOT_CALLED\n";
    // 启动时可能还未加载运行时；有界等待，不在 Present/Draw 上执行 discovery。
    bool exportsFound = false;
    for (unsigned attempt = 0; attempt < 24; ++attempt) {
        if (Cancelled(stop)) { report << "result=CANCELLED\n"; return; }
        report << "\n[snapshot " << attempt + 1 << "] tick=" << GetTickCount64() << '\n';
        const auto modules = EnumerateModules();
        const auto* runtime = SelectRuntime(modules, report);
        if (runtime) {
            exportsFound = true;
            const auto result = ProbeMetadata(BindApi(*runtime), report, stop);
            report.flush();
            if (result == MetadataResult::Ready) {
                report << "result=PHASE0_READY; phase1_invoke/phase2/phase3/phase4=NOT_RUN\n";
                log(0, "[Poser] Phase 0 metadata enumerated; Unity invocation requires verified main-thread dispatch.");
                return;
            }
            if (result == MetadataResult::Failed || result == MetadataResult::Cancelled) {
                report << "result=STOPPED; metadata failed or cancelled\n";
                log(1, "[Poser] Probe stopped after metadata failure/cancellation; see report.");
                return;
            }
        }
        report.flush();
        if (attempt != 23 && WaitForSingleObject(stop, 5000) == WAIT_OBJECT_0) {
            report << "result=CANCELLED\n";
            return;
        }
    }
    report << "result=PHASE0_BLOCKED; missing/ambiguous exports or runtime not ready\n"
        << "next=" << (exportsFound ? "RUNTIME_READINESS_RESEARCH" : "NATIVE_RESOLVER_RESEARCH")
        << "; no unverified patterns, RVAs or object layouts used\n"
        << "phase1_invoke/phase2/phase3/phase4=NOT_RUN; bones_file=NOT_CREATED\n";
    log(1, "[Poser] Phase 0 blocked after 24 snapshots; native resolver/runtime evidence required. No bones exported.");
}
}
