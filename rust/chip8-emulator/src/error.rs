use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    UnknownOpcode { address: u16, opcode: u16 },
    PcOutOfBounds(u16),
    MemoryOutOfBounds { address: u32, length: usize },
    StackOverflow(u16),
    StackUnderflow(u16),
    RomTooLarge { size: usize, capacity: usize },
    Assembly { line: usize, message: String },
    NotHalted { cycles: usize },
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::UnknownOpcode { address, opcode } => {
                write!(f, "unknown opcode {opcode:04X} at address {address:03X}")
            }
            Error::PcOutOfBounds(pc) => write!(f, "program counter {pc:03X} left addressable memory"),
            Error::MemoryOutOfBounds { address, length } => {
                write!(f, "access of {length} byte(s) at address {address:X} left addressable memory")
            }
            Error::StackOverflow(pc) => write!(f, "call stack overflow at address {pc:03X}"),
            Error::StackUnderflow(pc) => write!(f, "call stack underflow at address {pc:03X}"),
            Error::RomTooLarge { size, capacity } => {
                write!(f, "rom of {size} bytes does not fit in {capacity} bytes of program memory")
            }
            Error::Assembly { line, message } => write!(f, "assembly error on line {line}: {message}"),
            Error::NotHalted { cycles } => {
                write!(f, "program did not reach a halt loop within {cycles} cycles")
            }
        }
    }
}

impl std::error::Error for Error {}
