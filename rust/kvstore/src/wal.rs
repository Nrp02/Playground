//! Write-ahead log (WAL) implementation.
//!
//! The WAL is an append-only file consisting of a sequence of records. Each
//! record encodes either a `Set(key, value)` or a `Delete(key)` operation.
//! On startup, the store replays the WAL from the beginning to reconstruct
//! its in-memory state. Compaction rewrites the WAL to contain only the
//! minimal set of records needed to reproduce the current state.
//!
//! On-disk record format (all integers little-endian):
//!
//! ```text
//! [1 byte  op]           0 = Set, 1 = Delete
//! [4 bytes key_len]
//! [key_len bytes key]
//! -- for Set only --
//! [4 bytes value_len]
//! [value_len bytes value]
//! ```

use std::collections::BTreeMap;
use std::fs::{File, OpenOptions};
use std::io::{self, BufReader, BufWriter, Read, Write};
use std::path::{Path, PathBuf};

const OP_SET: u8 = 0;
const OP_DELETE: u8 = 1;

/// A single operation recorded in the WAL.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Record {
    Set(String, String),
    Delete(String),
}

/// Handle to an on-disk write-ahead log.
///
/// Wraps a buffered, append-mode file handle and knows how to serialize
/// records to it, flush them durably, and replay an existing log from disk.
pub struct Wal {
    path: PathBuf,
    writer: BufWriter<File>,
}

impl Wal {
    /// Open (creating if necessary) the WAL file at `path` for appending.
    pub fn open<P: AsRef<Path>>(path: P) -> io::Result<Self> {
        let path = path.as_ref().to_path_buf();
        let file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)?;
        Ok(Wal {
            path,
            writer: BufWriter::new(file),
        })
    }

    /// Replay every record in the WAL file at `path`, in order.
    ///
    /// If the file does not exist, returns an empty vector (a fresh store).
    /// If the file exists but ends with a truncated/corrupt trailing record
    /// (e.g. the process crashed mid-write), that partial record is
    /// discarded and all records preceding it are still returned.
    pub fn replay<P: AsRef<Path>>(path: P) -> io::Result<Vec<Record>> {
        let path = path.as_ref();
        let file = match File::open(path) {
            Ok(f) => f,
            Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(Vec::new()),
            Err(e) => return Err(e),
        };
        let mut reader = BufReader::new(file);
        let mut records = Vec::new();

        loop {
            match read_record(&mut reader) {
                Ok(Some(record)) => records.push(record),
                Ok(None) => break, // clean EOF
                Err(e) if e.kind() == io::ErrorKind::UnexpectedEof => {
                    // Truncated trailing record from a crash mid-write; stop here.
                    break;
                }
                Err(e) => return Err(e),
            }
        }

        Ok(records)
    }

    /// Append a `Set` record and flush it to disk.
    pub fn append_set(&mut self, key: &str, value: &str) -> io::Result<()> {
        write_record(&mut self.writer, &Record::Set(key.to_string(), value.to_string()))?;
        self.writer.flush()
    }

    /// Append a `Delete` record and flush it to disk.
    pub fn append_delete(&mut self, key: &str) -> io::Result<()> {
        write_record(&mut self.writer, &Record::Delete(key.to_string()))?;
        self.writer.flush()
    }

    /// Rewrite the WAL from scratch so that it contains exactly one `Set`
    /// record per live key in `state` (in key order), with no delete
    /// records and no history of overwritten values.
    ///
    /// This is done by writing to a temporary file and atomically renaming
    /// it over the existing WAL, so a crash during compaction cannot leave
    /// the log in a half-written state.
    pub fn compact(&mut self, state: &BTreeMap<String, String>) -> io::Result<()> {
        let tmp_path = self.path.with_extension("compact.tmp");
        {
            let tmp_file = OpenOptions::new()
                .create(true)
                .write(true)
                .truncate(true)
                .open(&tmp_path)?;
            let mut tmp_writer = BufWriter::new(tmp_file);
            for (key, value) in state.iter() {
                write_record(&mut tmp_writer, &Record::Set(key.clone(), value.clone()))?;
            }
            tmp_writer.flush()?;
            tmp_writer.get_ref().sync_all()?;
        }

        std::fs::rename(&tmp_path, &self.path)?;

        // Re-open the writer in append mode pointing at the freshly
        // compacted file, so subsequent appends continue correctly.
        let file = OpenOptions::new().create(true).append(true).open(&self.path)?;
        self.writer = BufWriter::new(file);
        Ok(())
    }

    /// Path to the underlying WAL file on disk.
    pub fn path(&self) -> &Path {
        &self.path
    }
}

