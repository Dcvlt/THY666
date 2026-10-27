//! One staged package and one native update owner. No automatic device discovery.
use crate::{
    catalog::Catalog,
    native,
    package::{self, PackageInfo},
    protocol, simulation,
};
use serde_json::{json, Value};
use std::{
    collections::BTreeSet,
    path::Path,
    sync::{Arc, Mutex},
    thread,
};

pub const PROTOCOL: &str = "THY666-UPDATER/1";

#[derive(Clone)]
struct Image {
    bytes: Arc<Vec<u8>>,
    info: PackageInfo,
}

#[derive(Clone, Debug)]
pub struct Operation {
    pub operation_id: String,
    pub sequence: u64,
    pub state: String,
    pub phase: String,
    pub completed: usize,
    pub total: usize,
    pub message: String,
    pub expected_build: Option<String>,
    pub firmware_kind: String,
    pub expected_uid: Option<String>,
    pub target_uid: Option<String>,
    pub simulation: bool,
    pub mutation_started: bool,
    pub indeterminate: bool,
}
impl Operation {
    pub fn json(&self) -> Value {
        json!({"operation_id":self.operation_id,"sequence":self.sequence,
        "state":self.state,"phase":self.phase,"completed":self.completed,"total":self.total,
        "message":self.message,"expected_build":self.expected_build,"firmware_kind":self.firmware_kind,"expected_uid":self.expected_uid,
        "target_uid":self.target_uid,"simulation":self.simulation,"mutation_started":self.mutation_started,
        "indeterminate":self.indeterminate})
    }
}

#[derive(Default)]
struct State {
    image: Option<Image>,
    devices: BTreeSet<String>,
    operation: Option<Operation>,
    last_request: String,
    used_ids: BTreeSet<String>,
}

