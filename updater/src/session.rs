//! Portable pull-driven updater policy. Native HID and WebHID execute the same
//! actions; neither transport interprets bootloader tokens or package fields.
use crate::package::{self, Package};
use crate::protocol::{
    command, signed, Progress, ProtocolError, TransferOutcome, HOST_DELAY_MS, READ_POLL_MS,
};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Action {
    Write { id: u32, report: [u8; 65] },
    Read { id: u32, timeout_ms: u32 },
    Sleep { id: u32, ms: u64 },
    Done(TransferOutcome),
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
    fn elapsed_read_deadline_is_terminal_with_no_replay() {
        let raw = image();
        let pkg = package::parse(&raw, Some("deadbeef12345678")).unwrap();
        let mut session = Session::new(&pkg, None, 0).unwrap();
        assert!(matches!(session.next(0), Ok(Action::Write { id: 1, .. })));
        assert!(matches!(
            session.complete(1, Completion::Write(Ok(65)), 1),
            Ok(Action::Read {
                id: 2,
                timeout_ms: 250
            })
        ));
        let failure = session
            .complete(2, Completion::Read(Ok(Vec::new())), 60_000)
            .unwrap_err();
        assert!(failure.indeterminate && failure.mutation_started);
        assert!(failure.message.contains("timed out"));
        assert!(session.next(60_001).is_err());
    }
    #[test]
    fn backwards_clock_or_replayed_completion_cannot_advance_a_session() {
        let raw = image();
        let pkg = package::parse(&raw, Some("deadbeef12345678")).unwrap();
        let mut backwards = Session::new(&pkg, None, 10).unwrap();
        backwards.next(10).unwrap();
        assert!(
            backwards
                .complete(1, Completion::Write(Ok(65)), 9)
                .unwrap_err()
                .mutation_started
        );
        assert!(!backwards.active());
        let mut replayed = Session::new(&pkg, None, 0).unwrap();
        replayed.next(0).unwrap();
        replayed.complete(1, Completion::Write(Ok(65)), 1).unwrap();
        let error = replayed
            .complete(1, Completion::Write(Ok(65)), 2)
            .unwrap_err();
        assert!(error.message.contains("Unexpected action acknowledgement"));
        assert!(!replayed.active());
    }
    #[test]
    fn successful_phase_ending_replies_and_payload_writes_cannot_extend_deadlines() {
        let raw = image();
        let pkg = package::parse(&raw, Some("deadbeef12345678")).unwrap();
        for (stage, phase, deadline, completion) in [
            (
                Stage::PrepDone,
                "preparation",
                60_000,
                Completion::Read(Ok(command(b"SFIFDONE")[1..].to_vec())),
            ),
            (
                Stage::Ready2,
                "ready2",
                60_000,
                Completion::Read(Ok(command(b"FWSETRD2")[1..].to_vec())),
            ),
            (
                Stage::DelayReady,
                "checksums",
                60_000,
                Completion::Read(Ok(command(b"FWDLYRDY")[1..].to_vec())),
            ),
            (
                Stage::Application(448),
                "application",
                180_000,
                Completion::Write(Ok(65)),
            ),
        ] {
            let mut session = Session::new(&pkg, None, 0).unwrap();
            session.stage = stage;
            session.phase = phase;
            session.deadline = deadline;
            session.mutation_started = true;
            let action = session.next(1).unwrap();
            let id = match action {
                Action::Read { id, .. } | Action::Write { id, .. } => id,
                _ => panic!("Expected read/write"),
            };
            let failure = session.complete(id, completion, deadline).unwrap_err();
            assert_eq!(failure.phase, phase);
            assert!(failure.indeterminate);
            assert!(failure.message.contains("timed out"));
            assert!(!session.active());
            assert!(session.next(deadline + 1).is_err());
        }
    }
    #[test]
    fn expired_delay_cannot_refresh_a_phase_but_confirmed_activation_stays_uncertain() {
        let raw = image();
        let pkg = package::parse(&raw, Some("deadbeef12345678")).unwrap();
        for (phase, next) in [
            ("preparation", Stage::Metadata),
            ("ready2", Stage::Application(0)),
            ("checksums", Stage::Activation(0)),
        ] {
            let mut session = Session::new(&pkg, None, 0).unwrap();
            session.phase = phase;
            session.deadline = 60_000;
            session.mutation_started = true;
            session.delay(next);
            let action = session.next(59_999).unwrap();
            let Action::Sleep { id, .. } = action else {
                panic!("Expected delay");
            };
            let error = session.complete(id, Completion::Sleep, 60_000).unwrap_err();
            assert_eq!(error.phase, phase);
            assert!(error.indeterminate);
            assert!(!session.active());
        }
        let mut activation = Session::new(&pkg, None, 0).unwrap();
        activation.phase = "activation";
        activation.deadline = 10_000;
        activation.mutation_started = true;
        activation.outcome.activation_writes = 1;
        activation.delay(Stage::Activation(1));
        let Action::Sleep { id, .. } = activation.next(9_999).unwrap() else {
            panic!("Expected delay");
        };
        let Action::Done(outcome) = activation.complete(id, Completion::Sleep, 10_000).unwrap()
        else {
            panic!("Expected uncertainty");
        };
        assert_eq!(outcome.activation_writes, 1);
        assert!(outcome.warning.is_some());
        assert!(outcome.awaiting_application_verification);
    }
}

