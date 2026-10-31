//! Native CDC bytes only. Musical state, framing and the backup codec remain in
//! the shared Companion client. No device is discovered or opened at startup.
use serde_json::{json, Value};
use serialport::{SerialPort, SerialPortInfo, SerialPortType};
use std::{
    collections::{BTreeSet, VecDeque},
    io::{Read, Write},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    time::{Duration, Instant},
};
use thy666_updater::package;

pub const VID: u16 = 0xcafe;
pub const PID: u16 = 0x4009;
pub const READ_BYTES_MAX: usize = 4096;
pub const COMMAND_BYTES_MAX: usize = 160;
const SERIAL_TIMEOUT: Duration = Duration::from_millis(40);

#[derive(Default)]
pub struct Companion {
    discovered: BTreeSet<String>,
    session: Option<Arc<Session>>,
}

pub struct Session {
    id: String,
    cancelled: AtomicBool,
    ports: Ports,
    mode: Mutex<Mode>,
    exchange: Mutex<Vec<u8>>,
}
#[derive(Clone, Copy, Eq, PartialEq)]
enum Mode {
    Unselected,
    Stream,
    Exchange,
}
enum Ports {
    Native {
        reader: Mutex<Option<Box<dyn SerialPort>>>,
        writer: Mutex<Option<Box<dyn SerialPort>>>,
    },
    Simulated(Mutex<VecDeque<u8>>),
}

fn opaque_id(port: &SerialPortInfo) -> String {
    let serial = match &port.port_type {
        SerialPortType::UsbPort(info) => info.serial_number.as_deref().unwrap_or(""),
        _ => "",
    };
    package::sha256(format!("thy666-cdc\0{}\0{serial}", port.port_name).as_bytes())
}
fn compatible(port: &SerialPortInfo) -> bool {
    matches!(&port.port_type, SerialPortType::UsbPort(info) if info.vid == VID && info.pid == PID)
}
fn native_ports() -> Result<Vec<SerialPortInfo>, String> {
    let mut ports: Vec<_> = serialport::available_ports()
        .map_err(|_| "Could not enumerate serial interfaces")?
        .into_iter()
        .filter(compatible)
        .collect();
    ports.sort_by(|a, b| a.port_name.cmp(&b.port_name));
    Ok(ports)
}
fn fresh_session() -> Result<String, String> {
    let mut bytes = [0u8; 16];
    getrandom::fill(&mut bytes).map_err(|_| "Could not create a private device session")?;
    Ok(bytes.iter().map(|byte| format!("{byte:02x}")).collect())
}

