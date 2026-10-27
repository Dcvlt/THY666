//! Small, dependency-free WASM ABI around the native package/session policy.
//! It never imports USB, networking, clocks or operating-system capabilities.
use crate::{
    package,
    protocol::ProtocolError,
    session::{Action, Completion, Session},
};
use serde_json::{json, Value};

pub const ABI_VERSION: u32 = 1;
pub const METADATA_MAX: usize = 4096;
pub const INPUT_MAX: usize = 4 + METADATA_MAX + package::PACKAGE_BYTES_MAX;

#[derive(Default)]
pub struct BrowserCore {
    raw: Option<Vec<u8>>,
    expected_build: Option<String>,
    session: Option<Session>,
}

fn generic(message: &str) -> ProtocolError {
    ProtocolError {
        phase: "preflight",
        message: message.into(),
        mutation_started: false,
        indeterminate: false,
    }
}
fn failure(error: ProtocolError) -> Value {
    json!({"ok":false,"error":{"phase":error.phase,"message":error.message,
        "mutation_started":error.mutation_started,"indeterminate":error.indeterminate}})
}
pub fn info_json(info: &package::PackageInfo) -> Value {
    json!({"file_bytes":info.file_bytes,"file_sha256":info.file_sha256,
        "application_bytes":info.application_bytes,"application_sha256":info.application_sha256,
        "crc32":format!("{:08x}",info.crc32),"report_count":info.report_count,
        "initial_msp":(info.kind!="stock").then(||format!("{:08x}",info.initial_msp)),
        "reset_handler":(info.kind!="stock").then(||format!("{:08x}",info.reset_handler)),
        "build_id":info.build_id,"build_identity_verified":info.build_identity_verified,
        "kind":info.kind,"installable":info.installable,"preparation_required":true,"signed":false})
}

impl BrowserCore {
    pub fn active(&self) -> bool {
        self.session.as_ref().is_some_and(Session::active)
    }
    pub fn reset(&mut self) -> bool {
        if self.active() {
            return false;
        }
        *self = Self::default();
        true
    }
    pub fn request(&mut self, metadata: &Value, payload: &[u8], now: u64) -> Value {
        match self.handle(metadata, payload, now) {
            Ok(value) => value,
            Err(error) => {
                // A malformed completion cannot be repaired and replayed. The
                // corresponding host operation may already have reached USB.
                let error = if metadata.get("cmd").and_then(Value::as_str) == Some("feed") {
                    self.session
                        .as_mut()
                        .map_or(error.clone(), |session| session.abort(&error.message))
                } else {
                    error
                };
                failure(error)
            }
        }
    }
    fn handle(
        &mut self,
        metadata: &Value,
        payload: &[u8],
        now: u64,
    ) -> Result<Value, ProtocolError> {
        match metadata.get("cmd").and_then(Value::as_str) {
            Some("inspect") => {
                if self.active() {
                    return Err(self
                        .session
                        .as_ref()
                        .unwrap()
                        .error("An update is active; package inspection is blocked"));
                }
                // Invalidate the old package before every fallible parse.
                self.raw = None;
                self.expected_build = None;
                self.session = None;
                if payload.len() > package::PACKAGE_BYTES_MAX {
                    return Err(generic("Package exceeds the supported size"));
                }
                let first =
                    package::parse(payload, None).map_err(|error| generic(&error.to_string()))?;
                let provided = metadata
                    .get("expected_build")
                    .and_then(Value::as_str)
                    .filter(|value| !value.is_empty());
                let name = metadata.get("name").and_then(Value::as_str).unwrap_or("");
                if name.len() > 512 {
                    return Err(generic("Package filename is too long"));
                }
                let expected = provided.map(str::to_owned).or_else(|| {
                    if first.is_stock() {
                        None
                    } else {
                        package::infer_build(first.application(), name)
                    }
                });
                let inspected = package::parse(payload, expected.as_deref())
                    .map_err(|error| generic(&error.to_string()))?;
                let result = json!({"ok":true,"package":info_json(&inspected.info)});
                self.raw = Some(payload.to_vec());
                self.expected_build = expected;
                Ok(result)
            }
            Some("begin") => {
                if !payload.is_empty() {
                    return Err(generic("Session requests must not carry package bytes"));
                }
                if let Some(session) = &self.session {
                    return Err(session
                        .error("An update session already exists; do not start a replacement"));
                }
                let bytes = self
                    .raw
                    .as_ref()
                    .ok_or_else(|| generic("Inspect a supported package before beginning"))?;
                let pkg = package::parse(bytes, self.expected_build.as_deref())
                    .map_err(|error| generic(&error.to_string()))?;
                let uid = match metadata.get("expected_uid") {
                    None | Some(Value::Null) => None,
                    Some(Value::String(value)) if !value.is_empty() => Some(value.as_str()),
                    _ => {
                        return Err(generic(
                            "Expected UID must contain exactly 24 hexadecimal characters",
                        ))
                    }
                };
                let mut session = Session::new(&pkg, uid, now)?;
                let action = session.next(now)?;
                let result = action_json(&session, action);
                self.session = Some(session);
                Ok(result)
            }
            Some("feed") => {
                if !payload.is_empty() {
                    return Err(generic("Session requests must not carry package bytes"));
                }
                let session = self
                    .session
                    .as_mut()
                    .ok_or_else(|| generic("No update session exists"))?;
                let id = metadata
                    .get("id")
                    .and_then(Value::as_u64)
                    .and_then(|id| u32::try_from(id).ok())
                    .ok_or_else(|| session.error("Missing action acknowledgement identity"))?;
                let error = metadata
                    .get("error")
                    .and_then(Value::as_bool)
                    .unwrap_or(false);
                let complete = match metadata.get("kind").and_then(Value::as_str) {
                    Some("write") => Completion::Write(if error {
                        Err(())
                    } else {
                        Ok(metadata
                            .get("count")
                            .and_then(Value::as_u64)
                            .and_then(|count| usize::try_from(count).ok())
                            .ok_or_else(|| session.error("Invalid write acknowledgement"))?)
                    }),
                    Some("read") => Completion::Read(if error {
                        Err(())
                    } else {
                        let array = metadata
                            .get("data")
                            .and_then(Value::as_array)
                            .filter(|array| array.len() <= 64)
                            .ok_or_else(|| session.error("Invalid input report acknowledgement"))?;
                        let bytes: Result<Vec<u8>, _> = array
                            .iter()
                            .map(|value| {
                                value
                                    .as_u64()
                                    .and_then(|value| u8::try_from(value).ok())
                                    .ok_or_else(|| session.error("Invalid input report byte"))
                            })
                            .collect();
                        Ok(bytes?)
                    }),
                    Some("sleep") if !error => Completion::Sleep,
                    _ => return Err(session.error("Unsupported action acknowledgement")),
                };
                let action = session.complete(id, complete, now)?;
                // Keep ABI memory bounded over thousands of application reports.
                session.drain_progress();
                Ok(action_json(session, action))
            }
            _ => Err(generic("Unsupported updater core command")),
        }
    }
}

