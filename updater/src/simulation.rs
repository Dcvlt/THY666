//! Explicit synthetic transport for host tests and `--simulate`. Never opens USB.
use crate::{
    package::Package,
    protocol::{Transport, SIGNATURE},
};
use std::collections::VecDeque;

pub const UID: &str = "0123456789abcdef01234567";
pub struct SimulatedTransport {
    replies: VecDeque<Vec<u8>>,
    activation: usize,
    pub writes: usize,
}
impl SimulatedTransport {
    pub fn new(package: &Package<'_>) -> Self {
        let mut replies = VecDeque::new();
        let marker = |token: &str| {
            let mut b = token.as_bytes().to_vec();
            b.extend_from_slice(SIGNATURE);
            b
        };
        let integer = |value: u32| {
            let mut b = vec![0; 64];
            b[..4].copy_from_slice(&value.to_le_bytes());
            b
        };
        replies.push_back(marker("FILSTATU"));
        replies.push_back(integer(1));
        replies.push_back(marker("SFIFDONE"));
        replies.push_back(marker("FWMETABG"));
        replies.push_back(integer(4));
        for value in [
            1, 1, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0x01234567, 0x89abcdef, 0x01234567,
        ] {
            replies.push_back(integer(value));
        }
        replies.push_back(marker("FWMETADN"));
        replies.push_back(marker("FCMDDONE"));
        replies.push_back(marker("FCMDDONE"));
        replies.push_back(marker("FWSETRD1"));
        replies.push_back(marker("FWSETRD2"));
        replies.push_back(marker("TRACRCVL"));
        replies.push_back(integer(package.info.crc32));
        replies.push_back(marker("EXTCRCVL"));
        replies.push_back(integer(package.info.crc32));
        replies.push_back(marker("FWDLYRDY"));
        if package.is_stock() {
            replies.push_back(marker("DCRCRCVL"));
            replies.push_back(integer(package.header.metadata_word_1));
            replies.push_back(marker("FWDCPRDY"));
        }
        Self {
            replies,
            activation: 0,
            writes: 0,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{package, protocol};

    #[test]
    fn stock_simulation_includes_decode_verification_without_physical_usb() {
        let payload: Vec<u8> = (0..112).map(|i| (i * 37 + 11) as u8).collect();
        let package = package::synthetic_stock_package(&payload);
        let mut transport = SimulatedTransport::new(&package);
        let outcome = protocol::runpkg(&package, &mut transport, Some(UID), |_| {}).unwrap();
        assert_eq!(outcome.decoded_crc, Some(package.header.metadata_word_1));
        assert!(outcome.awaiting_application_verification);
        assert_eq!(outcome.activation_writes, 2);
        assert!(outcome.warning.is_some());
        assert!(transport.replies.is_empty());
    }
}
impl Transport for SimulatedTransport {
    fn write_report(&mut self, bytes: &[u8; 65]) -> Result<usize, String> {
        self.writes += 1;
        if bytes[1..9] == *b"ZCSMCCOZ" {
            self.activation += 1;
            if self.activation > 2 {
                return Err("Simulated reboot disconnect".into());
            }
        }
        Ok(65)
    }
    fn read_report(&mut self, _: u32) -> Result<Vec<u8>, String> {
        Ok(self.replies.pop_front().unwrap_or_default())
    }
    fn sleep(&mut self, _: u64) {}
}