impl Companion {
    pub fn is_open(&self) -> bool {
        self.session.is_some()
    }
    pub fn list(&mut self, simulation: bool) -> Result<Value, String> {
        if self.is_open() {
            return Err(
                "Disconnect the current Companion session before discovering another board".into(),
            );
        }
        let devices: Vec<Value> = if simulation {
            vec![
                json!({"id":"simulation-cdc","label":"SIMULATION — no physical board","usbVendorId":VID,"usbProductId":PID}),
            ]
        } else {
            native_ports()?.iter().enumerate().map(|(index,port)| json!({"id":opaque_id(port),
                "label":format!("THY666 application {}",index+1),"usbVendorId":VID,"usbProductId":PID})).collect()
        };
        self.discovered = devices
            .iter()
            .filter_map(|v| v["id"].as_str().map(str::to_owned))
            .collect();
        Ok(json!({"devices":devices}))
    }
    pub fn open(&mut self, device_id: &str, simulation: bool) -> Result<Value, String> {
        if self.is_open() {
            return Err("A Companion session is already open".into());
        }
        if !self.discovered.contains(device_id) {
            return Err("Discover and explicitly select the THY666 application first".into());
        }
        let id = fresh_session()?;
        let ports = if simulation {
            if device_id != "simulation-cdc" {
                return Err("Unknown simulated interface".into());
            }
            Ports::Simulated(Mutex::new(VecDeque::new()))
        } else {
            // Never accept arbitrary OS paths or fall back to a VID/PID match.
            let ports = native_ports()?;
            let selected: Vec<_> = ports
                .iter()
                .filter(|port| opaque_id(port) == device_id)
                .collect();
            if selected.len() != 1 {
                return Err(
                    "The selected application interface changed; discover and select it again"
                        .into(),
                );
            }
            let mut writer = serialport::new(&selected[0].port_name, 115200)
                .data_bits(serialport::DataBits::Eight)
                .parity(serialport::Parity::None)
                .stop_bits(serialport::StopBits::One)
                .flow_control(serialport::FlowControl::None)
                .timeout(SERIAL_TIMEOUT)
                .open()
                .map_err(|_| {
                    "Could not open the selected THY666 application; close other clients"
                })?;
            writer
                .write_data_terminal_ready(true)
                .map_err(|_| "Could not enable the THY666 console")?;
            let reader = match writer.try_clone() {
                Ok(value) => value,
                Err(_) => {
                    let _ = writer.write_data_terminal_ready(false);
                    return Err("Could not create the THY666 read channel".into());
                }
            };
            Ports::Native {
                reader: Mutex::new(Some(reader)),
                writer: Mutex::new(Some(writer)),
            }
        };
        self.session = Some(Arc::new(Session {
            id: id.clone(),
            cancelled: AtomicBool::new(false),
            ports,
            mode: Mutex::new(Mode::Unselected),
            exchange: Mutex::new(Vec::new()),
        }));
        self.discovered.clear();
        Ok(json!({"session":id,"usbVendorId":VID,"usbProductId":PID}))
    }
    pub fn session(&self, id: &str) -> Result<Arc<Session>, String> {
        self.session
            .as_ref()
            .filter(|s| s.id == id && !s.cancelled.load(Ordering::Acquire))
            .cloned()
            .ok_or_else(|| "This Companion session is closed or has been replaced".into())
    }
    pub fn close(&mut self, id: &str) -> Result<(), String> {
        if self.session.as_ref().is_none_or(|s| s.id != id) {
            return Ok(());
        }
        // Keep the coordinator locked until both OS handles are actually gone.
        if let Some(session) = self.session.take() {
            session.close()?;
        }
        Ok(())
    }
    pub fn shutdown(&mut self) {
        if let Some(session) = self.session.take() {
            let _ = session.close();
        }
    }
}

pub fn validate_command(bytes: &[u8]) -> Result<(), String> {
    if bytes.len() < 2 || bytes.len() > COMMAND_BYTES_MAX || bytes.last() != Some(&b'\n') {
        return Err("Companion commands must be one bounded, newline-terminated line".into());
    }
    let body = &bytes[..bytes.len() - 1];
    if body.iter().any(|b| !(32..=126).contains(b)) {
        return Err("Companion commands must contain one printable ASCII line".into());
    }
    let command = std::str::from_utf8(body).map_err(|_| "Invalid Companion command")?;
    let pad_command = ["pad ", "velocity "].iter().any(|prefix| {
        command.strip_prefix(prefix).is_some_and(|id| {
            !id.is_empty()
                && id.bytes().all(|b| b.is_ascii_digit())
                && id.parse::<u8>().is_ok_and(|id| id < 96)
        })
    });
    let allowed = ["status", "io", "settings", "display", "pads"].contains(&command)
        || pad_command
        || profile_command(command)
        || ["companion ", "backup ", "song "]
            .iter()
            .any(|prefix| command.starts_with(prefix));
    if !allowed {
        return Err("This command is outside the Companion protocol".into());
    }
    Ok(())
}

fn profile_command(command: &str) -> bool {
    let parts: Vec<_> = command.split(' ').collect();
    let number = |text: &str, min: u32, max: u32| {
        !text.is_empty() && text.bytes().all(|b| b.is_ascii_digit())
            && text.parse::<u32>().is_ok_and(|value| value >= min && value <= max)
    };
    match parts.as_slice() {
        ["profile", "caps" | "status"] => true,
        ["profile", "start", seconds] => number(seconds, 1, 60),
        ["profile", "stop", id] => number(id, 1, u32::MAX),
        ["profile", "metric", id, index] => number(id, 1, u32::MAX) && number(index, 0, 18),
        _ => false,
    }
}

