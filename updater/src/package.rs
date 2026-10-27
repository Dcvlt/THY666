//! Inspection of the supported THY666 profile and one exact original v5 image.
//!
//! The recovered header also describes encrypted vendor images, scale updates
//! and filesystem preparation. Encrypted input is accepted only when the entire
//! file matches the recorded original v5 digest and header. Ciphertext is never
//! interpreted as vectors or searched for a THY666 application build string.
//! Calculated CRC/SHA values support integrity checks; they do not authenticate
//! an author or establish bootloader/recovery qualification.

use std::collections::BTreeSet;
use std::fmt;

pub const HEADER_BYTES: usize = 78;
pub const REPORT_BYTES: usize = 64;
pub const VECTOR_WORDS: usize = 98;
pub const VECTOR_BYTES: usize = VECTOR_WORDS * 4;
pub const APPLICATION_BASE: u32 = 0x0804_0000;
pub const SETTINGS_BASE: u32 = 0x080c_0000;
pub const APPLICATION_BYTES_MAX: usize = (SETTINGS_BASE - APPLICATION_BASE) as usize;
pub const PACKAGE_BYTES_MAX: usize = HEADER_BYTES + APPLICATION_BYTES_MAX;
pub const OBSERVED_METADATA: [u32; 3] = [0xebf3_a7f7, 0x429b_51d6, 0xaa01_7d8c];
pub const STOCK_V5_SHA256: &str =
    "125e18005b4e6f797d2b300649f5207d3e7a6f4b2e0af5e3000234b4f4efe990";
pub const STOCK_V5_APPLICATION_SHA256: &str =
    "8587d676318e73b442d52d86988579549cf2e38627583b13aaad3d3915f90b0f";
pub const STOCK_V5_APPLICATION_BYTES: usize = 133_552;
pub const STOCK_V5_FILE_BYTES: usize = HEADER_BYTES + STOCK_V5_APPLICATION_BYTES;

const FIELDS: [(usize, usize, &str); 11] = [
    (6, 6, "firmware_enabled"),
    (12, 6, "scales_enabled"),
    (18, 6, "filesystem_prepare_enabled"),
    (24, 6, "firmware_start"),
    (30, 6, "firmware_end"),
    (36, 8, "metadata_word_0"),
    (44, 8, "metadata_word_1"),
    (52, 8, "metadata_word_2"),
    (60, 6, "required_hardware_version"),
    (66, 6, "firmware_encrypted"),
    (72, 6, "scales_encrypted"),
];

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PackageError {
    MissingHeader,
    NonHexField(&'static str),
    ApplicationBounds,
    UnsupportedFormat(&'static str),
    PayloadLimit,
    UnalignedPayload,
    InvalidStack(u32),
    InvalidVector { index: usize, pointer: u32 },
    InvalidBuildId,
    AmbiguousBuildId,
    BuildIdMismatch,
}

impl fmt::Display for PackageError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MissingHeader => write!(f, "Missing complete THY333 header"),
            Self::NonHexField(name) => write!(f, "Non-hexadecimal header field {name}"),
            Self::ApplicationBounds => write!(f, "Application bounds leave the payload or are reversed"),
            Self::UnsupportedFormat(reason) => write!(f, "Unsupported .333 package: {reason}"),
            Self::PayloadLimit => write!(f, "Application must contain the complete F405 vector table and fit below the settings partition"),
            Self::UnalignedPayload => write!(f, "Application length must be a multiple of 64 bytes; package it with explicit deterministic padding"),
            Self::InvalidStack(_) => write!(f, "Initial stack must be 8-byte aligned inside SRAM or CCM"),
            Self::InvalidVector { index, .. } => write!(f, "Vector {index} must be Thumb code after the vector table and entirely inside the application"),
            Self::InvalidBuildId => write!(f, "Expected build ID must be exactly 16 lowercase hexadecimal characters"),
            Self::AmbiguousBuildId => write!(f, "Application contains more than one possible THY666 build ID; identity is ambiguous"),
            Self::BuildIdMismatch => write!(f, "Application build ID differs from the expected build"),
        }
    }
}

