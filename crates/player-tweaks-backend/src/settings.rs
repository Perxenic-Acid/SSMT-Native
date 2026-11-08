use serde_json::{json, Value};
use std::path::{Path, PathBuf};
use std::{fs, io::Write};

pub fn defaults() -> Value {
    json!({"camera":{"customFov":false,"fov":60,"preserveAimingFov":true,"preserveCutsceneFov":true,"restoreInUi":true,"transitionSpeed":8,"disableInputSmoothing":false,"disableTransitionBlend":false,"disableCharacterFade":true,"disableEventCameraMovement":false,"cameraZoom":false},"gameplay":{"fpsUnlock":true,"targetFps":120,"fastTeamPage":true}})
}

pub fn normalize(value: &Value) -> Value {
    let mut result = defaults();
    for section in ["camera", "gameplay"] {
        for (key, default) in
            result[section].as_object_mut().unwrap()
        {
            let Some(candidate) = value
                .get(section)
                .and_then(|value| value.get(key))
            else {
                continue;
            };
            let valid = if default.is_boolean() {
                candidate.is_boolean()
            } else {
                candidate.as_f64().is_some_and(|number| {
                    number.is_finite()
                        && match key.as_str() {
                            "fov" => (30.0..=120.0)
                                .contains(&number),
                            "transitionSpeed" => (0.0
                                ..=30.0)
                                .contains(&number),
                            "targetFps" => {
                                (30.0..=240.0)
                                    .contains(&number)
                                    && number.fract() == 0.0
                            }
                            _ => false,
                        }
                })
            };
            if valid {
                *default = candidate.clone();
            }
        }
    }
    result
}

pub fn atomic_write(
    path: &Path,
    bytes: &[u8],
) -> Result<(), String> {
    fs::create_dir_all(path.parent().ok_or("缺少父目录")?)
        .map_err(|error| error.to_string())?;
    let temporary = path.with_extension(format!(
        "{}.tmp",
        std::process::id()
    ));
    let mut file = fs::OpenOptions::new()
        .create_new(true)
        .write(true)
        .open(&temporary)
        .map_err(|error| error.to_string())?;
    let result = (|| {
        file.write_all(bytes)
            .and_then(|_| file.sync_all())
            .map_err(|error| error.to_string())?;
        drop(file);
        fs::rename(&temporary, path)
            .map_err(|error| error.to_string())
    })();
    if result.is_err() {
        let _ = fs::remove_file(temporary);
    }
    result
}

fn path(context: &Value) -> Result<PathBuf, String> {
    let game = context
        .get("gameName")
        .and_then(Value::as_str)
        .ok_or("缺少游戏名称")?;
    // 文件名由 UTF-8 字节编码得到，游戏名不能改变数据目录边界。
    let name: String = game
        .as_bytes()
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect();
    let root = context
        .get("dataRoot")
        .and_then(Value::as_str)
        .ok_or("缺少数据目录")?;
    Ok(PathBuf::from(root)
        .join("games")
        .join(name)
        .join("settings.json"))
}

pub fn read(context: &Value) -> Result<Value, String> {
    let path = path(context)?;
    match fs::read(&path) {
        Ok(bytes) => serde_json::from_slice(&bytes)
            .map(|value| normalize(&value))
            .map_err(|error| error.to_string()),
        Err(error)
            if error.kind()
                == std::io::ErrorKind::NotFound =>
        {
            let legacy = &context["migrations"]
                ["legacy-game-config"];
            let value = normalize(
                &json!({"camera":legacy["playerTweaksCamera"],"gameplay":legacy["playerTweaksGameplay"]}),
            );
            atomic_write(
                &path,
                &serde_json::to_vec(&value)
                    .map_err(|error| error.to_string())?,
            )?;
            Ok(value)
        }
        Err(error) => Err(error.to_string()),
    }
}

pub fn save(
    context: &Value,
    value: &Value,
) -> Result<Value, String> {
    let value = normalize(value);
    atomic_write(
        &path(context)?,
        &serde_json::to_vec(&value)
            .map_err(|error| error.to_string())?,
    )?;
    Ok(value)
}

