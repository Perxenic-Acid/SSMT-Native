//! 本插件自有设置，首次写入接管旧命名空间；不修改宿主设置文件。
use serde_json::Value;
use std::{collections::BTreeMap, path::PathBuf};

pub struct PluginSettingsStore {
    path: PathBuf,
    values: BTreeMap<String, Value>,
}
impl PluginSettingsStore {
    pub fn new(
        path: impl Into<PathBuf>,
    ) -> Result<Self, String> {
        let path = path.into();
        let values = match std::fs::read(&path) {
            Ok(bytes) => serde_json::from_slice(&bytes)
                .map_err(|error| error.to_string())?,
            Err(error)
                if error.kind()
                    == std::io::ErrorKind::NotFound =>
            {
                BTreeMap::new()
            }
            Err(error) => return Err(error.to_string()),
        };
        Ok(Self { path, values })
    }
    pub fn from_default_location() -> Result<Self, String> {
        let path = crate::context::data_root()
            .join("settings.json");
        let exists = path.exists();
        let mut store = Self::new(path)?;
        if !exists {
            store.values = serde_json::from_value(
                crate::context::value()
                    .get("legacySettings")
                    .cloned()
                    .unwrap_or(serde_json::json!({})),
            )
            .map_err(|error| error.to_string())?;
        }
        Ok(store)
    }
    pub fn get(
        &self,
        _plugin: &str,
        key: &str,
    ) -> Result<Option<Value>, String> {
        Ok(self.values.get(key).cloned())
    }
    pub fn set(
        &mut self,
        plugin: &str,
        key: &str,
        value: Value,
    ) -> Result<(), String> {
        self.set_many(plugin, [(key.to_owned(), value)])
    }
    pub fn remove(
        &mut self,
        _plugin: &str,
        key: &str,
    ) -> Result<bool, String> {
        let removed = self.values.remove(key).is_some();
        if removed {
            self.save()?;
        }
        Ok(removed)
    }
    pub fn set_many(
        &mut self,
        _plugin: &str,
        values: impl IntoIterator<Item = (String, Value)>,
    ) -> Result<(), String> {
        self.values.extend(values);
        self.save()
    }
    fn save(&self) -> Result<(), String> {
        let root =
            self.path.parent().ok_or("设置目录无效")?;
        std::fs::create_dir_all(root)
            .map_err(|error| error.to_string())?;
        let transaction=ssmt_plugin_graphics::managed_files::DirectoryTransaction::open(root)?;
        transaction.commit(BTreeMap::from([(
            self.path
                .file_name()
                .ok_or("设置文件名无效")?
                .to_string_lossy()
                .into_owned(),
            Some(
                serde_json::to_vec(&self.values)
                    .map_err(|error| error.to_string())?,
            ),
        )]))
    }
}
