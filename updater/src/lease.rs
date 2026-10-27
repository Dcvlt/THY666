//! Advisory process lease shared by THY666 helper/CLI instances. Other vendors'
//! software does not honor this lease and must be closed explicitly by the user.
use std::{
    fs::{self, File},
    path::Path,
};

pub struct UpdateLease {
    _file: File,
}
impl UpdateLease {
    pub fn acquire() -> Result<Self, String> {
        let base = if cfg!(target_os = "windows") {
            std::env::var_os("LOCALAPPDATA").map(std::path::PathBuf::from)
        } else if cfg!(target_os = "macos") {
            std::env::var_os("HOME").map(|p| std::path::PathBuf::from(p).join("Library/Caches"))
        } else {
            std::env::var_os("XDG_CACHE_HOME")
                .map(std::path::PathBuf::from)
                .filter(|p| p.is_absolute())
                .or_else(|| {
                    std::env::var_os("HOME").map(|p| std::path::PathBuf::from(p).join(".cache"))
                })
        }
        .ok_or("Cannot locate the current user's updater cache")?;
        if !base.is_absolute() {
            return Err("Updater cache must be an absolute user path".into());
        }
        let directory = base.join("THY666");
        fs::create_dir_all(&directory).map_err(|_| "Cannot create updater lease directory")?;
        Self::at(&directory.join("update.lock"))
    }
    fn at(path: &Path) -> Result<Self, String> {
        let file = File::options()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(path)
            .map_err(|_| "Cannot open updater process lease")?;
        file.try_lock()
            .map_err(|_| "Another THY666 updater owns the update lease; no device was opened")?;
        Ok(Self { _file: file })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn lease_prevents_parallel_owners_and_releases_on_drop() {
        let path = std::env::current_dir()
            .unwrap()
            .parent()
            .unwrap()
            .join("build")
            .join(format!("updater-lease-test-{}.lock", std::process::id()));
        let first = UpdateLease::at(&path).unwrap();
        assert!(UpdateLease::at(&path).is_err());
        drop(first);
        drop(UpdateLease::at(&path).unwrap());
        fs::remove_file(path).unwrap();
    }
}
