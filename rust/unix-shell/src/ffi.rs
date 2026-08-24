use std::ffi::CString;
use std::os::unix::io::RawFd;

#[cfg(target_os = "macos")]
mod signum {
    pub const SIGINT: i32 = 2;
    pub const SIGQUIT: i32 = 3;
    pub const SIGKILL: i32 = 9;
    pub const SIGTERM: i32 = 15;
    pub const SIGTSTP: i32 = 18;
    pub const SIGCONT: i32 = 19;
    pub const SIGTTIN: i32 = 21;
    pub const SIGTTOU: i32 = 22;
}

#[cfg(target_os = "linux")]
mod signum {
    pub const SIGINT: i32 = 2;
    pub const SIGQUIT: i32 = 3;
    pub const SIGKILL: i32 = 9;
    pub const SIGTERM: i32 = 15;
    pub const SIGCONT: i32 = 18;
    pub const SIGTSTP: i32 = 20;
    pub const SIGTTIN: i32 = 21;
    pub const SIGTTOU: i32 = 22;
}

pub use signum::*;

pub const SIG_DFL: usize = 0;
pub const SIG_IGN: usize = 1;

pub const WNOHANG: i32 = 1;
pub const WUNTRACED: i32 = 2;

extern "C" {
    fn fork() -> i32;
    fn execvp(file: *const i8, argv: *const *const i8) -> i32;
    fn pipe(fds: *mut i32) -> i32;
    fn dup2(oldfd: i32, newfd: i32) -> i32;
    fn close(fd: i32) -> i32;
    fn waitpid(pid: i32, status: *mut i32, options: i32) -> i32;
    fn kill(pid: i32, sig: i32) -> i32;
    fn setpgid(pid: i32, pgid: i32) -> i32;
    fn getpid() -> i32;
    fn tcsetpgrp(fd: i32, pgrp: i32) -> i32;
    fn isatty(fd: i32) -> i32;
    fn signal(signum: i32, handler: usize) -> usize;
    fn _exit(code: i32) -> !;
}

pub fn c_fork() -> std::io::Result<i32> {
    let pid = unsafe { fork() };
    if pid < 0 {
        Err(std::io::Error::last_os_error())
    } else {
        Ok(pid)
    }
}

pub fn c_execvp(program: &str, args: &[String]) -> std::io::Error {
    let c_program = match CString::new(program) {
        Ok(s) => s,
        Err(_) => return std::io::Error::from_raw_os_error(22),
    };
    let mut c_args: Vec<CString> = Vec::with_capacity(args.len());
    for a in args {
        match CString::new(a.as_str()) {
            Ok(s) => c_args.push(s),
            Err(_) => return std::io::Error::from_raw_os_error(22),
        }
    }
    let mut ptrs: Vec<*const i8> = c_args.iter().map(|s| s.as_ptr()).collect();
    ptrs.push(std::ptr::null());
    unsafe {
        execvp(c_program.as_ptr(), ptrs.as_ptr());
    }
    std::io::Error::last_os_error()
}

pub fn c_pipe() -> std::io::Result<(RawFd, RawFd)> {
    let mut fds: [i32; 2] = [0, 0];
    let rc = unsafe { pipe(fds.as_mut_ptr()) };
    if rc < 0 {
        Err(std::io::Error::last_os_error())
    } else {
        Ok((fds[0], fds[1]))
    }
}

pub fn c_dup2(oldfd: RawFd, newfd: RawFd) {
    unsafe {
        dup2(oldfd, newfd);
    }
}

pub fn c_close(fd: RawFd) {
    unsafe {
        close(fd);
    }
}

pub fn c_exit(code: i32) -> ! {
    unsafe { _exit(code) }
}

pub struct WaitStatus {
    pub pid: i32,
    pub exited: bool,
    pub exit_code: i32,
    pub signaled: bool,
    pub term_signal: i32,
    pub stopped: bool,
}

fn decode(pid: i32, status: i32) -> WaitStatus {
    let low7 = status & 0x7f;
    let exited = low7 == 0;
    let stopped = low7 == 0x7f;
    let signaled = !exited && !stopped;
    WaitStatus {
        pid,
        exited,
        exit_code: (status >> 8) & 0xff,
        signaled,
        term_signal: low7,
        stopped,
    }
}

pub fn c_waitpid(pid: i32, options: i32) -> Option<WaitStatus> {
    let mut status: i32 = 0;
    let rc = unsafe { waitpid(pid, &mut status as *mut i32, options) };
    if rc <= 0 {
        None
    } else {
        Some(decode(rc, status))
    }
}

pub fn c_kill(pid: i32, sig: i32) -> bool {
    unsafe { kill(pid, sig) == 0 }
}

pub fn c_setpgid(pid: i32, pgid: i32) {
    unsafe {
        setpgid(pid, pgid);
    }
}

pub fn c_getpid() -> i32 {
    unsafe { getpid() }
}

pub fn c_tcsetpgrp(fd: i32, pgrp: i32) {
    unsafe {
        tcsetpgrp(fd, pgrp);
    }
}

pub fn c_isatty(fd: i32) -> bool {
    unsafe { isatty(fd) == 1 }
}

pub fn c_signal_ignore(sig: i32) {
    unsafe {
        signal(sig, SIG_IGN);
    }
}

pub fn c_signal_default(sig: i32) {
    unsafe {
        signal(sig, SIG_DFL);
    }
}

pub const STDIN_FILENO: i32 = 0;
pub const STDOUT_FILENO: i32 = 1;