pub fn prepare(
    context: &Value,
    payload: &Value,
) -> Result<Value, String> {
    match context.get("gamePreset").and_then(Value::as_str)
    {
        Some("GIMI") => {
            let executable = PathBuf::from(
                context
                    .get("gameExecutable")
                    .and_then(Value::as_str)
                    .ok_or("缺少游戏程序")?,
            );
            super::star_rail::ensure_stopped(&executable)?;
            let value = read(context)?;
            let root = PathBuf::from(
                context
                    .get("packageRoot")
                    .and_then(Value::as_str)
                    .ok_or("缺少插件包目录")?,
            );
            if let Some(runtime) = payload
                .get("runtimeDirectory")
                .and_then(Value::as_str)
            {
                let legacy = PathBuf::from(runtime)
                    .join("SSMT-Player-Tweaks.dll");
                if legacy.is_file() {
                    let old = fs::read(&legacy).map_err(
                        |error| error.to_string(),
                    )?;
                    let packaged = fs::read(root.join(
                        "runtime/SSMT-Player-Tweaks.dll",
                    ))
                    .map_err(|error| error.to_string())?;
                    if old != packaged {
                        return Err("旧运行目录存在来源不同的 Player Tweaks DLL，请先备份并移出该文件后启动".into());
                    }
                    fs::remove_file(legacy).map_err(
                        |error| error.to_string(),
                    )?;
                    let ini = PathBuf::from(runtime)
                        .join("SSMT-Player-Tweaks.ini");
                    if ini.is_file() {
                        fs::rename(
                            &ini,
                            ini.with_extension(format!(
                                "ini.backup-{}",
                                std::time::SystemTime::now(
                                )
                                .duration_since(
                                    std::time::UNIX_EPOCH
                                )
                                .unwrap_or_default()
                                .as_nanos()
                            )),
                        )
                        .map_err(
                            |error| error.to_string(),
                        )?;
                    }
                }
            }
            let mut ini = String::new();
            for (section, keys) in [
                (
                    "Camera",
                    &[
                        "customFov",
                        "fov",
                        "preserveAimingFov",
                        "preserveCutsceneFov",
                        "restoreInUi",
                        "transitionSpeed",
                        "disableInputSmoothing",
                        "disableTransitionBlend",
                        "disableCharacterFade",
                        "disableEventCameraMovement",
                        "cameraZoom",
                    ][..],
                ),
                (
                    "Gameplay",
                    &[
                        "fpsUnlock",
                        "targetFps",
                        "fastTeamPage",
                    ][..],
                ),
            ] {
                ini.push_str(&format!("[{section}]\n"));
                for key in keys {
                    let field = &value
                        [section.to_ascii_lowercase()][key];
                    let rendered = field
                        .as_bool()
                        .map(|value| {
                            if value { "1" } else { "0" }
                                .to_owned()
                        })
                        .unwrap_or_else(|| {
                            field.to_string()
                        });
                    let name = format!(
                        "{}{}",
                        key[..1].to_ascii_uppercase(),
                        &key[1..]
                    );
                    ini.push_str(&format!(
                        "{name}={rendered}\n"
                    ));
                }
            }
            atomic_write(
                &root
                    .join("runtime/SSMT-Player-Tweaks.ini"),
                ini.as_bytes(),
            )?;
            Ok(Value::Null)
        }
        Some("SRMI") => {
            super::star_rail::execute(context, "prepare")
        }
        _ => Ok(Value::Null),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn settings_reject_invalid_numbers_and_unknown_fields()
    {
        let value = normalize(
            &json!({"camera":{"fov":999,"customFov":true,"transitionSpeed":-1},"gameplay":{"targetFps":120.5,"fpsUnlock":false},"other":"ignored"}),
        );
        assert_eq!(value["camera"]["fov"], 60);
        assert_eq!(value["camera"]["customFov"], true);
        assert_eq!(value["gameplay"]["targetFps"], 120);
        assert_eq!(value["gameplay"]["fpsUnlock"], false);
        assert!(value.get("other").is_none());
    }
}
