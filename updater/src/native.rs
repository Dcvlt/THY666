//! Explicit native access to the existing TheoryBoard update HID collection.
//!
//! Merely loading this module or checking `backend_available` performs no USB
//! work. Enumeration and opening are separate caller-requested operations.

use crate::package::sha256;
use crate::protocol::Transport;
use hidapi::{HidApi, HidDevice};
use std::ffi::{CStr, CString};
use std::fmt;

pub const UPDATE_VID: u16 = 0x0333;
pub const UPDATE_PID: u16 = 0x0333;
pub const UPDATE_USAGE_PAGE: u16 = 0xffab;
pub const UPDATE_USAGE: u16 = 0x0200;

#[derive(Clone)]
pub struct NativeDevice {
    /// Opaque identity of this enumerated OS interface, not an MCU UID.
    pub id: String,
    pub label: String,
    /// USB descriptor serials can be generic (including all zeroes).
    /// They must never be presented as unique physical-board authentication.
    pub serial: Option<String>,
    path: CString,
}

impl NativeDevice {
    pub fn path(&self) -> &CStr {
        &self.path
    }
}

// Avoid accidentally including the operating-system path or serial in logs.
impl fmt::Debug for NativeDevice {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("NativeDevice")
            .field("id", &self.id)
            .field("label", &self.label)
            .finish_non_exhaustive()
    }
}

pub struct NativeTransport {
    device: HidDevice,
}

/// Reports compile-time support only; never constructs a HID context.
pub const fn backend_available() -> bool {
    cfg!(any(
        target_os = "windows",
        target_os = "macos",
        target_os = "linux"
    ))
}

/// Pure interface filter shared by enumeration, opening and synthetic tests.
pub fn matches_update_interface(vid: u16, pid: u16, usage_page: u16, usage: u16) -> bool {
    (vid, pid, usage_page, usage) == (UPDATE_VID, UPDATE_PID, UPDATE_USAGE_PAGE, UPDATE_USAGE)
}

fn interface_id(path: &CStr, serial: Option<&str>) -> String {
    // Length-prefix components to avoid concatenation ambiguity. The hash is
    // for hiding raw OS paths in UI identifiers, not for authenticating a board.
    let mut bytes = Vec::with_capacity(path.to_bytes().len() + serial.map_or(0, str::len) + 17);
    bytes.extend_from_slice(&(path.to_bytes().len() as u64).to_le_bytes());
    bytes.extend_from_slice(path.to_bytes());
    bytes.push(u8::from(serial.is_some()));
    let serial = serial.unwrap_or("").as_bytes();
    bytes.extend_from_slice(&(serial.len() as u64).to_le_bytes());
    bytes.extend_from_slice(serial);
    format!("thy333-{}", &sha256(&bytes)[..24])
}

fn devices_from_context(api: &HidApi) -> Vec<NativeDevice> {
    let mut devices: Vec<_> = api
        .device_list()
        .filter(|info| {
            matches_update_interface(
                info.vendor_id(),
                info.product_id(),
                info.usage_page(),
                info.usage(),
            )
        })
        .map(|info| {
            let serial = info.serial_number().map(str::to_owned);
            NativeDevice {
                id: interface_id(info.path(), serial.as_deref()),
                label: String::new(),
                serial,
                path: info.path().to_owned(),
            }
        })
        .collect();
    devices.sort_by(|a, b| a.path.to_bytes().cmp(b.path.to_bytes()));
    for (index, device) in devices.iter_mut().enumerate() {
        device.label = format!("TheoryBoard update interface {}", index + 1);
    }
    devices
}

/// Enumerate matching interfaces without opening any HID handle.
pub fn enumerate() -> Result<Vec<NativeDevice>, String> {
    let api = HidApi::new().map_err(|_| "Unable to enumerate native HID interfaces".to_owned())?;
    Ok(devices_from_context(&api))
}

fn selected_device<'a>(devices: &'a [NativeDevice], id: &str) -> Result<&'a NativeDevice, String> {
    let mut matches = devices.iter().filter(|device| device.id == id);
    let selected = matches.next().ok_or_else(|| {
        "Selected update interface is no longer present; enumerate and select again".to_owned()
    })?;
    if matches.next().is_some() {
        return Err("Selected update interface is ambiguous; no handle was opened".to_owned());
    }
    Ok(selected)
}

/// Re-enumerate, require one exact opaque ID, then open only that saved path.
/// No VID/PID fallback or automatic choice is permitted.
pub fn open_selected(id: &str) -> Result<NativeTransport, String> {
    let api = HidApi::new().map_err(|_| "Unable to refresh native HID interfaces".to_owned())?;
    let devices = devices_from_context(&api);
    let selected = selected_device(&devices, id)?;
    let device = api.open_path(selected.path()).map_err(|_| {
        "Unable to open the selected update interface; close other clients and select again"
            .to_owned()
    })?;
    // Recheck the opened interface. A descriptor serial is not a device UID;
    // this prevents an interface mismatch but does not authenticate hardware.
    let info = device
        .get_device_info()
        .map_err(|_| "Unable to verify the opened update interface".to_owned())?;
    if !matches_update_interface(
        info.vendor_id(),
        info.product_id(),
        info.usage_page(),
        info.usage(),
    ) || interface_id(info.path(), info.serial_number()) != selected.id
    {
        return Err("Opened interface differs from the selected update interface".to_owned());
    }
    Ok(NativeTransport { device })
}