#[derive(Clone)]
pub struct Service {
    inner: Arc<Mutex<State>>,
    pub simulation: bool,
    catalog: Option<Arc<Catalog>>,
}
impl Service {
    pub fn new(simulation: bool) -> Self {
        Self {
            inner: Arc::new(Mutex::new(State::default())),
            simulation,
            catalog: None,
        }
    }
    pub fn with_catalog(mut self, path: impl AsRef<Path>) -> Result<Self, String> {
        self.catalog = Some(Arc::new(Catalog::load(path.as_ref())?));
        Ok(self)
    }
    /// Catalog inspection is read-only and remains usable while a worker runs.
    pub fn releases(&self) -> Value {
        json!({"protocol":PROTOCOL,"configured":self.catalog.is_some(),
            "releases":self.catalog.as_ref().map(|catalog|catalog.list()).unwrap_or_default(),"simulation":self.simulation})
    }
    pub fn select_release(&self, id: &str) -> Result<Value, String> {
        let mut state = self.inner.lock().unwrap();
        if Self::busy(&state) {
            return Err("An update owns the helper; release selection is unavailable".into());
        }
        // Failed selection must never leave the previously staged file usable.
        state.image = None;
        let selected = self
            .catalog
            .as_ref()
            .ok_or("No curated release catalog is configured")?
            .select(id)?;
        let result = info_json(&selected.info);
        state.image = Some(Image {
            bytes: Arc::new(selected.bytes),
            info: selected.info,
        });
        Ok(
            json!({"protocol":PROTOCOL,"package":result,"release_id":selected.id,"simulation":self.simulation}),
        )
    }
    fn busy(state: &State) -> bool {
        state
            .operation
            .as_ref()
            .is_some_and(|op| op.state == "running")
    }
    pub fn status(&self) -> Value {
        let state = self.inner.lock().unwrap();
        json!({"protocol":PROTOCOL,"version":env!("CARGO_PKG_VERSION"),"simulation":self.simulation,
            "mode":if self.simulation {"simulation"} else {"native"},
            "operation":state.operation.as_ref().map(Operation::json)})
    }
    pub fn inspect(
        &self,
        bytes: Vec<u8>,
        name: &str,
        expected: Option<&str>,
    ) -> Result<Value, String> {
        let mut state = self.inner.lock().unwrap();
        if Self::busy(&state) {
            return Err("An update owns the helper; inspection is unavailable".into());
        }
        // Invalidate the old file before any fallible validation, so a rejected
        // replacement cannot accidentally leave the previous package installable.
        state.image = None;
        let unbound = package::parse(&bytes, None).map_err(|e| e.to_string())?;
        let inferred = infer_build(unbound.application(), name);
        let expected = expected
            .filter(|value| !value.is_empty())
            .or(inferred.as_deref());
        let parsed = package::parse(&bytes, expected).map_err(|e| e.to_string())?;
        let info = parsed.info.clone();
        let result = info_json(&info);
        state.image = Some(Image {
            bytes: Arc::new(bytes),
            info,
        });
        Ok(json!({"protocol":PROTOCOL,"package":result,"simulation":self.simulation}))
    }
    pub fn devices(&self) -> Result<Value, String> {
        let mut state = self.inner.lock().unwrap();
        if Self::busy(&state) {
            return Err("An update owns the USB interface".into());
        }
        let devices = if self.simulation {
            vec![
                json!({"id":"simulation","label":"SIMULATION - no physical board","mode":"bootloader","simulation":true}),
            ]
        } else {
            native::enumerate()?
                .into_iter()
                .map(|d| json!({"id":d.id,"label":d.label,"mode":"bootloader","simulation":false}))
                .collect()
        };
        state.devices = devices
            .iter()
            .filter_map(|d| d["id"].as_str().map(str::to_owned))
            .collect();
        Ok(json!({"protocol":PROTOCOL,"devices":devices,"simulation":self.simulation}))
    }
    pub fn install(&self, request: &Value) -> Result<Value, String> {
        let id = required(request, "operation_id")?;
        if !(16..=64).contains(&id.len())
            || !id
                .bytes()
                .all(|c| c.is_ascii_alphanumeric() || b"_-".contains(&c))
        {
            return Err("Invalid operation identifier".into());
        }
        let device_id = required(request, "device_id")?;
        let digest = required(request, "package_sha256")?;
        for flag in [
            "confirmed",
            "preparation_acknowledged",
            "physical_target_confirmed",
        ] {
            if request[flag].as_bool() != Some(true) {
                return Err(
                    "Explicit installation and memory-preparation confirmation is required".into(),
                );
            }
        }
        let disposition = required(request, "backup_disposition")?;
        if !["downloaded", "unavailable"].contains(&disposition) {
            return Err("Choose an explicit backup disposition".into());
        }
        let expected_uid = match request.get("expected_uid") {
            None | Some(Value::Null) => None,
            Some(Value::String(uid)) if !uid.is_empty() => Some(uid.to_ascii_lowercase()),
            _ => return Err("Expected board UID must be a 24-digit hexadecimal string".into()),
        };
        if expected_uid
            .as_ref()
            .is_some_and(|s| s.len() != 24 || !s.bytes().all(|c| c.is_ascii_hexdigit()))
            || (disposition == "downloaded" && expected_uid.is_none())
        {
            return Err("A saved backup requires its valid board UID".into());
        }
        let fingerprint = package::sha256(
            &serde_json::to_vec(request).map_err(|_| "Invalid installation request")?,
        );
        let mut state = self.inner.lock().unwrap();
        if let Some(op) = &state.operation {
            if op.operation_id == id {
                if state.last_request != fingerprint {
                    return Err("Operation ID already belongs to a different request".into());
                }
                return Ok(json!({"protocol":PROTOCOL,"operation":op.json()}));
            }
        }
        if Self::busy(&state) {
            return Err("Another update already owns the USB interface".into());
        }
        if state.used_ids.contains(id) {
            return Err("A previous operation ID cannot be replayed".into());
        }
        if state.used_ids.len() >= 64 {
            return Err("Session operation limit reached; restart the idle helper".into());
        }
        if !state.devices.contains(device_id) {
            return Err("Explicitly discover and select the update-mode board first".into());
        }
        let image = state.image.clone().ok_or("Inspect a valid package first")?;
        if image.info.file_sha256 != digest || !image.info.installable {
            return Err(
                "Package identity changed or this exact firmware is not approved for transfer"
                    .into(),
            );
        }
        let op = Operation {
            operation_id: id.into(),
            sequence: 1,
            state: "running".into(),
            phase: "opening".into(),
            completed: 0,
            total: image.info.application_bytes,
            message: if self.simulation {
                "SIMULATION: no device will be opened".into()
            } else {
                "Opening the explicitly selected update interface".into()
            },
            expected_build: image.info.build_id.clone(),
            firmware_kind: image.info.kind.into(),
            expected_uid: expected_uid.clone(),
            target_uid: None,
            simulation: self.simulation,
            mutation_started: false,
            indeterminate: false,
        };
        state.operation = Some(op.clone());
        state.last_request = fingerprint;
        state.used_ids.insert(id.into());
        // Consume the staged selection. Every later attempt, including one
        // after an uncertain failure, needs fresh inspection and discovery.
        // Repeating this exact operation ID only returns its existing status.
        state.image = None;
        state.devices.clear();
        let manager = self.clone();
        let device_id = device_id.to_owned();
        let operation_id = id.to_owned();
        let task = thread::Builder::new()
            .name("thy666-update".into())
            .spawn(move || manager.run(operation_id, image, device_id, expected_uid));
        if task.is_err() {
            let current = state.operation.as_mut().unwrap();
            current.sequence += 1;
            current.state = "rejected".into();
            current.message = "Could not start the update worker; no device was opened".into();
        }
        Ok(json!({"protocol":PROTOCOL,"operation":state.operation.as_ref().unwrap().json()}))
    }
    fn run(&self, id: String, image: Image, device_id: String, expected_uid: Option<String>) {
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            let package = package::parse(&image.bytes, image.info.build_id.as_deref())
                .map_err(|_| "The staged package failed revalidation".to_owned())?;
            let lease = if self.simulation {
                None
            } else {
                Some(crate::lease::UpdateLease::acquire()?)
            };
            let progress = |p: protocol::Progress| {
                self.update(&id, |op| {
                    op.sequence += 1;
                    op.state = "running".into();
                    op.phase = p.phase.into();
                    op.completed = p.completed;
                    op.total = p.total;
                    op.message = p.message;
                    // Once preparation begins, a later host failure cannot promise no change.
                    if op.phase != "preflight" {
                        op.mutation_started = true;
                    }
                })
            };
            let outcome = if self.simulation {
                let mut transport = simulation::SimulatedTransport::new(&package);
                protocol::runpkg(&package, &mut transport, expected_uid.as_deref(), progress)
            } else {
                let mut transport = native::open_selected(&device_id)?;
                protocol::runpkg(&package, &mut transport, expected_uid.as_deref(), progress)
            };
            // Terminal status means both native handle and advisory process
            // ownership are released; the browser may now reconnect CDC.
            drop(lease);
            match outcome {
                Ok(outcome) => {
                    self.update(&id,|op|{
                    op.sequence+=1;op.state="awaiting-verification".into();op.phase="application-verification".into();
                    op.completed=image.info.application_bytes;op.total=image.info.application_bytes;
                    op.target_uid=Some(outcome.target_uid.to_ascii_lowercase());op.mutation_started=true;
                    op.message=if self.simulation {"SIMULATION complete. No physical update or application verification occurred".into()}
                    else {outcome.warning.unwrap_or_else(||"Transfer CRCs match; reconnect the application explicitly to check its build and identity".into())};
                });
                    Ok(())
                }
                Err(error) => {
                    self.update(&id, |op| {
                        op.sequence += 1;
                        op.state = "failed".into();
                        op.phase = error.phase.into();
                        op.message = error.message;
                        op.mutation_started = error.mutation_started;
                        op.indeterminate = error.indeterminate;
                    });
                    Ok(())
                }
            }
        }));
        let failure = match result {
            Ok(Err(message)) => Some(message),
            Err(_) => Some(
                "Update worker stopped unexpectedly; inspect the board before any new attempt"
                    .into(),
            ),
            _ => None,
        };
        if let Some(message) = failure {
            self.update(&id, |op| {
                op.sequence += 1;
                op.state = "failed".into();
                op.message = message;
                op.indeterminate = op.mutation_started;
            });
        }
    }
    fn update(&self, id: &str, change: impl FnOnce(&mut Operation)) {
        if let Ok(mut state) = self.inner.lock() {
            if let Some(op) = state.operation.as_mut().filter(|op| op.operation_id == id) {
                change(op);
            }
        }
    }
    pub fn progress(&self, id: &str) -> Result<Value, String> {
        let state = self.inner.lock().unwrap();
        let op = state
            .operation
            .as_ref()
            .filter(|op| op.operation_id == id)
            .ok_or("Unknown operation; do not start a replacement automatically")?;
        Ok(json!({"protocol":PROTOCOL,"operation":op.json()}))
    }
}