fn action_json(session: &Session, action: Action) -> Value {
    let action = match action {
        Action::Write { id, report } => json!({"kind":"write","id":id,"report":report.as_slice()}),
        Action::Read { id, timeout_ms } => json!({"kind":"read","id":id,"timeout_ms":timeout_ms}),
        Action::Sleep { id, ms } => json!({"kind":"sleep","id":id,"ms":ms}),
        Action::Done(value) => json!({"kind":"done","outcome":{
            "activation_writes":value.activation_writes,"warning":value.warning,
            "awaiting_application_verification":value.awaiting_application_verification,
            "transferred_crc":value.transferred_crc,"external_crc":value.external_crc,
            "decoded_crc":value.decoded_crc,"metadata_uid_verified":value.metadata_uid_verified,
            "target_uid":value.target_uid}}),
    };
    let progress = session.progress();
    json!({"ok":true,"action":action,"progress":{"phase":progress.phase,"completed":progress.completed,
        "total":progress.total,"message":progress.message,"state":progress.state}})
}

#[cfg(target_arch = "wasm32")]
mod abi {
    use super::*;
    use std::cell::RefCell;
    thread_local! {
        static INPUT: RefCell<Vec<u8>> = RefCell::new(vec![0; INPUT_MAX]);
        static OUTPUT: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
        static CORE: RefCell<BrowserCore> = RefCell::new(BrowserCore::default());
    }
    #[no_mangle]
    pub extern "C" fn thy_update_abi_version() -> u32 {
        ABI_VERSION
    }
    #[no_mangle]
    pub extern "C" fn thy_update_input_capacity() -> u32 {
        INPUT_MAX as u32
    }
    #[no_mangle]
    pub extern "C" fn thy_update_input_ptr() -> *mut u8 {
        INPUT.with(|buffer| buffer.borrow_mut().as_mut_ptr())
    }
    #[no_mangle]
    pub extern "C" fn thy_update_output_ptr() -> *const u8 {
        OUTPUT.with(|buffer| buffer.borrow().as_ptr())
    }
    #[no_mangle]
    pub extern "C" fn thy_update_reset() -> u32 {
        CORE.with(|core| u32::from(!core.borrow_mut().reset()))
    }
    #[no_mangle]
    pub extern "C" fn thy_update_request(length: u32, now_ms: f64) -> u32 {
        let result = INPUT.with(|buffer| {
            let buffer = buffer.borrow();
            let length = length as usize;
            if length < 4
                || length > INPUT_MAX
                || !now_ms.is_finite()
                || now_ms < 0.0
                || now_ms > 9_007_199_254_740_991.0
            {
                return failure(generic("Invalid bounded updater request"));
            }
            let json_length = u32::from_le_bytes(buffer[..4].try_into().unwrap()) as usize;
            if json_length == 0 || json_length > METADATA_MAX || json_length > length - 4 {
                return failure(generic("Invalid updater metadata length"));
            }
            let metadata: Value = match serde_json::from_slice(&buffer[4..4 + json_length]) {
                Ok(value) => value,
                Err(_) => return failure(generic("Invalid updater metadata")),
            };
            CORE.with(|core| {
                core.borrow_mut().request(
                    &metadata,
                    &buffer[4 + json_length..length],
                    now_ms.floor() as u64,
                )
            })
        });
        OUTPUT.with(|buffer| {
            let mut buffer = buffer.borrow_mut();
            *buffer = serde_json::to_vec(&result).unwrap_or_else(|_| b"{\"ok\":false,\"error\":{\"phase\":\"preflight\",\"message\":\"Core response serialization failed\",\"mutation_started\":false,\"indeterminate\":false}}".to_vec());
            buffer.len() as u32
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn image() -> Vec<u8> {
        let mut app = vec![0; 512];
        app[..4].copy_from_slice(&0x2002_0000u32.to_le_bytes());
        app[4..8].copy_from_slice(&0x0804_0191u32.to_le_bytes());
        app[400..416].copy_from_slice(b"deadbeef12345678");
        [
            format!(
                "THY333{:06X}{:06X}{:06X}{:06X}{:06X}{:08X}{:08X}{:08X}{:06X}{:06X}{:06X}",
                1,
                0,
                1,
                0,
                512,
                package::OBSERVED_METADATA[0],
                package::OBSERVED_METADATA[1],
                package::OBSERVED_METADATA[2],
                4,
                0,
                1
            )
            .into_bytes(),
            app,
        ]
        .concat()
    }
    #[test]
    fn inspection_binds_native_filename_policy_and_rejection_invalidates_it() {
        let mut core = BrowserCore::default();
        let checked = core.request(
            &json!({"cmd":"inspect","name":"THY666-instrument-alpha-deadbeef.333"}),
            &image(),
            0,
        );
        assert_eq!(checked["package"]["build_id"], "deadbeef12345678");
        assert_eq!(checked["package"]["installable"], true);
        assert_eq!(
            core.request(&json!({"cmd":"inspect"}), b"bad", 0)["ok"],
            false
        );
        assert_eq!(core.request(&json!({"cmd":"begin"}), &[], 0)["ok"], false);
    }
    #[test]
    fn active_session_blocks_replacement_reset_and_wrong_ack_is_terminal() {
        let mut core = BrowserCore::default();
        core.request(
            &json!({"cmd":"inspect","expected_build":"deadbeef12345678"}),
            &image(),
            0,
        );
        let first = core.request(&json!({"cmd":"begin"}), &[], 0);
        assert_eq!(first["action"]["kind"], "write");
        assert!(!core.reset());
        assert_eq!(core.request(&json!({"cmd":"begin"}), &[], 0)["ok"], false);
        assert_eq!(
            core.request(&json!({"cmd":"inspect"}), b"bad", 0)["ok"],
            false
        );
        let wrong = core.request(
            &json!({"cmd":"feed","kind":"write","id":2,"count":65}),
            &[],
            1,
        );
        assert_eq!(wrong["ok"], false);
        assert_eq!(wrong["error"]["mutation_started"], true);
        assert!(core.reset());
    }
    #[test]
    fn malformed_completion_is_terminal_even_before_transport_result_parsing() {
        let mut core = BrowserCore::default();
        core.request(
            &json!({"cmd":"inspect","expected_build":"deadbeef12345678"}),
            &image(),
            0,
        );
        core.request(&json!({"cmd":"begin"}), &[], 0);
        let bad = core.request(&json!({"cmd":"feed","id":1,"kind":"write"}), &[], 1);
        assert_eq!(bad["error"]["indeterminate"], true);
        let replay = core.request(
            &json!({"cmd":"feed","id":1,"kind":"write","count":65}),
            &[],
            2,
        );
        assert_eq!(replay["ok"], false);
        assert!(core.reset());
    }
}
