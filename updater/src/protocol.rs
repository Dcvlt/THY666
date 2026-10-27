//! Bounded replay of the observed application-only bootloader exchange.
//!
//! The original preparation-before-metadata ordering is preserved. Preparation
//! may alter external contents before the optional UID comparison. A verified
//! transfer and activation request are not flash readback or application health.

#[cfg(test)]
use crate::package;
#[cfg(not(target_arch = "wasm32"))]
use crate::package::Package;
use std::fmt;
#[cfg(not(target_arch = "wasm32"))]
use std::time::Instant;

pub const SIGNATURE: &[u8; 56] = b"sSJFfddSAD$srAffy84JappEKL3$SALgK@3J4hwd5920xg1!8hs7J6Kg";
pub(crate) const READ_POLL_MS: u32 = 250;
pub(crate) const HOST_DELAY_MS: u64 = 500;

/// The selected, exclusively owned transport. Empty reads mean timeout only;
/// errors and disconnection must be returned as errors. Implementations must
/// honor the read timeout. Phase deadlines are checked between calls; this trait
/// cannot cancel a backend write already blocking in the operating system.
pub trait Transport {
    fn write_report(&mut self, report: &[u8; 65]) -> Result<usize, String>;
    fn read_report(&mut self, timeout_ms: u32) -> Result<Vec<u8>, String>;
    fn sleep(&mut self, ms: u64);
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Progress {
    pub phase: &'static str,
    pub completed: usize,
    pub total: usize,
    pub message: String,
    pub state: &'static str,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct TransferOutcome {
    pub activation_writes: usize,
    pub warning: Option<String>,
    pub awaiting_application_verification: bool,
    pub transferred_crc: u32,
    pub external_crc: u32,
    /// Present only for the recorded encrypted stock profile.
    pub decoded_crc: Option<u32>,
    pub metadata_uid_verified: bool,
    /// Private runtime identity for explicit post-update application comparison.
    pub target_uid: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProtocolError {
    pub phase: &'static str,
    pub message: String,
    /// True after any attempted preparation write, even if completion is unknown.
    pub mutation_started: bool,
    /// The operation may already have changed device state; never auto-replay it.
    pub indeterminate: bool,
}

impl fmt::Display for ProtocolError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}: {}", self.phase, self.message)
    }
}
impl std::error::Error for ProtocolError {}

pub(crate) fn command(token: &[u8; 8]) -> [u8; 65] {
    let mut report = [0; 65];
    report[1..9].copy_from_slice(token);
    report[9..].copy_from_slice(SIGNATURE);
    report
}

pub(crate) fn signed(payload: &[u8; 64], token: &[u8; 8]) -> bool {
    payload[..8] == token[..] && payload[8..] == SIGNATURE[..]
}

