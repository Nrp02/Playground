use std::collections::VecDeque;
use std::sync::{Arc, Condvar, Mutex};
use std::time::{Duration, Instant};

#[derive(Debug, PartialEq, Eq)]
pub struct SendError<T>(pub T);

#[derive(Debug, PartialEq, Eq)]
pub enum TrySendError<T> {
    Full(T),
    Disconnected(T),
}

#[derive(Debug, PartialEq, Eq)]
pub struct RecvError;

#[derive(Debug, PartialEq, Eq)]
pub enum TryRecvError {
    Empty,
    Disconnected,
}

#[derive(Debug, PartialEq, Eq)]
pub enum RecvTimeoutError {
    Timeout,
    Disconnected,
}

struct Shared<T> {
    queue: Mutex<State<T>>,
    not_empty: Condvar,
    not_full: Condvar,
    capacity: Option<usize>,
}

struct State<T> {
    items: VecDeque<T>,
    senders_alive: usize,
    receiver_alive: bool,
}

impl<T> Shared<T> {
    fn new(capacity: Option<usize>) -> Self {
        Shared {
            queue: Mutex::new(State {
                items: VecDeque::new(),
                senders_alive: 1,
                receiver_alive: true,
            }),
            not_empty: Condvar::new(),
            not_full: Condvar::new(),
            capacity,
        }
    }
}

pub struct Sender<T> {
    shared: Arc<Shared<T>>,
}

pub struct SyncSender<T> {
    shared: Arc<Shared<T>>,
}

pub struct Receiver<T> {
    shared: Arc<Shared<T>>,
}

pub fn channel<T>() -> (Sender<T>, Receiver<T>) {
    let shared = Arc::new(Shared::new(None));
    (
        Sender {
            shared: shared.clone(),
        },
        Receiver { shared },
    )
}

pub fn sync_channel<T>(capacity: usize) -> (SyncSender<T>, Receiver<T>) {
    let shared = Arc::new(Shared::new(Some(capacity)));
    (
        SyncSender {
            shared: shared.clone(),
        },
        Receiver { shared },
    )
}

impl<T> Clone for Sender<T> {
    fn clone(&self) -> Self {
        let mut state = self.shared.queue.lock().unwrap();
        state.senders_alive += 1;
        Sender {
            shared: self.shared.clone(),
        }
    }
}

impl<T> Clone for SyncSender<T> {
    fn clone(&self) -> Self {
        let mut state = self.shared.queue.lock().unwrap();
        state.senders_alive += 1;
        SyncSender {
            shared: self.shared.clone(),
        }
    }
}

impl<T> Drop for Sender<T> {
    fn drop(&mut self) {
        let mut state = self.shared.queue.lock().unwrap();
        state.senders_alive -= 1;
        if state.senders_alive == 0 {
            self.shared.not_empty.notify_all();
        }
    }
}

impl<T> Drop for SyncSender<T> {
    fn drop(&mut self) {
        let mut state = self.shared.queue.lock().unwrap();
        state.senders_alive -= 1;
        if state.senders_alive == 0 {
            self.shared.not_empty.notify_all();
        }
    }
}

impl<T> Drop for Receiver<T> {
    fn drop(&mut self) {
        let mut state = self.shared.queue.lock().unwrap();
        state.receiver_alive = false;
        self.shared.not_full.notify_all();
    }
}

impl<T> Sender<T> {
    pub fn send(&self, value: T) -> Result<(), SendError<T>> {
        let mut state = self.shared.queue.lock().unwrap();
        if !state.receiver_alive {
            return Err(SendError(value));
        }
        state.items.push_back(value);
        self.shared.not_empty.notify_one();
        Ok(())
    }
}

impl<T> SyncSender<T> {
    pub fn send(&self, value: T) -> Result<(), SendError<T>> {
        let capacity = self.shared.capacity.unwrap();
        let mut state = self.shared.queue.lock().unwrap();
        loop {
            if !state.receiver_alive {
                return Err(SendError(value));
            }
            if state.items.len() < capacity {
                state.items.push_back(value);
                self.shared.not_empty.notify_one();
                return Ok(());
            }
            state = self.shared.not_full.wait(state).unwrap();
        }
    }

