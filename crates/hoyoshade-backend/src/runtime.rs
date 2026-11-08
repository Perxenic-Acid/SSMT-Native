use crate::policy::{Contribution, Policy};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use ssmt_plugin_graphics::{
    managed_reshade,
    paths::{canonical_path, checked_path, read_text},
    reshade_state,
};
use std::{
    fs,
    path::{Path, PathBuf},
};

pub fn executable(
    context: &Value,
    payload: &Value,
) -> Result<PathBuf, String> {
    payload
        .get("gameExecutable")
        .or_else(|| {
            payload.get("launchContext").and_then(|value| {
                value.get("gameExecutable")
            })
        })
        .and_then(Value::as_str)
        .or_else(|| context["gameExecutable"].as_str())
        .map(PathBuf::from)
        .ok_or("缺少游戏路径".into())
}

pub fn supports(
    policy: &Policy,
    executable: &Path,
) -> bool {
    let process = executable
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or_default();
    policy
        .processes
        .iter()
        .any(|name| name.eq_ignore_ascii_case(process))
}

pub fn prepare(
    context: &Value,
    payload: &Value,
) -> Result<Value, String> {
    let policy = Policy::load(context)?;
    let executable_path = executable(context, payload)?;
    let executable = executable_path.as_path();
    if !supports(&policy, executable) {
        return Err("HoYoShade 不支持此目标进程".into());
    }
    ensure_stopped(executable)?;
    ensure_no_external_host(executable)?;
    let dependency = policy.dependency_root(context)?;
    let dll = checked_path(&dependency, &policy.host_dll)?;
    let data = context["assetRoot"]
        .as_str()
        .or_else(|| context["dataRoot"].as_str())
        .ok_or("缺少插件数据目录")?;
    let game = context["gameName"]
        .as_str()
        .ok_or("缺少游戏名称")?;
    if game.trim().is_empty() {
        return Err("缺少游戏作用域".into());
    }
    let directory =
        Path::new(data).join("hot").join(format!(
            "{:x}",
            Sha256::digest(
                game.trim().to_lowercase().as_bytes()
            )
        ));
    let directory = directory.as_path();
    let mut contributions = Vec::new();
    if let Some(peers) = payload["peers"].as_array() {
        for peer in peers {
            if let Some(value) =
                peer["exports"].get("ssmt.reshade.v1")
            {
                let contribution: Contribution =
                    serde_json::from_value(value.clone())
                        .map_err(|error| {
                        format!(
                            "ReShade 共享贡献无效：{error}"
                        )
                    })?;
                if !contribution.addons.is_empty()
                    && (peer["nativeInjectionAcknowledged"].as_bool() != Some(true)
                        || !peer["permissions"].as_array().is_some_and(|permissions| permissions.iter().any(|permission| permission.as_str() == Some("native.inject")))) {
                    return Err("共享 Add-on 的来源插件缺少原生注入权限或风险确认".into());
                }
                contributions.push(contribution);
            }
        }
    }
    if contributions.is_empty() {
        contributions.push(policy.contribution(context)?);
    }
    fs::create_dir_all(directory)
        .map_err(|error| error.to_string())?;
    let root = canonical_path(directory)
        .map_err(|error| error.to_string())?;
    let mut composed = reshade_state::compose(
        contributions,
        &root,
        &[],
        "",
        "",
    )?;
    for (key, value) in [
        ("PresetPath", root.join("ReShadePreset.ini")),
        ("IntermediateCachePath", root.join("cache")),
    ] {
        managed_reshade::set_entry(
            &mut composed.game_ini,
            "GENERAL",
            key,
            &value.to_string_lossy(),
        );
    }
    managed_reshade::set_entry(
        &mut composed.game_ini,
        "ADDON",
        "AddonPath",
        &root.to_string_lossy(),
    );
    if !root.join("ReShade.ini").exists() {
        // 旧独立注入器将状态写在游戏目录。迁移界面、教程和按键，不继承它
        // 重写的 Add-on / Host 路径，也不修改任何游戏目录文件。
        let legacy = executable
            .parent()
            .ok_or("游戏目录无效")?
            .join("ReShade.ini");
        if legacy.is_file() {
            let mut ui = String::new();
            let mut keep = false;
            for line in read_text(&legacy)?.lines() {
                let trimmed = line.trim();
                if trimmed.starts_with('[')
                    && trimmed.ends_with(']')
                {
                    keep = [
                        "OVERLAY",
                        "INPUT",
                        "STYLE",
                        "SCREENSHOT",
                    ]
                    .iter()
                    .any(|section| {
                        trimmed[1..trimmed.len() - 1]
                            .eq_ignore_ascii_case(section)
                    });
                }
                if keep {
                    ui.push_str(line);
                    ui.push('\n');
                }
            }
            let current =
                managed_reshade::ManagedReShadeConfig {
                    game_ini: ui,
                    preset_ini: composed.preset_ini.clone(),
                };
            let defaults =
                managed_reshade::ManagedReShadeConfig {
                    game_ini: composed.game_ini.clone(),
                    preset_ini: composed.preset_ini.clone(),
                };
            let retained =
                managed_reshade::retain_user_state(
                    &defaults,
                    None,
                    &current,
                    "",
                    &[],
                );
            composed.game_ini = retained.game_ini;
            composed.preset_ini = retained.preset_ini;
        }
    }
    reshade_state::save_config(&root, composed, "", &[])?;
    let sha256 = format!(
        "{:x}",
        Sha256::digest(
            fs::read(&dll)
                .map_err(|error| error.to_string())?
        )
    );
    Ok(json!({
        "dlls": [{"path":dll,"sha256":sha256}],
        "logSources": [{"name":"ReShade","path":root.join("ReShade.log")}],
        "environment": {
            "RESHADE_BASE_PATH_OVERRIDE":root,
            "RESHADE_DISABLE_GRAPHICS_HOOK":null,
            "RESHADE_DISABLE_INPUT_HOOK":null,
            "GIMI_HOSTED_RESHADE_DLL":null,
            "GIMI_HOSTED_RESHADE_CONFIG":null
        },
        "requiredLaunchArgs":[], "excludedLaunchArgs":[]
    }))
}

