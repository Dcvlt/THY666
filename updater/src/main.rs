//! Standalone native executable. Only the explicitly confirmed install command
//! or authenticated bridge installation opens a device for update traffic.
use serde_json::json;
use std::{collections::BTreeMap, fs, path::Path, thread, time::Duration};
use thy666_updater::{backup, bridge, native, package, service::Service};

fn help() {
    println!("THY666 updater {}\n\nDouble-click/no arguments starts the helper and opens the local Companion.\nThe Companion server must already be available; hardware discovery stays explicit.\n\nCommands:\n  inspect PACKAGE.333 [--expected-build FULL16HEX]\n  backup-info FILE.thyb\n  devices\n  simulate PACKAGE.333 [--expected-build FULL16HEX]\n  install PACKAGE.333 --device ID [--expected-build FULL16HEX]\n      (--backup FILE.thyb | --without-backup)\n      --confirm-install --acknowledge-preparation --confirm-physical-board\n  serve [--port 3331] [--origin http://127.0.0.1:3330] [--simulate]\n      [--releases PATH] [--open]\n\ninspect, backup-info and simulate never open USB. devices enumerates HID only.\nserve starts a loopback helper without discovering/opening hardware.\nThe optional release catalog is explicit; a sibling releases.json is the default.\n\nInstallation prepares bootloader memory before UID/hardware verification;\nexternal contents may change. There is no raw-flash backup or automatic rollback.\nCompleted transfer still requires explicit application identity/build verification.",env!("CARGO_PKG_VERSION"));
}

