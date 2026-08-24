//! Command-line interface for the persistent key-value store.
//!
//! Usage:
//!   kvstore set <key> <value>
//!   kvstore get <key>
//!   kvstore del <key>
//!   kvstore list
//!   kvstore compact
//!   kvstore stats
//!
//! The store's WAL file defaults to `kvstore.wal` in the current directory.
//! Override with the `KVSTORE_PATH` environment variable.

mod store;
mod wal;

use std::env;
use std::path::PathBuf;
use std::process::ExitCode;

use store::Store;

const DEFAULT_WAL_PATH: &str = "kvstore.wal";

fn wal_path() -> PathBuf {
    env::var("KVSTORE_PATH")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from(DEFAULT_WAL_PATH))
}

fn print_usage(program: &str) {
    eprintln!("Usage:");
    eprintln!("  {program} set <key> <value>");
    eprintln!("  {program} get <key>");
    eprintln!("  {program} del <key>");
    eprintln!("  {program} list");
    eprintln!("  {program} compact");
    eprintln!("  {program} stats");
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    let program = args
        .first()
        .map(|s| s.as_str())
        .unwrap_or("kvstore")
        .to_string();

    if args.len() < 2 {
        print_usage(&program);
        return ExitCode::FAILURE;
    }

    let command = args[1].as_str();
    let path = wal_path();

    let mut store = match Store::open(&path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("error: failed to open store at {}: {e}", path.display());
            return ExitCode::FAILURE;
        }
    };

    let result = match command {
        "set" => {
            if args.len() != 4 {
                eprintln!("error: `set` requires exactly 2 arguments: <key> <value>");
                print_usage(&program);
                return ExitCode::FAILURE;
            }
            store.set(&args[2], &args[3]).map(|()| {
                println!("OK");
            })
        }
        "get" => {
            if args.len() != 3 {
                eprintln!("error: `get` requires exactly 1 argument: <key>");
                print_usage(&program);
                return ExitCode::FAILURE;
            }
            match store.get(&args[2]) {
                Some(value) => {
                    println!("{value}");
                    Ok(())
                }
                None => {
                    eprintln!("key not found: {}", args[2]);
                    return ExitCode::FAILURE;
                }
            }
        }
        "del" | "delete" => {
            if args.len() != 3 {
                eprintln!("error: `del` requires exactly 1 argument: <key>");
                print_usage(&program);
                return ExitCode::FAILURE;
            }
            match store.delete(&args[2]) {
                Ok(true) => {
                    println!("OK");
                    Ok(())
                }
                Ok(false) => {
                    eprintln!("key not found: {}", args[2]);
                    return ExitCode::FAILURE;
                }
                Err(e) => Err(e),
            }
        }
        "list" => {
            if store.is_empty() {
                println!("(empty)");
            } else {
                for (key, value) in store.iter() {
                    println!("{key}={value}");
                }
            }
            Ok(())
        }
        "compact" => {
            let before = store.wal_record_count();
            store.compact().map(|()| {
                println!(
                    "compacted WAL: {before} records -> {} records",
                    store.wal_record_count()
                );
            })
        }
        "stats" => {
            println!("wal path:       {}", store.wal_path().display());
            println!("live keys:      {}", store.len());
            println!("wal records:    {}", store.wal_record_count());
            Ok(())
        }
        other => {
            eprintln!("error: unknown command `{other}`");
            print_usage(&program);
            return ExitCode::FAILURE;
        }
    };

    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}