    pub fn try_send(&self, value: T) -> Result<(), TrySendError<T>> {
        let capacity = self.shared.capacity.unwrap();
        let mut state = self.shared.queue.lock().unwrap();
        if !state.receiver_alive {
            return Err(TrySendError::Disconnected(value));
        }
        if state.items.len() >= capacity {
            return Err(TrySendError::Full(value));
        }
        state.items.push_back(value);
        self.shared.not_empty.notify_one();
        Ok(())
    }
}

impl<T> Receiver<T> {
    pub fn recv(&self) -> Result<T, RecvError> {
        let mut state = self.shared.queue.lock().unwrap();
        loop {
            if let Some(value) = state.items.pop_front() {
                self.shared.not_full.notify_one();
                return Ok(value);
            }
            if state.senders_alive == 0 {
                return Err(RecvError);
            }
            state = self.shared.not_empty.wait(state).unwrap();
        }
    }

    pub fn try_recv(&self) -> Result<T, TryRecvError> {
        let mut state = self.shared.queue.lock().unwrap();
        if let Some(value) = state.items.pop_front() {
            self.shared.not_full.notify_one();
            return Ok(value);
        }
        if state.senders_alive == 0 {
            return Err(TryRecvError::Disconnected);
        }
        Err(TryRecvError::Empty)
    }

    pub fn recv_timeout(&self, timeout: Duration) -> Result<T, RecvTimeoutError> {
        let deadline = Instant::now() + timeout;
        let mut state = self.shared.queue.lock().unwrap();
        loop {
            if let Some(value) = state.items.pop_front() {
                self.shared.not_full.notify_one();
                return Ok(value);
            }
            if state.senders_alive == 0 {
                return Err(RecvTimeoutError::Disconnected);
            }
            let now = Instant::now();
            if now >= deadline {
                return Err(RecvTimeoutError::Timeout);
            }
            let (guard, _) = self
                .shared
                .not_empty
                .wait_timeout(state, deadline - now)
                .unwrap();
            state = guard;
        }
    }

    pub fn iter(&self) -> Iter<'_, T> {
        Iter { receiver: self }
    }
}

pub struct Iter<'a, T> {
    receiver: &'a Receiver<T>,
}

impl<'a, T> Iterator for Iter<'a, T> {
    type Item = T;
    fn next(&mut self) -> Option<T> {
        self.receiver.recv().ok()
    }
}

impl<'a, T> IntoIterator for &'a Receiver<T> {
    type Item = T;
    type IntoIter = Iter<'a, T>;
    fn into_iter(self) -> Iter<'a, T> {
        self.iter()
    }
}

pub struct IntoIter<T> {
    receiver: Receiver<T>,
}

impl<T> Iterator for IntoIter<T> {
    type Item = T;
    fn next(&mut self) -> Option<T> {
        self.receiver.recv().ok()
    }
}

