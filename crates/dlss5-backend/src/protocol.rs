use crate::{
    api, context,
    plugin::{
        dlss5_hot_runtime as hot, gimi_hot_runtime as gimi,
        managed_stack, registry::PluginRegistry,
    },
};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::path::Path;

fn string<'a>(
    payload: &'a Value,
    key: &str,
) -> Result<&'a str, String> {
    payload[key]
        .as_str()
        .ok_or_else(|| format!("缺少参数：{key}"))
}

pub async fn dispatch(
    method: &str,
    payload: &Value,
) -> Result<Value, String> {
    let game = context::value()["gameName"]
        .as_str()
        .ok_or("缺少游戏上下文")?
        .to_owned();
    if payload
        .get("gameName")
        .is_some_and(|value| value.as_str() != Some(&game))
    {
        return Err("不能跨游戏调用插件后台".into());
    }
    let executable = context::value()["gameExecutable"]
        .as_str()
        .unwrap_or_default()
        .to_owned();
    match method {
        "launch.describe" => {
            if !hot::applies(Path::new(&executable)) {
                return Ok(
                    json!({"exports":{},"launch":null}),
                );
            }
            // 路线必须同时满足插件声明的游戏预设与进程名。
            api::inspect_dlss5_route_options_blocking(
                game,
                executable,
                "auto".into(),
            )?;
            Ok(
                json!({"exports":{},"launch":{"priority":100,"resources":["graphics.reshade.host","graphics.dlss5.route"]}}),
            )
        }
        "launch.prepare" => {
            if payload["launchContext"]["existingDlls"].as_array().is_some_and(|paths| paths.iter().any(|path|
                path.as_str().and_then(|path|Path::new(path).file_name()).is_some_and(|name|name.to_string_lossy().eq_ignore_ascii_case("dxgi.dll")||name.to_string_lossy().eq_ignore_ascii_case("d3d11.dll")))) {
                return Err("热注入组件与额外图形代理 DLL 冲突".into());
            }
            let plan = hot::prepare_dlss5_hot_runtime(
                game, executable,
            )?
            .ok_or("此目标没有 DLSS5 启动计划")?;
            let mut dlls = Vec::new();
            for path in plan.dlls {
                let bytes = std::fs::read(&path)
                    .map_err(|error| error.to_string())?;
                dlls.push(json!({"path":path,"sha256":format!("{:x}",Sha256::digest(bytes))}));
            }
            Ok(
                json!({"dlls":dlls,"environment":plan.environment,"requiredLaunchArgs":plan.required_launch_args,
                "excludedLaunchArgs":plan.excluded_launch_args,"logSources":plan.log_sources}),
            )
        }
        "launch.commit" => {
            hot::configure_dlss5_launch_settings(
                game, executable,
            )?;
            Ok(Value::Null)
        }
        "launch.cancel" => {
            crate::gimi_settings::restore_dlss5(
                &executable,
            )?;
            Ok(Value::Null)
        }
        "cancel" => Ok(Value::Null),
        "disabled" => {
            crate::tasks::stop_all().await?;
            let _lock = crate::asset_lock()?;
            crate::gimi_settings::restore_dlss5(
                &executable,
            )?;
            match crate::plugin::dlss5::inspect_game_executable(Path::new(&executable)) {
                crate::plugin::dlss5::Dlss5State::Managed(state) if state.external_host.is_some()=>managed_stack::restore_swapper(&PluginRegistry::from_default_location()?,Path::new(&executable))?,
                crate::plugin::dlss5::Dlss5State::Broken{reason,..}=>return Err(reason),
                _=>{}
            }
            Ok(Value::Null)
        }
        "recover" => {
            crate::tasks::recover()?;
            let _lock = crate::asset_lock()?;
            crate::gimi_settings::restore_dlss5(
                &executable,
            )?;
            if Path::new(&executable).is_file() {
                gimi::ensure_stopped(Path::new(
                    &executable,
                ))?;
            }
            managed_stack::restore(Path::new(&executable))
                .map(
                    |restored| json!({"restored":restored}),
                )
        }
        "inspect_dlss5_game_state" => serde_json::to_value(
            api::inspect_dlss5_game_state_blocking(
                executable,
            )?,
        )
        .map_err(|error| error.to_string()),
        "inspect_dlss5_last_run" => serde_json::to_value(
            api::inspect_dlss5_last_run_blocking(
                executable,
                Some(game),
            )?,
        )
        .map_err(|error| error.to_string()),
        "inspect_dlss5_route_options" => {
            serde_json::to_value(
                api::inspect_dlss5_route_options_blocking(
                    game,
                    executable,
                    payload["apiOverride"]
                        .as_str()
                        .unwrap_or("auto")
                        .into(),
                )?,
            )
            .map_err(|error| error.to_string())
        }
        "dlss5_hot_runtime_status" => serde_json::to_value(
            hot::dlss5_hot_runtime_status(game).await?,
        )
        .map_err(|error| error.to_string()),
        "gimi_hot_runtime_status" => serde_json::to_value(
            gimi::gimi_hot_runtime_status(game).await?,
        )
        .map_err(|error| error.to_string()),
        "dlss5_hot_runtime_history" => {
            serde_json::to_value(
                hot::catalog::dlss5_hot_runtime_history(
                    game,
                )
                .await?,
            )
            .map_err(|error| error.to_string())
        }
        "open_dlss5_swapper" => {
            managed_stack::open_swapper(
                &PluginRegistry::from_default_location()?,
            )?;
            Ok(Value::Null)
        }
        "task.start" => {
            crate::tasks::start(payload.clone())
        }
        "task.read" => {
            crate::tasks::read(string(payload, "taskId")?)
        }
        "task.active" => crate::tasks::active(),
        "task.cancel" => {
            crate::tasks::cancel(string(payload, "taskId")?)
        }
        _ => Err(format!("未知 DLSS5 后台方法：{method}")),
    }
}

