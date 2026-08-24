use crate::env::ShellEnv;
use crate::expand;
use crate::ffi;
use crate::jobs::{state_label, JobState};
use crate::shell::Shell;

const NAMES: &[&str] = &[
    "cd", "pwd", "exit", "export", "unset", "echo", "jobs", "fg", "bg", "kill", "alias",
    "unalias", "history", "type", "which", "help", "true", "false",
];

pub fn is_builtin(name: &str) -> bool {
    NAMES.contains(&name)
}

fn do_cd(args: &[String], env: &mut ShellEnv) -> i32 {
    let target = if let Some(first) = args.first() {
        if first == "-" {
            match env.get("OLDPWD") {
                Some(p) => p,
                None => {
                    eprintln!("cd: OLDPWD not set");
                    return 1;
                }
            }
        } else {
            first.clone()
        }
    } else {
        match env.get("HOME") {
            Some(p) => p,
            None => {
                eprintln!("cd: HOME not set");
                return 1;
            }
        }
    };

    let old = std::env::current_dir()
        .map(|p| p.to_string_lossy().to_string())
        .unwrap_or_default();

    match std::env::set_current_dir(&target) {
        Ok(()) => {
            env.set("OLDPWD", &old);
            let new = std::env::current_dir()
                .map(|p| p.to_string_lossy().to_string())
                .unwrap_or(target);
            env.set("PWD", &new);
            0
        }
        Err(e) => {
            eprintln!("cd: {}: {}", target, e);
            1
        }
    }
}

fn do_pwd() -> i32 {
    match std::env::current_dir() {
        Ok(p) => {
            println!("{}", p.display());
            0
        }
        Err(e) => {
            eprintln!("pwd: {}", e);
            1
        }
    }
}

fn do_export(args: &[String], env: &mut ShellEnv) -> i32 {
    if args.is_empty() {
        for name in env.exported_names() {
            let value = env.get(&name).unwrap_or_default();
            println!("declare -x {}=\"{}\"", name, value);
        }
        return 0;
    }
    for arg in args {
        if let Some((name, value)) = arg.split_once('=') {
            env.export_with_value(name, value);
        } else {
            env.export(arg);
        }
    }
    0
}

fn do_unset(args: &[String], env: &mut ShellEnv) -> i32 {
    for name in args {
        env.unset(name);
    }
    0
}

fn do_echo(args: &[String]) -> i32 {
    let mut newline = true;
    let mut start = 0;
    if args.first().map(|s| s.as_str()) == Some("-n") {
        newline = false;
        start = 1;
    }
    let text = args[start..].join(" ");
    if newline {
        println!("{}", text);
    } else {
        print!("{}", text);
        use std::io::Write;
        let _ = std::io::stdout().flush();
    }
    0
}

fn do_type(args: &[String]) -> i32 {
    let mut status = 0;
    for name in args {
        if is_builtin(name) {
            println!("{} is a shell builtin", name);
        } else if let Some(path) = expand::resolve_program_path(name) {
            println!("{} is {}", name, path);
        } else {
            println!("{}: not found", name);
            status = 1;
        }
    }
    status
}

fn do_which(args: &[String]) -> i32 {
    let mut status = 0;
    for name in args {
        match expand::resolve_program_path(name) {
            Some(path) => println!("{}", path),
            None => {
                println!("{} not found", name);
                status = 1;
            }
        }
    }
    status
}

fn do_help() -> i32 {
    println!("unix-shell builtins:");
    for name in NAMES {
        println!("  {}", name);
    }
    0
}

fn parse_job_spec(spec: Option<&String>, shell: &Shell) -> Option<usize> {
    match spec {
        Some(s) => {
            let trimmed = s.strip_prefix('%').unwrap_or(s);
            trimmed.parse::<usize>().ok()
        }
        None => shell.jobs.most_recent(),
    }
}

fn do_jobs(shell: &Shell) -> i32 {
    for job in shell.jobs.list() {
        println!("[{}]  {:<10} {}", job.id, state_label(&job.state), job.command);
    }
    0
}

fn do_fg(args: &[String], shell: &mut Shell) -> i32 {
    let id = match parse_job_spec(args.first(), shell) {
        Some(id) => id,
        None => {
            eprintln!("fg: no current job");
            return 1;
        }
    };
    let (pgid, pids, cmd_text) = match shell.jobs.find(id) {
        Some(j) => (j.pgid, j.pids.clone(), j.command.clone()),
        None => {
            eprintln!("fg: no such job: {}", id);
            return 1;
        }
    };
    println!("{}", cmd_text);
    if shell.interactive {
        ffi::c_tcsetpgrp(ffi::STDIN_FILENO, pgid);
    }
    ffi::c_kill(-pgid, ffi::SIGCONT);
    shell.jobs.set_running(id);
    let status = shell.jobs.wait_foreground(pgid, &pids);
    if shell.interactive {
        ffi::c_tcsetpgrp(ffi::STDIN_FILENO, shell.shell_pgid);
    }
    if status == 256 {
        if let Some(j) = shell.jobs.find_mut(id) {
            j.state = JobState::Stopped;
        }
        println!("\n[{}]+  Stopped                 {}", id, cmd_text);
        148
    } else {
        shell.jobs.remove(id);
        status
    }
}

