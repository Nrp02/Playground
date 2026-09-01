use std::collections::BTreeMap;
use std::fs;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};
use tar_archive::{Archive, Builder, Error, Header};

static COUNTER: AtomicU64 = AtomicU64::new(0);

fn temp_dir(label: &str) -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    let n = COUNTER.fetch_add(1, Ordering::SeqCst);
    let path = std::env::temp_dir().join(format!(
        "tar_archive_test_{label}_{}_{nanos}_{n}",
        std::process::id()
    ));
    path
}

struct Cleanup(Vec<PathBuf>);

impl Drop for Cleanup {
    fn drop(&mut self) {
        for p in &self.0 {
            let _ = fs::remove_dir_all(p);
            let _ = fs::remove_file(p);
        }
    }
}

fn lcg_bytes(len: usize, seed: u64) -> Vec<u8> {
    let mut state = seed;
    let mut out = Vec::with_capacity(len);
    for _ in 0..len {
        state = state
            .wrapping_mul(6364136223846793005)
            .wrapping_add(1442695040888963407);
        out.push((state >> 24) as u8);
    }
    out
}

#[derive(Debug, PartialEq, Eq)]
enum Snap {
    Dir,
    File(Vec<u8>),
}

fn snapshot(base: &Path) -> BTreeMap<String, Snap> {
    let mut out = BTreeMap::new();
    walk(base, Path::new(""), &mut out);
    out
}

fn walk(base: &Path, rel: &Path, out: &mut BTreeMap<String, Snap>) {
    let full = base.join(rel);
    let Ok(entries) = fs::read_dir(&full) else {
        return;
    };
    for entry in entries.flatten() {
        let rel_child = rel.join(entry.file_name());
        let meta = fs::metadata(entry.path()).unwrap();
        if meta.is_dir() {
            out.insert(rel_child.to_string_lossy().into_owned(), Snap::Dir);
            walk(base, &rel_child, out);
        } else {
            out.insert(
                rel_child.to_string_lossy().into_owned(),
                Snap::File(fs::read(entry.path()).unwrap()),
            );
        }
    }
}

#[test]
fn header_round_trip_random_and_boundary_sizes() {
    let sizes: [u64; 6] = [0, 1, 511, 512, 513, 8_589_934_591];
    for &size in &sizes {
        let header = Header::regular("some/path/file.txt", size, 0o644, 1_700_000_000);
        let bytes = header.to_bytes().expect("encode");
        let decoded = Header::from_bytes(&bytes).expect("decode").expect("some");
        assert_eq!(decoded.name, "some/path/file.txt");
        assert_eq!(decoded.size, size);
        assert_eq!(decoded.mode, 0o644);
        assert_eq!(decoded.mtime, 1_700_000_000);
    }
}

#[test]
fn checksum_detects_flipped_byte() {
    let header = Header::regular("file.txt", 5, 0o644, 42);
    let mut bytes = header.to_bytes().unwrap();
    assert!(Header::from_bytes(&bytes).unwrap().is_some());
    bytes[20] ^= 0xFF;
    match Header::from_bytes(&bytes) {
        Err(Error::BadChecksum) => {}
        other => panic!("expected BadChecksum, got {other:?}"),
    }
}

#[test]
fn archive_length_is_block_multiple_and_ends_with_two_zero_blocks() {
    let mut out = Vec::new();
    {
        let mut builder = Builder::new(&mut out);
        builder.append_file_bytes("a.txt", 0o644, 0, b"hello").unwrap();
        builder.append_file_bytes("b.txt", 0o644, 0, &vec![7u8; 1000]).unwrap();
        builder.finish().unwrap();
    }
    assert_eq!(out.len() % 512, 0);
    assert!(out.len() >= 1024);
    let last = &out[out.len() - 1024..];
    assert!(last.iter().all(|&b| b == 0));
}

fn build_tree(root: &Path) {
    fs::create_dir_all(root).unwrap();
    fs::write(root.join("empty.txt"), b"").unwrap();
    fs::write(root.join("small.txt"), b"hello world").unwrap();
    fs::write(root.join("binary.dat"), lcg_bytes(5000, 99)).unwrap();
    fs::create_dir_all(root.join("nested/deeper")).unwrap();
    fs::write(root.join("nested/deeper/file.txt"), b"deep file").unwrap();
    fs::write(root.join("nested/other.bin"), lcg_bytes(2049, 7)).unwrap();
}

#[test]
fn full_round_trip_byte_identical() {
    let source = temp_dir("rt_source");
    let archive = temp_dir("rt_archive");
    let extract = temp_dir("rt_extract");
    let _cleanup = Cleanup(vec![source.clone(), archive.clone(), extract.clone()]);

    build_tree(&source);

    let mut bytes = Vec::new();
    {
        let mut builder = Builder::new(&mut bytes);
        builder.append_dir_all("root", &source).unwrap();
        builder.finish().unwrap();
    }
    fs::write(&archive, &bytes).unwrap();

    Archive::new(Cursor::new(bytes)).extract_to(&extract).unwrap();

    let mut expected = BTreeMap::new();
    expected.insert("root".to_string(), Snap::Dir);
    for (k, v) in snapshot(&source) {
        expected.insert(format!("root/{k}"), v);
    }
    let actual = snapshot(&extract);
    assert_eq!(expected, actual);
}

#[test]
fn long_path_prefix_splitting_round_trips() {
    let a = "p".repeat(80);
    let b = "q".repeat(80);
    let name = format!("{a}/{b}/final_component.txt");
    let header = Header::regular(&name, 4, 0o644, 0);
    let bytes = header.to_bytes().expect("should split into prefix/name");
    let decoded = Header::from_bytes(&bytes).unwrap().unwrap();
    assert_eq!(decoded.name, name);
}

