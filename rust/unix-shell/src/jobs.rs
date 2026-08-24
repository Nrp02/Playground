use crate::ffi;

#[derive(Debug, Clone, PartialEq)]
pub enum JobState {
    Running,
    Stopped,
    Done(i32),
    Killed(i32),
}

#[derive(Debug, Clone)]
pub struct Job {
    pub id: usize,
    pub pgid: i32,
    pub pids: Vec<i32>,
    pub command: String,
    pub state: JobState,
    pub background: bool,
}

pub struct JobTable {
    jobs: Vec<Job>,
    next_id: usize,
}

impl JobTable {
    pub fn new() -> Self {
        JobTable {
            jobs: Vec::new(),
            next_id: 1,
        }
    }

    pub fn add(&mut self, pgid: i32, pids: Vec<i32>, command: String, background: bool) -> usize {
        let id = self.next_id;
        self.next_id += 1;
        self.jobs.push(Job {
            id,
            pgid,
            pids,
            command,
            state: JobState::Running,
            background,
        });
        id
    }

    pub fn list(&self) -> &[Job] {
        &self.jobs
    }

    pub fn find(&self, id: usize) -> Option<&Job> {
        self.jobs.iter().find(|j| j.id == id)
    }

    pub fn find_mut(&mut self, id: usize) -> Option<&mut Job> {
        self.jobs.iter_mut().find(|j| j.id == id)
    }

    pub fn most_recent(&self) -> Option<usize> {
        self.jobs
            .iter()
            .filter(|j| matches!(j.state, JobState::Running | JobState::Stopped))
            .map(|j| j.id)
            .max()
    }

    pub fn set_running(&mut self, id: usize) {
        if let Some(j) = self.find_mut(id) {
            j.state = JobState::Running;
        }
    }

    pub fn remove(&mut self, id: usize) {
        self.jobs.retain(|j| j.id != id);
    }

    pub fn reap(&mut self, announce: bool) {
        loop {
            let status = ffi::c_waitpid(-1, ffi::WNOHANG | ffi::WUNTRACED);
            let status = match status {
                Some(s) => s,
                None => break,
            };
            let pgid = status.pid;
            let mut finished_id: Option<(usize, String)> = None;
            if let Some(job) = self
                .jobs
                .iter_mut()
                .find(|j| j.pgid == pgid || j.pids.contains(&status.pid))
            {
                if status.exited {
                    job.state = JobState::Done(status.exit_code);
                    if job.background {
                        finished_id = Some((job.id, job.command.clone()));
                    }
                } else if status.signaled {
                    job.state = JobState::Killed(status.term_signal);
                    if job.background {
                        finished_id = Some((job.id, job.command.clone()));
                    }
                } else if status.stopped {
                    job.state = JobState::Stopped;
                    if announce {
                        println!("\n[{}]+  Stopped                 {}", job.id, job.command);
                    }
                }
            }
            if let Some((id, cmd)) = finished_id {
                if announce {
                    println!("[{}]+  Done                    {}", id, cmd);
                }
                self.remove(id);
            }
        }
    }

    pub fn wait_foreground(&mut self, pgid: i32, pids: &[i32]) -> i32 {
        let mut last_status = 0;
        let mut remaining: Vec<i32> = pids.to_vec();
        let mut stopped = false;
        while !remaining.is_empty() {
            let status = match ffi::c_waitpid(-pgid, ffi::WUNTRACED) {
                Some(s) => s,
                None => break,
            };
            if status.stopped {
                stopped = true;
                break;
            }
            remaining.retain(|p| *p != status.pid);
            if status.exited {
                last_status = status.exit_code;
            } else if status.signaled {
                last_status = 128 + status.term_signal;
            }
        }
        if stopped {
            256
        } else {
            last_status
        }
    }
}

pub fn state_label(state: &JobState) -> String {
    match state {
        JobState::Running => "Running".to_string(),
        JobState::Stopped => "Stopped".to_string(),
        JobState::Done(code) => format!("Done({})", code),
        JobState::Killed(sig) => format!("Killed(signal {})", sig),
    }
}
