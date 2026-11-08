use serde::Deserialize;
use serde_json::Value;
use ssmt_plugin_graphics::{
    managed_reshade,
    paths::{canonical_path, checked_path, read_text},
};
use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
};
#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Policy {
    schema_version: u32,
    pub processes: Vec<String>,
    dependency: String,
    pub host_dll: String,
    config: String,
    preset: String,
    addons: String,
}

pub use ssmt_plugin_graphics::reshade_state::Contribution;

impl Policy {
    pub fn load(context: &Value) -> Result<Self, String> {
        let root = canonical_path(Path::new(
            context["packageRoot"]
                .as_str()
                .ok_or("缺少插件包目录")?,
        ))
        .map_err(|e| e.to_string())?;
        let text = read_text(&checked_path(
            &root,
            "host/graphics.json",
        )?)?;
        let policy: Self = serde_json::from_str(&text)
            .map_err(|e| format!("Shade 声明无效：{e}"))?;
        if policy.schema_version != 1
            || policy.processes.is_empty()
        {
            return Err("不支持的 Shade 声明".into());
        }
        if policy.dependency.is_empty()
            || !policy.dependency.bytes().all(|byte| {
                byte.is_ascii_alphanumeric()
                    || matches!(byte, b'-' | b'_' | b'.')
            })
        {
            return Err("Shade 依赖名称无效".into());
        }
        for path in [
            &policy.host_dll,
            &policy.config,
            &policy.preset,
            &policy.addons,
        ]
        .into_iter()
        .chain(&policy.processes)
        {
            ssmt_plugin_graphics::validate_package_relative_path(path).map_err(|e| e.to_string())?;
        }
        if policy
            .processes
            .iter()
            .any(|name| name.contains(['/', '\\']))
        {
            return Err("Shade 目标必须是文件名".into());
        }
        Ok(policy)
    }

    pub fn dependency_root(
        &self,
        context: &Value,
    ) -> Result<PathBuf, String> {
        let dependency = &context["externalDependencies"]
            [&self.dependency];
        if dependency["status"].as_str() != Some("ready") {
            return Err(
                "Shade Bridge 的资源目录尚未就绪".into()
            );
        }
        let path = dependency["path"]
            .as_str()
            .ok_or("缺少 Shade 资源目录")?;
        canonical_path(Path::new(path))
            .map_err(|e| e.to_string())
    }

    pub fn contribution(
        &self,
        context: &Value,
    ) -> Result<Contribution, String> {
        let root = self.dependency_root(context)?;
        let mut config =
            read_text(&checked_path(&root, &self.config)?)?;
        rebase_search_paths(&mut config, &root);
        let preset = if root.join(&self.preset).is_file() {
            read_text(&checked_path(&root, &self.preset)?)?
        } else {
            String::new()
        };
        let mut addons = BTreeMap::new();
        let directory = root.join(&self.addons);
        if directory.exists() {
            let directory = canonical_path(&directory)
                .map_err(|e| e.to_string())?;
            if !directory.starts_with(&root) {
                return Err(
                    "Shade Add-on 越出资源目录".into()
                );
            }
            for entry in fs::read_dir(directory)
                .map_err(|e| e.to_string())?
            {
                let path = entry
                    .map_err(|e| e.to_string())?
                    .path();
                let name = path
                    .file_name()
                    .unwrap_or_default()
                    .to_string_lossy()
                    .into_owned();
                if !path.is_file()
                    || !name
                        .to_ascii_lowercase()
                        .ends_with(".addon64")
                {
                    continue;
                }
                addons.insert(
                    name.clone(),
                    checked_path(
                        &root,
                        &format!("{}/{name}", self.addons),
                    )?,
                );
            }
        }
        Ok(Contribution {
            source: format!(
                "{} v{}：{}",
                context["pluginId"]
                    .as_str()
                    .unwrap_or("HoYoShade"),
                context["pluginVersion"]
                    .as_str()
                    .unwrap_or(""),
                root.join(&self.config).display()
            ),
            config,
            preset,
            addons,
        })
    }
}

fn rebase_search_paths(text: &mut String, base: &Path) {
    for key in ["EffectSearchPaths", "TextureSearchPaths"] {
        let value = text.lines().find_map(|line| {
            line.trim()
                .split_once('=')
                .filter(|(name, _)| {
                    name.trim().eq_ignore_ascii_case(key)
                })
                .map(|(_, value)| value.to_string())
        });
        if let Some(value) = value {
            let paths = value
                .split(',')
                .map(|part| {
                    let path = Path::new(part.trim());
                    if path.is_absolute() {
                        path.to_string_lossy().into_owned()
                    } else {
                        base.join(path)
                            .to_string_lossy()
                            .into_owned()
                    }
                })
                .collect::<Vec<_>>()
                .join(",");
            managed_reshade::set_entry(
                text, "GENERAL", key, &paths,
            );
        }
    }
}
