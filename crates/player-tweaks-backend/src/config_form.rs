use serde_json::{json, Value};

pub fn describe(context: &Value) -> Value {
    let star_rail =
        context["gamePreset"].as_str() == Some("SRMI");
    let mut groups = Vec::new();
    for (section, title, fields) in [
        (
            "camera",
            "镜头设置",
            vec![
                ("customFov", "自定义视野", ""),
                (
                    "fov",
                    "视野角度",
                    "启用自定义视野后生效，范围为 30–120。",
                ),
                ("preserveAimingFov", "保留瞄准视野", ""),
                ("preserveCutsceneFov", "保留过场视野", ""),
                ("restoreInUi", "界面中还原视野", ""),
                ("transitionSpeed", "过渡速度", ""),
                (
                    "disableInputSmoothing",
                    "关闭输入平滑",
                    "",
                ),
                (
                    "disableTransitionBlend",
                    "关闭过渡混合",
                    "",
                ),
                (
                    "disableCharacterFade",
                    "关闭角色淡出",
                    "",
                ),
                (
                    "disableEventCameraMovement",
                    "关闭事件镜头移动",
                    "",
                ),
                ("cameraZoom", "自由镜头距离", ""),
            ],
        ),
        (
            "gameplay",
            "游戏设置",
            vec![
                ("fpsUnlock", "启用帧率解锁", ""),
                ("targetFps", "目标帧率", ""),
                ("fastTeamPage", "快速队伍界面", ""),
            ],
        ),
    ] {
        if star_rail && section == "camera" {
            continue;
        }
        let fields: Vec<Value> = fields.into_iter().filter(|(key,_,_)| !star_rail || *key == "fpsUnlock").map(|(key,label,description)| {
            let bounds = match key { "fov" => Some((30.0,120.0,0.1)), "transitionSpeed" => Some((0.0,30.0,0.1)), "targetFps" => Some((30.0,240.0,1.0)), _ => None };
            let mut field = json!({"path":[section,key],"kind":if bounds.is_some(){"number"}else{"boolean"},"label":label});
            if !description.is_empty() { field["description"] = json!(description); }
            if let Some((min,max,step)) = bounds { field["min"]=json!(min); field["max"]=json!(max); field["step"]=json!(step); }
            field
        }).collect();
        groups.push(json!({"id":section,"title":title,"fields":fields}));
    }
    json!({"encoding":"ssmt-config/1","title":"Player Tweaks","description":if star_rail {"崩铁使用 120 FPS；停用插件时恢复原有帧率设置。"}else{"调整镜头和游戏体验，设置在下一次启动游戏时应用。"},"groups":groups})
}