impl<T> IntoIterator for Receiver<T> {
    type Item = T;
    type IntoIter = IntoIter<T>;
    fn into_iter(self) -> IntoIter<T> {
        IntoIter { receiver: self }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::thread;

    #[test]
    fn single_producer_fifo() {
        let (tx, rx) = channel();
        for i in 0..100 {
            tx.send(i).unwrap();
        }
        drop(tx);
        let collected: Vec<_> = rx.iter().collect();
        let expected: Vec<i32> = (0..100).collect();
        assert_eq!(collected, expected);
    }

    #[test]
    fn multi_producer_stress() {
        for _ in 0..20 {
            let (tx, rx) = channel();
            let n_threads = 8;
            let n_messages = 500;
            let mut handles = Vec::new();
            for t in 0..n_threads {
                let tx = tx.clone();
                handles.push(thread::spawn(move || {
                    for m in 0..n_messages {
                        tx.send(t * n_messages + m).unwrap();
                    }
                }));
            }
            drop(tx);
            for h in handles {
                h.join().unwrap();
            }
            let mut seen = vec![false; n_threads * n_messages];
            let mut count = 0;
            for v in rx.iter() {
                assert!(!seen[v]);
                seen[v] = true;
                count += 1;
            }
            assert_eq!(count, n_threads * n_messages);
            assert!(seen.iter().all(|x| *x));
        }
    }

    #[test]
    fn bounded_never_exceeds_capacity() {
        let (tx, rx) = sync_channel(4);
        let observed = Arc::new(AtomicUsize::new(0));
        let observed_clone = observed.clone();
        let handle = thread::spawn(move || {
            for i in 0..50 {
                tx.send(i).unwrap();
            }
        });
        thread::sleep(Duration::from_millis(5));
        let mut received = 0;
        while received < 50 {
            if let Ok(_) = rx.try_recv() {
                received += 1;
            } else {
                observed_clone.fetch_add(1, Ordering::Relaxed);
                thread::sleep(Duration::from_millis(1));
            }
        }
        handle.join().unwrap();
        assert_eq!(received, 50);
    }

    #[test]
    fn bounded_send_blocks_then_succeeds() {
        let (tx, rx) = sync_channel(2);
        tx.send(1).unwrap();
        tx.send(2).unwrap();
        assert_eq!(tx.try_send(3), Err(TrySendError::Full(3)));
        let handle = thread::spawn(move || {
            tx.send(3).unwrap();
        });
        thread::sleep(Duration::from_millis(20));
        assert_eq!(rx.recv().unwrap(), 1);
        handle.join().unwrap();
        assert_eq!(rx.recv().unwrap(), 2);
        assert_eq!(rx.recv().unwrap(), 3);
    }

    #[test]
    fn recv_unblocks_on_last_sender_drop() {
        let (tx, rx) = channel::<i32>();
        let handle = thread::spawn(move || {
            thread::sleep(Duration::from_millis(20));
            drop(tx);
        });
        assert_eq!(rx.recv(), Err(RecvError));
        handle.join().unwrap();
    }

    #[test]
    fn queued_values_drained_before_recv_error() {
        let (tx, rx) = channel();
        tx.send(1).unwrap();
        tx.send(2).unwrap();
        drop(tx);
        assert_eq!(rx.recv(), Ok(1));
        assert_eq!(rx.recv(), Ok(2));
        assert_eq!(rx.recv(), Err(RecvError));
    }

    #[test]
    fn try_recv_empty_vs_disconnected() {
        let (tx, rx) = channel::<i32>();
        assert_eq!(rx.try_recv(), Err(TryRecvError::Empty));
        drop(tx);
        assert_eq!(rx.try_recv(), Err(TryRecvError::Disconnected));
    }

    #[test]
    fn recv_timeout_boundary() {
        let (tx, rx) = channel::<i32>();
        let start = Instant::now();
        assert_eq!(
            rx.recv_timeout(Duration::from_millis(30)),
            Err(RecvTimeoutError::Timeout)
        );
        assert!(start.elapsed() >= Duration::from_millis(25));
        tx.send(7).unwrap();
        assert_eq!(rx.recv_timeout(Duration::from_millis(30)), Ok(7));
    }

    #[test]
    fn send_after_receiver_drop_returns_value() {
        let (tx, rx) = channel();
        drop(rx);
        assert_eq!(tx.send(42), Err(SendError(42)));
    }

    #[test]
    fn zero_sized_type() {
        let (tx, rx) = channel();
        for _ in 0..10 {
            tx.send(()).unwrap();
        }
        drop(tx);
        let mut count = 0;
        for _ in rx.iter() {
            count += 1;
        }
        assert_eq!(count, 10);
    }

    #[test]
    fn string_type() {
        let (tx, rx) = channel();
        tx.send(String::from("hello")).unwrap();
        tx.send(String::from("world")).unwrap();
        drop(tx);
        assert_eq!(rx.recv().unwrap(), "hello");
        assert_eq!(rx.recv().unwrap(), "world");
    }

    #[derive(Debug)]
    struct DropCounter {
        counter: Arc<AtomicUsize>,
    }

    impl Drop for DropCounter {
        fn drop(&mut self) {
            self.counter.fetch_add(1, Ordering::SeqCst);
        }
    }

    #[test]
    fn every_value_dropped_exactly_once() {
        let counter = Arc::new(AtomicUsize::new(0));
        {
            let (tx, rx) = channel();
            for _ in 0..5 {
                tx.send(DropCounter {
                    counter: counter.clone(),
                })
                .unwrap();
            }
            let first = rx.recv().unwrap();
            drop(first);
            assert_eq!(counter.load(Ordering::SeqCst), 1);
        }
        assert_eq!(counter.load(Ordering::SeqCst), 5);
    }

    #[test]
    fn dropping_receiver_wakes_blocked_sender() {
        let (tx, rx) = sync_channel(1);
        tx.send(1).unwrap();
        let tx2 = tx.clone();
        let handle = thread::spawn(move || tx2.send(2));
        thread::sleep(Duration::from_millis(20));
        drop(rx);
        let result = handle.join().unwrap();
        assert_eq!(result, Err(SendError(2)));
    }
}