impl std::error::Error for PackageError {}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Header {
    pub firmware_enabled: u32,
    pub scales_enabled: u32,
    pub filesystem_prepare_enabled: u32,
    pub firmware_start: u32,
    pub firmware_end: u32,
    pub metadata_word_0: u32,
    pub metadata_word_1: u32,
    pub metadata_word_2: u32,
    pub required_hardware_version: u32,
    pub firmware_encrypted: u32,
    pub scales_encrypted: u32,
}

impl Header {
    /// Parse fixed-width hexadecimal fields only, as `thy333_container.py` does.
    /// This does not establish that the described package is supported.
    pub fn parse(data: &[u8]) -> Result<Self, PackageError> {
        if data.len() < HEADER_BYTES || &data[..6] != b"THY333" {
            return Err(PackageError::MissingHeader);
        }
        let mut values = [0u32; 11];
        for (value, (offset, width, name)) in values.iter_mut().zip(FIELDS) {
            for &byte in &data[offset..offset + width] {
                let digit = match byte {
                    b'0'..=b'9' => byte - b'0',
                    b'a'..=b'f' => byte - b'a' + 10,
                    b'A'..=b'F' => byte - b'A' + 10,
                    _ => return Err(PackageError::NonHexField(name)),
                };
                *value = (*value << 4) | u32::from(digit);
            }
        }
        let header = Self {
            firmware_enabled: values[0],
            scales_enabled: values[1],
            filesystem_prepare_enabled: values[2],
            firmware_start: values[3],
            firmware_end: values[4],
            metadata_word_0: values[5],
            metadata_word_1: values[6],
            metadata_word_2: values[7],
            required_hardware_version: values[8],
            firmware_encrypted: values[9],
            scales_encrypted: values[10],
        };
        if header.firmware_start > header.firmware_end
            || header.firmware_end as usize > data.len() - HEADER_BYTES
        {
            return Err(PackageError::ApplicationBounds);
        }
        Ok(header)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PackageInfo {
    pub kind: &'static str,
    /// Supported transfer policy, separate from THY666's embedded build check.
    pub installable: bool,
    pub file_bytes: usize,
    pub file_sha256: String,
    pub application_bytes: usize,
    pub application_sha256: String,
    pub crc32: u32,
    pub report_count: usize,
    /// Zero means unavailable for the encoded stock image, not a decoded vector.
    pub initial_msp: u32,
    /// Zero means unavailable for the encoded stock image, not a decoded vector.
    pub reset_handler: u32,
    pub build_id: Option<String>,
    /// A caller-supplied expected ID was checked against the embedded ID.
    /// This is not a signature or proof of source/build review.
    pub build_identity_verified: bool,
}

#[derive(Debug)]
pub struct Package<'a> {
    pub header: Header,
    pub info: PackageInfo,
    application: &'a [u8],
    profile: Profile,
}

#[derive(Debug, Eq, PartialEq)]
enum Profile {
    Thy666,
    StockV5,
}

impl Package<'_> {
    pub fn application(&self) -> &[u8] {
        self.application
    }

    pub fn is_stock(&self) -> bool {
        self.profile == Profile::StockV5
    }

    /// Only a bound THY666 build or the exact allowlisted stock artifact can be
    /// transferred. Stock has no readable THY666 build identity to verify.
    pub fn approved_for_transfer(&self) -> bool {
        self.is_stock() || self.info.build_identity_verified
    }

    /// Seven little-endian integer report values in recovered host order.
    /// All three header metadata words remain opaque and are preserved exactly.
    pub fn parameters(&self) -> [u32; 7] {
        [
            self.header.metadata_word_0,
            self.header.metadata_word_1,
            self.application.len() as u32,
            self.header.metadata_word_2,
            1,
            0,
            1,
        ]
    }
}

