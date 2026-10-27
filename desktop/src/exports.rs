//! Collision-checked names for WebView-owned export downloads.
//!
//! Requested must not create or truncate a file. A random suffix prevents the
//! WebView from reusing the deterministic board/build filename, and a no-follow
//! metadata check rejects every existing entry. This is not an atomic no-replace
//! guarantee against a concurrent process racing the later WebView write.

use std::{
    fs, io,
    path::{Path, PathBuf},
};

const ATTEMPTS: usize = 8;
const EXTENSIONS: [&str; 6] = ["thyb", "thyl", "thys", "json", "mid", "midi"];

pub(crate) fn valid_export_name(destination: &Path) -> bool {
    let Some(name) = destination.file_name().and_then(|name| name.to_str()) else {
        return false;
    };
    !name.is_empty()
        && name.len() <= 200
        && !name
            .chars()
            .any(|character| character.is_control() || "<>:\"/\\|?*".contains(character))
        && destination.file_stem().is_some_and(|stem| !stem.is_empty())
        && destination
            .extension()
            .and_then(|extension| extension.to_str())
            .is_some_and(|extension| EXTENSIONS.contains(&extension.to_ascii_lowercase().as_str()))
}

pub(crate) fn unique_destination(destination: &Path) -> Result<PathBuf, &'static str> {
    unique_destination_with(
        destination,
        || {
            let mut nonce = [0u8; 16];
            getrandom::fill(&mut nonce)
                .map_err(|_| "Could not choose a private export filename")?;
            Ok(nonce)
        },
        |candidate| fs::symlink_metadata(candidate).map(|_| ()),
    )
}