fn ensure_stopped(executable: &Path) -> Result<(), String> {
    let mut system = sysinfo::System::new();
    system.refresh_processes_specifics(
        sysinfo::ProcessRefreshKind::new()
            .with_exe(sysinfo::UpdateKind::Always),
    );
    let canonical = executable
        .canonicalize()
        .map_err(|error| error.to_string())?;
    if system.processes().values().any(|process| {
        process
            .exe()
            .and_then(|path| path.canonicalize().ok())
            .as_ref()
            == Some(&canonical)
    }) {
        return Err("请关闭游戏后再准备热注入组件".into());
    }
    Ok(())
}
fn ensure_no_external_host(
    executable: &Path,
) -> Result<(), String> {
    let game = executable.parent().ok_or("游戏目录无效")?;
    for name in ["dxgi.dll", "d3d11.dll"] {
        if game.join(name).exists() {
            return Err(format!("游戏目录存在图形代理 {name}，无法确认单一 ReShade Host。请确认来源并通过原管理器恢复 RHI / ReShade 安装后再使用 SSMT 热注入；SSMT 不会覆盖原文件"));
        }
    }
    // ReShade 先读取游戏目录的 INSTALL.BasePath，再读取环境变量；旧配置也会劫走自有 Host。
    let ini = game.join("ReShade.ini");
    if ini.is_file() {
        let text = read_text(&ini)?;
        let mut install = false;
        for line in text.lines().map(str::trim) {
            if line.starts_with('[') && line.ends_with(']')
            {
                install = line[1..line.len() - 1]
                    .trim()
                    .eq_ignore_ascii_case("INSTALL");
            } else if install {
                if let Some((key, value)) =
                    line.split_once('=')
                {
                    if key
                        .trim()
                        .eq_ignore_ascii_case("BasePath")
                        && !value.trim().is_empty()
                    {
                        return Err("游戏目录的 ReShade.ini 指向另一套 BasePath，请通过原管理器恢复该配置后再使用 SSMT 热注入；SSMT 不会修改原配置".into());
                    }
                }
            }
        }
    }
    Ok(())
}
