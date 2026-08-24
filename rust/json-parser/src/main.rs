//! CLI front-end: reads a JSON file, parses it with the hand-written
//! parser in this crate, and prints the re-serialization.
//!
//! Usage: json-parser [--compact] <path-to-json-file>
//!
//! By default the output is pretty-printed with two-space indentation;
//! pass `--compact` to print it with no insignificant whitespace instead.

use std::env;
use std::fs;
use std::process::ExitCode;

fn print_usage(program: &str) {
    eprintln!("Usage: {program} [--compact] <path-to-json-file>");
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    let program = args.first().map(|s| s.as_str()).unwrap_or("json-parser").to_string();

    let mut compact = false;
    let mut path: Option<&str> = None;

    for arg in args.iter().skip(1) {
        match arg.as_str() {
            "--compact" => compact = true,
            "-h" | "--help" => {
                print_usage(&program);
                return ExitCode::SUCCESS;
            }
            _ if path.is_none() => path = Some(arg.as_str()),
            other => {
                eprintln!("error: unexpected extra argument '{other}'");
                print_usage(&program);
                return ExitCode::FAILURE;
            }
        }
    }

    let path = match path {
        Some(p) => p,
        None => {
            print_usage(&program);
            return ExitCode::FAILURE;
        }
    };

    let contents = match fs::read_to_string(path) {
        Ok(contents) => contents,
        Err(e) => {
            eprintln!("error: failed to read '{path}': {e}");
            return ExitCode::FAILURE;
        }
    };

    match json_parser::parse(&contents) {
        Ok(value) => {
            if compact {
                println!("{}", value.to_compact_string());
            } else {
                println!("{}", value.to_pretty_string());
            }
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("error: failed to parse '{path}': {e}");
            ExitCode::FAILURE
        }
    }
}
