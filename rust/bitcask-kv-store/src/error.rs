use std::fmt;
use std::io;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    CrcMismatch { file_id: u64, offset: u64 },
    KeyMismatch { file_id: u64, offset: u64 },
    MissingDataFile(u64),
    BadHintFile(String),
    KeyTooLarge(usize),
    ValueTooLarge(usize),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Io(e) => write!(f, "io error: {e}"),
            Error::CrcMismatch { file_id, offset } => write!(
                f,
                "crc mismatch in data file {file_id:010} at offset {offset}: record is corrupt"
            ),
            Error::KeyMismatch { file_id, offset } => write!(
                f,
                "keydir pointed at data file {file_id:010} offset {offset} but the record holds a different key"
            ),
            Error::MissingDataFile(id) => write!(f, "data file {id:010} referenced by the keydir is missing"),
            Error::BadHintFile(name) => write!(f, "unusable hint file: {name}"),
            Error::KeyTooLarge(len) => write!(f, "key of {len} bytes exceeds the u32 length field"),
            Error::ValueTooLarge(len) => write!(f, "value of {len} bytes exceeds the u32 length field"),
        }
    }
}

impl std::error::Error for Error {}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Error {
        Error::Io(e)
    }
}
