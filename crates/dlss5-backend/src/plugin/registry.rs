//! 此处只保存本插件请求快照，不访问或控制 SSMT 的插件注册表。
use serde::Deserialize;
use ssmt_plugin_graphics::reshade_state::Contribution;
use std::{collections::BTreeMap, path::PathBuf};

#[derive(Debug, Clone, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum ExternalDependencyStatus {
    Missing,
    Invalid,
    Ready,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ExternalDependencyState {
    pub status: ExternalDependencyStatus,
    pub path: Option<PathBuf>,
    pub reason: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Manifest {
    pub id: String,
    pub name: String,
    pub version: String,
}

#[derive(Debug, Clone)]
pub struct InstalledPlugin {
    pub manifest: Manifest,
    pub package_root: PathBuf,
    pub enabled: bool,
    pub external_dependencies:
        BTreeMap<String, ExternalDependencyState>,
}

impl InstalledPlugin {
    pub fn id(&self) -> &str {
        &self.manifest.id
    }
    pub fn version(&self) -> &str {
        &self.manifest.version
    }
}

#[derive(Clone)]
pub struct PluginRegistry {
    plugin: InstalledPlugin,
    pub contributions: Vec<Contribution>,
    #[cfg(test)]
    fixture_root: Option<PathBuf>,
    #[cfg(test)]
    fixtures: BTreeMap<String, InstalledPlugin>,
    #[cfg(test)]
    games: BTreeMap<(String, String), bool>,
}

impl PluginRegistry {
    pub fn from_default_location() -> Result<Self, String> {
        let context = crate::context::value();
        let package_root = PathBuf::from(
            context["packageRoot"]
                .as_str()
                .ok_or("缺少插件目录")?,
        );
        let manifest = serde_json::from_slice(
            &std::fs::read(
                package_root.join("ssmt-plugin.json"),
            )
            .map_err(|error| error.to_string())?,
        )
        .map_err(|error| error.to_string())?;
        let external_dependencies = serde_json::from_value(
            context["externalDependencies"].clone(),
        )
        .map_err(|error| error.to_string())?;
        let mut contributions = Vec::new();
        if let Some(peers) = context
            .get("launchPeers")
            .and_then(serde_json::Value::as_array)
        {
            for peer in peers {
                if let Some(value) =
                    peer["exports"].get("ssmt.reshade.v1")
                {
                    let contribution: Contribution =
                        serde_json::from_value(
                            value.clone(),
                        )
                        .map_err(
                            |error| error.to_string(),
                        )?;
                    if !contribution.addons.is_empty() && (peer["nativeInjectionAcknowledged"].as_bool()!=Some(true)
                        || !peer["permissions"].as_array().is_some_and(|permissions|permissions.iter().any(|permission|permission.as_str()==Some("native.inject")))) {
                        return Err("共享 Add-on 的来源插件缺少原生注入权限或风险确认".into());
                    }
                    contributions.push(contribution);
                }
            }
        }
        Ok(Self {
            plugin: InstalledPlugin {
                manifest,
                package_root,
                enabled: true,
                external_dependencies,
            },
            contributions,
            #[cfg(test)]
            fixture_root: None,
            #[cfg(test)]
            fixtures: BTreeMap::new(),
            #[cfg(test)]
            games: BTreeMap::new(),
        })
    }
    pub fn find(
        &self,
        id: &str,
    ) -> Option<&InstalledPlugin> {
        #[cfg(test)]
        if self.fixture_root.is_some() {
            return self.fixtures.get(id);
        }
        (self.plugin.id() == id).then_some(&self.plugin)
    }
    pub fn for_game(&self, _game: &str) -> Self {
        #[allow(unused_mut)]
        let mut scoped = self.clone();
        #[cfg(test)]
        if self.fixture_root.is_some() {
            for (id, plugin) in &mut scoped.fixtures {
                if let Some(enabled) = self
                    .games
                    .get(&(_game.to_owned(), id.clone()))
                {
                    plugin.enabled = *enabled;
                }
            }
        }
        scoped
    }
    pub fn managed_game_executables_for_plugin(
        &self,
        id: &str,
    ) -> Vec<PathBuf> {
        if self.find(id).is_none() {
            return Vec::new();
        }
        crate::context::value()["gameExecutable"]
            .as_str()
            .map(PathBuf::from)
            .into_iter()
            .collect()
    }
}

#[cfg(test)]
impl PluginRegistry {
    pub fn new(
        root: impl Into<PathBuf>,
    ) -> Result<Self, String> {
        let root = root.into();
        std::fs::create_dir_all(&root)
            .map_err(|error| error.to_string())?;
        let mut registry = Self {
            plugin: InstalledPlugin {
                manifest: Manifest {
                    id: String::new(),
                    name: String::new(),
                    version: String::new(),
                },
                package_root: root.clone(),
                enabled: false,
                external_dependencies: BTreeMap::new(),
            },
            contributions: Vec::new(),
            fixture_root: Some(root),
            fixtures: BTreeMap::new(),
            games: BTreeMap::new(),
        };
        registry.refresh()?;
        Ok(registry)
    }
    pub fn plugins_root(&self) -> &std::path::Path {
        self.fixture_root.as_ref().unwrap()
    }
    pub fn refresh(&mut self) -> Result<(), String> {
        for id in std::fs::read_dir(self.plugins_root())
            .map_err(|error| error.to_string())?
            .flatten()
        {
            if !id.path().is_dir() {
                continue;
            }
            for version in std::fs::read_dir(id.path())
                .map_err(|error| error.to_string())?
                .flatten()
            {
                let package_root = version.path();
                let path =
                    package_root.join("ssmt-plugin.json");
                if !path.is_file() {
                    continue;
                }
                let manifest: Manifest =
                    serde_json::from_slice(
                        &std::fs::read(path).map_err(
                            |error| error.to_string(),
                        )?,
                    )
                    .map_err(|error| error.to_string())?;
                let previous =
                    self.fixtures.get(&manifest.id);
                let enabled = previous
                    .is_some_and(|plugin| plugin.enabled);
                let external_dependencies = previous
                    .map(|plugin| {
                        plugin.external_dependencies.clone()
                    })
                    .unwrap_or_default();
                self.fixtures.insert(
                    manifest.id.clone(),
                    InstalledPlugin {
                        manifest,
                        package_root,
                        enabled,
                        external_dependencies,
                    },
                );
            }
        }
        Ok(())
    }
    pub fn set_enabled(
        &mut self,
        id: &str,
        enabled: bool,
    ) -> Result<(), String> {
        self.fixtures
            .get_mut(id)
            .ok_or("测试插件不存在")?
            .enabled = enabled;
        Ok(())
    }
    pub fn set_game_enabled(
        &mut self,
        game: &str,
        id: &str,
        enabled: bool,
    ) -> Result<(), String> {
        self.games
            .insert((game.into(), id.into()), enabled);
        Ok(())
    }
    pub fn set_external_dependency_path(
        &mut self,
        id: &str,
        dependency: &str,
        path: &std::path::Path,
    ) -> Result<(), String> {
        self.fixtures
            .get_mut(id)
            .ok_or("测试插件不存在")?
            .external_dependencies
            .insert(
                dependency.into(),
                ExternalDependencyState {
                    status: ExternalDependencyStatus::Ready,
                    path: Some(path.into()),
                    reason: None,
                },
            );
        Ok(())
    }
}

pub fn game_scope_preset(_game: &str) -> String {
    crate::context::value()["gamePreset"]
        .as_str()
        .unwrap_or_default()
        .into()
}