#[derive(Debug)]
pub enum Completion {
    Write(Result<usize, ()>),
    Read(Result<Vec<u8>, ()>),
    Sleep,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Stage {
    Preparation,
    PrepMarker,
    PrepStatus,
    PrepDone,
    Metadata,
    DumpMetadata,
    MetaBegin,
    Hardware,
    MetaWord(usize),
    MetaEnd,
    MetaComplete,
    Transfer,
    Ready1,
    Parameter(usize),
    Ready2,
    Application(usize),
    FinishPayload,
    TransferMarker,
    TransferCrc,
    ExternalMarker,
    ExternalCrc,
    DelayReady,
    DecodeMarker,
    DecodeCrc,
    DecodeReady,
    Activation(usize),
    Delay,
    Done,
}

pub struct Session {
    application: Vec<u8>,
    parameters: [u32; 7],
    stock: bool,
    hardware: u32,
    expected_uid: Option<String>,
    expected_decoded_crc: u32,
    crc: u32,
    stage: Stage,
    after_delay: Option<Stage>,
    pending: Option<Action>,
    next_id: u32,
    phase: &'static str,
    deadline: u64,
    reads_left: usize,
    last_now: u64,
    mutation_started: bool,
    completed: usize,
    ready_extras: usize,
    metadata: [u32; 14],
    reported_hardware: u32,
    outcome: TransferOutcome,
    progress: Progress,
    events: Vec<Progress>,
    failed: Option<ProtocolError>,
}

impl Session {
    pub fn new(
        pkg: &Package<'_>,
        expected_uid: Option<&str>,
        now: u64,
    ) -> Result<Self, ProtocolError> {
        let preflight = |message: &str| ProtocolError {
            phase: "preflight",
            message: message.into(),
            mutation_started: false,
            indeterminate: false,
        };
        if !pkg.approved_for_transfer() {
            return Err(preflight("Transfer requires a checked THY666 build or the exact supported original stock package"));
        }
        if expected_uid
            .is_some_and(|uid| uid.len() != 24 || !uid.bytes().all(|b| b.is_ascii_hexdigit()))
        {
            return Err(preflight(
                "Expected UID must contain exactly 24 hexadecimal characters",
            ));
        }
        let mut value = Self {
            application: pkg.application().to_vec(),
            parameters: pkg.parameters(),
            stock: pkg.is_stock(),
            hardware: pkg.header.required_hardware_version,
            expected_uid: expected_uid.map(str::to_owned),
            expected_decoded_crc: pkg.header.metadata_word_1,
            crc: package::crc32_normal(pkg.application(), 0),
            stage: Stage::Preparation,
            after_delay: None,
            pending: None,
            next_id: 0,
            phase: "preflight",
            deadline: now,
            reads_left: 0,
            last_now: now,
            mutation_started: false,
            completed: 0,
            ready_extras: 0,
            metadata: [0; 14],
            reported_hardware: 0,
            outcome: TransferOutcome {
                activation_writes: 0,
                warning: None,
                awaiting_application_verification: true,
                transferred_crc: 0,
                external_crc: 0,
                decoded_crc: None,
                metadata_uid_verified: expected_uid.is_some(),
                target_uid: String::new(),
            },
            progress: Progress {
                phase: "preflight",
                completed: 0,
                total: pkg.application().len(),
                message: String::new(),
                state: "updating",
            },
            events: Vec::new(),
            failed: None,
        };
        value.transition(Stage::Preparation, now);
        Ok(value)
    }

