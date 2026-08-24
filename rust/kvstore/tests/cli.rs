//! Black-box integration tests that exercise the compiled `kvstore`
//! binary directly via `std::process::Command`, the same way a user would
//! invoke it from a shell.
//!
//! These complement the unit tests under `src/` (which test `Store` and
//! `Wal` in-process) by verifying the CLI's argument handling and exit
//! codes, and — most importantly — by proving that persistence survives
//! across genuinely separate OS process invocations rather than just
//! separate `Store` values within one process.

use std::path::PathBuf;
use std::process::{Command, Output};

fn temp_wal_path(name: &str) -> PathBuf {
    let mut p = std::env::temp_dir();
    p.push(format!(
        "kvstore_cli_test_{}_{}_{}",
        name,
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    p
}

/// Run the compiled `kvstore` binary with `args`, pointing it at `wal_path`
/// via the `KVSTORE_PATH` environment variable, and capture its output.
fn run(wal_path: &PathBuf, args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_kvstore"))
        .args(args)
        .env("KVSTORE_PATH", wal_path)
        .output()
        .expect("failed to execute kvstore binary")
}

fn stdout_of(output: &Output) -> String {
    String::from_utf8_lossy(&output.stdout).trim().to_string()
}

fn stderr_of(output: &Output) -> String {
    String::from_utf8_lossy(&output.stderr).trim().to_string()
}

#[test]
fn set_then_get_round_trips() {
    let path = temp_wal_path("set_get");
    assert!(run(&path, &["set", "foo", "bar"]).status.success());

    let get_out = run(&path, &["get", "foo"]);
    assert!(get_out.status.success());
    assert_eq!(stdout_of(&get_out), "bar");
    std::fs::remove_file(&path).ok();
}

#[test]
fn get_missing_key_fails_with_nonzero_exit() {
    let path = temp_wal_path("missing");
    let out = run(&path, &["get", "nope"]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("key not found"));
    std::fs::remove_file(&path).ok();
}

#[test]
fn persists_across_separate_process_invocations() {
    let path = temp_wal_path("persist");
    assert!(run(&path, &["set", "a", "1"]).status.success());
    assert!(run(&path, &["set", "b", "2"]).status.success());

    // Every `run` call spawns a brand-new OS process with a fresh `Store`,
    // so these reads only succeed if state was durably written to disk by
    // the earlier, now-exited processes.
    assert_eq!(stdout_of(&run(&path, &["get", "a"])), "1");
    assert_eq!(stdout_of(&run(&path, &["get", "b"])), "2");
    std::fs::remove_file(&path).ok();
}

#[test]
fn overwrite_then_reopen_sees_latest_value() {
    let path = temp_wal_path("overwrite_persist");
    assert!(run(&path, &["set", "k", "v1"]).status.success());
    assert!(run(&path, &["set", "k", "v2"]).status.success());
    assert_eq!(stdout_of(&run(&path, &["get", "k"])), "v2");
    std::fs::remove_file(&path).ok();
}

#[test]
fn delete_then_get_fails() {
    let path = temp_wal_path("delete");
    assert!(run(&path, &["set", "k", "v"]).status.success());

    let del_out = run(&path, &["del", "k"]);
    assert!(del_out.status.success());
    assert_eq!(stdout_of(&del_out), "OK");

    let get_out = run(&path, &["get", "k"]);
    assert!(!get_out.status.success());
    std::fs::remove_file(&path).ok();
}

#[test]
fn deleting_missing_key_fails() {
    let path = temp_wal_path("delete_missing");
    let out = run(&path, &["del", "nope"]);
    assert!(!out.status.success());
    std::fs::remove_file(&path).ok();
}

#[test]
fn list_shows_all_live_keys_in_sorted_order() {
    let path = temp_wal_path("list");
    assert!(run(&path, &["set", "banana", "2"]).status.success());
    assert!(run(&path, &["set", "apple", "1"]).status.success());
    assert!(run(&path, &["set", "cherry", "3"]).status.success());

    let out = run(&path, &["list"]);
    assert!(out.status.success());
    assert_eq!(stdout_of(&out), "apple=1\nbanana=2\ncherry=3");
    std::fs::remove_file(&path).ok();
}

#[test]
fn list_on_empty_store_prints_placeholder() {
    let path = temp_wal_path("empty_list");
    let out = run(&path, &["list"]);
    assert!(out.status.success());
    assert_eq!(stdout_of(&out), "(empty)");
    std::fs::remove_file(&path).ok();
}

#[test]
fn deleted_key_disappears_from_list_but_others_remain() {
    let path = temp_wal_path("list_after_delete");
    assert!(run(&path, &["set", "keep", "1"]).status.success());
    assert!(run(&path, &["set", "drop", "2"]).status.success());
    assert!(run(&path, &["del", "drop"]).status.success());

    let out = run(&path, &["list"]);
    assert_eq!(stdout_of(&out), "keep=1");
    std::fs::remove_file(&path).ok();
}

#[test]
fn compact_shrinks_wal_file_on_disk_and_preserves_data() {
    let path = temp_wal_path("compact");
    for i in 0..50 {
        assert!(run(&path, &["set", "same-key", &i.to_string()]).status.success());
    }
    let size_before = std::fs::metadata(&path).unwrap().len();

    let compact_out = run(&path, &["compact"]);
    assert!(compact_out.status.success());

    let size_after = std::fs::metadata(&path).unwrap().len();
    assert!(
        size_after < size_before,
        "expected compaction to shrink the WAL file on disk (before: {size_before}, after: {size_after})"
    );

    // The latest value must survive compaction and a fresh process read.
    assert_eq!(stdout_of(&run(&path, &["get", "same-key"])), "49");
    std::fs::remove_file(&path).ok();
}

#[test]
fn stats_reports_live_key_count() {
    let path = temp_wal_path("stats");
    assert!(run(&path, &["set", "a", "1"]).status.success());
    assert!(run(&path, &["set", "b", "2"]).status.success());

    let out = run(&path, &["stats"]);
    assert!(out.status.success());
    let text = stdout_of(&out);
    assert!(text.contains("live keys:      2"), "unexpected stats output: {text}");
    std::fs::remove_file(&path).ok();
}

#[test]
fn missing_arguments_print_usage_and_fail() {
    let path = temp_wal_path("usage_none");
    let out = run(&path, &[]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("Usage"));
    std::fs::remove_file(&path).ok();
}

#[test]
fn set_with_wrong_argument_count_fails() {
    let path = temp_wal_path("usage_set");
    let out = run(&path, &["set", "onlykey"]);
    assert!(!out.status.success());
    std::fs::remove_file(&path).ok();
}

#[test]
fn unknown_command_fails_cleanly() {
    let path = temp_wal_path("unknown");
    let out = run(&path, &["frobnicate"]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("unknown command"));
    std::fs::remove_file(&path).ok();
}
