mod builtins;
mod env;
mod exec;
mod expand;
mod ffi;
mod history;
mod jobs;
mod lexer;
mod parser;
mod shell;

use shell::Shell;
use std::io::Write;
use std::process::ExitCode;

fn prompt() -> String {
    let cwd = std::env::current_dir()
        .map(|p| p.to_string_lossy().to_string())
        .unwrap_or_else(|_| "?".to_string());
    let home = std::env::var("HOME").unwrap_or_default();
    let display = if !home.is_empty() && cwd.starts_with(&home) {
        format!("~{}", &cwd[home.len()..])
    } else {
        cwd
    };
    format!("unix-shell:{}$ ", display)
}

fn run_repl() -> i32 {
    let interactive = exec::stdin_is_tty();
    let mut shell = Shell::new(interactive);
    let mut input = String::new();

    loop {
        if shell.should_exit {
            break;
        }
        if interactive {
            print!("{}", prompt());
            let _ = std::io::stdout().flush();
        }
        input.clear();
        let bytes = std::io::stdin().read_line(&mut input).unwrap_or(0);
        if bytes == 0 {
            if interactive {
                println!();
            }
            break;
        }
        let line = input.trim_end_matches('\n');
        if !line.trim().is_empty() {
            shell.history.add(line);
        }
        shell.run_line(line);
    }

    shell.history.save();
    if shell.should_exit {
        shell.exit_code
    } else {
        shell.env.last_status
    }
}

fn run_script(path: &str, script_args: &[String]) -> i32 {
    let contents = match std::fs::read_to_string(path) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("unix-shell: {}: {}", path, e);
            return 127;
        }
    };
    let mut shell = Shell::new(false);
    for (i, arg) in script_args.iter().enumerate() {
        shell.env.set(&(i + 1).to_string(), arg);
    }
    shell.run_line(&contents);
    if shell.should_exit {
        shell.exit_code
    } else {
        shell.env.last_status
    }
}

fn run_command_string(command: &str) -> i32 {
    let mut shell = Shell::new(false);
    shell.run_line(command);
    if shell.should_exit {
        shell.exit_code
    } else {
        shell.env.last_status
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().collect();

    let code = if args.len() >= 3 && args[1] == "-c" {
        run_command_string(&args[2..].join(" "))
    } else if args.len() >= 2 {
        run_script(&args[1], &args[2..])
    } else {
        run_repl()
    };

    ExitCode::from((code.clamp(0, 255)) as u8)
}
