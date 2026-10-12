#pragma once

#include "GenshinNativeRuntime.h"

namespace poser {

// Read-only timing evidence for the game's own Animator IK callback. The hook
// only records a bounded POD sample and then calls the original method.
bool InstallIKTimingProbe(GenshinNativeRuntime& runtime, DWORD mainThread,
    uintptr_t selectedAnimator, std::ostream& report);
bool RemoveIKTimingProbe(std::ostream& report);
bool IKTimingProbeInstalled();
void ReportIKTimingProbe(std::ostream& report);

}
