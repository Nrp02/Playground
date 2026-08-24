use std::fs;
use std::io::Write;
use std::path::PathBuf;

pub struct History {
    entries: Vec<String>,
    path: Option<PathBuf>,
}

fn history_path() -> Option<PathBuf> {
    std::env::var("HOME")
        .ok()
        .map(|home| PathBuf::from(home).join(".unix_shell_history"))
}

impl History {
    pub fn load() -> Self {
        let path = history_path();
        let entries = match &path {
            Some(p) => fs::read_to_string(p)
                .map(|s| s.lines().map(|l| l.to_string()).collect())
                .unwrap_or_default(),
            None => Vec::new(),
        };
        History { entries, path }
    }

    pub fn add(&mut self, line: &str) {
        let trimmed = line.trim();
        if trimmed.is_empty() {
            return;
        }
        if self.entries.last().map(|s| s.as_str()) != Some(trimmed) {
            self.entries.push(trimmed.to_string());
        }
    }

    pub fn save(&self) {
        if let Some(path) = &self.path {
            if let Ok(mut f) = fs::File::create(path) {
                for line in &self.entries {
                    let _ = writeln!(f, "{}", line);
                }
            }
        }
    }

    pub fn list(&self) -> &[String] {
        &self.entries
    }

    pub fn resolve(&self, spec: &str) -> Option<String> {
        if spec == "!" {
            return self.entries.last().cloned();
        }
        if let Ok(n) = spec.parse::<usize>() {
            return self.entries.get(n.checked_sub(1)?).cloned();
        }
        self.entries
            .iter()
            .rev()
            .find(|e| e.starts_with(spec))
            .cloned()
    }
}