impl Session {
    pub fn read(&self) -> Result<Value, String> {
        self.claim_mode(Mode::Stream)?;
        let bytes = self.read_bytes()?;
        Ok(json!({"bytes":bytes,"done":self.cancelled.load(Ordering::Acquire)}))
    }
    fn read_bytes(&self) -> Result<Vec<u8>, String> {
        if self.cancelled.load(Ordering::Acquire) {
            return Ok(Vec::new());
        }
        let bytes = match &self.ports {
            Ports::Native { reader, .. } => {
                let mut guard = reader
                    .lock()
                    .map_err(|_| "The native read channel stopped")?;
                let Some(port) = guard.as_mut() else {
                    return Ok(Vec::new());
                };
                let mut buffer = vec![0; READ_BYTES_MAX];
                match port.read(&mut buffer) {
                    Ok(count) => {
                        buffer.truncate(count);
                        buffer
                    }
                    Err(error)
                        if matches!(
                            error.kind(),
                            std::io::ErrorKind::TimedOut | std::io::ErrorKind::WouldBlock
                        ) =>
                    {
                        Vec::new()
                    }
                    Err(_) => {
                        self.cancelled.store(true, Ordering::Release);
                        return Err("The THY666 serial connection was lost".into());
                    }
                }
            }
            Ports::Simulated(queue) => {
                let mut queue = queue
                    .lock()
                    .map_err(|_| "The simulation read channel stopped")?;
                let length = queue.len().min(READ_BYTES_MAX);
                let bytes: Vec<_> = queue.drain(..length).collect();
                drop(queue);
                if bytes.is_empty() {
                    std::thread::sleep(SERIAL_TIMEOUT);
                }
                bytes
            }
        };
        Ok(bytes)
    }
    pub fn write(&self, bytes: &[u8]) -> Result<(), String> {
        self.claim_mode(Mode::Stream)?;
        self.write_bytes(bytes)
    }
    fn write_bytes(&self, bytes: &[u8]) -> Result<(), String> {
        validate_command(bytes)?;
        if self.cancelled.load(Ordering::Acquire) {
            return Err("This Companion session is closed".into());
        }
        match &self.ports {
            Ports::Native { writer, .. } => {
                let mut guard = writer
                    .lock()
                    .map_err(|_| "The native write channel stopped")?;
                let port = guard.as_mut().ok_or("This Companion session is closed")?;
                if port.write_all(bytes).is_err() {
                    self.cancelled.store(true, Ordering::Release);
                    return Err("Could not deliver the complete Companion command; its outcome is uncertain".into());
                }
                Ok(())
            }
            Ports::Simulated(queue) => {
                let response =
                    simulation_response(std::str::from_utf8(bytes).unwrap_or("").trim_end());
                let mut queue = queue
                    .lock()
                    .map_err(|_| "The simulation write channel stopped")?;
                if queue.len() + response.len() > READ_BYTES_MAX {
                    return Err("The simulation response queue is full".into());
                }
                queue.extend(response.as_bytes());
                Ok(())
            }
        }
    }
    fn claim_mode(&self, next: Mode) -> Result<(), String> {
        let mut mode = self
            .mode
            .lock()
            .map_err(|_| "The device session mode stopped")?;
        if *mode != Mode::Unselected && *mode != next {
            return Err("Stream and exchange consumers cannot share one serial session".into());
        }
        *mode = next;
        Ok(())
    }
    pub fn exchange_many(&self, commands: &[String]) -> Result<Vec<String>, String> {
        if commands.is_empty() || commands.len() > 32 {
            return Err("A console batch requires 1 to 32 commands".into());
        }
        // Reject the entire batch before its first side effect.
        for command in commands {
            validate_command(format!("{command}\n").as_bytes())?;
        }
        self.claim_mode(Mode::Exchange)?;
        let mut fragment = self
            .exchange
            .lock()
            .map_err(|_| "The console exchange queue stopped")?;
        let mut lines = Vec::with_capacity(commands.len());
        for command in commands {
            self.write_bytes(format!("{command}\n").as_bytes())?;
            let deadline = Instant::now() + Duration::from_secs(3);
            let mut discarded = 0;
            loop {
                if self.cancelled.load(Ordering::Acquire) {
                    return Err("The console exchange was cancelled".into());
                }
                if Instant::now() >= deadline {
                    self.cancelled.store(true, Ordering::Release);
                    return Err(
                        "The console response timed out; reconnect before sending another command"
                            .into(),
                    );
                }
                if let Some(end) = fragment.iter().position(|b| *b == b'\n') {
                    let bytes: Vec<_> = fragment.drain(..=end).collect();
                    let line = match std::str::from_utf8(&bytes) {
                        Ok(text) => text.trim().to_owned(),
                        Err(_) => {
                            self.cancelled.store(true, Ordering::Release);
                            return Err("The console returned invalid text".into());
                        }
                    };
                    if response_matches(command, &line) {
                        lines.push(line);
                        break;
                    }
                    discarded += bytes.len();
                    if discarded > READ_BYTES_MAX {
                        self.cancelled.store(true, Ordering::Release);
                        return Err("Too much unsolicited console data was received".into());
                    }
                    continue;
                }
                let bytes = self.read_bytes()?;
                if fragment.len() + bytes.len() > READ_BYTES_MAX {
                    self.cancelled.store(true, Ordering::Release);
                    return Err("The console response exceeded its line limit".into());
                }
                fragment.extend(bytes);
            }
        }
        Ok(lines)
    }
    fn close(&self) -> Result<(), String> {
        self.cancelled.store(true, Ordering::Release);
        match &self.ports {
            Ports::Native { reader, writer } => {
                let mut read = reader
                    .lock()
                    .map_err(|_| "Could not close the native read channel")?;
                let mut write = writer
                    .lock()
                    .map_err(|_| "Could not close the native write channel")?;
                if let Some(port) = write.as_mut() {
                    let _ = port.write_data_terminal_ready(false);
                }
                read.take();
                write.take();
                Ok(())
            }
            Ports::Simulated(queue) => {
                queue
                    .lock()
                    .map_err(|_| "Could not close the simulation channel")?
                    .clear();
                Ok(())
            }
        }
    }
}

