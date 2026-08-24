use crate::env::ShellEnv;
use crate::expand;
use crate::ffi;
use crate::parser::SimpleCommand;
use std::os::unix::io::{IntoRawFd, RawFd};

fn open_redirect_fds(cmd: &SimpleCommand, env: &ShellEnv) -> Result<Vec<(RawFd, RawFd)>, String> {
    let mut plan: Vec<(RawFd, RawFd)> = Vec::new();

    if let Some(target) = &cmd.in_redirect {
        let path = expand::expand_path(target, env);
        let file = std::fs::File::open(&path)
            .map_err(|e| format!("{}: {}", path, e))?;
        plan.push((file.into_raw_fd(), ffi::STDIN_FILENO));
    }

    for redir in &cmd.out_redirects {
        let path = expand::expand_path(&redir.target, env);
        let mut opts = std::fs::OpenOptions::new();
        opts.write(true).create(true);
        if redir.append {
            opts.append(true);
        } else {
            opts.truncate(true);
        }
        let file = opts.open(&path).map_err(|e| format!("{}: {}", path, e))?;
        plan.push((file.into_raw_fd(), redir.fd));
    }

    Ok(plan)
}

fn apply_fd_plan(plan: &[(RawFd, RawFd)]) {
    for (src, dst) in plan {
        ffi::c_dup2(*src, *dst);
        ffi::c_close(*src);
    }
}

pub fn with_redirects_applied<F: FnOnce() -> i32>(
    cmd: &SimpleCommand,
    env: &ShellEnv,
    f: F,
) -> Result<i32, String> {
    if cmd.in_redirect.is_none() && cmd.out_redirects.is_empty() {
        return Ok(f());
    }
    let plan = open_redirect_fds(cmd, env)?;
    let mut saved: Vec<(RawFd, RawFd)> = Vec::new();
    for (_, dst) in &plan {
        let backup = unsafe { libc_dup(*dst) };
        saved.push((backup, *dst));
    }
    apply_fd_plan(&plan);
    let code = f();
    for (backup, dst) in saved {
        ffi::c_dup2(backup, dst);
        ffi::c_close(backup);
    }
    Ok(code)
}

unsafe fn libc_dup(fd: RawFd) -> RawFd {
    extern "C" {
        fn dup(fd: i32) -> i32;
    }
    dup(fd)
}

fn command_text(argvs: &[Vec<String>]) -> String {
    argvs
        .iter()
        .map(|a| a.join(" "))
        .collect::<Vec<String>>()
        .join(" | ")
}

pub fn spawn_pipeline(
    commands: &[SimpleCommand],
    env: &ShellEnv,
    interactive: bool,
    foreground: bool,
) -> Result<(i32, Vec<i32>, String), String> {
    let mut argvs: Vec<Vec<String>> = Vec::with_capacity(commands.len());
    for cmd in commands {
        let argv = expand::expand_word_list(&cmd.words, env);
        if argv.is_empty() {
            return Err("no command specified".to_string());
        }
        argvs.push(argv);
    }
    let cmd_text = command_text(&argvs);

    let mut pids: Vec<i32> = Vec::with_capacity(commands.len());
    let mut pgid: i32 = 0;
    let mut prev_read: Option<RawFd> = None;

    for (idx, cmd) in commands.iter().enumerate() {
        let is_last = idx == commands.len() - 1;
        let (read_fd, write_fd): (Option<RawFd>, Option<RawFd>) = if !is_last {
            let (r, w) = ffi::c_pipe().map_err(|e| e.to_string())?;
            (Some(r), Some(w))
        } else {
            (None, None)
        };

        let argv = &argvs[idx];
        let program_path = expand::resolve_program_path(&argv[0]);

        match ffi::c_fork().map_err(|e| e.to_string())? {
            0 => {
                for sig in [
                    ffi::SIGINT,
                    ffi::SIGQUIT,
                    ffi::SIGTSTP,
                    ffi::SIGTTIN,
                    ffi::SIGTTOU,
                ] {
                    ffi::c_signal_default(sig);
                }

                ffi::c_setpgid(0, pgid);
                if interactive && foreground {
                    let actual = if pgid == 0 { ffi::c_getpid() } else { pgid };
                    ffi::c_tcsetpgrp(ffi::STDIN_FILENO, actual);
                }

                if let Some(pr) = prev_read {
                    ffi::c_dup2(pr, ffi::STDIN_FILENO);
                    ffi::c_close(pr);
                }
                if let Some(wf) = write_fd {
                    ffi::c_dup2(wf, ffi::STDOUT_FILENO);
                    ffi::c_close(wf);
                }
                if let Some(rf) = read_fd {
                    ffi::c_close(rf);
                }

                match open_redirect_fds(cmd, env) {
                    Ok(plan) => apply_fd_plan(&plan),
                    Err(e) => {
                        eprintln!("unix-shell: {}", e);
                        ffi::c_exit(1);
                    }
                }

                if crate::builtins::is_builtin(&argv[0]) {
                    let code = crate::builtins::run_in_child(&argv[0], &argv[1..], env);
                    ffi::c_exit(code);
                }

                match program_path {
                    Some(path) => {
                        let err = ffi::c_execvp(&path, argv);
                        eprintln!("unix-shell: {}: {}", argv[0], err);
                        ffi::c_exit(126);
                    }
                    None => {
                        eprintln!("unix-shell: {}: command not found", argv[0]);
                        ffi::c_exit(127);
                    }
                }
            }
            child_pid => {
                if idx == 0 {
                    pgid = child_pid;
                }
                ffi::c_setpgid(child_pid, pgid);
                pids.push(child_pid);

                if let Some(pr) = prev_read {
                    ffi::c_close(pr);
                }
                if let Some(wf) = write_fd {
                    ffi::c_close(wf);
                }
                prev_read = read_fd;
            }
        }
    }

    if let Some(pr) = prev_read {
        ffi::c_close(pr);
    }

    if interactive && foreground {
        ffi::c_tcsetpgrp(ffi::STDIN_FILENO, pgid);
    }

    Ok((pgid, pids, cmd_text))
}

pub fn stdin_is_tty() -> bool {
    ffi::c_isatty(ffi::STDIN_FILENO)
}
