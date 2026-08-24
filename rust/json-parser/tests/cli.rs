//! Black-box integration tests for the compiled `json-parser` binary,
//! run via `std::process::Command` against the example fixture files
//! under `examples/`. These complement the in-process parser/round-trip
//! tests by verifying argument handling, exit codes, and that the CLI's
//! stdout is itself valid re-parseable JSON.

use std::path::PathBuf;
use std::process::{Command, Output};

fn example_path(name: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("examples").join(name)
}

fn run(args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_json-parser"))
        .args(args)
        .output()
        .expect("failed to execute json-parser binary")
}

fn stdout_of(output: &Output) -> String {
    String::from_utf8_lossy(&output.stdout).to_string()
}

fn stderr_of(output: &Output) -> String {
    String::from_utf8_lossy(&output.stderr).to_string()
}

#[test]
fn pretty_prints_sample_example_successfully() {
    let path = example_path("sample.json");
    let out = run(&[path.to_str().unwrap()]);
    assert!(out.status.success(), "stderr: {}", stderr_of(&out));

    let printed = stdout_of(&out);
    assert!(printed.contains('\n'), "expected indented, multi-line output");

    // The CLI's own output must itself be valid, re-parseable JSON that
    // round-trips to the same value as parsing the original file directly.
    let original = std::fs::read_to_string(&path).unwrap();
    let expected = json_parser::parse(&original).unwrap();
    let reparsed = json_parser::parse(&printed).unwrap();
    assert_eq!(expected, reparsed);
}

#[test]
fn pretty_prints_org_chart_example_successfully() {
    let path = example_path("org_chart.json");
    let out = run(&[path.to_str().unwrap()]);
    assert!(out.status.success(), "stderr: {}", stderr_of(&out));

    let original = std::fs::read_to_string(&path).unwrap();
    let expected = json_parser::parse(&original).unwrap();
    let reparsed = json_parser::parse(&stdout_of(&out)).unwrap();
    assert_eq!(expected, reparsed);
}

#[test]
fn compact_flag_produces_single_line_equivalent_output() {
    let path = example_path("sample.json");
    let pretty = run(&[path.to_str().unwrap()]);
    let compact = run(&["--compact", path.to_str().unwrap()]);
    assert!(pretty.status.success());
    assert!(compact.status.success());

    let compact_text = stdout_of(&compact);
    assert_eq!(compact_text.lines().count(), 1, "compact output should be a single line");
    assert!(!compact_text.contains("  "), "compact output should have no indentation");

    let from_pretty = json_parser::parse(&stdout_of(&pretty)).unwrap();
    let from_compact = json_parser::parse(&compact_text).unwrap();
    assert_eq!(from_pretty, from_compact);
}

#[test]
fn missing_file_argument_fails_with_usage() {
    let out = run(&[]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("Usage"));
}

#[test]
fn nonexistent_file_fails_cleanly_without_panicking() {
    let out = run(&["does/not/exist.json"]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("failed to read"));
    // A panic would print a "thread 'main' panicked" backtrace banner to
    // stderr instead of our own clean error message.
    assert!(!stderr_of(&out).contains("panicked"));
}

#[test]
fn malformed_json_file_fails_cleanly_without_panicking() {
    let dir = std::env::temp_dir();
    let path = dir.join(format!(
        "json_parser_cli_test_malformed_{}_{}.json",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    std::fs::write(&path, "{\"a\": 1, \"b\": [1, 2,]}").unwrap();

    let out = run(&[path.to_str().unwrap()]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("failed to parse"));
    assert!(!stderr_of(&out).contains("panicked"));

    std::fs::remove_file(&path).ok();
}

#[test]
fn unexpected_extra_argument_fails_with_usage() {
    let path = example_path("sample.json");
    let out = run(&[path.to_str().unwrap(), "extra-arg"]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("Usage"));
}

#[test]
fn help_flag_exits_successfully() {
    let out = run(&["--help"]);
    assert!(out.status.success());
    assert!(stderr_of(&out).contains("Usage"));
}