fn response_matches(command: &str, line: &str) -> bool {
    let namespace = command.split(' ').next().unwrap_or("");
    let prefix = match namespace {
        "companion" => "COMPANION/1 ",
        "backup" => "BACKUP ",
        "song" => "SONG ",
        "status" => "STATUS ",
        "io" => "IO ",
        "settings" => "SETTINGS ",
        "display" => "DISPLAY ",
        "pads" => "PADS ",
        "pad" => "PAD ",
        "velocity" => "VELOCITY ",
        "profile" if command.starts_with("profile metric ") => "PROFILE_METRIC ",
        "profile" => "PROFILE ",
        _ => return false,
    };
    line.starts_with(prefix) || line.starts_with("ERR ") || line.starts_with("COMPANION/1 ERR ")
}

fn simulation_response(command: &str) -> String {
    // The fake serial path exercises byte delivery, not a second musical model.
    // It deliberately cannot masquerade as a verified instrument application.
    match command {
        "status" => {
            "STATUS THYINSTRUMENT/1 build=1234567890abcdef ready=0 errors=0 voices=0 simulation=1\n"
                .into()
        }
        "companion caps" => "ERR desktop-simulation-no-instrument-state\n".into(),
        _ => "ERR desktop-simulation-read-only\n".into(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn explicit_discovery_session_tokens_and_stale_close_are_enforced_without_usb() {
        let mut c = Companion::default();
        assert!(c.open("simulation-cdc", true).is_err());
        assert_eq!(c.list(true).unwrap()["devices"][0]["id"], "simulation-cdc");
        let opened = c.open("simulation-cdc", true).unwrap();
        let first = opened["session"].as_str().unwrap().to_owned();
        assert!(c.list(true).is_err());
        assert!(c.session("arbitrary").is_err());
        c.close(&first).unwrap();
        c.list(true).unwrap();
        let second = c.open("simulation-cdc", true).unwrap()["session"]
            .as_str()
            .unwrap()
            .to_owned();
        assert_ne!(first, second);
        c.close(&first).unwrap();
        assert!(c.session(&second).is_ok());
        c.shutdown();
        assert!(!c.is_open());
    }
    #[test]
    fn one_bounded_console_command_is_allowed_but_binary_multiline_and_raw_flash_are_not() {
        for command in [
            "companion caps\n",
            "backup export\n",
            "song caps\n",
            "status\n",
            "io\n",
            "settings\n",
            "display\n",
            "pads\n",
            "backup status\n",
            "pad 0\n",
            "velocity 95\n",
            "profile caps\n",
            "profile status\n",
            "profile start 1\n",
            "profile start 60\n",
            "profile stop 4294967295\n",
            "profile metric 1 0\n",
            "profile metric 4294967295 18\n",
        ] {
            assert!(validate_command(command.as_bytes()).is_ok());
        }
        for command in [
            "",
            "\n",
            "status",
            "status\r\n",
            "status\nio\n",
            "nor erase\n",
            "flash read\n",
            "status\0\n",
            "profile\n",
            "profile start 0\n",
            "profile start 61\n",
            "profile stop 0\n",
            "profile stop 4294967296\n",
            "profile metric 1 19\n",
            "profile metric 1 -1\n",
            "profile metric 0 1\n",
            "profile status extra\n",
            "profile erase\n",
        ] {
            assert!(validate_command(command.as_bytes()).is_err());
        }
        assert!(validate_command(&vec![b'a'; COMMAND_BYTES_MAX + 1]).is_err());
    }
    #[test]
    fn simulation_bytes_and_cancellation_never_claim_a_healthy_instrument() {
        let mut c = Companion::default();
        c.list(true).unwrap();
        let info = c.open("simulation-cdc", true).unwrap();
        let id = info["session"].as_str().unwrap();
        let s = c.session(id).unwrap();
        s.write(b"status\n").unwrap();
        let result = s.read().unwrap();
        let bytes: Vec<u8> = serde_json::from_value(result["bytes"].clone()).unwrap();
        assert!(String::from_utf8(bytes).unwrap().contains("ready=0"));
        c.close(id).unwrap();
        assert_eq!(s.read().unwrap()["done"], true);
        assert!(s.write(b"status\n").is_err());
    }
    #[test]
    fn filter_and_opaque_ids_never_expose_unrelated_ports_or_private_paths() {
        let make = |vid, pid| SerialPortInfo {
            port_name: "synthetic-port".into(),
            port_type: SerialPortType::UsbPort(serialport::UsbPortInfo {
                vid,
                pid,
                serial_number: Some("synthetic-serial".into()),
                manufacturer: None,
                product: None,
            }),
        };
        assert!(compatible(&make(VID, PID)));
        assert!(!compatible(&make(1, PID)));
        assert!(!compatible(&make(VID, 1)));
        let id = opaque_id(&make(VID, PID));
        assert_eq!(id.len(), 64);
        assert!(id.bytes().all(|b| b.is_ascii_hexdigit()));
        assert!(!id.contains("synthetic"));
    }
    #[test]
    fn batches_serialize_replies_validate_before_delivery_and_cannot_mix_with_stream_consumers() {
        let mut c = Companion::default();
        c.list(true).unwrap();
        let opened = c.open("simulation-cdc", true).unwrap();
        let s = c.session(opened["session"].as_str().unwrap()).unwrap();
        assert!(s
            .exchange_many(&["status".into(), "flash erase".into()])
            .is_err());
        if let Ports::Simulated(queue) = &s.ports {
            assert!(queue.lock().unwrap().is_empty());
        }
        let lines = s
            .exchange_many(&["status".into(), "companion caps".into()])
            .unwrap();
        assert_eq!(lines.len(), 2);
        assert!(lines[0].starts_with("STATUS "));
        assert!(lines[1].starts_with("ERR "));
        assert!(s.read().is_err());
        assert!(s.write(b"status\n").is_err());
        assert!(s.exchange_many(&vec!["status".into(); 33]).is_err());
    }
    #[test]
    fn console_reply_matching_discards_greetings_and_keeps_errors() {
        assert!(!response_matches(
            "companion caps",
            "THY666 instrument ready"
        ));
        assert!(response_matches(
            "companion caps",
            "COMPANION/1 CAPS role=instrument"
        ));
        assert!(response_matches("backup caps", "BACKUP version=1"));
        assert!(response_matches("backup status", "BACKUP version=1 state=queued"));
        assert!(response_matches("pads", "PADS frames=12 scans=192 errors=0"));
        assert!(!response_matches("pads", "PAD index=0 raw=1200"));
        assert!(response_matches("song caps", "SONG version=1"));
        assert!(response_matches("io", "ERR busy"));
        assert!(!response_matches("status", "IO din_ready=1"));
        assert!(response_matches("profile status", "PROFILE version=1 id=1 active=0"));
        assert!(response_matches("profile start 30", "PROFILE version=1 id=1 active=1"));
        assert!(response_matches("profile metric 1 18", "PROFILE_METRIC id=1 index=18 unit=us"));
        assert!(!response_matches("profile metric 1 0", "PROFILE version=1"));
        assert!(!response_matches("profile status", "PROFILE_METRIC id=1 index=0"));
        assert!(response_matches("profile stop 1", "ERR profile-session"));
    }
}
