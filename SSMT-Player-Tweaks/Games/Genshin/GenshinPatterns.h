#pragma once

#include <string_view>

namespace SSMT::Tweaks::Genshin::Patterns
{
    inline constexpr std::string_view InputZoomTick =
        "41 57 41 56 41 54 56 57 55 53 48 81 EC 80 03 00 00 "
        "66 44 0F 29 9C 24 70 03 00 00 66 44 0F 29 94 24 60 03 00 00 "
        "66 44 0F 29 8C 24 50 03 00 00 66 44 0F 29 84 24 40 03 00 00 "
        "66 0F 29 BC 24 30 03 00 00 0F 29 B4 24 20 03 00 00 "
        "4D 89 CC 4C 89 C6 66 0F 28 F1 49 89 CE";

    inline constexpr std::string_view InputZoomAdjust =
        "56 57 48 83 EC 58 66 44 0F 29 44 24 40 0F 29 7C 24 30 "
        "66 0F 29 74 24 20 48 89 D7 48 89 CE 80 3D ?? ?? ?? ?? 00 "
        "0F 85 ?? ?? ?? ?? 83 7F 34 00 75 ?? 80 BF FB 04 00 00 00";

    inline constexpr std::string_view UpdateManualLocateRatio =
        "56 48 83 EC 20 48 89 CE 80 3D ?? ?? ?? ?? 00 75 ?? "
        "48 89 F1 E8 ?? ?? ?? ?? F2 0F 11 86 48 04 00 00 "
        "80 3D ?? ?? ?? ?? 00 75 ??";

    inline constexpr std::string_view ScriptedManualLocateRatio =
        "56 48 83 EC 50 44 0F 29 44 24 40 0F 29 7C 24 30 "
        "0F 29 74 24 20 48 89 CE 80 3D ?? ?? ?? ?? 00 75 ?? "
        "F2 0F 10 76 68 48 89 F1 E8 ?? ?? ?? ?? 66 44 0F 28 C0 "
        "F2 0F 10 7E 50 48 89 F1 E8 ?? ?? ?? ?? F2 41 0F 5C F0 "
        "F2 0F 5C F8 F2 0F 5E F7";

    inline constexpr std::string_view ZoomDistanceLimit =
        "56 57 48 83 EC 58 66 0F 29 74 24 40 48 89 D6 48 89 CF "
        "80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? "
        "8B 86 CC 04 00 00 89 44 24 38 48 8B 86 C4 04 00 00 "
        "48 89 44 24 30 48 8D 4C 24 30 BA 01 00 00 00 E8 ?? ?? ?? ??";

    inline constexpr std::string_view ZoomRadiusUpdate =
        "56 57 53 48 81 EC 80 00 00 00 66 0F 29 7C 24 70 0F 29 74 24 60 "
        "4C 89 CB 4C 89 C6 66 0F 28 F1 48 89 CF 80 3D ?? ?? ?? ?? 00 "
        "0F 85 ?? ?? ?? ?? F2 0F 10 86 10 02 00 00 F2 0F 11 86 78 05 00 00";

    inline constexpr std::string_view ZoomRadiusSmoothDamp =
        "56 48 83 EC 70 44 0F 29 44 24 60 0F 29 7C 24 50 0F 29 74 24 40 "
        "66 0F 28 F3 66 44 0F 28 C2 66 0F 28 D9 66 0F 28 F8 "
        "48 8B B4 24 B0 00 00 00 F2 0F 10 84 24 A8 00 00 00 "
        "F2 0F 10 94 24 A0 00 00 00 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ??";

    inline constexpr std::string_view ZoomCollectAvatarState =
        "41 56 56 57 55 53 48 83 EC 60 4C 89 C3 48 89 D6 48 89 CF "
        "80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? "
        "48 8B 47 18 48 85 C0 74 ?? 48 8B 00 48 3B 05 ?? ?? ?? ?? 74 ?? "
        "48 89 F1 48 83 C4 60 5B 5D 5F 5E 41 5E E9 ?? ?? ?? ??";

    inline constexpr std::string_view ChangeFov =
        "40 53 48 83 EC 60 0F 29 74 24 ?? 48 8B D9 0F 28 F1 "
        "E8 ?? ?? ?? ?? 48 85 C0 0F 84 ?? ?? ?? ?? "
        "E8 ?? ?? ?? ?? 48 8B C8";