pub fn info_json(info: &PackageInfo) -> Value {
    json!({"file_bytes":info.file_bytes,"file_sha256":info.file_sha256,
    "application_bytes":info.application_bytes,"application_sha256":info.application_sha256,"crc32":format!("{:08x}",info.crc32),
    "report_count":info.report_count,"initial_msp":(info.kind!="stock").then(||format!("{:08x}",info.initial_msp)),"reset_handler":(info.kind!="stock").then(||format!("{:08x}",info.reset_handler)),
    "build_id":info.build_id,"build_identity_verified":info.build_identity_verified,"kind":info.kind,"installable":info.installable,"preparation_required":true,"signed":false})
}

fn required<'a>(v: &'a Value, name: &str) -> Result<&'a str, String> {
    v.get(name)
        .and_then(Value::as_str)
        .filter(|s| !s.is_empty())
        .ok_or_else(|| format!("Missing {name}"))
}

/// A standard filename supplies only a prefix. Bind it only when exactly one
/// complete NUL-terminated build literal matches; this is not authentication.
pub use crate::package::infer_build;

#[cfg(test)]
mod tests {
    use super::*;
    const BUILD: &str = "1234567890abcdef";
    fn image() -> Vec<u8> {
        let fields = [
            (6, 1u32),
            (6, 0),
            (6, 1),
            (6, 0),
            (6, 512),
            (8, 0xebf3a7f7),
            (8, 0x429b51d6),
            (8, 0xaa017d8c),
            (6, 4),
            (6, 0),
            (6, 1),
        ];
        let mut raw = b"THY333".to_vec();
        for (width, value) in fields {
            raw.extend_from_slice(format!("{value:0width$X}").as_bytes());
        }
        let mut app = vec![0u8; 512];
        app[..4].copy_from_slice(&0x20020000u32.to_le_bytes());
        app[4..8].copy_from_slice(&(package::APPLICATION_BASE + 393).to_le_bytes());
        app[416..432].copy_from_slice(BUILD.as_bytes());
        raw.extend(app);
        raw
    }
    fn ready() -> Service {
        let s = Service::new(true);
        s.inspect(image(), "THY666-instrument-alpha-12345678.333", None)
            .unwrap();
        s.devices().unwrap();
        s
    }
    fn request(s: &Service) -> Value {
        let hash = s
            .inner
            .lock()
            .unwrap()
            .image
            .as_ref()
            .unwrap()
            .info
            .file_sha256
            .clone();
        json!({"operation_id":"0123456789abcdef0123456789abcdef","device_id":"simulation","package_sha256":hash,
            "confirmed":true,"preparation_acknowledged":true,"physical_target_confirmed":true,"backup_disposition":"unavailable"})
    }
    fn finish(s: &Service) -> Value {
        for _ in 0..200 {
            let v = s.progress("0123456789abcdef0123456789abcdef").unwrap();
            if v["operation"]["state"] != "running" {
                return v;
            }
            thread::sleep(std::time::Duration::from_millis(5));
        }
        panic!("Simulation worker did not finish");
    }
    #[test]
    fn filename_binding_does_not_invent_ambiguous_or_unrelated_builds() {
        let raw = image();
        assert_eq!(
            infer_build(&raw[78..], "THY666-instrument-alpha-12345678.333").as_deref(),
            Some(BUILD)
        );
        assert!(infer_build(&raw[78..], "renamed.333").is_none());
        let mut app = raw[78..].to_vec();
        app.extend_from_slice(b"12345678abcdef90\0");
        assert!(infer_build(&app, "THY666-instrument-alpha-12345678.333").is_none());
    }
    #[test]
    fn accepted_operation_is_idempotent_but_changed_or_replayed_identity_is_not() {
        let s = ready();
        let req = request(&s);
        s.install(&req).unwrap();
        s.install(&req).unwrap();
        let final_state = finish(&s);
        assert_eq!(final_state["operation"]["state"], "awaiting-verification");
        assert_eq!(final_state["operation"]["target_uid"], simulation::UID);
        assert_eq!(final_state["operation"]["simulation"], true);
        assert_eq!(s.inner.lock().unwrap().used_ids.len(), 1);
        let mut changed = req.clone();
        changed["expected_uid"] = simulation::UID.into();
        assert!(s.install(&changed).is_err());
        assert_eq!(
            s.install(&req).unwrap()["operation"]["sequence"],
            final_state["operation"]["sequence"]
        );
    }
    #[test]
    fn rejected_file_clears_previous_staging_and_never_falls_back_to_it() {
        let s = ready();
        let req = request(&s);
        assert!(s.inspect(vec![0; 78], "bad.333", None).is_err());
        assert!(s.install(&req).is_err());
        assert!(s.status()["operation"].is_null());
    }
    #[test]
    fn preparation_identity_mismatch_remains_potentially_mutating() {
        let s = ready();
        let mut req = request(&s);
        req["expected_uid"] = "fedcba9876543210fedcba98".into();
        req["backup_disposition"] = "downloaded".into();
        s.install(&req).unwrap();
        let state = finish(&s);
        assert_eq!(state["operation"]["state"], "failed");
        assert_eq!(state["operation"]["mutation_started"], true);
        assert_eq!(state["operation"]["indeterminate"], true);
    }
    #[test]
    fn explicit_confirmation_and_discovery_are_required_before_worker_creation() {
        let s = ready();
        for flag in [
            "confirmed",
            "preparation_acknowledged",
            "physical_target_confirmed",
        ] {
            let mut req = request(&s);
            req[flag] = false.into();
            assert!(s.install(&req).is_err());
        }
        let req = request(&s);
        s.inner.lock().unwrap().devices.clear();
        assert!(s.install(&req).is_err());
        assert!(s.status()["operation"].is_null());
        assert!(s.progress("unknown").is_err());
    }
    #[test]
    fn release_selection_is_explicit_revalidated_and_invalidates_stale_staging() {
        let fixture = crate::catalog::tests::Fixture::new();
        let entry = fixture.entry();
        fixture.write(entry);
        let s = Service::new(true).with_catalog(&fixture.manifest).unwrap();
        assert_eq!(s.releases()["configured"], true);
        assert!(s.inner.lock().unwrap().image.is_none());
        let selected = s.select_release("current").unwrap();
        assert_eq!(selected["release_id"], "current");
        assert!(s.inner.lock().unwrap().image.is_some());
        assert!(s.select_release("unknown").is_err());
        assert!(s.inner.lock().unwrap().image.is_none());
        s.select_release("current").unwrap();
        std::fs::write(fixture.directory.join("synthetic.333"), b"changed").unwrap();
        assert!(s.select_release("current").is_err());
        assert!(s.inner.lock().unwrap().image.is_none());
        assert_eq!(Service::new(true).releases()["configured"], false);
    }
    #[test]
    fn release_listing_during_operation_is_read_only_but_selection_is_blocked() {
        let fixture = crate::catalog::tests::Fixture::new();
        let entry = fixture.entry();
        fixture.write(entry);
        let s = Service::new(true).with_catalog(&fixture.manifest).unwrap();
        s.select_release("current").unwrap();
        let mut state = s.inner.lock().unwrap();
        state.operation = Some(Operation {
            operation_id: "synthetic-operation".into(),
            sequence: 1,
            state: "running".into(),
            phase: "testing".into(),
            completed: 0,
            total: 512,
            message: "synthetic".into(),
            expected_build: Some(BUILD.into()),
            firmware_kind: "thy666".into(),
            expected_uid: None,
            target_uid: None,
            simulation: true,
            mutation_started: false,
            indeterminate: false,
        });
        drop(state);
        assert_eq!(s.releases()["releases"][0]["available"], true);
        assert!(s.select_release("current").is_err());
        assert!(s.inner.lock().unwrap().image.is_some());
    }
}