    pub fn progress(&self) -> &Progress {
        &self.progress
    }
    pub fn drain_progress(&mut self) -> Vec<Progress> {
        std::mem::take(&mut self.events)
    }
    pub fn active(&self) -> bool {
        self.stage != Stage::Done && self.failed.is_none()
    }
    pub fn abort(&mut self, message: &str) -> ProtocolError {
        let error = self.error(message);
        self.failed = Some(error.clone());
        self.pending = None;
        error
    }
    pub fn error(&self, message: impl Into<String>) -> ProtocolError {
        ProtocolError {
            phase: self.phase,
            message: message.into(),
            mutation_started: self.mutation_started,
            indeterminate: self.mutation_started,
        }
    }
    fn fail<T>(&mut self, message: impl Into<String>) -> Result<T, ProtocolError> {
        let error = self.error(message);
        self.failed = Some(error.clone());
        self.pending = None;
        Err(error)
    }
    fn clock(&mut self, now: u64) -> Result<(), ProtocolError> {
        if let Some(error) = self.failed.clone() {
            return Err(error);
        }
        if now < self.last_now {
            return self.fail("Updater clock moved backwards; no further action is allowed");
        }
        self.last_now = now;
        Ok(())
    }
    fn emit(&mut self, state: &'static str, message: &str) {
        self.progress = Progress {
            phase: self.phase,
            completed: self.completed,
            total: self.application.len(),
            message: message.into(),
            state,
        };
        self.events.push(self.progress.clone());
    }
    fn phase(&mut self, name: &'static str, timeout: u64, message: &str, now: u64) {
        self.phase = name;
        self.deadline = now.saturating_add(timeout);
        self.reads_left = (timeout / u64::from(READ_POLL_MS)).max(1) as usize;
        self.emit("updating", message);
    }
    fn transition(&mut self, next: Stage, now: u64) {
        self.stage = next;
        match next {
            Stage::Preparation => self.phase(
                "preparation",
                60_000,
                "Preparing device memory; external contents may change before UID verification",
                now,
            ),
            Stage::Metadata => self.phase(
                "metadata",
                30_000,
                "Checking hardware metadata after preparation",
                now,
            ),
            Stage::Transfer => self.phase("ready1", 30_000, "Starting application transfer", now),
            Stage::Ready2 => self.phase(
                "ready2",
                60_000,
                "Waiting for the device to accept application data",
                now,
            ),
            Stage::Application(0) => self.phase(
                "application",
                180_000,
                "Transferring application reports",
                now,
            ),
            Stage::FinishPayload => self.phase(
                "checksums",
                60_000,
                "Checking transferred and external CRC replies",
                now,
            ),
            Stage::DecodeMarker => self.phase(
                "decoding",
                60_000,
                "Checking the original stock decoded CRC reply",
                now,
            ),
            Stage::Activation(0) => self.phase(
                "activation",
                10_000,
                "Requesting activation; application verification is still required",
                now,
            ),
            Stage::Done => {
                self.phase = "awaiting-application-verification";
                self.emit("awaiting-verification", if self.stock { "Transfer and decoded CRCs match; verify the stock USB return and device behavior" } else { "Transfer CRCs match; verify the running application build and identity" });
            }
            _ => {}
        }
    }
    fn delay(&mut self, next: Stage) {
        self.after_delay = Some(next);
        self.stage = Stage::Delay;
    }