fn stock_header() -> Header {
    Header {
        firmware_enabled: 1,
        scales_enabled: 0,
        filesystem_prepare_enabled: 1,
        firmware_start: 0,
        firmware_end: STOCK_V5_APPLICATION_BYTES as u32,
        metadata_word_0: OBSERVED_METADATA[0],
        metadata_word_1: OBSERVED_METADATA[1],
        metadata_word_2: OBSERVED_METADATA[2],
        required_hardware_version: 4,
        firmware_encrypted: 1,
        scales_encrypted: 1,
    }
}

fn stock_profile_matches(header: &Header, file_bytes: usize, file_sha256: &str) -> bool {
    file_bytes == STOCK_V5_FILE_BYTES && file_sha256 == STOCK_V5_SHA256 && header == &stock_header()
}

/// Inspect a bounded supported package without any I/O or device access.
///
/// An expected ID is optional for inspection. Transfer policy can require
/// `approved_for_transfer()` before proceeding. The exact stock image instead
/// uses whole-file allowlisting and rejects a THY666 build expectation.
/// A `.333` file has no signed
/// manifest; the embedded ID alone cannot verify the build's source provenance.
pub fn parse<'a>(
    data: &'a [u8],
    expected_build: Option<&str>,
) -> Result<Package<'a>, PackageError> {
    let header = Header::parse(data)?;
    if header.firmware_encrypted != 0 {
        let digest = sha256(data);
        if !stock_profile_matches(&header, data.len(), &digest) {
            return Err(PackageError::UnsupportedFormat(
                "encrypted input must exactly match the allowlisted original THY333 v5 package",
            ));
        }
        if expected_build.is_some() {
            return Err(PackageError::UnsupportedFormat(
                "a THY666 build expectation does not apply to the original stock package",
            ));
        }
        let application = &data[HEADER_BYTES..];
        let crc32 = crc32_normal(application, 0);
        if crc32 != OBSERVED_METADATA[0] || sha256(application) != STOCK_V5_APPLICATION_SHA256 {
            return Err(PackageError::UnsupportedFormat(
                "original stock application integrity differs",
            ));
        }
        let info = PackageInfo {
            kind: "stock",
            installable: true,
            file_bytes: data.len(),
            file_sha256: digest,
            application_bytes: application.len(),
            application_sha256: STOCK_V5_APPLICATION_SHA256.into(),
            crc32,
            report_count: application.len().div_ceil(REPORT_BYTES),
            initial_msp: 0,
            reset_handler: 0,
            build_id: None,
            build_identity_verified: false,
        };
        return Ok(Package {
            header,
            info,
            application,
            profile: Profile::StockV5,
        });
    }
    if header.firmware_enabled != 1 {
        return Err(PackageError::UnsupportedFormat(
            "exactly one application must be enabled",
        ));
    }
    if header.scales_enabled != 0 {
        return Err(PackageError::UnsupportedFormat(
            "scale updates are not supported",
        ));
    }
    if header.filesystem_prepare_enabled != 1 {
        return Err(PackageError::UnsupportedFormat(
            "filesystem preparation selector differs from the observed THY666 profile",
        ));
    }
    if [
        header.metadata_word_0,
        header.metadata_word_1,
        header.metadata_word_2,
    ] != OBSERVED_METADATA
    {
        return Err(PackageError::UnsupportedFormat(
            "opaque metadata differs from the observed THY666 profile",
        ));
    }
    if header.scales_encrypted > 1 {
        return Err(PackageError::UnsupportedFormat(
            "invalid inactive scale encryption selector",
        ));
    }
    if header.required_hardware_version != 4 {
        return Err(PackageError::UnsupportedFormat(
            "only observed hardware version 4 is supported",
        ));
    }
    if header.firmware_start != 0 || header.firmware_end as usize != data.len() - HEADER_BYTES {
        return Err(PackageError::UnsupportedFormat(
            "skipped prefixes or trailing sections are not supported",
        ));
    }
    let application = &data[HEADER_BYTES..];
    if !(VECTOR_BYTES..=APPLICATION_BYTES_MAX).contains(&application.len()) {
        return Err(PackageError::PayloadLimit);
    }
    if application.len() % REPORT_BYTES != 0 {
        return Err(PackageError::UnalignedPayload);
    }
    let crc32 = crc32_normal(application, 0);
    let word = |index: usize| {
        u32::from_le_bytes(application[index * 4..index * 4 + 4].try_into().unwrap())
    };
    let initial_msp = word(0);
    if !plausible_initial_stack(initial_msp) {
        return Err(PackageError::InvalidStack(initial_msp));
    }
    let end = APPLICATION_BASE + application.len() as u32;
    for index in 1..VECTOR_WORDS {
        let pointer = word(index);
        if pointer == 0 && index != 1 {
            continue;
        }
        let address = pointer & !1;
        if pointer & 1 == 0
            || address < APPLICATION_BASE + VECTOR_BYTES as u32
            || address.checked_add(2).is_none_or(|last| last > end)
        {
            return Err(PackageError::InvalidVector { index, pointer });
        }
    }
    let mut build_id = None;
    if let Some(expected) = expected_build {
        if !valid_build_id(expected.as_bytes()) {
            return Err(PackageError::InvalidBuildId);
        }
        // Existing firmware has no build-string marker and includes other hex
        // strings (such as digit lookup tables). Do not guess an ID. Binding
        // checks only the independently supplied ID, not source provenance.
        let mut needle = expected.as_bytes().to_vec();
        needle.push(0);
        let matches = application[VECTOR_BYTES..]
            .windows(needle.len())
            .filter(|window| *window == needle)
            .count();
        match matches {
            0 => return Err(PackageError::BuildIdMismatch),
            1 => build_id = Some(expected.to_owned()),
            _ => return Err(PackageError::AmbiguousBuildId),
        }
    }
    let info = PackageInfo {
        kind: "thy666",
        installable: expected_build.is_some(),
        file_bytes: data.len(),
        file_sha256: sha256(data),
        application_bytes: application.len(),
        application_sha256: sha256(application),
        crc32,
        report_count: application.len() / REPORT_BYTES,
        initial_msp,
        reset_handler: word(1),
        build_id,
        build_identity_verified: expected_build.is_some(),
    };
    Ok(Package {
        header,
        info,
        application,
        profile: Profile::Thy666,
    })
}

