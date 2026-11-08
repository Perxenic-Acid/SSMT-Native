use crate::plugin::{
    commands::installed_plugin_registry,
    dlss5::{
        inspect_game_executable, Dlss5Route, Dlss5State,
    },
    managed_stack::{self, Dlss5RouteScan},
    registry::{game_scope_preset, PluginRegistry},
};
use serde::Serialize;
use std::{
    fs,
    io::{Read, Seek, SeekFrom},
    path::PathBuf,
    time::UNIX_EPOCH,
};
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Dlss5GameStateSnapshot {
    pub state: &'static str,
    pub route: Option<String>,
    pub uses_reshade: Option<bool>,
    pub uses_proxy: Option<bool>,
    pub manifest_path: Option<String>,
    pub reason: Option<String>,
    pub managed_host: Option<bool>,
    pub owner: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Dlss5LastRunStatus {
    pub verdict: &'static str,
    pub reason: &'static str,
    pub observed_at_ms: Option<u128>,
    pub warning: Option<&'static str>,
}

fn inspect_dlss5_last_run_log(
    path: &std::path::Path,
) -> Result<Dlss5LastRunStatus, String> {
    let mut file = fs::File::open(path)
        .map_err(|error| error.to_string())?;
    let metadata = file
        .metadata()
        .map_err(|error| error.to_string())?;
    let observed_at_ms =
        metadata.modified().ok().and_then(|time| {
            time.duration_since(UNIX_EPOCH)
                .ok()
                .map(|duration| duration.as_millis())
        });
    // ReShade.log 可非常大；只读取最后 1 MiB，避免设置页扫描整份日志。
    const MAX_LOG_BYTES: u64 = 1024 * 1024;
    file.seek(SeekFrom::Start(
        metadata.len().saturating_sub(MAX_LOG_BYTES),
    ))
    .map_err(|error| error.to_string())?;
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes)
        .map_err(|error| error.to_string())?;
    let log = String::from_utf8_lossy(&bytes);
    let (verdict, reason) = if log
        .contains("[DLSS 5 Feed] stopped: Direct3D 11 multithread protection is unavailable")
    {
        ("inactive", "d3d11_interposer_race")
    } else if log.contains("[DLSS 5 Feed] stopped:") {
        ("inactive", "feeder_stopped")
    } else if log.contains("[DLSS 5 Bridge] stopped:") {
        ("inactive", "bridge_stopped")
    } else if log.contains("NO DLSS CREATE SEEN") {
        ("inactive", "no_dlss_create")
    } else if log.contains("NR-VERDICT state=ENGAGED") {
        ("engaged", "nr_evaluated")
    } else if log.contains("inline feature 18 evaluation succeeded") {
        ("engaged", "feature18_evaluated")
    } else {
        ("unknown", "no_nr_evidence")
    };
    let warning = log
        .contains("most likely a shadow map or atlas")
        .then_some("depth_source_mismatch");
    Ok(Dlss5LastRunStatus {
        verdict,
        reason,
        observed_at_ms,
        warning,
    })
}

impl From<&Dlss5State> for Dlss5GameStateSnapshot {
    fn from(value: &Dlss5State) -> Self {
        match value {
            Dlss5State::NotManaged => Self {
                state: "not_managed",
                route: None,
                uses_reshade: None,
                uses_proxy: None,
                manifest_path: None,
                reason: None,
                managed_host: None,
                owner: None,
            },
            Dlss5State::Managed(managed) => Self {
                state: "managed",
                route: Some(match &managed.route {
                    Dlss5Route::Native => {
                        "native".to_string()
                    }
                    Dlss5Route::Feeder => {
                        "feeder".to_string()
                    }
                    Dlss5Route::RenoDx => {
                        "renodx".to_string()
                    }
                    Dlss5Route::OptiScaler => {
                        "optiscaler".to_string()
                    }
                    Dlss5Route::Unknown(route) => {
                        route.clone()
                    }
                }),
                uses_reshade: Some(managed.uses_reshade),
                uses_proxy: Some(managed.uses_proxy),
                manifest_path: Some(
                    managed
                        .manifest_path
                        .to_string_lossy()
                        .into_owned(),
                ),
                reason: None,
                managed_host: Some(
                    managed.external_host.is_some(),
                ),
                owner: managed
                    .external_host
                    .as_ref()
                    .map(|host| host.owner.clone()),
            },
            Dlss5State::Broken {
                manifest_path,
                reason,
            } => Self {
                state: "broken",
                route: None,
                uses_reshade: None,
                uses_proxy: None,
                manifest_path: Some(
                    manifest_path
                        .to_string_lossy()
                        .into_owned(),
                ),
                reason: Some(reason.clone()),
                managed_host: None,
                owner: None,
            },
        }
    }
}

pub fn inspect_dlss5_game_state_blocking(
    game_executable: String,
) -> Result<Dlss5GameStateSnapshot, String> {
    let game_executable = PathBuf::from(game_executable);
    if !game_executable.is_file() {
        return Err(format!(
            "game executable does not exist: {}",
            game_executable.display()
        ));
    }
    Ok(Dlss5GameStateSnapshot::from(
        &inspect_game_executable(&game_executable),
    ))
}

