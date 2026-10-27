//! Offline configuration-file integrity checks. This is not raw flash readback.
use std::fmt;

#[derive(Clone, Debug)]
pub struct BackupInfo {
    pub uid: String,
    pub build: String,
    pub payload_bytes: usize,
    pub payload_crc: u32,
}

pub fn ieee_crc(bytes: &[u8]) -> u32 {
    let mut crc = !0u32;
    for byte in bytes {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320 & 0u32.wrapping_sub(crc & 1));
        }
    }
    !crc
}

#[derive(Debug)]
pub struct BackupError(pub &'static str);
impl fmt::Display for BackupError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.0)
    }
}
impl std::error::Error for BackupError {}

pub fn inspect(bytes: &[u8]) -> Result<BackupInfo, BackupError> {
    if bytes.len() < 65 || bytes.len() > 13_889 || &bytes[..4] != b"THYB" {
        return Err(BackupError("Not a complete THY666 configuration backup"));
    }
    let u16_at = |at| u16::from_le_bytes(bytes[at..at + 2].try_into().unwrap());
    let u32_at = |at| u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap());
    if u16_at(4) != 1
        || u16_at(6) != 64
        || u16_at(16) != 13
        || u16_at(18) != 0x413
        || u32_at(20) != 0x10001
        || u32_at(52) != 1
        || u32_at(56) != 0
    {
        return Err(BackupError(
            "Unsupported configuration backup version or target",
        ));
    }
    if u32_at(8) as usize != bytes.len() - 64
        || u32_at(60) != ieee_crc(&bytes[..60])
        || u32_at(12) != ieee_crc(&bytes[64..])
    {
        return Err(BackupError("Backup length or checksum mismatch"));
    }
    let hex = |part: &[u8]| part.iter().map(|b| format!("{b:02x}")).collect::<String>();
    Ok(BackupInfo {
        uid: hex(&bytes[24..36]),
        build: hex(&bytes[36..44]),
        payload_bytes: bytes.len() - 64,
        payload_crc: u32_at(12),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn backup_crc_is_distinct_from_bootloader_crc() {
        assert_eq!(ieee_crc(b"123456789"), 0xcbf4_3926);
        assert_ne!(
            ieee_crc(b"123456789"),
            crate::package::crc32_normal(b"123456789", 0)
        );
    }
    #[test]
    fn checks_both_checksums_before_returning_identity() {
        let mut b = vec![0; 65];
        b[..4].copy_from_slice(b"THYB");
        for (at, v) in [(4, 1u16), (6, 64), (16, 13), (18, 0x413)] {
            b[at..at + 2].copy_from_slice(&v.to_le_bytes());
        }
        for (at, v) in [(8, 1u32), (20, 0x10001), (52, 1)] {
            b[at..at + 4].copy_from_slice(&v.to_le_bytes());
        }
        let crc = ieee_crc(&b[64..]);
        b[12..16].copy_from_slice(&crc.to_le_bytes());
        let crc = ieee_crc(&b[..60]);
        b[60..64].copy_from_slice(&crc.to_le_bytes());
        assert_eq!(inspect(&b).unwrap().uid, "000000000000000000000000");
        b[64] = 1;
        assert!(inspect(&b).is_err());
        assert!(inspect(&b[..20]).is_err());
    }
}