fn write_record<W: Write>(writer: &mut W, record: &Record) -> io::Result<()> {
    match record {
        Record::Set(key, value) => {
            writer.write_all(&[OP_SET])?;
            write_bytes(writer, key.as_bytes())?;
            write_bytes(writer, value.as_bytes())?;
        }
        Record::Delete(key) => {
            writer.write_all(&[OP_DELETE])?;
            write_bytes(writer, key.as_bytes())?;
        }
    }
    Ok(())
}

fn write_bytes<W: Write>(writer: &mut W, bytes: &[u8]) -> io::Result<()> {
    let len = bytes.len() as u32;
    writer.write_all(&len.to_le_bytes())?;
    writer.write_all(bytes)?;
    Ok(())
}

/// Read a single record from `reader`. Returns `Ok(None)` on a clean EOF
/// (i.e. zero bytes read at a record boundary). Returns
/// `Err(UnexpectedEof)` if the stream ends partway through a record.
fn read_record<R: Read>(reader: &mut R) -> io::Result<Option<Record>> {
    let mut op_byte = [0u8; 1];
    if reader.read(&mut op_byte)? == 0 {
        return Ok(None);
    }

    let key = read_bytes(reader)?;
    let key = String::from_utf8(key).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;

    match op_byte[0] {
        OP_SET => {
            let value = read_bytes(reader)?;
            let value =
                String::from_utf8(value).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
            Ok(Some(Record::Set(key, value)))
        }
        OP_DELETE => Ok(Some(Record::Delete(key))),
        other => Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("unknown WAL opcode: {other}"),
        )),
    }
}

fn read_bytes<R: Read>(reader: &mut R) -> io::Result<Vec<u8>> {
    let mut len_buf = [0u8; 4];
    reader.read_exact(&mut len_buf)?;
    let len = u32::from_le_bytes(len_buf) as usize;
    let mut buf = vec![0u8; len];
    reader.read_exact(&mut buf)?;
    Ok(buf)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    fn temp_path(name: &str) -> PathBuf {
        let mut p = std::env::temp_dir();
        p.push(format!(
            "kvstore_wal_test_{}_{}_{}",
            name,
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        p
    }

    #[test]
    fn replay_missing_file_is_empty() {
        let path = temp_path("missing");
        let records = Wal::replay(&path).unwrap();
        assert!(records.is_empty());
    }

    #[test]
    fn append_and_replay_round_trip() {
        let path = temp_path("round_trip");
        {
            let mut wal = Wal::open(&path).unwrap();
            wal.append_set("a", "1").unwrap();
            wal.append_set("b", "2").unwrap();
            wal.append_delete("a").unwrap();
            wal.append_set("c", "3").unwrap();
        }

        let records = Wal::replay(&path).unwrap();
        assert_eq!(
            records,
            vec![
                Record::Set("a".into(), "1".into()),
                Record::Set("b".into(), "2".into()),
                Record::Delete("a".into()),
                Record::Set("c".into(), "3".into()),
            ]
        );
        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn compact_reduces_to_live_state() {
        let path = temp_path("compact");
        let mut wal = Wal::open(&path).unwrap();
        wal.append_set("a", "1").unwrap();
        wal.append_set("a", "2").unwrap();
        wal.append_set("b", "x").unwrap();
        wal.append_delete("b").unwrap();

        let mut state = BTreeMap::new();
        state.insert("a".to_string(), "2".to_string());
        wal.compact(&state).unwrap();

        let records = Wal::replay(&path).unwrap();
        assert_eq!(records, vec![Record::Set("a".into(), "2".into())]);

        // Appending after compaction should still work.
        wal.append_set("d", "4").unwrap();
        let records = Wal::replay(&path).unwrap();
        assert_eq!(
            records,
            vec![
                Record::Set("a".into(), "2".into()),
                Record::Set("d".into(), "4".into()),
            ]
        );
        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn truncated_trailing_record_is_ignored() {
        let path = temp_path("truncated");
        {
            let mut wal = Wal::open(&path).unwrap();
            wal.append_set("full", "record").unwrap();
        }
        // Simulate a crash mid-write by appending a partial record: opcode
        // byte + a key length prefix, but no key bytes.
        {
            let mut file = OpenOptions::new().append(true).open(&path).unwrap();
            file.write_all(&[OP_SET]).unwrap();
            file.write_all(&10u32.to_le_bytes()).unwrap();
            file.write_all(b"short").unwrap(); // fewer than 10 bytes
        }

        let records = Wal::replay(&path).unwrap();
        assert_eq!(records, vec![Record::Set("full".into(), "record".into())]);
        std::fs::remove_file(&path).ok();
    }
}