struct Args {
    positionals: Vec<String>,
    options: BTreeMap<String, String>,
}
impl Args {
    fn parse(raw: &[String]) -> Result<Self, String> {
        let flags = [
            "without-backup",
            "confirm-install",
            "acknowledge-preparation",
            "confirm-physical-board",
            "simulate",
            "open",
        ];
        let values = [
            "expected-build",
            "device",
            "backup",
            "port",
            "origin",
            "releases",
        ];
        let mut positionals = Vec::new();
        let mut options = BTreeMap::new();
        let mut at = 0;
        while at < raw.len() {
            if let Some(key) = raw[at].strip_prefix("--") {
                let value = if flags.contains(&key) {
                    "true".into()
                } else if values.contains(&key) {
                    at += 1;
                    raw.get(at)
                        .filter(|v| !v.starts_with("--"))
                        .ok_or_else(|| format!("Missing --{key} value"))?
                        .clone()
                } else {
                    return Err(format!("Unknown option --{key}"));
                };
                if options.insert(key.into(), value).is_some() {
                    return Err(format!("Repeated --{key} option"));
                }
            } else {
                positionals.push(raw[at].clone());
            }
            at += 1;
        }
        Ok(Self {
            positionals,
            options,
        })
    }
    fn get(&self, key: &str) -> Option<&str> {
        self.options.get(key).map(String::as_str)
    }
    fn flag(&self, key: &str) -> bool {
        self.get(key) == Some("true")
    }
    fn only(&self, allowed: &[&str], count: usize) -> Result<(), String> {
        if self.positionals.len() != count
            || self
                .options
                .keys()
                .any(|key| !allowed.contains(&key.as_str()))
        {
            return Err("Options or argument count do not match this command; use --help".into());
        }
        Ok(())
    }
}
fn read_bounded(path: &str, max: u64) -> Result<Vec<u8>, String> {
    let metadata = fs::metadata(path).map_err(|_| "Could not read the selected file")?;
    if !metadata.is_file() || metadata.len() > max {
        return Err("Selected file exceeds the format limit or is not a regular file".into());
    }
    // Bound the actual read too, so changing a file between metadata and read
    // cannot force an unbounded allocation.
    use std::io::Read;
    let mut file = fs::File::open(path)
        .map_err(|_| "Could not open the selected file")?
        .take(max + 1);
    let mut bytes = Vec::new();
    file.read_to_end(&mut bytes)
        .map_err(|_| "Selected file read failed")?;
    if bytes.len() as u64 > max {
        return Err("Selected file grew beyond the format limit".into());
    }
    Ok(bytes)
}
fn random_hex() -> Result<String, String> {
    let mut bytes = [0; 32];
    getrandom::fill(&mut bytes).map_err(|_| "Operating-system randomness is unavailable")?;
    Ok(bytes.iter().map(|byte| format!("{byte:02x}")).collect())
}
fn execute(raw: &[String]) -> Result<(), String> {
    if raw.is_empty() {
        return execute(&["serve".into(), "--open".into()]);
    }
    if matches!(raw[0].as_str(), "--help" | "-h" | "help") {
        help();
        return Ok(());
    }
    if raw[0] == "--version" {
        println!("{}", env!("CARGO_PKG_VERSION"));
        return Ok(());
    }
    let args = Args::parse(&raw[1..])?;
    match raw[0].as_str() {
        "inspect" | "simulate" | "install" => {
            let command = &raw[0];
            if command == "inspect" || command == "simulate" {
                args.only(&["expected-build"], 1)?;
            } else {
                args.only(
                    &[
                        "device",
                        "expected-build",
                        "backup",
                        "without-backup",
                        "confirm-install",
                        "acknowledge-preparation",
                        "confirm-physical-board",
                    ],
                    1,
                )?;
            }
            let bytes = read_bounded(&args.positionals[0], package::PACKAGE_BYTES_MAX as u64)?;
            let name = Path::new(&args.positionals[0])
                .file_name()
                .and_then(|s| s.to_str())
                .ok_or("Expected a package filename")?;
            let service = Service::new(command == "simulate");
            let inspected = service.inspect(bytes, name, args.get("expected-build"))?;
            if command == "inspect" {
                println!("{}", serde_json::to_string_pretty(&inspected).unwrap());
                return Ok(());
            }
            let expected_uid = if let Some(file) = args.get("backup") {
                if args.flag("without-backup") {
                    return Err("Choose --backup or --without-backup, not both".into());
                }
                Some(
                    backup::inspect(&read_bounded(file, 13_889)?)
                        .map_err(|e| e.to_string())?
                        .uid,
                )
            } else {
                None
            };
            if command == "install"
                && (!args.flag("confirm-install")
                    || !args.flag("acknowledge-preparation")
                    || !args.flag("confirm-physical-board")
                    || (expected_uid.is_none() && !args.flag("without-backup")))
            {
                return Err("Installation needs the three explicit confirmations and a backup disposition; use --help".into());
            }
            if !inspected["package"]["installable"]
                .as_bool()
                .unwrap_or(false)
            {
                return Err("Firmware needs a bound THY666 build or exact supported stock identity before transfer".into());
            }
            let devices = service.devices()?;
            let device_id = if command == "simulate" {
                "simulation"
            } else {
                args.get("device")
                    .ok_or("Select the exact device ID from the devices command")?
            };
            if !devices["devices"]
                .as_array()
                .unwrap()
                .iter()
                .any(|d| d["id"] == device_id)
            {
                return Err(
                    "Selected update-mode interface is unavailable; no installation started".into(),
                );
            }
            let operation_id = random_hex()?;
            let mut request = json!({"operation_id":operation_id,"device_id":device_id,"package_sha256":inspected["package"]["file_sha256"],
                "confirmed":true,"preparation_acknowledged":true,"physical_target_confirmed":true,
                "backup_disposition":if expected_uid.is_some(){"downloaded"}else{"unavailable"}});
            if let Some(uid) = expected_uid {
                request["expected_uid"] = uid.into();
            }
            let mut current = service.install(&request)?;
            let mut sequence = 0;
            loop {
                let op = &current["operation"];
                let next = op["sequence"].as_u64().unwrap_or(0);
                if next != sequence {
                    println!("{}", op);
                    sequence = next;
                }
                if op["state"] != "running" {
                    if op["state"] == "failed" || op["state"] == "rejected" {
                        return Err("Operation stopped. Do not retry automatically; inspect its reported phase and device state".into());
                    }
                    return Ok(());
                }
                thread::sleep(Duration::from_millis(200));
                current = service.progress(&operation_id)?;
            }
        }
        "backup-info" => {
            args.only(&[], 1)?;
            let info = backup::inspect(&read_bounded(&args.positionals[0], 13_889)?)
                .map_err(|e| e.to_string())?;
            println!(
                "{}",
                json!({"format":"THYB/1","integrity_checked":true,"payload_bytes":info.payload_bytes,
                "payload_crc":format!("{:08x}",info.payload_crc),"source_build":info.build,
                "uid_redacted":true,"raw_flash_backup":false,"payload_semantics_verified":false})
            );
            Ok(())
        }
        "devices" => {
            args.only(&[], 0)?;
            let devices = native::enumerate()?;
            println!(
                "{}",
                json!({"devices":devices.into_iter().map(|d|json!({"id":d.id,"label":d.label,"mode":"bootloader"})).collect::<Vec<_>>()})
            );
            Ok(())
        }
        "serve" => {
            args.only(&["origin", "port", "simulate", "releases", "open"], 0)?;
            let origin = args
                .get("origin")
                .unwrap_or("http://127.0.0.1:3330")
                .to_owned();
            if !bridge::valid_origin(&origin) {
                return Err(
                    "Companion origin must be an exact loopback HTTP origin with a port".into(),
                );
            }
            let port = args
                .get("port")
                .unwrap_or("3331")
                .parse::<u16>()
                .map_err(|_| "Invalid helper port")?;
            let mut service = Service::new(args.flag("simulate"));
            if let Some(path) = args.get("releases") {
                service = service.with_catalog(Path::new(path))?;
            } else {
                let sibling = std::env::current_exe()
                    .ok()
                    .and_then(|exe| exe.parent().map(|parent| parent.join("releases.json")));
                if let Some(path) = sibling.filter(|path| {
                    std::fs::symlink_metadata(path)
                        .is_ok_and(|metadata| metadata.file_type().is_file())
                }) {
                    service = service.with_catalog(path)?;
                }
            }
            bridge::serve_with_browser(
                bridge::Config {
                    origin,
                    port,
                    token: random_hex()?,
                },
                service,
                args.flag("open"),
            )
        }
        _ => Err("Unknown command; use --help".into()),
    }
}
fn main() {
    if let Err(error) = execute(&std::env::args().skip(1).collect::<Vec<_>>()) {
        eprintln!("Updater: {error}");
        std::process::exit(1);
    }
}
