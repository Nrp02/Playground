use std::process::{Command, Output};

fn run(args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_unix-shell"))
        .args(args)
        .output()
        .expect("failed to execute unix-shell binary")
}

fn stdout_of(output: &Output) -> String {
    String::from_utf8_lossy(&output.stdout).trim().to_string()
}

#[test]
fn runs_simple_command() {
    let output = run(&["-c", "echo hello"]);
    assert_eq!(stdout_of(&output), "hello");
    assert!(output.status.success());
}

#[test]
fn pipeline_pipes_output_between_commands() {
    let output = run(&["-c", "echo hello world | wc -w"]);
    assert_eq!(stdout_of(&output), "2");
}

#[test]
fn variable_assignment_and_expansion() {
    let output = run(&["-c", "x=42; echo $x"]);
    assert_eq!(stdout_of(&output), "42");
}

#[test]
fn exit_status_reflects_last_command() {
    let output = run(&["-c", "false"]);
    assert!(!output.status.success());
    assert_eq!(output.status.code(), Some(1));
}

#[test]
fn and_or_control_flow() {
    let output = run(&["-c", "true && echo yes || echo no"]);
    assert_eq!(stdout_of(&output), "yes");

    let output = run(&["-c", "false && echo yes || echo no"]);
    assert_eq!(stdout_of(&output), "no");
}

#[test]
fn output_redirection_writes_to_file() {
    let dir = std::env::temp_dir();
    let path = dir.join(format!(
        "unix_shell_test_redirect_{}_{}.txt",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    let path_str = path.to_string_lossy().to_string();

    let output = run(&["-c", &format!("echo redirected > {}", path_str)]);
    assert!(output.status.success());

    let contents = std::fs::read_to_string(&path).unwrap();
    assert_eq!(contents.trim(), "redirected");

    std::fs::remove_file(&path).unwrap();
}

#[test]
fn cd_and_pwd_builtins() {
    let dir = std::env::temp_dir();
    let dir_str = dir.to_string_lossy().to_string();
    let output = run(&["-c", &format!("cd {} && pwd", dir_str)]);
    let printed = stdout_of(&output);
    let canonical_expected = std::fs::canonicalize(&dir).unwrap();
    let canonical_printed = std::fs::canonicalize(&printed).unwrap();
    assert_eq!(canonical_printed, canonical_expected);
}
