#pragma once
#include <Windows.h>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace poser {
// 借用游戏拥有的缓存地址；不声明官方 Il2CppClass/MethodInfo 的 C++ 布局。
struct NativeClass {
    uint32_t metadataId = 0, methodStart = 0;
    uint16_t methodCount = 0;
    uintptr_t address = 0, metadataRecord = 0;
    std::string namespaze, name;
};
struct NativeMethod {
    uint32_t metadataId = 0;
    uint8_t parameterCount = 0;
    uintptr_t descriptor = 0, entry = 0, target = 0;
    std::string name;
};
enum class NativeResult { Waiting, Ready, Unsupported, Failed, Cancelled };

// 仅供已验证的少量 typed call 使用，不暴露通用 RuntimeInvoke。
struct NativeCallContext {
    uintptr_t reflectionType = 0, reflectionClassSlot = 0, reflectionCacheSlot = 0;
    uintptr_t threadCurrent = 0, threadAttach = 0, mainThreadSlot = 0;
    uintptr_t handleNew = 0, handleTarget = 0, handleFree = 0;
};

class GenshinNativeRuntime {
public:
    NativeResult Initialize(HMODULE image, HANDLE stop, std::ostream& report);
    NativeResult Snapshot(HANDLE stop, std::ostream& report);
    bool FindClass(const char* namespaze, const char* name, NativeClass& result) const;
    bool FindMethod(const NativeClass& klass, const char* name, uint8_t parameters,
        NativeMethod& result) const;
    bool DescribeClass(uintptr_t address, NativeClass& result) const;
    bool ResolveCallContext(HANDLE stop, NativeCallContext& result, std::ostream& report) const;
    bool ResolveScriptLateUpdate(HANDLE stop, uintptr_t& result, std::ostream& report) const;
private:
    bool initialized_ = false;
    uintptr_t image_ = 0;
    uint32_t imageSize_ = 0;
    uintptr_t headerSlot_ = 0, bodySlot_ = 0, classCacheSlot_ = 0, methodCacheSlot_ = 0, codeSlot_ = 0;
    uintptr_t header_ = 0, body_ = 0, classCache_ = 0, methodCache_ = 0, codeTable_ = 0;
    uintptr_t typeTable_ = 0, methodTable_ = 0, strings_ = 0;
    uint32_t typeCount_ = 0, methodCount_ = 0;
    std::vector<NativeClass> classes_;
};

namespace native_detail {
// 测试入口：覆盖歧义签名、相对寻址及缓存身份不一致时的拒绝行为。
std::vector<size_t> FindPattern(const std::vector<uint8_t>& bytes, const char* pattern);
uintptr_t RelativeTarget(uintptr_t instruction, int32_t displacement, size_t length);
bool ValidateClass(uintptr_t address, uintptr_t record, const std::string& name, uint16_t count);
bool ValidateMethod(uintptr_t descriptor, uintptr_t klass, uint32_t id, uint8_t parameters, uintptr_t entry);
}
}