/// Unit-test-only construction exercises encrypted transport with synthetic
/// bytes. Production parsing has no digest bypass and never calls this helper.
#[cfg(test)]
pub(crate) fn synthetic_stock_package(application: &[u8]) -> Package<'_> {
    let mut header = stock_header();
    header.firmware_end = application.len() as u32;
    header.metadata_word_0 = crc32_normal(application, 0);
    Package {
        info: PackageInfo {
            kind: "stock",
            installable: true,
            file_bytes: HEADER_BYTES + application.len(),
            file_sha256: String::new(),
            application_bytes: application.len(),
            application_sha256: sha256(application),
            crc32: header.metadata_word_0,
            report_count: application.len().div_ceil(REPORT_BYTES),
            initial_msp: 0,
            reset_handler: 0,
            build_id: None,
            build_identity_verified: false,
        },
        header,
        application,
        profile: Profile::StockV5,
    }
}

pub fn plausible_initial_stack(stack: u32) -> bool {
    stack % 8 == 0
        && ((0x2000_0000 < stack && stack <= 0x2002_0000)
            || (0x1000_0000 < stack && stack <= 0x1001_0000))
}

fn valid_build_id(bytes: &[u8]) -> bool {
    bytes.len() == 16
        && bytes
            .iter()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(byte))
}