fn do_bg(args: &[String], shell: &mut Shell) -> i32 {
    let id = match parse_job_spec(args.first(), shell) {
        Some(id) => id,
        None => {
            eprintln!("bg: no current job");
            return 1;
        }
    };
    let (pgid, cmd_text) = match shell.jobs.find(id) {
        Some(j) => (j.pgid, j.command.clone()),
        None => {
            eprintln!("bg: no such job: {}", id);
            return 1;
        }
    };
    ffi::c_kill(-pgid, ffi::SIGCONT);
    shell.jobs.set_running(id);
    println!("[{}]+ {} &", id, cmd_text);
    0
}

fn do_kill(args: &[String], shell: &mut Shell) -> i32 {
    let mut signal = ffi::SIGTERM;
    let mut targets = args;
    if let Some(first) = args.first() {
        if let Some(rest) = first.strip_prefix('-') {
            if let Ok(n) = rest.parse::<i32>() {
                signal = n;
                targets = &args[1..];
            } else {
                signal = match rest.to_uppercase().as_str() {
                    "KILL" => ffi::SIGKILL,
                    "TERM" => ffi::SIGTERM,
                    "INT" => ffi::SIGINT,
                    "STOP" => ffi::SIGTSTP,
                    "CONT" => ffi::SIGCONT,
                    _ => ffi::SIGTERM,
                };
                targets = &args[1..];
            }
        }
    }
    let mut status = 0;
    for target in targets {
        let pid = if let Some(job_spec) = target.strip_prefix('%') {
            match job_spec.parse::<usize>().ok().and_then(|id| shell.jobs.find(id)) {
                Some(j) => j.pgid,
                None => {
                    eprintln!("kill: no such job: {}", target);
                    status = 1;
                    continue;
                }
            }
        } else {
            match target.parse::<i32>() {
                Ok(p) => p,
                Err(_) => {
                    eprintln!("kill: invalid target: {}", target);
                    status = 1;
                    continue;
                }
            }
        };
        if !ffi::c_kill(pid, signal) {
            eprintln!("kill: ({}) - no such process", pid);
            status = 1;
        }
    }
    status
}

fn do_alias(args: &[String], shell: &mut Shell) -> i32 {
    if args.is_empty() {
        for (name, value) in shell.env.all_aliases() {
            println!("alias {}='{}'", name, value);
        }
        return 0;
    }
    for arg in args {
        if let Some((name, value)) = arg.split_once('=') {
            shell.env.set_alias(name, value);
        } else if let Some(value) = shell.env.get_alias(arg) {
            println!("alias {}='{}'", arg, value);
        } else {
            eprintln!("alias: {}: not found", arg);
            return 1;
        }
    }
    0
}

fn do_unalias(args: &[String], shell: &mut Shell) -> i32 {
    for name in args {
        shell.env.remove_alias(name);
    }
    0
}

fn do_history(shell: &Shell) -> i32 {
    for (i, entry) in shell.history.list().iter().enumerate() {
        println!("{:>5}  {}", i + 1, entry);
    }
    0
}

pub fn run_foreground(name: &str, args: &[String], shell: &mut Shell) -> i32 {
    match name {
        "cd" => do_cd(args, &mut shell.env),
        "pwd" => do_pwd(),
        "exit" => {
            let code = args
                .first()
                .and_then(|s| s.parse::<i32>().ok())
                .unwrap_or(shell.env.last_status);
            shell.should_exit = true;
            shell.exit_code = code;
            code
        }
        "export" => do_export(args, &mut shell.env),
        "unset" => do_unset(args, &mut shell.env),
        "echo" => do_echo(args),
        "jobs" => do_jobs(shell),
        "fg" => do_fg(args, shell),
        "bg" => do_bg(args, shell),
        "kill" => do_kill(args, shell),
        "alias" => do_alias(args, shell),
        "unalias" => do_unalias(args, shell),
        "history" => do_history(shell),
        "type" => do_type(args),
        "which" => do_which(args),
        "help" => do_help(),
        "true" => 0,
        "false" => 1,
        _ => {
            eprintln!("unix-shell: {}: not a builtin", name);
            127
        }
    }
}

pub fn run_in_child(name: &str, args: &[String], env: &ShellEnv) -> i32 {
    let mut env = env.clone();
    match name {
        "cd" => do_cd(args, &mut env),
        "pwd" => do_pwd(),
        "exit" => args
            .first()
            .and_then(|s| s.parse::<i32>().ok())
            .unwrap_or(env.last_status),
        "export" => do_export(args, &mut env),
        "unset" => do_unset(args, &mut env),
        "echo" => do_echo(args),
        "type" => do_type(args),
        "which" => do_which(args),
        "help" => do_help(),
        "true" => 0,
        "false" => 1,
        "jobs" | "fg" | "bg" | "kill" | "alias" | "unalias" | "history" => {
            eprintln!("unix-shell: {}: not available in this context", name);
            1
        }
        _ => {
            eprintln!("unix-shell: {}: not a builtin", name);
            127
        }
    }
}
