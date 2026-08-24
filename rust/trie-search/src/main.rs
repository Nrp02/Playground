mod levenshtein;
mod trie;

use std::env;
use std::fs;
use std::process::ExitCode;

use levenshtein::fuzzy_search;
use trie::Trie;

fn usage(program: &str) {
    eprintln!("Usage:");
    eprintln!("  {program} <wordlist> autocomplete <prefix> <n>");
    eprintln!("  {program} <wordlist> fuzzy <word> <maxdist>");
}

fn load_trie(path: &str) -> Result<Trie, String> {
    let contents = fs::read_to_string(path).map_err(|e| format!("failed to read {path}: {e}"))?;
    let mut trie = Trie::new();
    for line in contents.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        let mut parts = line.split_whitespace();
        let word = parts.next().unwrap_or("").to_lowercase();
        if word.is_empty() {
            continue;
        }
        let weight: u32 = parts.next().and_then(|w| w.parse().ok()).unwrap_or(1);
        trie.insert(&word, weight);
    }
    Ok(trie)
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    let program = args.first().cloned().unwrap_or_else(|| "trie-search".to_string());

    if args.len() < 5 {
        usage(&program);
        return ExitCode::FAILURE;
    }

    let wordlist_path = &args[1];
    let command = args[2].as_str();

    let trie = match load_trie(wordlist_path) {
        Ok(t) => t,
        Err(e) => {
            eprintln!("{e}");
            return ExitCode::FAILURE;
        }
    };

    match command {
        "autocomplete" => {
            let prefix = args[3].to_lowercase();
            let n: usize = match args[4].parse() {
                Ok(v) => v,
                Err(_) => {
                    eprintln!("invalid n: {}", args[4]);
                    return ExitCode::FAILURE;
                }
            };
            let results = trie.autocomplete(&prefix, n);
            if results.is_empty() {
                println!("no completions for '{prefix}'");
            }
            for (word, weight) in results {
                println!("{word}\t{weight}");
            }
        }
        "fuzzy" => {
            let word = args[3].to_lowercase();
            let max_dist: usize = match args[4].parse() {
                Ok(v) => v,
                Err(_) => {
                    eprintln!("invalid maxdist: {}", args[4]);
                    return ExitCode::FAILURE;
                }
            };
            let results = fuzzy_search(&trie, &word, max_dist);
            if results.is_empty() {
                println!("no matches for '{word}' within distance {max_dist}");
            }
            for (candidate, dist) in results {
                println!("{candidate}\t{dist}");
            }
        }
        other => {
            eprintln!("unknown command: {other}");
            usage(&program);
            return ExitCode::FAILURE;
        }
    }

    ExitCode::SUCCESS
}
