#pragma once

#include "Core/CameraPolicy.h"
#include "Core/SymbolResolver.h"

#include <ostream>
#include <optional>

namespace SSMT::Tweaks::Genshin
{
    struct GenshinCameraBridge
    {
        ResolvedSymbol setFov;
        ResolvedSymbol updateView;
        ResolvedSymbol blenderTick;
        ResolvedSymbol playerPerspective;
        ResolvedSymbol eventCamera;
        ResolvedSymbol inputZoomTick;
        ResolvedSymbol inputZoomAdjust;
        ResolvedSymbol updateManualLocateRatio;
        ResolvedSymbol scriptedManualLocateRatio;
        ResolvedSymbol zoomDistanceLimit;
        ResolvedSymbol zoomRadiusUpdate;
        ResolvedSymbol zoomRadiusSmoothDamp;
        ResolvedSymbol zoomCollectAvatarState;
        ResolvedSymbol findString;
        ResolvedSymbol findGameObject;
        ResolvedSymbol getActive;

        [[nodiscard]] bool SupportsCutsceneState() const;
        [[nodiscard]] std::optional<bool> IsCutsceneActive() const;
    };

    GenshinCameraBridge ResolveCameraBridge(
        const PatternScanner &scanner,
        const CameraPolicyConfig &config,
        std::ostream &log);
}