    inline constexpr std::string_view CameraUpdateView =
        "56 48 83 EC 40 0F 29 7C 24 30 0F 29 74 24 20 48 89 CE "
        "F3 0F 10 71 70 F3 0F 10 79 78 F3 0F 5C F7 "
        "F3 0F 59 B1 80 00 00 00 E8 ??";

    inline constexpr std::string_view CameraStateBlenderTick =
        "41 57 41 56 41 55 41 54 56 57 55 53 "
        "B8 88 18 00 00 E8 ?? ?? ?? ?? 48 29 C4 "
        "44 0F 29 8C 24 70 18 00 00";

    inline constexpr std::string_view EventCamera =
        "41 57 41 56 56 57 55 53 48 83 EC 48 48 89 D7 49 89 CE "
        "80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? 80";

    inline constexpr std::string_view FindGameObject =
        "40 53 48 83 EC ?? 48 89 4C 24 ?? 48 8D 54 24 ?? "
        "48 8D 4C 24 ?? E8 ?? ?? ?? ?? 48 8B 08 48 85 C9 "
        "75 ?? 48 8D 48 ?? E8 ?? ?? ?? ?? 48 8B 4C 24 ?? "
        "48 8B D8 48 85 C9 74 ?? 48 83 7C 24 ?? 00 76";

    inline constexpr std::string_view FindString =
        "56 48 83 EC 20 48 89 CE E8 ?? ?? ?? ?? 48 89 F1 "
        "89 C2 48 83 C4 20 5E E9 ?? ?? ?? ?? CC CC CC CC";

    inline constexpr std::string_view GetActive =
        "E8 ?? ?? ?? ?? 84 C0 74 ?? 48 89 F1 E8 ?? ?? ?? ?? "
        "48 8B 4E ?? 48 85 C9 0F 84 ?? ?? ?? ?? "
        "80 79 ?? ?? 0F 94 C1 08 C1";

    inline constexpr std::string_view PlayerPerspective =
        "E8 ?? ?? ?? ?? 48 8B BE ?? ?? ?? ?? "
        "80 3D ?? ?? ?? ?? ?? "
        "0F 85 ?? ?? ?? ?? "
        "80 BE ?? ?? ?? ?? ?? 74 11";

    inline constexpr std::string_view GetFrameCount =
        "E8 ?? ?? ?? ?? 85 C0 7E 0E "
        "E8 ?? ?? ?? ?? 0F 57 C0 "
        "F3 0F 2A C0 EB 08";

    inline constexpr std::string_view SetFrameCount =
        "E8 ?? ?? ?? ?? "
        "E8 ?? ?? ?? ?? "
        "83 F8 1F 0F 9C 05 ?? ?? ?? ?? "
        "48 8B 05";

    inline constexpr std::string_view SetSyncCount =
        "E8 ?? ?? ?? ?? "
        "E8 ?? ?? ?? ?? "
        "89 C6 "
        "E8 ?? ?? ?? ?? "
        "31 C9 89 F2 49 89 C0 "
        "E8 ?? ?? ?? ?? "
        "48 89 C6 "
        "48 8B 0D ?? ?? ?? ?? "
        "80 B9 ?? ?? ?? ?? ?? "
        "74 47 "
        "48 8B 3D ?? ?? ?? ?? "
        "48 85 DF 74 4C";

    inline constexpr std::string_view OpenTeamPage =
        "56 57 53 48 83 EC 20 "
        "89 CB "
        "80 3D ?? ?? ?? ?? 00 "
        "74 7A "
        "80 3D ?? ?? ?? ?? 00 "
        "48 8B 05";

    inline constexpr const char *CheckCanEnter =
        "56 48 81 EC 80 00 00 00 "
        "80 3D ?? ?? ?? ?? 00 "
        "0F 84 ?? ?? ?? ?? "
        "80 3D ?? ?? ?? ?? 00";

    inline constexpr const char *OpenTeam =
        "48 83 EC 28 "
        "80 3D ?? ?? ?? ?? 00 "
        "75 ?? "
        "48 8B 0D ?? ?? ?? ?? "
        "80 B9 C7 00 00 00 00 "
        "74 ?? "
        "B9 0C 00 00 00 "
        "E8 ?? ?? ?? ?? "
        "84 C0 74";
}