pub fn inspect_dlss5_last_run_blocking(
    game_executable: String,
    game_name: Option<String>,
) -> Result<Dlss5LastRunStatus, String> {
    let executable = PathBuf::from(game_executable);
    if !executable.is_file() {
        return Err(format!(
            "game executable does not exist: {}",
            executable.display()
        ));
    }
    let log = match inspect_game_executable(&executable) {
        Dlss5State::Managed(state) => {
            let game_log = executable
                .parent()
                .ok_or("game executable has no parent directory")?
                .join("ReShade.log");
            if !game_log.is_file() {
                return Ok(Dlss5LastRunStatus {
                    verdict: "unknown",
                    reason: "no_log",
                    observed_at_ms: None,
                    warning: None,
                });
            }
            let log_modified = fs::metadata(&game_log)
                .and_then(|metadata| metadata.modified())
                .map_err(|error| error.to_string())?;
            let manifest_modified =
                fs::metadata(&state.manifest_path)
                    .and_then(|metadata| {
                        metadata.modified()
                    })
                    .map_err(|error| error.to_string())?;
            if log_modified < manifest_modified {
                return Ok(Dlss5LastRunStatus {
                    verdict: "unknown",
                    reason: "log_precedes_install",
                    observed_at_ms: None,
                    warning: None,
                });
            }
            game_log
        }
        Dlss5State::NotManaged => {
            let Some(game_name) = game_name else {
                return Ok(Dlss5LastRunStatus {
                    verdict: "unknown",
                    reason: "no_log",
                    observed_at_ms: None,
                    warning: None,
                });
            };
            let Some(registry) =
                installed_plugin_registry()?
            else {
                return Ok(Dlss5LastRunStatus {
                    verdict: "unknown",
                    reason: "no_log",
                    observed_at_ms: None,
                    warning: None,
                });
            };
            let scoped = registry.for_game(&game_name);
            if !scoped
                .find(crate::plugin::dlss5::DLSS5_PLUGIN_ID)
                .is_some_and(|plugin| plugin.enabled)
            {
                return Ok(Dlss5LastRunStatus {
                    verdict: "unknown",
                    reason: "no_log",
                    observed_at_ms: None,
                    warning: None,
                });
            }
            let Some(log) =
                crate::plugin::dlss5_hot_runtime::log_path(
                    &game_name,
                )?
            else {
                return Ok(Dlss5LastRunStatus {
                    verdict: "unknown",
                    reason: "no_log",
                    observed_at_ms: None,
                    warning: None,
                });
            };
            log
        }
        Dlss5State::Broken { .. } => {
            return Ok(Dlss5LastRunStatus {
                verdict: "unknown",
                reason: "no_log",
                observed_at_ms: None,
                warning: None,
            });
        }
    };
    if !log.is_file() {
        return Ok(Dlss5LastRunStatus {
            verdict: "unknown",
            reason: "no_log",
            observed_at_ms: None,
            warning: None,
        });
    }
    inspect_dlss5_last_run_log(&log)
}

pub fn inspect_dlss5_route_options_blocking(
    game_name: String,
    game_executable: String,
    api_override: String,
) -> Result<Dlss5RouteScan, String> {
    let registry = PluginRegistry::from_default_location()
        .map_err(|error| error.to_string())?
        .for_game(&game_name);
    let executable = PathBuf::from(game_executable);
    if crate::plugin::dlss5_hot_runtime::applies(
        &executable,
    ) {
        let mut scan =
            crate::plugin::dlss5_hot_runtime::route_scan(
                &registry,
                &game_scope_preset(&game_name),
                &executable,
                &api_override,
            )?;
        if crate::plugin::gimi_hot_runtime::applies(
            &executable,
        ) {
            scan.installed_route = crate::plugin::dlss5_hot_runtime::gimi_route(&game_name).ok();
        }
        return Ok(scan);
    }
    managed_stack::inspect_routes(
        &registry,
        &executable,
        &api_override,
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::SystemTime;
    #[test]
    fn dlss5_last_run_recognizes_feature18_and_reports_depth_mismatch(
    ) {
        let dir = std::env::temp_dir().join(format!(
            "ssmt-dlss5-last-run-{}",
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir_all(&dir).unwrap();
        let log = dir.join("ReShade.log");
        fs::write(&log, "addon loaded\n[DLSS 5 Feed] stopped: Direct3D 11 multithread protection is unavailable on this device and a present-path interposer is loaded\n").unwrap();
        let status =
            inspect_dlss5_last_run_log(&log).unwrap();
        assert_eq!(status.verdict, "inactive");
        assert_eq!(status.reason, "d3d11_interposer_race");
        fs::write(
            &log,
            "addon loaded\n[DLSS 5 Bridge] stopped: resource build. The game renders normally.\n",
        )
        .unwrap();
        let status =
            inspect_dlss5_last_run_log(&log).unwrap();
        assert_eq!(status.verdict, "inactive");
        assert_eq!(status.reason, "bridge_stopped");
        fs::write(
            &log,
            "addon loaded\ninline feature 18 evaluation succeeded (count=60)\nThe depth source is 5120x3200, most likely a shadow map or atlas\n",
        )
        .unwrap();
        let status =
            inspect_dlss5_last_run_log(&log).unwrap();
        assert_eq!(status.verdict, "engaged");
        assert_eq!(status.reason, "feature18_evaluated");
        assert_eq!(
            status.warning,
            Some("depth_source_mismatch")
        );
        fs::write(
            &log,
            "addon loaded\nNR-VERDICT state=ENGAGED; successful frames 60\n",
        )
        .unwrap();
        assert_eq!(
            inspect_dlss5_last_run_log(&log)
                .unwrap()
                .verdict,
            "engaged"
        );
        fs::write(&log, "addon loaded\n").unwrap();
        let status =
            inspect_dlss5_last_run_log(&log).unwrap();
        assert_eq!(status.verdict, "unknown");
        assert_eq!(status.warning, None);
        fs::remove_dir_all(dir).unwrap();
    }
}
