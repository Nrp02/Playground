use crate::error::Error;
use std::path::{Component, Path};

pub const BLOCK_SIZE: usize = 512;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EntryType {
    Regular,
    Directory,
    Symlink,
    Other(u8),
}

impl EntryType {
    fn to_byte(self) -> u8 {
        match self {
            EntryType::Regular => b'0',
            EntryType::Directory => b'5',
            EntryType::Symlink => b'2',
            EntryType::Other(b) => b,
        }
    }

    fn from_byte(b: u8) -> Self {
        match b {
            b'0' | 0 => EntryType::Regular,
            b'5' => EntryType::Directory,
            b'2' => EntryType::Symlink,
            other => EntryType::Other(other),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Header {
    pub name: String,
    pub mode: u32,
    pub uid: u32,
    pub gid: u32,
    pub size: u64,
    pub mtime: u64,
    pub typeflag: EntryType,
    pub linkname: String,
    pub uname: String,
    pub gname: String,
    pub devmajor: u32,
    pub devminor: u32,
}

impl Header {
    pub fn regular(name: &str, size: u64, mode: u32, mtime: u64) -> Self {
        Header {
            name: name.to_string(),
            mode,
            uid: 0,
            gid: 0,
            size,
            mtime,
            typeflag: EntryType::Regular,
            linkname: String::new(),
            uname: String::new(),
            gname: String::new(),
            devmajor: 0,
            devminor: 0,
        }
    }

    pub fn directory(name: &str, mode: u32, mtime: u64) -> Self {
        Header {
            name: name.to_string(),
            mode,
            uid: 0,
            gid: 0,
            size: 0,
            mtime,
            typeflag: EntryType::Directory,
            linkname: String::new(),
            uname: String::new(),
            gname: String::new(),
            devmajor: 0,
            devminor: 0,
        }
    }

    pub fn symlink(name: &str, target: &str, mode: u32, mtime: u64) -> Self {
        Header {
            name: name.to_string(),
            mode,
            uid: 0,
            gid: 0,
            size: 0,
            mtime,
            typeflag: EntryType::Symlink,
            linkname: target.to_string(),
            uname: String::new(),
            gname: String::new(),
            devmajor: 0,
            devminor: 0,
        }
    }

    pub fn to_bytes(&self) -> Result<[u8; BLOCK_SIZE], Error> {
        let mut buf = [0u8; BLOCK_SIZE];
        let (prefix, name) = split_path(&self.name)?;
        write_str(&mut buf[0..100], &name, "name")?;
        write_octal(&mut buf[100..108], self.mode as u64, "mode")?;
        write_octal(&mut buf[108..116], self.uid as u64, "uid")?;
        write_octal(&mut buf[116..124], self.gid as u64, "gid")?;
        write_octal(&mut buf[124..136], self.size, "size")?;
        write_octal(&mut buf[136..148], self.mtime, "mtime")?;
        buf[156] = self.typeflag.to_byte();
        write_str(&mut buf[157..257], &self.linkname, "linkname")?;
        buf[257..263].copy_from_slice(b"ustar\0");
        buf[263..265].copy_from_slice(b"00");
        write_str(&mut buf[265..297], &self.uname, "uname")?;
        write_str(&mut buf[297..329], &self.gname, "gname")?;
        write_octal(&mut buf[329..337], self.devmajor as u64, "devmajor")?;
        write_octal(&mut buf[337..345], self.devminor as u64, "devminor")?;
        write_str(&mut buf[345..500], &prefix, "prefix")?;
        for b in buf[148..156].iter_mut() {
            *b = b' ';
        }
        let sum: u32 = buf.iter().map(|&b| b as u32).sum();
        write_checksum(&mut buf[148..156], sum);
        Ok(buf)
    }

    pub fn from_bytes(buf: &[u8; BLOCK_SIZE]) -> Result<Option<Header>, Error> {
        if buf.iter().all(|&b| b == 0) {
            return Ok(None);
        }
        let stored = read_octal(&buf[148..156])? as u32;
        let mut copy = *buf;
        for b in copy[148..156].iter_mut() {
            *b = b' ';
        }
        let sum: u32 = copy.iter().map(|&b| b as u32).sum();
        if sum != stored {
            return Err(Error::BadChecksum);
        }
        if &buf[257..263] != b"ustar\0" {
            return Err(Error::BadMagic);
        }
        let name = read_str(&buf[0..100]);
        let prefix = read_str(&buf[345..500]);
        let full_name = if prefix.is_empty() {
            name
        } else {
            format!("{prefix}/{name}")
        };
        let mode = read_octal(&buf[100..108])? as u32;
        let uid = read_octal(&buf[108..116])? as u32;
        let gid = read_octal(&buf[116..124])? as u32;
        let size = read_octal(&buf[124..136])?;
        let mtime = read_octal(&buf[136..148])?;
        let typeflag = EntryType::from_byte(buf[156]);
        let linkname = read_str(&buf[157..257]);
        let uname = read_str(&buf[265..297]);
        let gname = read_str(&buf[297..329]);
        let devmajor = read_octal(&buf[329..337])? as u32;
        let devminor = read_octal(&buf[337..345])? as u32;
        Ok(Some(Header {
            name: full_name,
            mode,
            uid,
            gid,
            size,
            mtime,
            typeflag,
            linkname,
            uname,
            gname,
            devmajor,
            devminor,
        }))
    }
}

pub fn split_path(path: &str) -> Result<(String, String), Error> {
    let bytes = path.as_bytes();
    if bytes.len() <= 100 {
        return Ok((String::new(), path.to_string()));
    }
    for i in (0..bytes.len()).rev() {
        if bytes[i] == b'/' {
            let prefix = &path[..i];
            let name = &path[i + 1..];
            if !name.is_empty() && prefix.len() <= 155 && name.len() <= 100 {
                return Ok((prefix.to_string(), name.to_string()));
            }
        }
    }
    Err(Error::PathTooLong(path.to_string()))
}

pub fn validate_safe_relative_path(path: &str) -> Result<(), Error> {
    if path.is_empty() {
        return Err(Error::UnsafePath(path.to_string()));
    }
    let p = Path::new(path);
    for component in p.components() {
        match component {
            Component::Normal(_) | Component::CurDir => {}
            Component::ParentDir | Component::RootDir | Component::Prefix(_) => {
                return Err(Error::UnsafePath(path.to_string()));
            }
        }
    }
    Ok(())
}

fn write_str(field: &mut [u8], s: &str, name: &str) -> Result<(), Error> {
    let bytes = s.as_bytes();
    if bytes.len() > field.len() {
        return Err(Error::FieldOverflow(name.to_string()));
    }
    for b in field.iter_mut() {
        *b = 0;
    }
    field[..bytes.len()].copy_from_slice(bytes);
    Ok(())
}

fn write_octal(field: &mut [u8], value: u64, name: &str) -> Result<(), Error> {
    let digits = field.len() - 1;
    let s = format!("{value:0digits$o}");
    if s.len() > digits {
        return Err(Error::FieldOverflow(name.to_string()));
    }
    field[..digits].copy_from_slice(s.as_bytes());
    field[digits] = 0;
    Ok(())
}

fn write_checksum(field: &mut [u8], value: u32) {
    let s = format!("{value:06o}");
    field[..6].copy_from_slice(s.as_bytes());
    field[6] = 0;
    field[7] = b' ';
}

fn read_octal(field: &[u8]) -> Result<u64, Error> {
    let mut end = field.len();
    while end > 0 && (field[end - 1] == 0 || field[end - 1] == b' ') {
        end -= 1;
    }
    let mut start = 0;
    while start < end && field[start] == b' ' {
        start += 1;
    }
    if start == end {
        return Ok(0);
    }
    let s = std::str::from_utf8(&field[start..end]).map_err(|_| Error::BadOctal)?;
    u64::from_str_radix(s, 8).map_err(|_| Error::BadOctal)
}

fn read_str(field: &[u8]) -> String {
    let end = field.iter().position(|&b| b == 0).unwrap_or(field.len());
    String::from_utf8_lossy(&field[..end]).into_owned()
}
