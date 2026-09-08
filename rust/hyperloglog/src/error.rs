use std::fmt;

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum HllError {
    InvalidPrecision(u8),
    PrecisionMismatch { left: u8, right: u8 },
    BadMagic,
    UnsupportedVersion(u8),
    UnknownMode(u8),
    Truncated { expected: usize, got: usize },
    ChecksumMismatch { expected: u64, got: u64 },
    RegisterOutOfRange(u8),
}

impl fmt::Display for HllError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            HllError::InvalidPrecision(p) => {
                write!(f, "precision {} outside the supported range 4..=18", p)
            }
            HllError::PrecisionMismatch { left, right } => {
                write!(f, "cannot merge sketches of precision {} and {}", left, right)
            }
            HllError::BadMagic => write!(f, "not a hyperloglog sketch"),
            HllError::UnsupportedVersion(v) => write!(f, "unsupported format version {}", v),
            HllError::UnknownMode(m) => write!(f, "unknown storage mode byte {}", m),
            HllError::Truncated { expected, got } => {
                write!(f, "truncated sketch: expected {} bytes, got {}", expected, got)
            }
            HllError::ChecksumMismatch { expected, got } => {
                write!(f, "checksum mismatch: expected {:#018x}, got {:#018x}", expected, got)
            }
            HllError::RegisterOutOfRange(v) => write!(f, "register value {} does not fit in 6 bits", v),
        }
    }
}

impl std::error::Error for HllError {}

pub type Result<T> = std::result::Result<T, HllError>;
