use std::collections::HashMap;
use std::collections::HashSet;

#[derive(Clone)]
pub struct ShellEnv {
    vars: HashMap<String, String>,
    exported: HashSet<String>,
    aliases: HashMap<String, String>,
    pub last_status: i32,
    pub last_bg_pid: i32,
    pub shell_pid: i32,
}

impl ShellEnv {
    pub fn new(shell_pid: i32) -> Self {
        ShellEnv {
            vars: HashMap::new(),
            exported: HashSet::new(),
            aliases: HashMap::new(),
            last_status: 0,
            last_bg_pid: 0,
            shell_pid,
        }
    }

    pub fn get(&self, name: &str) -> Option<String> {
        match name {
            "?" => return Some(self.last_status.to_string()),
            "$" => return Some(self.shell_pid.to_string()),
            "!" => {
                return if self.last_bg_pid == 0 {
                    Some(String::new())
                } else {
                    Some(self.last_bg_pid.to_string())
                }
            }
            _ => {}
        }
        if let Some(v) = self.vars.get(name) {
            return Some(v.clone());
        }
        std::env::var(name).ok()
    }

    pub fn set(&mut self, name: &str, value: &str) {
        self.vars.insert(name.to_string(), value.to_string());
        if self.exported.contains(name) {
            std::env::set_var(name, value);
        }
    }

    pub fn export(&mut self, name: &str) {
        self.exported.insert(name.to_string());
        if let Some(v) = self.vars.get(name).cloned() {
            std::env::set_var(name, v);
        }
    }

    pub fn export_with_value(&mut self, name: &str, value: &str) {
        self.set(name, value);
        self.export(name);
    }

    pub fn unset(&mut self, name: &str) {
        self.vars.remove(name);
        self.exported.remove(name);
        std::env::remove_var(name);
    }

    pub fn exported_names(&self) -> Vec<String> {
        let mut names: Vec<String> = self.exported.iter().cloned().collect();
        names.sort();
        names
    }

    pub fn set_alias(&mut self, name: &str, value: &str) {
        self.aliases.insert(name.to_string(), value.to_string());
    }

    pub fn get_alias(&self, name: &str) -> Option<&String> {
        self.aliases.get(name)
    }

    pub fn remove_alias(&mut self, name: &str) {
        self.aliases.remove(name);
    }

    pub fn all_aliases(&self) -> Vec<(String, String)> {
        let mut out: Vec<(String, String)> = self
            .aliases
            .iter()
            .map(|(k, v)| (k.clone(), v.clone()))
            .collect();
        out.sort_by(|a, b| a.0.cmp(&b.0));
        out
    }
}
