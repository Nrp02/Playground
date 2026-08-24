use crate::builtins;
use crate::env::ShellEnv;
use crate::exec;
use crate::expand;
use crate::ffi;
use crate::history::History;
use crate::jobs::{JobState, JobTable};
use crate::lexer;
use crate::parser;

fn is_assignment(word: &str) -> bool {
    let Some((name, _)) = word.split_once('=') else {
        return false;
    };
    !name.is_empty()
        && name
            .chars()
            .next()
            .is_some_and(|c| c.is_ascii_alphabetic() || c == '_')
        && name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
}

pub struct Shell {
    pub env: ShellEnv,
    pub jobs: JobTable,
    pub history: History,
    pub shell_pgid: i32,
    pub interactive: bool,
    pub should_exit: bool,
    pub exit_code: i32,
}

impl Shell {
    pub fn new(interactive: bool) -> Self {
        let pid = ffi::c_getpid();
        if interactive {
            ffi::c_setpgid(0, 0);
            ffi::c_tcsetpgrp(ffi::STDIN_FILENO, pid);
            for sig in [
                ffi::SIGINT,
                ffi::SIGQUIT,
                ffi::SIGTSTP,
                ffi::SIGTTIN,
                ffi::SIGTTOU,
            ] {
                ffi::c_signal_ignore(sig);
            }
        }
        Shell {
            env: ShellEnv::new(pid),
            jobs: JobTable::new(),
            history: History::load(),
            shell_pgid: pid,
            interactive,
            should_exit: false,
            exit_code: 0,
        }
    }

    pub fn run_line(&mut self, raw_line: &str) -> i32 {
        self.jobs.reap(self.interactive);

        let line = match self.expand_history_bang(raw_line) {
            Some(l) => l,
            None => {
                self.env.last_status = 1;
                return 1;
            }
        };
        let line = self.expand_alias_prefix(&line);

        let tokens = match lexer::lex(&line) {
            Ok(t) => t,
            Err(e) => {
                eprintln!("unix-shell: {}", e);
                self.env.last_status = 2;
                return 2;
            }
        };
        if tokens.is_empty() {
            return self.env.last_status;
        }
        let script = match parser::parse(tokens) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("unix-shell: {}", e);
                self.env.last_status = 2;
                return 2;
            }
        };
        self.run_script(&script);
        self.env.last_status
    }

    fn expand_history_bang(&self, line: &str) -> Option<String> {
        if !line.trim_start().starts_with('!') {
            return Some(line.to_string());
        }
        let spec = line.trim_start().trim_start_matches('!');
        match self.history.resolve(spec) {
            Some(resolved) => {
                println!("{}", resolved);
                Some(resolved)
            }
            None => {
                eprintln!("unix-shell: !{}: event not found", spec);
                None
            }
        }
    }

    fn expand_alias_prefix(&self, line: &str) -> String {
        let trimmed = line.trim_start();
        let first_word: String = trimmed
            .chars()
            .take_while(|c| !c.is_whitespace())
            .collect();
        if first_word.is_empty() {
            return line.to_string();
        }
        match self.env.get_alias(&first_word) {
            Some(value) => {
                let rest = &trimmed[first_word.len()..];
                format!("{}{}", value, rest)
            }
            None => line.to_string(),
        }
    }

    fn run_script(&mut self, script: &parser::Script) {
        for job in script {
            if self.should_exit {
                break;
            }
            let background = job.terminator == parser::Terminator::Background;
            self.run_and_or(&job.list, background);
        }
    }

    fn run_and_or(&mut self, list: &parser::AndOrList, background: bool) {
        let bg_this = background && list.rest.is_empty();
        let mut status = self.run_pipeline(&list.first, bg_this);
        self.env.last_status = status;
        for (connector, pipeline) in &list.rest {
            if self.should_exit {
                return;
            }
            let should_run = match connector {
                parser::Connector::And => status == 0,
                parser::Connector::Or => status != 0,
            };
            if !should_run {
                continue;
            }
            status = self.run_pipeline(pipeline, false);
            self.env.last_status = status;
        }
    }

    fn run_pipeline(&mut self, pipeline: &parser::Pipeline, background: bool) -> i32 {
        if pipeline.commands.len() == 1 && !background {
            let cmd = pipeline.commands[0].clone();
            let argv = expand::expand_word_list(&cmd.words, &self.env);
            if !argv.is_empty() && argv.iter().all(|w| is_assignment(w)) {
                for word in &argv {
                    let (name, value) = word.split_once('=').unwrap();
                    self.env.set(name, value);
                }
                return 0;
            }
            if let Some(name) = argv.first().cloned() {
                if builtins::is_builtin(&name) {
                    let args = argv[1..].to_vec();
                    let env_snapshot = self.env.clone();
                    let result = exec::with_redirects_applied(&cmd, &env_snapshot, || {
                        builtins::run_foreground(&name, &args, self)
                    });
                    return match result {
                        Ok(code) => code,
                        Err(e) => {
                            eprintln!("unix-shell: {}", e);
                            1
                        }
                    };
                }
            }
        }

        match exec::spawn_pipeline(&pipeline.commands, &self.env, self.interactive, !background) {
            Ok((pgid, pids, cmd_text)) => {
                if background {
                    let id = self.jobs.add(pgid, pids, cmd_text, true);
                    self.env.last_bg_pid = pgid;
                    println!("[{}] {}", id, pgid);
                    0
                } else {
                    let status = self.jobs.wait_foreground(pgid, &pids);
                    if self.interactive {
                        ffi::c_tcsetpgrp(ffi::STDIN_FILENO, self.shell_pgid);
                    }
                    if status == 256 {
                        let id = self.jobs.add(pgid, pids, cmd_text.clone(), false);
                        if let Some(j) = self.jobs.find_mut(id) {
                            j.state = JobState::Stopped;
                        }
                        println!("\n[{}]+  Stopped                 {}", id, cmd_text);
                        148
                    } else {
                        status
                    }
                }
            }
            Err(e) => {
                eprintln!("unix-shell: {}", e);
                127
            }
        }
    }
}