    pub fn next(&mut self, now: u64) -> Result<Action, ProtocolError> {
        self.clock(now)?;
        if self.pending.is_some() {
            return self.fail("An updater action is already outstanding");
        }
        if self.stage == Stage::Done {
            return Ok(Action::Done(self.outcome.clone()));
        }
        // Delays also remain inside their originating phase. A suspended host
        // must not resume a continuation and obtain a fresh phase deadline.
        if now >= self.deadline {
            if self.phase == "activation" && self.outcome.activation_writes > 0 {
                self.activation_uncertain(now);
                return Ok(Action::Done(self.outcome.clone()));
            }
            return self.fail("Phase timed out; do not automatically retry");
        }
        self.next_id = self
            .next_id
            .checked_add(1)
            .ok_or_else(|| self.error("Updater action limit exceeded"))?;
        let id = self.next_id;
        let action = if self.stage == Stage::Delay {
            Action::Sleep {
                id,
                ms: HOST_DELAY_MS,
            }
        } else {
            let token = match self.stage {
                Stage::Preparation => Some(b"XCFIFCFF"),
                Stage::Metadata => Some(b"XCFILNFS"),
                Stage::DumpMetadata => Some(b"DUMPMETA"),
                Stage::MetaComplete => Some(b"DONECMDS"),
                Stage::Transfer => Some(if self.stock { b"XCSMLRTF" } else { b"XCSMDIUS" }),
                Stage::FinishPayload => Some(b"DONE0000"),
                Stage::Activation(_) => Some(b"ZCSMCCOZ"),
                _ => None,
            };
            if let Some(token) = token {
                if self.stage == Stage::Preparation {
                    self.mutation_started = true;
                }
                Action::Write {
                    id,
                    report: command(token),
                }
            } else if let Stage::Parameter(index) = self.stage {
                let mut report = [0; 65];
                report[1..5].copy_from_slice(&self.parameters[index].to_le_bytes());
                Action::Write { id, report }
            } else if let Stage::Application(offset) = self.stage {
                let mut report = [0; 65];
                let count = (self.application.len() - offset).min(64);
                report[1..1 + count].copy_from_slice(&self.application[offset..offset + count]);
                Action::Write { id, report }
            } else {
                if self.reads_left == 0 {
                    return self.fail("Phase exhausted its bounded read window");
                }
                self.reads_left -= 1;
                Action::Read {
                    id,
                    timeout_ms: (self.deadline - now).clamp(1, u64::from(READ_POLL_MS)) as u32,
                }
            }
        };
        self.pending = Some(action.clone());
        Ok(action)
    }