pub async fn operation(
    payload: &Value,
    progress: impl Fn(crate::plugin::graphics_tasks::GraphicsProgress)
        + Send
        + Clone
        + 'static,
) -> Result<Value, String> {
    let game = context::value()["gameName"]
        .as_str()
        .ok_or("缺少游戏上下文")?
        .to_owned();
    let executable = context::value()["gameExecutable"]
        .as_str()
        .ok_or("缺少游戏程序")?
        .to_owned();
    match string(payload, "operation")? {
        "runtime.download" => {
            let profile =
                string(payload, "profile")?.to_owned();
            if payload["runtime"].as_str() == Some("gimi") {
                serde_json::to_value(gimi::download::download_gimi_hot_runtime_with_progress(game,executable,profile,progress).await?).map_err(|error|error.to_string())
            } else if payload["runtime"].as_str()
                == Some("dlss5")
            {
                serde_json::to_value(hot::download_dlss5_hot_runtime_with_progress(game,executable,profile,payload["route"].as_str().map(str::to_owned),progress).await?).map_err(|error|error.to_string())
            } else {
                Err("未知组件类型".into())
            }
        }
        "runtime.import" => serde_json::to_value(
            gimi::import_gimi_hot_runtime(
                game,
                executable,
                string(payload, "sourceDirectory")?.into(),
                string(payload, "profile")?.into(),
            )?,
        )
        .map_err(|error| error.to_string()),
        "runtime.select" => {
            hot::catalog::select_dlss5_hot_runtime(
                game,
                executable,
                string(payload, "root")?.into(),
            )?;
            Ok(Value::Null)
        }
        "runtime.remove" => {
            hot::catalog::remove_dlss5_hot_runtime(
                game,
                executable,
                string(payload, "root")?.into(),
            )?;
            Ok(Value::Null)
        }
        "route.restore" => {
            managed_stack::restore_swapper(
                &PluginRegistry::from_default_location()?,
                Path::new(&executable),
            )?;
            Ok(Value::Null)
        }
        "route.install" => {
            let registry =
                PluginRegistry::from_default_location()?;
            if let Err(error) = managed_stack::install_route(
                &registry,
                Path::new(&executable),
                string(payload, "route")?,
                payload["apiOverride"]
                    .as_str()
                    .unwrap_or("auto"),
                payload["antiCheatAcknowledged"]
                    .as_bool()
                    .unwrap_or(false),
            ) {
                let rollback =
                    managed_stack::restore_swapper(
                        &registry,
                        Path::new(&executable),
                    );
                return Err(format!(
                    "{error}; rollback: {rollback:?}"
                ));
            }
            Ok(Value::Null)
        }
        _ => Err("未知组件任务操作".into()),
    }
}
