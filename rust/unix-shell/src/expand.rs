use crate::env::ShellEnv;
use crate::lexer::WordPart;
use std::path::Path;

fn expand_parts(parts: &[WordPart], env: &ShellEnv) -> String {
    let mut s = String::new();
    for p in parts {
        match p {
            WordPart::Literal(l) => s.push_str(l),
            WordPart::Var(name) => {
                if let Some(v) = env.get(name) {
                    s.push_str(&v);
                }
            }
        }
    }
    s
}

fn expand_tilde(s: &str) -> String {
    if s == "~" {
        std::env::var("HOME").unwrap_or_else(|_| s.to_string())
    } else if let Some(rest) = s.strip_prefix("~/") {
        match std::env::var("HOME") {
            Ok(home) => format!("{}/{}", home, rest),
            Err(_) => s.to_string(),
        }
    } else {
        s.to_string()
    }
}

fn has_glob_meta(parts: &[WordPart]) -> bool {
    parts.iter().any(|p| match p {
        WordPart::Literal(s) => s.contains('*') || s.contains('?') || s.contains('['),
        WordPart::Var(_) => false,
    })
}

pub fn glob_match(pattern: &[char], name: &[char]) -> bool {
    match_from(pattern, 0, name, 0)
}

fn match_from(pat: &[char], pi: usize, name: &[char], ni: usize) -> bool {
    if pi == pat.len() {
        return ni == name.len();
    }
    match pat[pi] {
        '*' => {
            for k in ni..=name.len() {
                if match_from(pat, pi + 1, name, k) {
                    return true;
                }
            }
            false
        }
        '?' => ni < name.len() && match_from(pat, pi + 1, name, ni + 1),
        '[' => {
            if ni >= name.len() {
                return false;
            }
            let mut j = pi + 1;
            let negate = j < pat.len() && (pat[j] == '!' || pat[j] == '^');
            if negate {
                j += 1;
            }
            let class_start = j;
            while j < pat.len() && pat[j] != ']' {
                j += 1;
            }
            if j >= pat.len() {
                return pat[pi] == name[ni] && match_from(pat, pi + 1, name, ni + 1);
            }
            let class = &pat[class_start..j];
            let mut matched = false;
            let mut k = 0;
            while k < class.len() {
                if k + 2 < class.len() && class[k + 1] == '-' {
                    if name[ni] >= class[k] && name[ni] <= class[k + 2] {
                        matched = true;
                    }
                    k += 3;
                } else {
                    if class[k] == name[ni] {
                        matched = true;
                    }
                    k += 1;
                }
            }
            if negate {
                matched = !matched;
            }
            matched && match_from(pat, j + 1, name, ni + 1)
        }
        c => ni < name.len() && name[ni] == c && match_from(pat, pi + 1, name, ni + 1),
    }
}

fn glob_expand(pattern: &str) -> Vec<String> {
    let (dir, file_pattern) = match pattern.rfind('/') {
        Some(idx) => (&pattern[..idx.max(1)], &pattern[idx + 1..]),
        None => (".", pattern),
    };
    let dir_path = if dir.is_empty() { "/" } else { dir };
    let entries = match std::fs::read_dir(dir_path) {
        Ok(e) => e,
        Err(_) => return Vec::new(),
    };
    let pat_chars: Vec<char> = file_pattern.chars().collect();
    let show_hidden = file_pattern.starts_with('.');
    let mut matches: Vec<String> = Vec::new();
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().to_string();
        if name.starts_with('.') && !show_hidden {
            continue;
        }
        let name_chars: Vec<char> = name.chars().collect();
        if glob_match(&pat_chars, &name_chars) {
            let full = if pattern.contains('/') {
                format!("{}/{}", dir, name)
            } else {
                name
            };
            matches.push(full);
        }
    }
    matches.sort();
    matches
}

pub fn expand_word_list(words: &[Vec<WordPart>], env: &ShellEnv) -> Vec<String> {
    let mut out = Vec::new();
    for parts in words {
        let mut expanded = expand_parts(parts, env);
        if expanded.starts_with('~') {
            expanded = expand_tilde(&expanded);
        }
        if has_glob_meta(parts) {
            let matches = glob_expand(&expanded);
            if matches.is_empty() {
                out.push(expanded);
            } else {
                out.extend(matches);
            }
        } else {
            out.push(expanded);
        }
    }
    out
}

pub fn expand_path(parts: &[WordPart], env: &ShellEnv) -> String {
    let expanded = expand_parts(parts, env);
    if expanded.starts_with('~') {
        expand_tilde(&expanded)
    } else {
        expanded
    }
}

pub fn resolve_program_path(program: &str) -> Option<String> {
    if program.contains('/') {
        return if Path::new(program).is_file() {
            Some(program.to_string())
        } else {
            None
        };
    }
    let path_var = std::env::var("PATH").unwrap_or_default();
    for dir in path_var.split(':') {
        let candidate = format!("{}/{}", dir, program);
        if Path::new(&candidate).is_file() {
            return Some(candidate);
        }
    }
    None
}