#[test]
fn unrepresentable_path_errors_cleanly() {
    let single_component = "x".repeat(150);
    let header = Header::regular(&single_component, 1, 0o644, 0);
    match header.to_bytes() {
        Err(Error::PathTooLong(_)) => {}
        other => panic!("expected PathTooLong, got {other:?}"),
    }
}

#[test]
fn empty_archive_has_only_terminator() {
    let mut bytes = Vec::new();
    {
        let builder = Builder::new(&mut bytes);
        builder.finish().unwrap();
    }
    assert_eq!(bytes.len(), 1024);
    assert!(bytes.iter().all(|&b| b == 0));
    let entries: Vec<_> = Archive::new(Cursor::new(bytes)).collect();
    assert!(entries.is_empty());
}

#[test]
fn empty_file_entry_round_trips() {
    let mut bytes = Vec::new();
    {
        let mut builder = Builder::new(&mut bytes);
        builder.append_file_bytes("empty.txt", 0o644, 0, b"").unwrap();
        builder.finish().unwrap();
    }
    let mut entries = Archive::new(Cursor::new(bytes));
    let entry = entries.next().unwrap().unwrap();
    assert_eq!(entry.header.size, 0);
    assert!(entry.data.is_empty());
    assert!(entries.next().is_none());
}

#[test]
fn truncated_archive_errors_not_panics() {
    let mut bytes = Vec::new();
    {
        let mut builder = Builder::new(&mut bytes);
        builder.append_file_bytes("f.txt", 0o644, 0, &vec![1u8; 2000]).unwrap();
        builder.finish().unwrap();
    }
    bytes.truncate(600);
    let mut entries = Archive::new(Cursor::new(bytes));
    match entries.next() {
        Some(Err(Error::Truncated)) => {}
        other => panic!("expected Truncated, got {other:?}"),
    }
}

#[test]
fn bad_magic_errors_cleanly() {
    let header = Header::regular("f.txt", 1, 0o644, 0);
    let mut bytes = header.to_bytes().unwrap();
    bytes[257] = b'X';
    for b in bytes[148..156].iter_mut() {
        *b = b' ';
    }
    let sum: u32 = bytes.iter().map(|&b| b as u32).sum();
    let s = format!("{sum:06o}");
    bytes[148..154].copy_from_slice(s.as_bytes());
    bytes[154] = 0;
    bytes[155] = b' ';
    match Header::from_bytes(&bytes) {
        Err(Error::BadMagic) => {}
        other => panic!("expected BadMagic, got {other:?}"),
    }
}

#[test]
fn bad_octal_field_errors_cleanly() {
    let header = Header::regular("f.txt", 1, 0o644, 0);
    let mut bytes = header.to_bytes().unwrap();
    bytes[124] = b'9';
    bytes[125] = b'9';
    for b in bytes[148..156].iter_mut() {
        *b = b' ';
    }
    let sum: u32 = bytes.iter().map(|&b| b as u32).sum();
    let s = format!("{sum:06o}");
    bytes[148..154].copy_from_slice(s.as_bytes());
    bytes[154] = 0;
    bytes[155] = b' ';
    match Header::from_bytes(&bytes) {
        Err(Error::BadOctal) => {}
        other => panic!("expected BadOctal, got {other:?}"),
    }
}

fn single_entry_archive(name: &str) -> Vec<u8> {
    let mut bytes = Vec::new();
    let mut builder = Builder::new(&mut bytes);
    builder.append_file_bytes(name, 0o644, 0, b"payload").unwrap();
    builder.finish().unwrap();
    bytes
}

#[test]
fn path_traversal_rejected_on_extract() {
    let extract = temp_dir("traversal_extract");
    let _cleanup = Cleanup(vec![extract.clone()]);
    let bytes = single_entry_archive("../escape.txt");
    match Archive::new(Cursor::new(bytes)).extract_to(&extract) {
        Err(Error::UnsafePath(_)) => {}
        other => panic!("expected UnsafePath, got {other:?}"),
    }
}

#[test]
fn absolute_path_rejected_on_extract() {
    let extract = temp_dir("absolute_extract");
    let _cleanup = Cleanup(vec![extract.clone()]);
    let bytes = single_entry_archive("/etc/passwd_evil");
    match Archive::new(Cursor::new(bytes)).extract_to(&extract) {
        Err(Error::UnsafePath(_)) => {}
        other => panic!("expected UnsafePath, got {other:?}"),
    }
}

#[test]
fn real_tar_accepts_generated_archive() {
    let has_tar = Command::new("tar").arg("--version").output().is_ok();
    if !has_tar {
        eprintln!("skipping: tar binary not available");
        return;
    }
    let source = temp_dir("realtar_source");
    let archive = temp_dir("realtar_archive");
    let _cleanup = Cleanup(vec![source.clone(), archive.clone()]);
    build_tree(&source);

    let mut bytes = Vec::new();
    {
        let mut builder = Builder::new(&mut bytes);
        builder.append_dir_all("root", &source).unwrap();
        builder.finish().unwrap();
    }
    fs::write(&archive, &bytes).unwrap();

    let output = Command::new("tar")
        .arg("-tvf")
        .arg(&archive)
        .output()
        .expect("run tar");
    assert!(
        output.status.success(),
        "tar -tvf failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let listing = String::from_utf8_lossy(&output.stdout);
    assert!(listing.contains("root/empty.txt"));
    assert!(listing.contains("root/nested/deeper/file.txt"));
}
