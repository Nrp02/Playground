use std::fmt;
use std::io;

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    BadMagic,
    BadChecksum,
    BadOctal,
    Truncated,
    FieldOverflow(String),
    PathTooLong(String),
    UnsafePath(String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Io(e) => write!(f, "io error: {e}"),
            Error::BadMagic => write!(f, "bad ustar magic"),
            Error::BadChecksum => write!(f, "header checksum mismatch"),
            Error::BadOctal => write!(f, "malformed octal field"),
            Error::Truncated => write!(f, "truncated archive"),
            Error::FieldOverflow(field) => write!(f, "value too large for field: {field}"),
            Error::PathTooLong(path) => write!(f, "path cannot be represented in ustar format: {path}"),
            Error::UnsafePath(path) => write!(f, "unsafe path rejected: {path}"),
        }
    }
}

impl std::error::Error for Error {}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Error::Io(e)
    }
}
