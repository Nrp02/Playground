use crate::error::Error;
use crate::header::{validate_safe_relative_path, EntryType, Header, BLOCK_SIZE};
use std::fs;
use std::io::{self, Read};
use std::path::Path;

#[derive(Debug, Clone)]
pub struct Entry {
    pub header: Header,
    pub data: Vec<u8>,
}

pub struct Archive<R: Read> {
    reader: R,
    done: bool,
}

impl<R: Read> Archive<R> {
    pub fn new(reader: R) -> Self {
        Archive { reader, done: false }
    }

    pub fn list(self) -> Result<Vec<Header>, Error> {
        let mut headers = Vec::new();
        for entry in self {
            headers.push(entry?.header);
        }
        Ok(headers)
    }

    pub fn extract_to(self, dest: &Path) -> Result<(), Error> {
        fs::create_dir_all(dest)?;
        let dest = dest.canonicalize()?;
        for entry in self {
            let entry = entry?;
            let header = &entry.header;
            validate_safe_relative_path(&header.name)?;
            let out_path = dest.join(&header.name);
            if !out_path.starts_with(&dest) {
                return Err(Error::UnsafePath(header.name.clone()));
            }
            match header.typeflag {
                EntryType::Directory => {
                    fs::create_dir_all(&out_path)?;
                }
                EntryType::Symlink => {
                    if let Some(parent) = out_path.parent() {
                        fs::create_dir_all(parent)?;
                    }
                    create_symlink(&header.linkname, &out_path)?;
                }
                _ => {
                    if let Some(parent) = out_path.parent() {
                        fs::create_dir_all(parent)?;
                    }
                    fs::write(&out_path, &entry.data)?;
                }
            }
        }
        Ok(())
    }
}

#[cfg(unix)]
fn create_symlink(target: &str, out_path: &Path) -> Result<(), Error> {
    std::os::unix::fs::symlink(target, out_path)?;
    Ok(())
}

#[cfg(not(unix))]
fn create_symlink(_target: &str, _out_path: &Path) -> Result<(), Error> {
    Ok(())
}

impl<R: Read> Iterator for Archive<R> {
    type Item = Result<Entry, Error>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.done {
            return None;
        }
        let mut block = [0u8; BLOCK_SIZE];
        match read_full_block(&mut self.reader, &mut block) {
            Ok(false) => {
                self.done = true;
                None
            }
            Err(e) => {
                self.done = true;
                Some(Err(e))
            }
            Ok(true) => match Header::from_bytes(&block) {
                Ok(None) => {
                    self.done = true;
                    None
                }
                Err(e) => {
                    self.done = true;
                    Some(Err(e))
                }
                Ok(Some(header)) => {
                    let size = header.size as usize;
                    let mut data = vec![0u8; size];
                    if size > 0 {
                        if let Err(e) = read_exact_checked(&mut self.reader, &mut data) {
                            self.done = true;
                            return Some(Err(e));
                        }
                    }
                    let remainder = size % BLOCK_SIZE;
                    if remainder != 0 {
                        let pad = BLOCK_SIZE - remainder;
                        let mut padbuf = vec![0u8; pad];
                        if let Err(e) = read_exact_checked(&mut self.reader, &mut padbuf) {
                            self.done = true;
                            return Some(Err(e));
                        }
                    }
                    Some(Ok(Entry { header, data }))
                }
            },
        }
    }
}

fn read_full_block<R: Read>(reader: &mut R, buf: &mut [u8; BLOCK_SIZE]) -> Result<bool, Error> {
    let mut total = 0;
    while total < BLOCK_SIZE {
        let n = reader.read(&mut buf[total..])?;
        if n == 0 {
            if total == 0 {
                return Ok(false);
            }
            return Err(Error::Truncated);
        }
        total += n;
    }
    Ok(true)
}

fn read_exact_checked<R: Read>(reader: &mut R, buf: &mut [u8]) -> Result<(), Error> {
    match reader.read_exact(buf) {
        Ok(()) => Ok(()),
        Err(e) if e.kind() == io::ErrorKind::UnexpectedEof => Err(Error::Truncated),
        Err(e) => Err(Error::Io(e)),
    }
}
