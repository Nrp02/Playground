use crate::error::Error;
use crate::header::{Header, BLOCK_SIZE};
use std::fs;
use std::io::Write;
use std::path::Path;
use std::time::UNIX_EPOCH;

pub struct Builder<W: Write> {
    writer: W,
}

impl<W: Write> Builder<W> {
    pub fn new(writer: W) -> Self {
        Builder { writer }
    }

    pub fn append_file_bytes(
        &mut self,
        archive_path: &str,
        mode: u32,
        mtime: u64,
        data: &[u8],
    ) -> Result<(), Error> {
        let header = Header::regular(archive_path, data.len() as u64, mode, mtime);
        self.write_entry(&header, data)
    }

    pub fn append_directory(&mut self, archive_path: &str, mode: u32, mtime: u64) -> Result<(), Error> {
        let header = Header::directory(archive_path, mode, mtime);
        self.write_entry(&header, &[])
    }

    pub fn append_symlink(
        &mut self,
        archive_path: &str,
        target: &str,
        mode: u32,
        mtime: u64,
    ) -> Result<(), Error> {
        let header = Header::symlink(archive_path, target, mode, mtime);
        self.write_entry(&header, &[])
    }

    pub fn append_fs_file(&mut self, archive_path: &str, fs_path: &Path) -> Result<(), Error> {
        let metadata = fs::metadata(fs_path)?;
        let mode = file_mode(&metadata);
        let mtime = file_mtime(&metadata);
        let data = fs::read(fs_path)?;
        self.append_file_bytes(archive_path, mode, mtime, &data)
    }

    pub fn append_dir_all(&mut self, archive_path: &str, fs_path: &Path) -> Result<(), Error> {
        let metadata = fs::metadata(fs_path)?;
        let mode = file_mode(&metadata);
        let mtime = file_mtime(&metadata);
        self.append_directory(archive_path, mode, mtime)?;
        let mut children: Vec<_> = fs::read_dir(fs_path)?.filter_map(|e| e.ok()).collect();
        children.sort_by_key(|e| e.file_name());
        for child in children {
            let child_path = child.path();
            let child_archive_path = format!("{}/{}", archive_path, child.file_name().to_string_lossy());
            let child_meta = fs::symlink_metadata(&child_path)?;
            if child_meta.is_dir() {
                self.append_dir_all(&child_archive_path, &child_path)?;
            } else {
                self.append_fs_file(&child_archive_path, &child_path)?;
            }
        }
        Ok(())
    }

    fn write_entry(&mut self, header: &Header, data: &[u8]) -> Result<(), Error> {
        let bytes = header.to_bytes()?;
        self.writer.write_all(&bytes)?;
        self.writer.write_all(data)?;
        let remainder = data.len() % BLOCK_SIZE;
        if remainder != 0 {
            let pad = BLOCK_SIZE - remainder;
            self.writer.write_all(&vec![0u8; pad])?;
        }
        Ok(())
    }

    pub fn finish(self) -> Result<W, Error> {
        let Builder { mut writer } = self;
        writer.write_all(&[0u8; BLOCK_SIZE])?;
        writer.write_all(&[0u8; BLOCK_SIZE])?;
        writer.flush()?;
        Ok(writer)
    }
}

#[cfg(unix)]
fn file_mode(metadata: &fs::Metadata) -> u32 {
    use std::os::unix::fs::PermissionsExt;
    metadata.permissions().mode() & 0o7777
}

#[cfg(not(unix))]
fn file_mode(_metadata: &fs::Metadata) -> u32 {
    0o644
}

fn file_mtime(metadata: &fs::Metadata) -> u64 {
    metadata
        .modified()
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_secs())
        .unwrap_or(0)
}