fn unique_destination_with(
    destination: &Path,
    mut nonce: impl FnMut() -> Result<[u8; 16], &'static str>,
    mut inspect: impl FnMut(&Path) -> io::Result<()>,
) -> Result<PathBuf, &'static str> {
    if !valid_export_name(destination) {
        return Err("Invalid export filename");
    }
    let parent = destination.parent().ok_or("Invalid export directory")?;
    let stem = destination
        .file_stem()
        .and_then(|value| value.to_str())
        .ok_or("Invalid export filename")?;
    let extension = destination
        .extension()
        .and_then(|value| value.to_str())
        .ok_or("Invalid export filename")?;
    for _ in 0..ATTEMPTS {
        let suffix: String = nonce()?.iter().map(|byte| format!("{byte:02x}")).collect();
        let candidate = parent.join(format!("{stem}-{suffix}.{extension}"));
        match inspect(&candidate) {
            Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(candidate),
            Ok(()) => continue,
            Err(_) => return Err("Could not check the export destination"),
        }
    }
    Err("Could not choose an unused export filename")
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        sync::atomic::{AtomicUsize, Ordering},
        time::{SystemTime, UNIX_EPOCH},
    };

    static NEXT: AtomicUsize = AtomicUsize::new(0);
    struct Directory {
        path: PathBuf,
        base: PathBuf,
    }
    impl Directory {
        fn new() -> Self {
            let base = std::env::temp_dir().join("thy666-export-tests");
            fs::create_dir_all(&base).unwrap();
            let base = base.canonicalize().unwrap();
            let name = format!(
                "case-{}-{}-{}",
                std::process::id(),
                SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .unwrap()
                    .as_nanos(),
                NEXT.fetch_add(1, Ordering::Relaxed)
            );
            let path = base.join(name);
            fs::create_dir(&path).unwrap();
            Self { path, base }
        }
    }
    impl Drop for Directory {
        fn drop(&mut self) {
            // Remove only the verified per-test directory, never its parent.
            if let Ok(resolved) = self.path.canonicalize() {
                if resolved != self.base && resolved.starts_with(&self.base) {
                    let _ = fs::remove_dir_all(resolved);
                }
            }
        }
    }
    fn named(parent: &Path, byte: u8, extension: &str) -> PathBuf {
        parent.join(format!(
            "board-build-{}.{extension}",
            format!("{byte:02x}").repeat(16)
        ))
    }
    fn inspect(path: &Path) -> io::Result<()> {
        fs::symlink_metadata(path).map(|_| ())
    }

    #[test]
    fn suffix_preserves_parent_extension_and_existing_original_without_creating_files() {
        let directory = Directory::new();
        let source = directory.path.join("board-build.THYB");
        fs::write(&source, b"earlier backup").unwrap();
        let result = unique_destination_with(&source, || Ok([0xab; 16]), inspect).unwrap();
        assert_eq!(result, named(&directory.path, 0xab, "THYB"));
        assert_eq!(result.parent(), source.parent());
        assert_eq!(fs::read(&source).unwrap(), b"earlier backup");
        assert_eq!(fs::read_dir(&directory.path).unwrap().count(), 1);
        assert!(!result.exists());
    }

    #[test]
    fn colliding_file_and_directory_are_preserved_and_a_new_nonce_is_used() {
        let directory = Directory::new();
        let file = named(&directory.path, 0, "thyb");
        let folder = named(&directory.path, 1, "thyb");
        fs::write(&file, b"keep this export").unwrap();
        fs::create_dir(&folder).unwrap();
        let mut next = 0;
        let result = unique_destination_with(
            &directory.path.join("board-build.thyb"),
            || {
                let value = next;
                next += 1;
                Ok([value; 16])
            },
            inspect,
        )
        .unwrap();
        assert_eq!(next, 3);
        assert_eq!(result, named(&directory.path, 2, "thyb"));
        assert_eq!(fs::read(file).unwrap(), b"keep this export");
        assert!(folder.is_dir());
        assert_eq!(fs::read_dir(&directory.path).unwrap().count(), 2);
    }

    #[test]
    fn collisions_are_bounded_and_do_not_truncate_an_existing_export() {
        let directory = Directory::new();
        let file = named(&directory.path, 0, "thyb");
        fs::write(&file, b"unchanged").unwrap();
        let mut attempts = 0;
        assert!(unique_destination_with(
            &directory.path.join("board-build.thyb"),
            || {
                attempts += 1;
                Ok([0; 16])
            },
            inspect
        )
        .is_err());
        assert_eq!(attempts, ATTEMPTS);
        assert_eq!(fs::read(file).unwrap(), b"unchanged");
    }

    #[test]
    fn inspection_errors_and_random_failures_refuse_the_download() {
        for kind in [
            io::ErrorKind::PermissionDenied,
            io::ErrorKind::InvalidInput,
            io::ErrorKind::Other,
        ] {
            assert_eq!(
                unique_destination_with(
                    Path::new("board-build.thyb"),
                    || Ok([0; 16]),
                    |_| Err(io::Error::from(kind))
                ),
                Err("Could not check the export destination")
            );
        }
        let mut inspections = 0;
        assert_eq!(
            unique_destination_with(
                Path::new("board-build.thyb"),
                || Err("random unavailable"),
                |_| {
                    inspections += 1;
                    Err(io::Error::from(io::ErrorKind::NotFound))
                }
            ),
            Err("random unavailable")
        );
        assert_eq!(inspections, 0);
    }

    #[test]
    fn invalid_names_are_rejected_before_randomness_or_filesystem_access() {
        for name in [
            "",
            ".",
            "..",
            "backup",
            ".thyb",
            "backup.exe",
            "backup:stream.thyb",
            "backup\n.thyb",
            "backup?.thyb",
        ] {
            assert!(unique_destination_with(
                Path::new(name),
                || panic!("invalid name requested randomness"),
                |_| panic!("invalid name inspected disk")
            )
            .is_err());
        }
        assert!(!valid_export_name(Path::new(&format!(
            "{}.thyb",
            "x".repeat(201)
        ))));
        for extension in EXTENSIONS {
            assert!(valid_export_name(Path::new(&format!("backup.{extension}"))));
        }
    }

    #[cfg(unix)]
    #[test]
    fn dangling_symlinks_are_existing_targets_even_when_their_referent_is_missing() {
        let directory = Directory::new();
        let link = named(&directory.path, 0, "thyb");
        std::os::unix::fs::symlink(directory.path.join("missing"), &link).unwrap();
        assert!(!link.exists());
        let mut next = 0;
        let result = unique_destination_with(
            &directory.path.join("board-build.thyb"),
            || {
                let value = next;
                next += 1;
                Ok([value; 16])
            },
            inspect,
        )
        .unwrap();
        assert_eq!(result, named(&directory.path, 1, "thyb"));
        assert!(fs::symlink_metadata(link).unwrap().file_type().is_symlink());
    }
}
