//! Curated local release manifests. No directory scans, downloads or device I/O.
use crate::package::{self, PackageInfo, PACKAGE_BYTES_MAX};
use serde_json::{json, Value};
use std::{
    collections::BTreeSet,
    fs,
    io::Read,
    path::{Path, PathBuf},
};

const MANIFEST_BYTES_MAX: usize = 65536;
const RELEASES_MAX: usize = 64;

#[derive(Clone)]
struct Entry {
    id: String,
    label: String,
    kind: String,
    file: String,
    build_id: Option<String>,
    sha256: String,
}

pub struct Catalog {
    directory: PathBuf,
    entries: Vec<Entry>,
}
pub struct Selection {
    pub id: String,
    pub bytes: Vec<u8>,
    pub info: PackageInfo,
}

fn hexadecimal(value: &str, length: usize) -> bool {
    value.len() == length
        && value
            .bytes()
            .all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
}
fn text<'a>(object: &'a Value, key: &str) -> Result<&'a str, String> {
    object
        .get(key)
        .and_then(Value::as_str)
        .filter(|s| !s.is_empty())
        .ok_or_else(|| format!("Release catalog requires a text {key}"))
}
fn basename(name: &str) -> bool {
    if name.len() > 255
        || !name.ends_with(".333")
        || name.starts_with('.')
        || name.ends_with([' ', '.'])
        || name
            .chars()
            .any(|c| c.is_control() || "\\/:*?\"<>|".contains(c))
    {
        return false;
    }
    let stem = name.split('.').next().unwrap_or("").to_ascii_uppercase();
    ![
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
        "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    ]
    .contains(&stem.as_str())
}
fn read_file(path: &Path, limit: usize) -> Result<Vec<u8>, String> {
    let metadata = fs::symlink_metadata(path).map_err(|_| "Curated release file is unavailable")?;
    if !metadata.file_type().is_file() || metadata.len() > limit as u64 {
        return Err("Curated release must be a bounded regular file, not a link".into());
    }
    let file = fs::File::open(path).map_err(|_| "Could not open curated release file")?;
    let metadata = file
        .metadata()
        .map_err(|_| "Could not verify curated release file")?;
    if !metadata.is_file() || metadata.len() > limit as u64 {
        return Err("Curated release is not a bounded regular file".into());
    }
    let mut bytes = Vec::new();
    file.take(limit as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| "Could not read curated release file")?;
    if bytes.len() > limit {
        return Err("Curated release grew beyond its format limit".into());
    }
    Ok(bytes)
}

impl Catalog {
    pub fn load(path: &Path) -> Result<Self, String> {
        let bytes = read_file(path, MANIFEST_BYTES_MAX)?;
        let absolute = path
            .canonicalize()
            .map_err(|_| "Could not resolve curated release catalog")?;
        let directory = absolute
            .parent()
            .ok_or("Catalog needs a containing directory")?
            .to_owned();
        let manifest: Value =
            serde_json::from_slice(&bytes).map_err(|_| "Malformed release catalog JSON")?;
        let object = manifest
            .as_object()
            .ok_or("Release catalog must be an object")?;
        if object
            .keys()
            .any(|key| !["format", "version", "releases"].contains(&key.as_str()))
            || manifest["format"] != "thy666.release-catalog"
            || manifest["version"].as_u64() != Some(1)
        {
            return Err("Unsupported release catalog format".into());
        }
        let releases = manifest["releases"]
            .as_array()
            .filter(|items| items.len() <= RELEASES_MAX)
            .ok_or("Release catalog needs a bounded releases array")?;
        let mut entries = Vec::new();
        let mut ids = BTreeSet::new();
        for release in releases {
            let object = release
                .as_object()
                .ok_or("Release catalog entries must be objects")?;
            if object.keys().any(|key| {
                !["id", "label", "kind", "file", "build_id", "sha256"].contains(&key.as_str())
            }) {
                return Err("Unknown release catalog field".into());
            }
            let id = text(release, "id")?;
            if id.len() > 64
                || !id
                    .bytes()
                    .all(|c| c.is_ascii_alphanumeric() || b"_-".contains(&c))
                || !ids.insert(id.to_owned())
            {
                return Err("Release IDs must be unique bounded identifiers".into());
            }
            let label = text(release, "label")?;
            if label.len() > 256
                || label.chars().count() > 128
                || label.chars().any(char::is_control)
            {
                return Err("Release label is invalid".into());
            }
            let kind = text(release, "kind")?;
            if !["thy666", "stock"].contains(&kind) {
                return Err("Unsupported release kind".into());
            }
            let file = text(release, "file")?;
            if !basename(file) {
                return Err("Release file must be a portable sibling .333 basename".into());
            }
            let sha256 = text(release, "sha256")?;
            if !hexadecimal(sha256, 64) {
                return Err("Release requires an exact lowercase SHA-256".into());
            }
            let build_id = match release.get("build_id") {
                None | Some(Value::Null) => None,
                Some(Value::String(build)) if hexadecimal(build, 16) => Some(build.clone()),
                _ => return Err("Release build ID must be full lowercase hexadecimal".into()),
            };
            if (kind == "thy666") != build_id.is_some() {
                return Err(
                    "THY666 requires a full build ID; stock must not claim a THY666 build".into(),
                );
            }
            entries.push(Entry {
                id: id.into(),
                label: label.into(),
                kind: kind.into(),
                file: file.into(),
                build_id,
                sha256: sha256.into(),
            });
        }
        Ok(Self { directory, entries })
    }

    pub fn list(&self) -> Vec<Value> {
        self.entries.iter().map(|entry| {
            let selected=self.select(&entry.id);
            let (available,reason,bytes)=match selected {Ok(value)=>(true,None,Some(value.info.application_bytes)),Err(reason)=>(false,Some(reason),None)};
            json!({"id":entry.id,"label":entry.label,"kind":entry.kind,"build_id":entry.build_id,
                "sha256":entry.sha256,"available":available,"disabled_reason":reason,"application_bytes":bytes})
        }).collect()
    }

    pub fn select(&self, id: &str) -> Result<Selection, String> {
        let entry = self
            .entries
            .iter()
            .find(|entry| entry.id == id)
            .ok_or("Unknown curated release ID")?;
        let path = self.directory.join(&entry.file);
        let resolved = path
            .canonicalize()
            .map_err(|_| "Curated release file is unavailable")?;
        if resolved.parent() != Some(self.directory.as_path()) {
            return Err("Curated release file leaves its catalog directory".into());
        }
        let bytes = read_file(&path, PACKAGE_BYTES_MAX)?;
        if path
            .canonicalize()
            .map_err(|_| "Curated release path changed while reading")?
            != resolved
            || !fs::symlink_metadata(&path)
                .map_err(|_| "Curated release path changed while reading")?
                .file_type()
                .is_file()
        {
            return Err("Curated release path changed while reading".into());
        }
        if package::sha256(&bytes) != entry.sha256 {
            return Err("Curated release SHA-256 differs from its reviewed catalog entry".into());
        }
        let info = package::parse(&bytes, entry.build_id.as_deref())
            .map_err(|error| error.to_string())?
            .info;
        if info.kind != entry.kind || !info.installable {
            return Err(
                "Curated release kind or transfer approval differs from its catalog entry".into(),
            );
        }
        Ok(Selection {
            id: entry.id.clone(),
            bytes,
            info,
        })
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};
    pub(crate) const BUILD: &str = "1234567890abcdef";
    pub(crate) struct Fixture {
        pub directory: PathBuf,
        pub manifest: PathBuf,
    }
    impl Fixture {
        pub(crate) fn new() -> Self {
            static SEQUENCE: AtomicUsize = AtomicUsize::new(0);
            let base = std::env::current_dir()
                .unwrap()
                .parent()
                .unwrap()
                .join("build")
                .canonicalize()
                .unwrap();
            let directory = base.join(format!(
                "updater-catalog-test-{}-{}",
                std::process::id(),
                SEQUENCE.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir(&directory).unwrap();
            Self {
                manifest: directory.join("releases.json"),
                directory,
            }
        }
        pub(crate) fn write(&self, entry: Value) {
            fs::write(
                &self.manifest,
                json!({"format":"thy666.release-catalog","version":1,"releases":[entry]})
                    .to_string(),
            )
            .unwrap();
        }
        pub(crate) fn entry(&self) -> Value {
            let bytes = image();
            fs::write(self.directory.join("synthetic.333"), &bytes).unwrap();
            json!({"id":"current","label":"Synthetic current THY666","kind":"thy666","file":"synthetic.333","build_id":BUILD,"sha256":package::sha256(&bytes)})
        }
    }
    impl Drop for Fixture {
        fn drop(&mut self) {
            let resolved = self.directory.canonicalize().unwrap();
            let base = std::env::current_dir()
                .unwrap()
                .parent()
                .unwrap()
                .join("build")
                .canonicalize()
                .unwrap();
            assert_eq!(resolved.parent(), Some(base.as_path()));
            assert!(resolved
                .file_name()
                .unwrap()
                .to_str()
                .unwrap()
                .starts_with("updater-catalog-test-"));
            fs::remove_dir_all(resolved).unwrap();
        }
    }
    pub(crate) fn image() -> Vec<u8> {
        let mut bytes =
            b"THY333000001000000000001000000000200EBF3A7F7429B51D6AA017D8C000004000000000001"
                .to_vec();
        let mut app = vec![0; 512];
        app[..4].copy_from_slice(&0x20020000u32.to_le_bytes());
        app[4..8].copy_from_slice(&(package::APPLICATION_BASE + 393).to_le_bytes());
        app[416..432].copy_from_slice(BUILD.as_bytes());
        bytes.extend(app);
        bytes
    }
    #[test]
    fn known_release_is_explicitly_listed_and_revalidated_on_every_selection() {
        let fixture = Fixture::new();
        let entry = fixture.entry();
        fixture.write(entry);
        let catalog = Catalog::load(&fixture.manifest).unwrap();
        assert_eq!(catalog.list()[0]["available"], true);
        assert_eq!(
            catalog.select("current").unwrap().info.build_id.as_deref(),
            Some(BUILD)
        );
        let mut changed = image();
        changed[580] ^= 1;
        fs::write(fixture.directory.join("synthetic.333"), changed).unwrap();
        assert!(catalog.select("current").is_err());
        assert_eq!(catalog.list()[0]["available"], false);
        assert!(catalog.select("unknown").is_err());
    }
    #[test]
    fn malformed_paths_digests_builds_and_metadata_are_rejected_without_scanning() {
        let fixture = Fixture::new();
        let original = fixture.entry();
        for (field, value) in [
            ("file", json!("../synthetic.333")),
            ("file", json!("other/synthetic.333")),
            ("file", json!("other\\synthetic.333")),
            ("file", json!("C:synthetic.333")),
            ("file", json!("CON.333")),
            ("sha256", json!("bad")),
            ("build_id", json!("12345678")),
            ("kind", json!("vendor")),
            ("id", json!("")),
            ("label", json!("bad\nlabel")),
            ("build_id", json!(true)),
        ] {
            let mut entry = original.clone();
            entry[field] = value;
            fixture.write(entry);
            assert!(Catalog::load(&fixture.manifest).is_err(), "field {field}");
        }
        fixture.write(original.clone());
        let mut value: Value =
            serde_json::from_slice(&fs::read(&fixture.manifest).unwrap()).unwrap();
        value["releases"] = json!([original.clone(), original]);
        fs::write(&fixture.manifest, value.to_string()).unwrap();
        assert!(Catalog::load(&fixture.manifest).is_err());
    }
    #[test]
    fn changed_expected_build_and_stock_kind_cannot_bypass_package_approval() {
        let fixture = Fixture::new();
        let mut entry = fixture.entry();
        entry["build_id"] = "1234567890abcdee".into();
        fixture.write(entry.clone());
        assert!(Catalog::load(&fixture.manifest)
            .unwrap()
            .select("current")
            .is_err());
        entry["kind"] = "stock".into();
        entry["build_id"] = Value::Null;
        fixture.write(entry);
        let catalog = Catalog::load(&fixture.manifest).unwrap();
        assert_eq!(catalog.list()[0]["available"], false);
        assert!(catalog.select("current").is_err());
    }
    #[test]
    fn missing_oversized_and_directory_packages_are_disabled() {
        let fixture = Fixture::new();
        let entry = fixture.entry();
        fixture.write(entry);
        let catalog = Catalog::load(&fixture.manifest).unwrap();
        let path = fixture.directory.join("synthetic.333");
        fs::remove_file(&path).unwrap();
        assert!(!catalog.list()[0]["available"].as_bool().unwrap());
        fs::write(&path, vec![0; PACKAGE_BYTES_MAX + 1]).unwrap();
        assert!(catalog.select("current").is_err());
        fs::remove_file(&path).unwrap();
        fs::create_dir(&path).unwrap();
        assert!(catalog.select("current").is_err());
    }
    #[cfg(unix)]
    #[test]
    fn sibling_symlinks_are_rejected_even_when_their_bytes_would_match() {
        let fixture = Fixture::new();
        let entry = fixture.entry();
        fixture.write(entry);
        let path = fixture.directory.join("synthetic.333");
        fs::rename(&path, fixture.directory.join("target.333")).unwrap();
        std::os::unix::fs::symlink("target.333", &path).unwrap();
        assert!(Catalog::load(&fixture.manifest)
            .unwrap()
            .select("current")
            .is_err());
    }
}
