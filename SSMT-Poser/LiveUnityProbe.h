#pragma once
#include "GenshinNativeRuntime.h"
#include <filesystem>

namespace poser {
bool ProbeLiveUnity(GenshinNativeRuntime& runtime, HANDLE stop, std::ostream& report,
    const std::filesystem::path& directory);
namespace live_detail {
bool ValidateReflection(uintptr_t object, uintptr_t runtimeClass, uintptr_t descriptor);
bool ValidateArray(uintptr_t array, uintptr_t elementClass, uint64_t& length);
}
}