/// Recovered updater CRC: polynomial 0x04c11db7, no reflection/final XOR.
pub fn crc32_normal(data: &[u8], initial: u32) -> u32 {
    let mut crc = initial;
    for &byte in data {
        crc ^= u32::from(byte) << 24;
        for _ in 0..8 {
            crc = (crc << 1)
                ^ if crc & 0x8000_0000 != 0 {
                    0x04c1_1db7
                } else {
                    0
                };
        }
    }
    crc
}

/// SHA-256 for artifact identity (FIPS 180-4); no authentication is implied.
pub fn sha256(data: &[u8]) -> String {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2,
    ];
    fn block(state: &mut [u32; 8], input: &[u8]) {
        let mut w = [0u32; 64];
        for (i, bytes) in input.chunks_exact(4).enumerate() {
            w[i] = u32::from_be_bytes(bytes.try_into().unwrap());
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16]
                .wrapping_add(s0)
                .wrapping_add(w[i - 7])
                .wrapping_add(s1);
        }
        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h] = *state;
        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ (!e & g);
            let t1 = h
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K[i])
                .wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);
            h = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }
        for (sum, value) in state.iter_mut().zip([a, b, c, d, e, f, g, h]) {
            *sum = sum.wrapping_add(value);
        }
    }
    let mut state = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
        0x5be0cd19,
    ];
    let mut chunks = data.chunks_exact(64);
    for chunk in &mut chunks {
        block(&mut state, chunk);
    }
    let remainder = chunks.remainder();
    let mut tail = [0u8; 128];
    tail[..remainder.len()].copy_from_slice(remainder);
    tail[remainder.len()] = 0x80;
    let padded = if remainder.len() < 56 { 64 } else { 128 };
    tail[padded - 8..padded].copy_from_slice(&(data.len() as u64).wrapping_mul(8).to_be_bytes());
    for chunk in tail[..padded].chunks_exact(64) {
        block(&mut state, chunk);
    }
    state.iter().map(|word| format!("{word:08x}")).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    const ID: &str = "deadbeef12345678";

    fn application(size: usize) -> Vec<u8> {
        let mut app = vec![0u8; size];
        app[..4].copy_from_slice(&0x2002_0000u32.to_le_bytes());
        app[4..8].copy_from_slice(&(APPLICATION_BASE + VECTOR_BYTES as u32 + 1).to_le_bytes());
        if size >= 17 + VECTOR_BYTES {
            app[VECTOR_BYTES..VECTOR_BYTES + 16].copy_from_slice(ID.as_bytes());
        }
        app
    }

    fn container(app: &[u8]) -> Vec<u8> {
        let mut raw = format!(
            "THY333{:06X}{:06X}{:06X}{:06X}{:06X}{:08X}{:08X}{:08X}{:06X}{:06X}{:06X}",
            1,
            0,
            1,
            0,
            app.len(),
            OBSERVED_METADATA[0],
            OBSERVED_METADATA[1],
            OBSERVED_METADATA[2],
            4,
            0,
            0
        )
        .into_bytes();
        assert_eq!(raw.len(), HEADER_BYTES);
        raw.extend_from_slice(app);
        raw
    }

    fn field(raw: &mut [u8], index: usize, value: u32) {
        let (offset, width, _) = FIELDS[index];
        raw[offset..offset + width].copy_from_slice(format!("{value:0width$X}").as_bytes());
    }

    #[test]
    fn header_and_payload_match_python_contract() {
        let raw = container(&application(512));
        assert_eq!(&raw[..36], b"THY333000001000000000001000000000200");
        let package = parse(&raw, Some(ID)).unwrap();
        assert_eq!(package.application(), application(512));
        assert_eq!(package.info.file_bytes, 590);
        assert_eq!(package.info.report_count, 8);
        // Independently calculated with Python thy333_container.crc32_normal
        // and hashlib, including the exact fixed header and synthetic bytes.
        assert_eq!(package.info.crc32, 0x97af_210a);
        assert_eq!(
            package.info.application_sha256,
            "a6672b26f6416cb98df65e2898708c2a705d0768aee333aaede19921a81f9d34"
        );
        assert_eq!(
            package.info.file_sha256,
            "245784eaf6aa0b4ad9ca07a600ae05a83d35e9c0159032ef69147c392bef85c2"
        );
        assert_eq!(package.info.build_id.as_deref(), Some(ID));
        assert!(package.info.build_identity_verified);
        assert_eq!(package.info.kind, "thy666");
        assert!(package.info.installable && package.approved_for_transfer());
        assert_ne!(package.info.crc32, OBSERVED_METADATA[0]);
        assert_eq!(
            package.parameters(),
            [
                OBSERVED_METADATA[0],
                OBSERVED_METADATA[1],
                512,
                OBSERVED_METADATA[2],
                1,
                0,
                1
            ]
        );
        assert!(!parse(&raw, None).unwrap().info.build_identity_verified);
        assert!(!parse(&raw, None).unwrap().approved_for_transfer());
        assert_eq!(parse(&raw, None).unwrap().info.build_id, None);
    }

    #[test]
    fn malformed_fixed_header_and_bounds_fail_closed() {
        let good = container(&application(512));
        assert_eq!(Header::parse(&good[..77]), Err(PackageError::MissingHeader));
        for index in 0..FIELDS.len() {
            let mut raw = good.clone();
            raw[FIELDS[index].0] = b'?';
            assert!(matches!(
                Header::parse(&raw),
                Err(PackageError::NonHexField(_))
            ));
        }
        let mut raw = good.clone();
        field(&mut raw, 3, 513);
        assert_eq!(Header::parse(&raw), Err(PackageError::ApplicationBounds));
        field(&mut raw, 3, 0);
        field(&mut raw, 4, 513);
        assert_eq!(Header::parse(&raw), Err(PackageError::ApplicationBounds));
        let mut raw = good;
        raw[0] = b'X';
        assert_eq!(Header::parse(&raw), Err(PackageError::MissingHeader));
    }

    #[test]
    fn encrypted_scale_unknown_profile_prefix_and_tail_are_unsupported() {
        let good = container(&application(512));
        for (index, value) in [
            (0, 0),
            (0, 2),
            (1, 1),
            (2, 0),
            (2, 2),
            (3, 64),
            (5, 1),
            (6, 1),
            (7, 1),
            (8, 3),
            (9, 1),
            (10, 2),
        ] {
            let mut raw = good.clone();
            field(&mut raw, index, value);
            assert!(
                matches!(parse(&raw, None), Err(PackageError::UnsupportedFormat(_))),
                "field {index}"
            );
        }
        let mut tail = good.clone();
        tail.push(0);
        assert!(matches!(
            parse(&tail, None),
            Err(PackageError::UnsupportedFormat(_))
        ));
        let mut valid_inactive_selector = good;
        field(&mut valid_inactive_selector, 10, 1);
        assert_eq!(
            parse(&valid_inactive_selector, None)
                .unwrap()
                .header
                .scales_encrypted,
            1
        );
    }

    #[test]
    fn stock_allowlist_requires_the_complete_recorded_identity_and_header() {
        let header = stock_header();
        assert!(stock_profile_matches(
            &header,
            STOCK_V5_FILE_BYTES,
            STOCK_V5_SHA256
        ));
        assert!(!stock_profile_matches(
            &header,
            STOCK_V5_FILE_BYTES - 1,
            STOCK_V5_SHA256
        ));
        assert!(!stock_profile_matches(
            &header,
            STOCK_V5_FILE_BYTES,
            &"0".repeat(64)
        ));
        for index in 0..11 {
            let mut changed = header.clone();
            let fields = [
                &mut changed.firmware_enabled,
                &mut changed.scales_enabled,
                &mut changed.filesystem_prepare_enabled,
                &mut changed.firmware_start,
                &mut changed.firmware_end,
                &mut changed.metadata_word_0,
                &mut changed.metadata_word_1,
                &mut changed.metadata_word_2,
                &mut changed.required_hardware_version,
                &mut changed.firmware_encrypted,
                &mut changed.scales_encrypted,
            ];
            *fields.into_iter().nth(index).unwrap() ^= 1;
            assert!(!stock_profile_matches(
                &changed,
                STOCK_V5_FILE_BYTES,
                STOCK_V5_SHA256
            ));
        }
    }

    #[test]
    fn encrypted_lookalike_with_valid_plaintext_vectors_and_build_is_rejected() {
        let mut raw = container(&application(512));
        field(&mut raw, 9, 1);
        field(&mut raw, 10, 1);
        for expected in [None, Some(ID)] {
            assert!(matches!(
                parse(&raw, expected),
                Err(PackageError::UnsupportedFormat(_))
            ));
        }
        // A same-sized synthetic image with matching fields is still not the
        // recorded artifact. No original bytes are committed as a test fixture.
        let mut same_sized = container(&vec![0xa5; STOCK_V5_APPLICATION_BYTES]);
        field(&mut same_sized, 9, 1);
        field(&mut same_sized, 10, 1);
        assert_eq!(Header::parse(&same_sized).unwrap(), stock_header());
        assert!(matches!(
            parse(&same_sized, None),
            Err(PackageError::UnsupportedFormat(_))
        ));
        same_sized[HEADER_BYTES + 500] ^= 1;
        assert!(matches!(
            parse(&same_sized, None),
            Err(PackageError::UnsupportedFormat(_))
        ));
    }

    #[test]
    fn synthetic_encoded_transport_fixture_has_no_invented_build_or_vectors() {
        let bytes = vec![0xa5; 112];
        let pkg = synthetic_stock_package(&bytes);
        assert_eq!(pkg.info.kind, "stock");
        assert!(pkg.approved_for_transfer() && pkg.info.installable);
        assert!(!pkg.info.build_identity_verified);
        assert!(pkg.info.build_id.is_none());
        assert_eq!((pkg.info.initial_msp, pkg.info.reset_handler), (0, 0));
        assert_eq!(pkg.info.report_count, 2);
        assert_eq!(pkg.parameters()[2], 112);
    }

    #[test]
    fn payload_and_current_settings_partition_are_bounded() {
        for size in [0, VECTOR_BYTES - 1, APPLICATION_BYTES_MAX + REPORT_BYTES] {
            let app = if size >= 8 {
                application(size)
            } else {
                vec![0u8; size]
            };
            assert!(matches!(
                parse(&container(&app), None),
                Err(PackageError::PayloadLimit)
            ));
        }
        assert!(matches!(
            parse(&container(&application(513)), None),
            Err(PackageError::UnalignedPayload)
        ));
        assert!(parse(&container(&application(APPLICATION_BYTES_MAX)), None).is_ok());
    }

    #[test]
    fn calculated_payload_crc_is_separate_from_opaque_header_word_zero() {
        let mut raw = container(&application(512));
        let original = parse(&raw, None).unwrap().info;
        raw[HEADER_BYTES + 500] ^= 1;
        let modified = parse(&raw, None).unwrap().info;
        assert_ne!(original.crc32, modified.crc32);
        assert_ne!(original.file_sha256, modified.file_sha256);
        assert_ne!(original.application_sha256, modified.application_sha256);
        assert_eq!(
            Header::parse(&raw).unwrap().metadata_word_0,
            OBSERVED_METADATA[0]
        );
    }

    #[test]
    fn all_nonzero_vectors_stay_inside_payload_after_vector_table() {
        for (index, pointer) in [
            (1, 0),
            (1, APPLICATION_BASE + 392),
            (2, APPLICATION_BASE + 1),
            (20, 0x0800_3043),
            (97, APPLICATION_BASE + 513),
            (2, u32::MAX),
            (2, SETTINGS_BASE + 1),
        ] {
            let mut app = application(512);
            app[index * 4..index * 4 + 4].copy_from_slice(&pointer.to_le_bytes());
            assert!(
                matches!(
                    parse(&container(&app), None),
                    Err(PackageError::InvalidVector { .. })
                ),
                "index {index}"
            );
        }
        let mut app = application(512);
        app[8..12].copy_from_slice(&(APPLICATION_BASE + 511).to_le_bytes());
        assert!(parse(&container(&app), None).is_ok()); // Last complete halfword.
    }

    #[test]
    fn stack_bounds_match_python_candidate_validation() {
        for stack in [
            0x2000_0000u32,
            0x2002_0008,
            0x2001_ffff,
            0x1001_0008,
            0x1000_0000,
        ] {
            let mut app = application(512);
            app[..4].copy_from_slice(&stack.to_le_bytes());
            assert!(matches!(
                parse(&container(&app), None),
                Err(PackageError::InvalidStack(_))
            ));
        }
        for stack in [0x2000_0008u32, 0x2002_0000, 0x1000_0008, 0x1001_0000] {
            assert!(plausible_initial_stack(stack));
        }
    }

    #[test]
    fn expected_build_identity_is_explicit_and_unique() {
        let raw = container(&application(512));
        assert!(matches!(
            parse(&raw, Some("ABCDEF0123456789")),
            Err(PackageError::InvalidBuildId)
        ));
        assert!(matches!(
            parse(&raw, Some("deadbeef12345679")),
            Err(PackageError::BuildIdMismatch)
        ));
        let mut app = application(512);
        app[VECTOR_BYTES..VECTOR_BYTES + 16].fill(0);
        assert!(matches!(
            parse(&container(&app), Some(ID)),
            Err(PackageError::BuildIdMismatch)
        ));
        app[VECTOR_BYTES..VECTOR_BYTES + 16].copy_from_slice(ID.as_bytes());
        app[450..466].copy_from_slice(ID.as_bytes());
        assert!(matches!(
            parse(&container(&app), Some(ID)),
            Err(PackageError::AmbiguousBuildId)
        ));
        app[450..466].fill(0);
        app[450..466].copy_from_slice(b"0123456789abcdef");
        assert!(parse(&container(&app), Some(ID)).is_ok());
        assert_eq!(parse(&container(&app), None).unwrap().info.build_id, None);
        // A non-NUL-terminated ID does not identify the firmware build string.
        app[VECTOR_BYTES + 16] = b'x';
        assert!(matches!(
            parse(&container(&app), Some(ID)),
            Err(PackageError::BuildIdMismatch)
        ));
    }

    #[test]
    fn crc_matches_recovered_independent_vectors() {
        assert_eq!(crc32_normal(b"123456789", 0), 0x89a1_897f);
        assert_eq!(crc32_normal(b"123456789", u32::MAX), 0x0376_e6e7);
        assert_eq!(crc32_normal(b"", 0x1234_5678), 0x1234_5678);
    }

    #[test]
    fn sha256_matches_independent_vectors_and_two_block_padding() {
        assert_eq!(
            sha256(b""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
        assert_eq!(
            sha256(b"abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        assert_eq!(
            sha256(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
        );
        assert_eq!(
            sha256(&vec![b'a'; 1_000_000]),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
        );
    }
}

/// Infer a complete build from the standard basename only when unambiguous.
pub fn infer_build(app: &[u8], name: &str) -> Option<String> {
    let prefix = name
        .strip_prefix("THY666-instrument-alpha-")?
        .strip_suffix(".333")?;
    if prefix.len() != 8 || !prefix.bytes().all(|c| c.is_ascii_hexdigit()) {
        return None;
    }
    let prefix = prefix.to_ascii_lowercase();
    let mut found = BTreeSet::new();
    for w in app.windows(17) {
        if w[16] == 0 && w[..16].iter().all(|c| c.is_ascii_hexdigit()) {
            let text = std::str::from_utf8(&w[..16]).ok()?.to_ascii_lowercase();
            if text.starts_with(&prefix) {
                found.insert(text);
            }
        }
    }
    if found.len() == 1 {
        found.into_iter().next()
    } else {
        None
    }
}