    pub fn complete(
        &mut self,
        id: u32,
        result: Completion,
        now: u64,
    ) -> Result<Action, ProtocolError> {
        self.clock(now)?;
        let action = self
            .pending
            .take()
            .ok_or_else(|| self.error("No updater action is outstanding"))?;
        let matches = match (&action, &result) {
            (Action::Write { id: pending, .. }, Completion::Write(_))
            | (Action::Read { id: pending, .. }, Completion::Read(_))
            | (Action::Sleep { id: pending, .. }, Completion::Sleep) => *pending == id,
            _ => false,
        };
        if !matches {
            return self.fail("Unexpected action acknowledgement; no retry is allowed");
        }
        // Validate the old phase before interpreting a successful reply, write
        // acknowledgement or timer completion: processing any of those can
        // transition phases and would otherwise erase evidence of the timeout.
        if now >= self.deadline {
            if self.phase == "activation" && self.outcome.activation_writes > 0 {
                self.activation_uncertain(now);
                return Ok(Action::Done(self.outcome.clone()));
            }
            return self.fail("Phase timed out; do not automatically retry");
        }
        match result {
            Completion::Sleep => {
                let next = self
                    .after_delay
                    .take()
                    .ok_or_else(|| self.error("Missing updater delay continuation"))?;
                self.transition(next, now);
            }
            Completion::Write(result) => match result {
                Ok(65) => self.written(now)?,
                other => {
                    if matches!(self.stage, Stage::Activation(_))
                        && self.outcome.activation_writes > 0
                    {
                        self.activation_uncertain(now);
                    } else {
                        return self.fail(if other.is_err() {
                            "HID write failed; delivery is uncertain; do not retry"
                        } else {
                            "Incomplete HID write; delivery is uncertain; do not retry"
                        });
                    }
                }
            },
            Completion::Read(result) => {
                let payload = match result {
                    Ok(value) => value,
                    Err(()) => return self.fail("HID read failed; do not automatically retry"),
                };
                if !payload.is_empty() {
                    let data: [u8; 64] = match payload.try_into() {
                        Ok(value) => value,
                        Err(_) => return self.fail("Expected exactly 64 input payload bytes"),
                    };
                    self.received(data, now)?;
                }
            }
        }
        self.next(now)
    }