/// Native adapter for the same pull-driven session used by the browser.
/// The host alone selects/opens the device. This never reconnects or retries
/// payload writes, restores backups, or claims the application is running.
#[cfg(not(target_arch = "wasm32"))]
pub fn runpkg<T: Transport>(
    pkg: &Package<'_>,
    transport: &mut T,
    expected_uid: Option<&str>,
    mut callback: impl FnMut(Progress),
) -> Result<TransferOutcome, ProtocolError> {
    use crate::session::{Action, Completion, Session};
    let started = Instant::now();
    let now = || started.elapsed().as_millis().min(u128::from(u64::MAX)) as u64;
    let mut session = Session::new(pkg, expected_uid, 0)?;
    let mut action = session.next(now())?;
    loop {
        for event in session.drain_progress() {
            callback(event);
        }
        let (id, completion) = match action {
            Action::Write { id, report } => (
                id,
                Completion::Write(transport.write_report(&report).map_err(|_| ())),
            ),
            Action::Read { id, timeout_ms } => (
                id,
                Completion::Read(transport.read_report(timeout_ms).map_err(|_| ())),
            ),
            Action::Sleep { id, ms } => {
                transport.sleep(ms);
                (id, Completion::Sleep)
            }
            Action::Done(outcome) => return Ok(outcome),
        };
        action = session.complete(id, completion, now())?;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::VecDeque;

    const BUILD: &str = "deadbeef12345678";
    const UID: &str = "111111112222222233333333";

    fn fixture() -> Vec<u8> {
        let mut app = vec![0; 512];
        app[..4].copy_from_slice(&0x2002_0000u32.to_le_bytes());
        app[4..8].copy_from_slice(&0x0804_0191u32.to_le_bytes());
        app[400..416].copy_from_slice(BUILD.as_bytes());
        let header = format!(
            "THY333{:06X}{:06X}{:06X}{:06X}{:06X}{:08X}{:08X}{:08X}{:06X}{:06X}{:06X}",
            1,
            0,
            1,
            0,
            app.len(),
            package::OBSERVED_METADATA[0],
            package::OBSERVED_METADATA[1],
            package::OBSERVED_METADATA[2],
            4,
            0,
            1
        );
        [header.into_bytes(), app].concat()
    }

    fn marker(token: &[u8; 8]) -> Vec<u8> {
        command(token)[1..].to_vec()
    }
    fn number(value: u32) -> Vec<u8> {
        let mut payload = vec![0x5a; 64]; // Actual integer tails need not be zero.
        payload[..4].copy_from_slice(&value.to_le_bytes());
        payload
    }

    struct Fake {
        input: VecDeque<Result<Vec<u8>, String>>,
        writes: Vec<[u8; 65]>,
        fail_write: Option<usize>,
        short_write: Option<usize>,
        calls: usize,
        reads: usize,
        sleeps: Vec<u64>,
    }

    impl Fake {
        fn new(crc: u32) -> Self {
            let mut input = vec![marker(b"FILSTATU"), number(1), marker(b"SFIFDONE")];
            let mut metadata_begin = marker(b"FWMETABG");
            metadata_begin[8..].fill(0xa5);
            input.extend([metadata_begin, number(4)]);
            for index in 0..14 {
                input.push(number(match index {
                    11 => 0x1111_1111,
                    12 => 0x2222_2222,
                    13 => 0x3333_3333,
                    _ => index,
                }));
            }
            let mut metadata_end = marker(b"FWMETADN");
            metadata_end[8..].fill(0xa5);
            input.extend([
                metadata_end,
                marker(b"FCMDDONE"),
                marker(b"FCMDDONE"),
                marker(b"FWSETRD1"),
                marker(b"FWSETRD2"),
                marker(b"TRACRCVL"),
                number(crc),
                marker(b"EXTCRCVL"),
                number(crc),
                marker(b"FWDLYRDY"),
            ]);
            Self {
                input: input.into_iter().map(Ok).collect(),
                writes: vec![],
                fail_write: None,
                short_write: None,
                calls: 0,
                reads: 0,
                sleeps: vec![],
            }
        }
        fn sent(&self, token: &[u8; 8]) -> bool {
            self.writes.iter().any(|r| r == &command(token))
        }
    }

    impl Transport for Fake {
        fn write_report(&mut self, report: &[u8; 65]) -> Result<usize, String> {
            let call = self.calls;
            self.calls += 1;
            self.writes.push(*report);
            if self.fail_write == Some(call) {
                Err("synthetic transport error with private path excluded".into())
            } else if self.short_write == Some(call) {
                Ok(12)
            } else {
                Ok(65)
            }
        }
        fn read_report(&mut self, timeout_ms: u32) -> Result<Vec<u8>, String> {
            assert!((1..=READ_POLL_MS).contains(&timeout_ms));
            self.reads += 1;
            self.input.pop_front().unwrap_or(Ok(vec![]))
        }
        fn sleep(&mut self, ms: u64) {
            self.sleeps.push(ms);
        }
    }

    #[test]
    fn observed_success_preserves_framing_parameters_uid_and_progress() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        let mut fake = Fake::new(pkg.info.crc32);
        let mut events = vec![];
        let outcome = runpkg(&pkg, &mut fake, Some(UID), |p| events.push(p)).unwrap();
        assert!(outcome.awaiting_application_verification && outcome.metadata_uid_verified);
        assert_eq!(outcome.target_uid, UID);
        assert_eq!(outcome.activation_writes, 5);
        assert_eq!(fake.writes[0], command(b"XCFIFCFF"));
        assert_eq!(
            &fake.writes[1..5],
            &[
                command(b"XCFILNFS"),
                command(b"DUMPMETA"),
                command(b"DONECMDS"),
                command(b"XCSMDIUS")
            ]
        );
        for (report, value) in fake.writes[5..12].iter().zip(pkg.parameters()) {
            assert_eq!(report[0], 0);
            assert_eq!(&report[1..5], &value.to_le_bytes());
            assert!(report[5..].iter().all(|&b| b == 0));
        }
        assert_eq!(
            fake.writes[12..20]
                .iter()
                .flat_map(|r| r[1..].iter().copied())
                .collect::<Vec<_>>(),
            pkg.application()
        );
        assert_eq!(events.last().unwrap().state, "awaiting-verification");
        assert_eq!(events.last().unwrap().completed, 512);
        assert!(fake.input.is_empty());
    }

    fn encrypted_fake(pkg: &Package<'_>) -> Fake {
        let mut fake = Fake::new(pkg.info.crc32);
        fake.input.extend([
            Ok(marker(b"DCRCRCVL")),
            Ok(number(pkg.header.metadata_word_1)),
            Ok(marker(b"FWDCPRDY")),
        ]);
        fake
    }

    #[test]
    fn encrypted_stock_uses_declared_length_crc_zero_tail_and_decode_stage() {
        let bytes: Vec<u8> = (0..112).map(|i| (i * 37 + 11) as u8).collect();
        let pkg = package::synthetic_stock_package(&bytes);
        let mut fake = encrypted_fake(&pkg);
        let mut events = vec![];
        let result = runpkg(&pkg, &mut fake, Some(UID), |p| events.push(p)).unwrap();
        assert!(fake.sent(b"XCSMLRTF"));
        assert!(!fake.sent(b"XCSMDIUS"));
        assert_eq!(
            u32::from_le_bytes(fake.writes[7][1..5].try_into().unwrap()),
            112
        );
        assert_eq!(&fake.writes[12][1..], &bytes[..64]);
        assert_eq!(&fake.writes[13][1..49], &bytes[64..]);
        assert_eq!(&fake.writes[13][49..], &[0; 16]);
        let wire: Vec<u8> = fake.writes[12..14]
            .iter()
            .flat_map(|r| r[1..].iter().copied())
            .collect();
        assert_ne!(package::crc32_normal(&wire, 0), pkg.info.crc32);
        assert_eq!(result.transferred_crc, package::crc32_normal(&bytes, 0));
        assert_eq!(result.decoded_crc, Some(pkg.header.metadata_word_1));
        assert!(result.awaiting_application_verification);
        assert_eq!(events.last().unwrap().completed, bytes.len());
        assert_eq!(events.last().unwrap().total, bytes.len());
        assert!(events.iter().any(|p| p.phase == "decoding"));
        assert!(fake.input.is_empty());
    }

    #[test]
    fn encrypted_stock_rejects_wire_extent_crc_and_missing_or_bad_decode_replies() {
        let bytes: Vec<u8> = (0..112).map(|i| (i * 37 + 11) as u8).collect();
        let pkg = package::synthetic_stock_package(&bytes);
        let mut wire = bytes.clone();
        wire.resize(128, 0);
        for index in [25, 27] {
            let mut fake = encrypted_fake(&pkg);
            fake.input[index] = Ok(number(package::crc32_normal(&wire, 0)));
            let error = runpkg(&pkg, &mut fake, None, |_| {}).unwrap_err();
            assert_eq!(error.phase, "checksums");
            assert!(error.mutation_started && error.indeterminate);
            assert!(!fake.sent(b"ZCSMCCOZ"));
        }
        for index in [29, 30, 31] {
            let mut fake = encrypted_fake(&pkg);
            fake.input[index] = Ok(if index == 30 {
                number(pkg.header.metadata_word_1 ^ 1)
            } else {
                marker(b"FWDLYRDY")
            });
            let error = runpkg(&pkg, &mut fake, None, |_| {}).unwrap_err();
            assert_eq!(error.phase, "decoding");
            assert!(!fake.sent(b"ZCSMCCOZ"));
            let mut absent = encrypted_fake(&pkg);
            absent.input.truncate(index);
            assert_eq!(
                runpkg(&pkg, &mut absent, None, |_| {}).unwrap_err().phase,
                "decoding"
            );
            assert!(absent.reads <= index + 240);
            assert!(!absent.sent(b"ZCSMCCOZ"));
        }
    }

    #[test]
    fn encrypted_final_report_partial_write_is_not_replayed_or_finalized() {
        let bytes = vec![0xa5; 112];
        let pkg = package::synthetic_stock_package(&bytes);
        let mut fake = encrypted_fake(&pkg);
        fake.short_write = Some(13);
        let error = runpkg(&pkg, &mut fake, None, |_| {}).unwrap_err();
        assert_eq!(error.phase, "application");
        assert!(error.mutation_started && error.indeterminate);
        assert_eq!(fake.calls, 14);
        assert!(!fake.sent(b"DONE0000"));
        assert!(!fake.sent(b"ZCSMCCOZ"));
    }

    #[test]
    fn invalid_expected_identity_and_unreviewed_build_send_nothing() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        let mut fake = Fake::new(pkg.info.crc32);
        assert!(
            !runpkg(&pkg, &mut fake, Some("bad"), |_| {})
                .unwrap_err()
                .mutation_started
        );
        let unreviewed = package::parse(&raw, None).unwrap();
        assert!(
            !runpkg(&unreviewed, &mut fake, None, |_| {})
                .unwrap_err()
                .mutation_started
        );
        assert!(fake.writes.is_empty());
    }

    #[test]
    fn uid_mismatch_occurs_after_preparation_but_before_payload() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        let mut fake = Fake::new(pkg.info.crc32);
        let error = runpkg(&pkg, &mut fake, Some("444444445555555566666666"), |_| {}).unwrap_err();
        assert!(error.mutation_started && error.indeterminate);
        assert_eq!(error.phase, "metadata");
        assert!(fake.sent(b"XCFIFCFF"));
        assert!(!fake.sent(b"XCSMDIUS"));
        assert!(!fake.sent(b"ZCSMCCOZ"));
    }

    #[test]
    fn unrecognized_preparation_status_stops_before_metadata_or_application() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        for status in [0, 2, u32::MAX] {
            let mut fake = Fake::new(pkg.info.crc32);
            fake.input[1] = Ok(number(status));
            let error = runpkg(&pkg, &mut fake, Some(UID), |_| {}).unwrap_err();
            assert_eq!(error.phase, "preparation");
            assert!(error.mutation_started && error.indeterminate);
            assert!(error.message.contains("Unrecognized preparation status"));
            assert_eq!(fake.writes, [command(b"XCFIFCFF")]);
            assert!(!fake.sent(b"XCFILNFS"));
            assert!(!fake.sent(b"DUMPMETA"));
            assert!(!fake.sent(b"XCSMDIUS"));
            assert!(!fake.sent(b"DONE0000"));
            assert!(!fake.sent(b"ZCSMCCOZ"));
            assert_eq!(fake.reads, 2);
        }
    }

    #[test]
    fn hardware_mismatch_and_metadata_error_do_not_activate() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        for error_index in [3, 4] {
            let mut fake = Fake::new(pkg.info.crc32);
            fake.input[error_index] = Ok(if error_index == 3 {
                marker(b"FWMETAER")
            } else {
                number(3)
            });
            assert!(
                runpkg(&pkg, &mut fake, None, |_| {})
                    .unwrap_err()
                    .mutation_started
            );
            assert!(!fake.sent(b"XCSMDIUS"));
            assert!(!fake.sent(b"ZCSMCCOZ"));
        }
    }

    #[test]
    fn partial_or_failed_write_is_never_retried_including_preparation() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        for index in [0, 7, 14] {
            for short in [false, true] {
                let mut fake = Fake::new(pkg.info.crc32);
                if short {
                    fake.short_write = Some(index);
                } else {
                    fake.fail_write = Some(index);
                }
                let error = runpkg(&pkg, &mut fake, Some(UID), |_| {}).unwrap_err();
                assert!(error.mutation_started && error.indeterminate);
                assert_eq!(fake.calls, index + 1);
                assert!(!fake.sent(b"ZCSMCCOZ"));
                assert!(!error.message.contains("private path"));
            }
        }
    }

    #[test]
    fn malformed_short_and_unexpected_reports_fail_without_activation() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        for input in [vec![0; 63], vec![0; 65], marker(b"FWDLYRDY"), {
            let mut bad = marker(b"FILSTATU");
            bad[63] ^= 1;
            bad
        }] {
            let mut fake = Fake::new(pkg.info.crc32);
            fake.input[0] = Ok(input);
            assert!(runpkg(&pkg, &mut fake, None, |_| {}).is_err());
            assert_eq!(fake.calls, 1);
        }
    }

    #[test]
    fn only_two_known_extra_completions_are_allowed_at_ready1() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        let mut fake = Fake::new(pkg.info.crc32);
        fake.input.insert(22, Ok(marker(b"FCMDDONE")));
        assert_eq!(
            runpkg(&pkg, &mut fake, None, |_| {}).unwrap_err().phase,
            "ready1"
        );
        assert!(!fake.sent(b"DONE0000"));
    }

    #[test]
    fn crc_mismatch_never_activates_or_uses_legacy_header_crc() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        for index in [25, 27] {
            let mut fake = Fake::new(pkg.info.crc32);
            fake.input[index] = Ok(number(package::OBSERVED_METADATA[0]));
            let error = runpkg(&pkg, &mut fake, None, |_| {}).unwrap_err();
            assert_eq!(error.phase, "checksums");
            assert!(!fake.sent(b"ZCSMCCOZ"));
        }
    }

    #[test]
    fn empty_reads_and_transport_errors_have_finite_bounds_in_each_phase() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        for index in [0, 3, 20, 23, 24, 28] {
            let mut fake = Fake::new(pkg.info.crc32);
            fake.input.truncate(index);
            assert!(runpkg(&pkg, &mut fake, None, |_| {}).is_err());
            assert!(fake.reads <= index + 240);
            assert!(!fake.sent(b"ZCSMCCOZ"));
            let mut fake = Fake::new(pkg.info.crc32);
            fake.input[index] = Err("synthetic error".into());
            assert!(runpkg(&pkg, &mut fake, None, |_| {}).is_err());
            assert!(!fake.sent(b"ZCSMCCOZ"));
        }
    }

    #[test]
    fn late_activation_disconnect_requires_application_verification() {
        let raw = fixture();
        let pkg = package::parse(&raw, Some(BUILD)).unwrap();
        // 4 setup commands + transfer command + 7 parameters + 8 data + DONE = 21.
        let mut fake = Fake::new(pkg.info.crc32);
        fake.fail_write = Some(23);
        let result = runpkg(&pkg, &mut fake, Some(UID), |_| {}).unwrap();
        assert_eq!(result.activation_writes, 2);
        assert!(result.warning.is_some());
        assert!(result.awaiting_application_verification);
        assert_eq!(fake.calls, 24);
        let mut fake = Fake::new(pkg.info.crc32);
        fake.fail_write = Some(21);
        let error = runpkg(&pkg, &mut fake, None, |_| {}).unwrap_err();
        assert_eq!(error.phase, "activation");
        assert!(error.indeterminate);
        assert_eq!(fake.calls, 22);
    }
}
