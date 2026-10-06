#include "CameraPolicy.h"

#include <algorithm>
#include <cmath>

namespace SSMT::Tweaks
{
    float ResolveCameraFov(
        float incoming,
        const void *camera,
        const CameraContext &context,
        const CameraPolicyConfig &config,
        std::uint64_t nowMs,
        FovTransitionState &state)
    {
        if (!std::isfinite(incoming) || incoming <= 0.0f || incoming >= 180.0f) return incoming;

        const bool preserve = (context.aiming && config.preserveAimingFov) ||
            (context.cutscene && config.preserveCutsceneFov);
        const bool restore = context.ui && config.restoreInUi;
        const bool overrideFov = config.customFov && !preserve && !restore &&
            std::isfinite(config.fov) && config.fov >= 30.0f && config.fov <= 120.0f;
        const float target = overrideFov ? config.fov : incoming;

        // 瞄准和过场的镜头值由游戏所有，不能让上一状态的过渡继续污染它。
        if (preserve || !config.customFov)
        {
            state = {camera, target, nowMs, true};
            return target;
        }

        if (!state.initialized || state.camera != camera || nowMs < state.lastTickMs ||
            nowMs - state.lastTickMs > 1000)
        {
            state = {camera, incoming, nowMs, true};
        }

        const auto elapsedMs = nowMs - state.lastTickMs;
        state.lastTickMs = nowMs;
        if (config.transitionSpeed <= 0.0f || !std::isfinite(config.transitionSpeed))
        {
            state.output = target;
        }
        else if (elapsedMs > 0)
        {
            const float weight = 1.0f - std::exp(-config.transitionSpeed *
                static_cast<float>(elapsedMs) / 1000.0f);
            state.output += (target - state.output) * std::clamp(weight, 0.0f, 1.0f);
            if (std::fabs(target - state.output) < 0.01f) state.output = target;
        }
        return state.output;
    }
}