    fn activation_uncertain(&mut self, now: u64) {
        self.outcome.warning = Some("HID disappeared or activation delivery became uncertain after an activation request; verify the application before any further update".into());
        self.transition(Stage::Done, now);
    }
    fn written(&mut self, now: u64) -> Result<(), ProtocolError> {
        let next = match self.stage {
            Stage::Preparation => Stage::PrepMarker,
            Stage::Metadata => {
                self.delay(Stage::DumpMetadata);
                return Ok(());
            }
            Stage::DumpMetadata => Stage::MetaBegin,
            Stage::MetaComplete => {
                if self.reported_hardware != self.hardware {
                    return self.fail("Hardware version mismatch; memory preparation may already have changed contents");
                }
                self.outcome.target_uid = format!(
                    "{:08X}{:08X}{:08X}",
                    self.metadata[11], self.metadata[12], self.metadata[13]
                );
                if self
                    .expected_uid
                    .as_ref()
                    .is_some_and(|uid| !self.outcome.target_uid.eq_ignore_ascii_case(uid))
                {
                    return self.fail(
                        "Device UID mismatch; memory preparation may already have changed contents",
                    );
                }
                Stage::Transfer
            }
            Stage::Transfer => Stage::Ready1,
            Stage::Parameter(index) => {
                if index < 6 {
                    Stage::Parameter(index + 1)
                } else {
                    Stage::Ready2
                }
            }
            Stage::Application(offset) => {
                self.completed = (offset + 64).min(self.application.len());
                self.emit("updating", "Transferring application reports");
                if self.completed == self.application.len() {
                    Stage::FinishPayload
                } else {
                    Stage::Application(self.completed)
                }
            }
            Stage::FinishPayload => Stage::TransferMarker,
            Stage::Activation(attempt) => {
                self.outcome.activation_writes += 1;
                if attempt < 4 {
                    self.delay(Stage::Activation(attempt + 1));
                    return Ok(());
                }
                Stage::Done
            }
            _ => return self.fail("Unexpected write completion"),
        };
        self.transition(next, now);
        Ok(())
    }
    fn integer(&mut self, data: &[u8; 64]) -> Result<u32, ProtocolError> {
        if [
            b"FILSTATU",
            b"SFIFDONE",
            b"FCMDDONE",
            b"FWSETRD1",
            b"FWSETRD2",
            b"TRACRCVL",
            b"EXTCRCVL",
            b"FWDLYRDY",
            b"DCRCRCVL",
            b"FWDCPRDY",
        ]
        .iter()
        .any(|token| signed(data, token))
        {
            return self.fail("Received a signed marker where an integer was required");
        }
        Ok(u32::from_le_bytes(data[..4].try_into().unwrap()))
    }
    fn received(&mut self, data: [u8; 64], now: u64) -> Result<(), ProtocolError> {
        let marker = match self.stage {
            Stage::PrepMarker => Some((b"FILSTATU", Stage::PrepStatus, false)),
            Stage::PrepDone => Some((b"SFIFDONE", Stage::Metadata, false)),
            Stage::MetaBegin => Some((b"FWMETABG", Stage::Hardware, true)),
            Stage::MetaEnd => Some((b"FWMETADN", Stage::MetaComplete, true)),
            Stage::Ready2 => Some((b"FWSETRD2", Stage::Application(0), false)),
            Stage::TransferMarker => Some((b"TRACRCVL", Stage::TransferCrc, false)),
            Stage::ExternalMarker => Some((b"EXTCRCVL", Stage::ExternalCrc, false)),
            Stage::DelayReady => Some((
                b"FWDLYRDY",
                if self.stock {
                    Stage::DecodeMarker
                } else {
                    Stage::Activation(0)
                },
                false,
            )),
            Stage::DecodeMarker => Some((b"DCRCRCVL", Stage::DecodeCrc, false)),
            Stage::DecodeReady => Some((b"FWDCPRDY", Stage::Activation(0), false)),
            _ => None,
        };
        if let Some((token, next, prefix)) = marker {
            if !(if prefix {
                data[..8] == token[..]
            } else {
                signed(&data, token)
            }) {
                return self.fail("Unexpected or malformed readiness/completion report");
            }
            if prefix {
                self.transition(next, now);
            } else {
                self.delay(next);
            }
            return Ok(());
        }
        let next = match self.stage {
            Stage::PrepStatus => {
                if self.integer(&data)? != 1 {
                    return self.fail("Unrecognized preparation status; memory preparation may already have changed contents");
                }
                Stage::PrepDone
            }
            Stage::Hardware => {
                self.reported_hardware = self.integer(&data)?;
                Stage::MetaWord(0)
            }
            Stage::MetaWord(index) => {
                self.metadata[index] = self.integer(&data)?;
                if index < 13 {
                    Stage::MetaWord(index + 1)
                } else {
                    Stage::MetaEnd
                }
            }
            Stage::Ready1 => {
                if signed(&data, b"FWSETRD1") {
                    self.delay(Stage::Parameter(0));
                    return Ok(());
                }
                if signed(&data, b"FCMDDONE") && self.ready_extras < 2 {
                    self.ready_extras += 1;
                    self.delay(Stage::Ready1);
                    return Ok(());
                }
                return self.fail("Unexpected report while waiting for FWSETRD1");
            }
            Stage::TransferCrc => {
                self.outcome.transferred_crc = self.integer(&data)?;
                Stage::ExternalMarker
            }
            Stage::ExternalCrc => {
                self.outcome.external_crc = self.integer(&data)?;
                if self.outcome.transferred_crc != self.crc || self.outcome.external_crc != self.crc
                {
                    return self.fail("Device CRC replies differ from the application payload; activation was not requested");
                }
                Stage::DelayReady
            }
            Stage::DecodeCrc => {
                let value = self.integer(&data)?;
                if value != self.expected_decoded_crc {
                    return self.fail("Decoded stock CRC differs from the recorded package; activation was not requested");
                }
                self.outcome.decoded_crc = Some(value);
                Stage::DecodeReady
            }
            _ => return self.fail("Unexpected input report"),
        };
        self.transition(next, now);
        Ok(())
    }
}
