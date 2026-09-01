use std::collections::BTreeMap;
use std::fs::{self, File};
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};
use tar_archive::{Archive, Builder, EntryType, Header};

struct CleanupGuard(Vec<PathBuf>);

impl Drop for CleanupGuard {
    fn drop(&mut self) {
        for path in &self.0 {
            if path.is_dir() {
                let _ = fs::remove_dir_all(path);
            } else {
                let _ = fs::remove_file(path);
            }
        }
    }
}

#[derive(Debug, PartialEq, Eq)]
enum Snapshot {
    Dir,
    File(Vec<u8>),
    Symlink(String),
}

fn snapshot(base: &Path) -> std::io::Result<BTreeMap<String, Snapshot>> {
    let mut out = BTreeMap::new();
    walk_snapshot(base, Path::new(""), &mut out)?;
    Ok(out)
}

fn walk_snapshot(base: &Path, rel: &Path, out: &mut BTreeMap<String, Snapshot>) -> std::io::Result<()> {
    for entry in fs::read_dir(base.join(rel))? {
        let entry = entry?;
        let rel_child = rel.join(entry.file_name());
        let meta = fs::symlink_metadata(entry.path())?;
        if meta.file_type().is_symlink() {
            let target = fs::read_link(entry.path())?;
            out.insert(
                rel_child.to_string_lossy().into_owned(),
                Snapshot::Symlink(target.to_string_lossy().into_owned()),
            );
        } else if meta.is_dir() {
            out.insert(rel_child.to_string_lossy().into_owned(), Snapshot::Dir);
            walk_snapshot(base, &rel_child, out)?;
        } else {
            out.insert(
                rel_child.to_string_lossy().into_owned(),
                Snapshot::File(fs::read(entry.path())?),
            );
        }
    }
    Ok(())
}

fn unique_temp_dir(label: &str) -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    std::env::temp_dir().join(format!("tar_archive_demo_{label}_{}_{nanos}", std::process::id()))
}

fn lcg_bytes(len: usize, seed: u64) -> Vec<u8> {
    let mut state = seed;
    let mut out = Vec::with_capacity(len);
    for _ in 0..len {
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        out.push((state >> 24) as u8);
    }
    out
}

fn build_source_tree(root: &Path) -> std::io::Result<PathBuf> {
    fs::create_dir_all(root)?;
    fs::write(root.join("empty.txt"), b"")?;
    fs::write(root.join("binary.dat"), lcg_bytes(4096, 42))?;
    fs::write(root.join("large.bin"), lcg_bytes(3 * 1024 * 1024, 1234))?;
    fs::create_dir_all(root.join("nested/dir"))?;
    fs::write(root.join("nested/dir/deep.txt"), b"nested content")?;
    let long_a = "a".repeat(60);
    let long_b = "b".repeat(60);
    let long_dir = root.join(&long_a).join(&long_b);
    fs::create_dir_all(&long_dir)?;
    fs::write(
        long_dir.join("file_at_the_end_of_a_very_long_ustar_path_needing_prefix_split.txt"),
        b"long path content",
    )?;
    Ok(root.to_path_buf())
}

fn print_listing(headers: &[Header]) {
    println!("{:<40} {:>10} {:<10} {:>12}", "name", "size", "type", "mtime");
    for h in headers {
        let kind = match h.typeflag {
            EntryType::Regular => "file",
            EntryType::Directory => "dir",
            EntryType::Symlink => "symlink",
            EntryType::Other(_) => "other",
        };
        println!("{:<40} {:>10} {:<10} {:>12}", h.name, h.size, kind, h.mtime);
    }
}

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let source_root = unique_temp_dir("source");
    let archive_path = unique_temp_dir("archive").with_extension("tar");
    let extract_root = unique_temp_dir("extract");

    let mut guard = CleanupGuard(vec![source_root.clone(), archive_path.clone(), extract_root.clone()]);

    build_source_tree(&source_root)?;

    {
        let file = File::create(&archive_path)?;
        let mut builder = Builder::new(file);
        builder.append_dir_all("data", &source_root)?;
        let file = builder.finish()?;
        drop(file);
    }

    let list_file = File::open(&archive_path)?;
    let headers = Archive::new(list_file).list()?;
    print_listing(&headers);

    let extract_file = File::open(&archive_path)?;
    Archive::new(extract_file).extract_to(&extract_root)?;

    let original = snapshot(&source_root)?;
    let mut original_prefixed = BTreeMap::new();
    original_prefixed.insert("data".to_string(), Snapshot::Dir);
    for (k, v) in original {
        original_prefixed.insert(format!("data/{k}"), v);
    }
    let extracted = snapshot(&extract_root)?;
    if original_prefixed == extracted {
        println!("extracted tree is byte-identical to the original");
    } else {
        println!("MISMATCH between extracted tree and original");
    }

    let malicious_traversal = build_malicious_archive("../escape.txt")?;
    guard.0.push(malicious_traversal.clone());
    match Archive::new(File::open(&malicious_traversal)?).extract_to(&extract_root) {
        Err(e) => println!("path traversal correctly rejected: {e}"),
        Ok(()) => println!("BUG: path traversal was not rejected"),
    }

    let malicious_absolute = build_malicious_archive("/etc/evil.txt")?;
    guard.0.push(malicious_absolute.clone());
    match Archive::new(File::open(&malicious_absolute)?).extract_to(&extract_root) {
        Err(e) => println!("absolute path correctly rejected: {e}"),
        Ok(()) => println!("BUG: absolute path was not rejected"),
    }

    let mut valid_bytes = Vec::new();
    {
        let mut builder = Builder::new(&mut valid_bytes);
        builder.append_file_bytes("checksum_demo.txt", 0o644, 0, b"hello world")?;
        builder.finish()?;
    }
    valid_bytes[10] ^= 0xFF;
    match Archive::new(Cursor::new(valid_bytes)).list() {
        Err(e) => println!("corrupted checksum correctly rejected: {e}"),
        Ok(_) => println!("BUG: corrupted checksum was not rejected"),
    }

    drop(guard);
    Ok(())
}

fn build_malicious_archive(name: &str) -> std::io::Result<PathBuf> {
    let path = unique_temp_dir("malicious").with_extension("tar");
    let file = File::create(&path)?;
    let mut builder = Builder::new(file);
    builder
        .append_file_bytes(name, 0o644, 0, b"payload")
        .map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e.to_string()))?;
    let file = builder
        .finish()
        .map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e.to_string()))?;
    drop(file);
    Ok(path)
}

fn main() {
    if let Err(e) = run() {
        eprintln!("demo failed: {e}");
        std::process::exit(1);
    }
}