fn validate_output_report(report: &[u8]) -> Result<(), String> {
    if report.len() != 65 || report[0] != 0 {
        return Err(
            "Update output must contain a zero report ID and exactly 64 payload bytes".to_owned(),
        );
    }
    Ok(())
}

fn normalized_input(report: &[u8]) -> Result<[u8; 64], String> {
    // HIDAPI's unnumbered read API omits the zero report ID. A 65-byte read
    // could represent a different numbered report and is not silently stripped.
    report
        .try_into()
        .map_err(|_| "Update input must contain exactly 64 unnumbered payload bytes".to_owned())
}

impl Transport for NativeTransport {
    fn write_report(&mut self, report: &[u8; 65]) -> Result<usize, String> {
        validate_output_report(report)?;
        // HIDAPI exposes a blocking write with backend-defined timing, not a
        // portable timeout/cancel operation. Never retry a failed/short write:
        // the bootloader may have consumed part or all of this report.
        self.device
            .write(report)
            .map_err(|_| "Native HID write failed; transfer state is unknown".to_owned())
    }

    fn read_report(&mut self, timeout_ms: u32) -> Result<Vec<u8>, String> {
        let timeout = i32::try_from(timeout_ms)
            .map_err(|_| "Native HID read timeout exceeds the finite supported range".to_owned())?;
        let mut report = [0u8; 65];
        let count = self
            .device
            .read_timeout(&mut report, timeout)
            .map_err(|_| "Native HID read failed".to_owned())?;
        if count == 0 {
            return Ok(Vec::new());
        }
        // The backend cannot write beyond the supplied buffer. Check anyway so
        // an invalid length cannot cause a panic in this boundary adapter.
        let bytes = report
            .get(..count)
            .ok_or_else(|| "Native HID returned an invalid input length".to_owned())?;
        Ok(normalized_input(bytes)?.to_vec())
    }

    fn sleep(&mut self, ms: u64) {
        std::thread::sleep(std::time::Duration::from_millis(ms));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn synthetic_device(path: &str, serial: Option<&str>) -> NativeDevice {
        let path = CString::new(path).unwrap();
        NativeDevice {
            id: interface_id(&path, serial),
            label: "TheoryBoard update interface 1".to_owned(),
            serial: serial.map(str::to_owned),
            path,
        }
    }

    #[test]
    fn only_exact_update_collection_matches() {
        assert!(matches_update_interface(0x0333, 0x0333, 0xffab, 0x0200));
        for (vid, pid, page, usage) in [
            (0x0334, 0x0333, 0xffab, 0x0200),
            (0x0333, 0x0334, 0xffab, 0x0200),
            (0x0333, 0x0333, 0xffaa, 0x0200),
            (0x0333, 0x0333, 0xffab, 0x0201),
            (0x0333, 0x0333, 0, 0),
        ] {
            assert!(!matches_update_interface(vid, pid, page, usage));
        }
    }

    #[test]
    fn opaque_ids_distinguish_paths_and_do_not_claim_serial_uniqueness() {
        let a = synthetic_device("synthetic-interface-A", Some("0000000000"));
        let b = synthetic_device("synthetic-interface-B", Some("0000000000"));
        assert_ne!(a.id, b.id);
        assert_eq!(a.id.len(), 31);
        assert!(!a.id.contains("synthetic"));
        assert!(!format!("{a:?}").contains("0000000000"));
        assert!(!format!("{a:?}").contains("synthetic-interface"));
        assert_ne!(
            interface_id(a.path(), None),
            interface_id(a.path(), Some(""))
        );
    }

    #[test]
    fn selection_requires_one_exact_interface_with_no_fallback() {
        let a = synthetic_device("synthetic-A", None);
        let b = synthetic_device("synthetic-B", None);
        let devices = [a.clone(), b];
        assert_eq!(selected_device(&devices, &a.id).unwrap().id, a.id);
        assert!(selected_device(&devices, "other").is_err());
        assert!(selected_device(&[a.clone(), a.clone()], &a.id).is_err());
        assert!(selected_device(&[], &a.id).is_err());
    }

    #[test]
    fn reports_require_explicit_zero_output_id_and_unnumbered_64_byte_input() {
        let report = [0u8; 65];
        assert!(validate_output_report(&report).is_ok());
        let mut numbered = report;
        numbered[0] = 1;
        assert!(validate_output_report(&numbered).is_err());
        assert!(validate_output_report(&report[..64]).is_err());
        assert_eq!(normalized_input(&[7u8; 64]).unwrap(), [7u8; 64]);
        assert!(normalized_input(&report).is_err());
        assert!(normalized_input(&numbered).is_err());
        assert!(normalized_input(&[]).is_err());
        assert!(normalized_input(&report[..63]).is_err());
    }
}
